#lang rosette
#| Proves leaf and parent refinement across verified child boundaries and admits their results at exact RTL child occurrences. |#
;; SPDX-License-Identifier: Apache-2.0

(require racket/match (only-in racket/base [equal? host-equal?] [eq? host-eq?])
         "engine.rkt" "spec-engine.rkt" (only-in "snapshot.rhm" formal_snapshot) (only-in "spec.rhm" spec_snapshot)
         (only-in "fifo-monitor.rhm" fifo_monitor)
         (only-in "fifo-view.rhm" fifo_view_parts fifo_view_layout fifo_view_bool)
         (only-in "wiring.rhm" cut_parts leaf_protocol leaf_layout series_wiring wrapper_wiring))
(provide prove-leaf prove-parent prepare-proved-wiring proved-child-model symbolic-cut-state proved-edge-at view-agrees? pack-pins unpack-pins)
(provide exn:fail:input-contract? exn:fail:input-contract-goal exn:fail:input-contract-inputs)
(provide check-batch-total! (rename-out [check-phase check-refinement-phase!]))
(provide symbolic-wrapper-cut symbolic-fifo-cut fifo-cut-inputs fifo-cut-outputs fifo-cut-locals fifo-cut-batch fifo-cut-domain)
(provide derive-fifo-transactions prove-fifo-transactions compose-fifo-series prove-fifo-wrapper fifo-transactions-for? fifo-transactions-capacity fifo-transactions-width
         fresh-fifo-history fifo-allows? (struct-out fifo-history) (struct-out fifo-events))

;; Private constructors. These are live elaboration identities, not transferable certificates.
;; A conditional result carries its current-input premises in the exact model snapshot.
(struct refinement-result (definition model protocol layout))
(struct proved-cut (definition layout graph models))
(struct exn:fail:input-contract exn:fail (goal inputs) #:transparent)

#| Pack the public logical samples in declared physical member order. |#
(define (pack-pins layout samples)
  (for/hash ([(pin fields) layout])
    (define words
      (for/list ([f fields])
        (define v (hash-ref samples (car f)))
        (cond [(boolean? v) (if v (bv 1 1) (bv 0 1))]
              [(bv? v) v] [else (bv v (cadr f))])))
    (values pin (foldl (lambda (v acc) (concat acc v)) (car words) (cdr words)))))

#| Recover logical bitvectors or naturals from the corresponding packed RTL outputs. |#
(define (unpack-pins layout pins)
  (for*/hash ([(pin fields) layout]
              [field (in-list fields)])
    (define packed (hash-ref pins pin))
    (define lo (apply + (map cadr (cdr (member field fields)))))
    (define hi (+ lo (cadr field)))
    (values (car field) (if (bv? packed) (extract (sub1 hi) lo packed) (bitwise-bit-field packed lo hi)))))

#| Reject hidden constraints from an observer, relation, or enclosing Rosette query. |#
(define (clean-vc!)
  (unless (host-equal? (vc) vc-true) (error 'refinement "unexpected ambient verification condition")))

#| Prove an unconditional pin sampler produces a defined, dense batch on every pin valuation.
   In particular a payload's presence guard is an obligation, not an environment restriction. |#
(define (check-batch-total! snapshot)
  (unless (and (zero? (hash-count (hash-ref snapshot "state")))
               (zero? (hash-count (hash-ref snapshot "assumptions")))
               (positive? (hash-count (hash-ref snapshot "outputs"))))
    (error 'batch-sampler "expected an unconditional stateless batch projection"))
  (define edge (symbolic-spec-edge snapshot))
  (check-phase 'batch-sampler #t
               (list (cons 'total (&& (edge-defined edge)
                                      (andmap observation-care (hash-values (edge-outputs edge))))))))

#| Interpret only the assumptions recorded in the contract being certified, on this query's sample. |#
(define (allowed-inputs? model inputs)
  (andmap identity (hash-values (spec-input-assumptions model inputs))))

#| Never issue evidence from an infeasible premise, failed obligation, or unknown solver answer. |#
(define (check-phase name premise goals #:inputs [inputs #f])
  (clean-vc!)
  (unless (sat? (solve (assert premise))) (error 'refinement "~a: infeasible or unknown domain" name))
  (for ([goal goals])
    (define answer (solve (assert (&& premise (! (cdr goal))))))
    (when (and inputs (string? (car goal)) (sat? answer))
      (define full (complete-solution answer (symbolics (hash-values inputs))))
      (define sample (for/hash ([(k v) inputs])
                       (define x (evaluate v full))
                       (values k (if (bv? x) (bitvector->natural x) x))))
      (raise (exn:fail:input-contract (format "~a/~a not proved: ~a" name (car goal) sample)
                                     (current-continuation-marks) (car goal) sample)))
    (unless (unsat? answer) (error 'refinement "~a/~a not proved: ~a" name (car goal) answer))))

#| Compare observation names and care, and compare values exactly when cared about. |#
(define (view-agrees? actual expected)
  (unless (host-equal? (sort (hash-keys actual) string<?) (sort (hash-keys expected) string<?))
    (error 'prove-leaf "observation schema mismatch"))
  (for/fold ([ok #t]) ([(name x) actual])
    (define y (hash-ref expected name))
    (&& ok (equal? (observation-care x) (observation-care y))
        (=> (observation-care x) (equal? (observation-value x) (observation-value y))))))

#| Compare a next-state value with its concrete start; ignore only unused sequence slots. |#
(define (at-start? value type start)
  (match type
    [(list "bool") (equal? value start)]
    [(list "bits" w) (bveq value (bv start w))]
    [(list "seq" _ element)
     (define n (sequence-value-count value))
     (&& (bveq n (bv (length start) (type-of n)))
         (for/fold ([ok #t]) ([slot (sequence-value-slots value)] [word start])
           (&& ok (at-start? slot element word))))]))

#| Every permitted reset must establish the start and relation from arbitrary old RTL state.
   Check the non-reset domain separately so a reset-only contract cannot pass preservation. |#
(define (check-synchronous! model spec old-domain related-old related-next common #:allowed-inputs [allowed #t])
  (define reset (hash-ref (edge-inputs spec) "reset"))
  (unless (boolean? reset) (error 'refinement "expected Boolean synchronous reset"))
  (define next (edge-next spec))
  (define start (for/fold ([ok #t]) ([(k type) (hash-ref model "state")])
                  (&& ok (at-start? (hash-ref next k) type (hash-ref (hash-ref model "initial") k)))))
  (define legal (&& old-domain (edge-legal spec) allowed))
  (define goals (list* (cons 'successor related-next) (cons 'legal-next (spec-state-legal? next)) common))
  (check-phase 'reset (&& legal reset) (cons (cons 'initial start) goals))
  (check-phase 'preservation (&& legal (! reset) related-old) goals))

#| Take the snapshot of the definition being certified. The binding supplies trusted physical
   observation care and event decoding and, for synchronous leaves, an abstraction relation. |#
(define (prove-leaf definition spec-model prefixes observe #:relation [relation #f])
  (clean-vc!)
  (define protocol (leaf_protocol definition))
  (unless (if relation (equal? protocol "single-clock-reset") (equal? protocol "combinational"))
    (error 'prove-leaf "exactly the clocked profile requires a state relation"))
  (define snapshot (formal_snapshot definition))
  (define model (spec_snapshot spec-model))
  (define spec (symbolic-spec-edge model))
  (define inputs (edge-inputs spec))
  (define allowed (allowed-inputs? model inputs))
  (define layout (leaf_layout definition (prefixes definition)))
  (define packed (pack-pins (first layout) inputs))
  (define plan (and relation (prepare_transition_snapshot snapshot "clock")))
  (define old (and plan (symbolic_transition_state plan)))
  (define rtl (if plan (step_transition plan old packed) (combinational_sample snapshot packed)))
  (define ports (unpack-pins (second layout) (transition_step_outputs rtl)))
  (define-values (outputs events) (observe ports inputs))
  ;; Even a mistaken observer must not be able to relabel a cared physical output's value.
  (define physical (for/hash ([(key field) outputs])
                     (define bits (hash-ref ports key))
                     (define v (if (boolean? (observation-value field)) (bveq bits (bv 1 1)) bits))
                     (values key (observation (observation-care field) v))))
  (define common
    (list (cons 'rtl-domain (transition_step_validity rtl)) (cons 'defined (edge-defined spec))
          (cons 'outputs (view-agrees? outputs (edge-outputs spec)))
          (cons 'physical-pins (view-agrees? physical (edge-outputs spec)))
          (cons 'transfers (view-agrees? events (edge-events spec)))))
  (cond
    [plan
     (check-synchronous! model spec #t (relation old (edge-state spec))
                         (relation (transition_step_state rtl) (edge-next spec)) common #:allowed-inputs allowed)]
    [else
     (unless (zero? (hash-count (edge-state spec))) (error 'prove-leaf "expected a stateless model"))
     (check-phase 'combinational allowed
                  (cons (cons 'domain (edge-legal spec)) common))])
  (refinement-result definition model protocol layout))

#| A publicly steppable plan requires child contracts to hold without any caller assumptions. |#
(define (prepare-proved-wiring cut results)
  (admit-wiring cut results #f))

#| Bind input predicates and cared output values to the cut's actual external signature. |#
(define (check-parent-schema! graph model)
  (unless (host-equal? (hash-ref graph "inputs") (hash-ref model "inputs"))
    (error 'prove-parent "parent input schema mismatch"))
  (for ([(k field) (hash-ref model "outputs")])
    (unless (host-equal? (hash-ref (hash-ref graph "output-types") k #f) (second (second field)))
      (error 'prove-parent "parent output schema mismatch: ~a" k))))

#| Match exact occurrence evidence, then discharge ordered goals. A cut admitted under a
   parent contract stays private to the proof of that same snapshot. |#
(define (admit-wiring cut results parent)
  (define-values (definition layout graph definitions layouts locals) (cut_parts cut))
  (when parent (check-parent-schema! graph parent))
  (when (and (list? results) (ormap fifo-transactions? results))
    (error 'prepare-proved-wiring "transaction-only evidence cannot supply cycle pins"))
  (unless (and (list? results) (andmap refinement-result? results))
    (error 'prepare-proved-wiring "expected completed refinement results"))
  (define models
    (for/hash ([(site definition) definitions])
      (define candidates (filter (lambda (r) (host-eq? definition (refinement-result-definition r))) results))
      (when (null? candidates)
        (error 'prepare-proved-wiring "no evidence for RTL specialization at ~a" site))
      (define target (hash-ref (hash-ref graph "children") site))
      (define protocol (leaf_protocol definition))
      (define matched (findf (lambda (r) (and (host-equal? protocol (refinement-result-protocol r))
                                              (host-equal? (hash-ref layouts site) (refinement-result-layout r))
                                              (host-equal? target (refinement-result-model r)))) candidates))
      (unless matched (error 'prepare-proved-wiring "evidence does not cover contract at ~a" site))
      (values site (refinement-result-model matched))))
  (define all (for/fold ([all models]) ([(site rtl) locals]) (hash-set all site rtl)))
  (when (for/or ([model (hash-values models)]) (positive? (hash-count (hash-ref model "assumptions"))))
    (discharge-wired-inputs!
     graph all (lambda (inputs legal goals)
                 (define domain (if parent (&& legal (allowed-inputs? parent inputs)) legal))
                 (check-phase 'child-input domain goals #:inputs inputs))))
  (proved-cut definition layout graph all))

#| Access the admitted operational model for one occurrence, e.g. to allocate its abstract state. |#
(define (proved-child-model plan site) (hash-ref (proved-cut-models plan) site))

#| Allocate one independent abstract state per child and the actual local RTL register fields, if any. |#
(define (symbolic-cut-state plan)
  (for/hash ([(site model) (proved-cut-models plan)]) (values site (symbolic-spec-state model))))

#| Step an admitted cut without inspecting child RTL or accepting replacement models from the caller. |#
(define (proved-edge-at plan states inputs)
  (wired-edge-at (proved-cut-graph plan) (proved-cut-models plan) states inputs))

#| Prove a synchronous parent from an unrestricted plan, or discharge a raw cut with
   child evidence inside this proof. Only the full parent contract can leave as evidence. |#
(define (prove-parent cut spec-model observe #:relation relation #:child-evidence [results #f])
  (clean-vc!)
  (define model (spec_snapshot spec-model))
  (define plan (cond [results (admit-wiring cut results model)]
                     [else (check-parent-schema! (proved-cut-graph cut) model) cut]))
  (define spec (symbolic-spec-edge model))
  (define states (symbolic-cut-state plan))
  (define-values (pins edges settled) (proved-edge-at plan states (edge-inputs spec)))
  (define after (for/hash ([(site e) edges]) (values site (edge-next e))))
  (define-values (outputs events) (observe pins (edge-inputs spec)))
  ;; The observer defines care and transfers, but cannot replace a physical value on a cared pin.
  (define physical (for/hash ([(k field) outputs])
                     (values k (observation (observation-care field) (hash-ref pins k)))))
  (define common
    (list (cons 'children
                (for/fold ([ok settled]) ([e (hash-values edges)])
                  (&& ok (edge-legal e) (edge-defined e) (spec-state-legal? (edge-next e)))))
          (cons 'defined (edge-defined spec))
          (cons 'outputs (view-agrees? outputs (edge-outputs spec)))
          (cons 'physical-pins (view-agrees? physical (edge-outputs spec)))
          (cons 'transfers (view-agrees? events (edge-events spec)))))
  (check-synchronous! model spec (andmap spec-state-legal? (hash-values states))
                      (relation states (edge-state spec)) (relation after (edge-next spec)) common
                      #:allowed-inputs (allowed-inputs? model (edge-inputs spec)))
  (refinement-result (proved-cut-definition plan) model "single-clock-reset" (proved-cut-layout plan)))

;; Only certification can construct evidence. No child states, transition plan, or cycle pins escape.
(struct fifo-transactions (definition layout ingress egress capacity width sampler monitor))
(struct fifo-history (count slots) #:transparent)
(struct fifo-events (reset accepted? accepted retired? retired) #:transparent)

#| Match the exact run-local definition and ordered endpoint mapping, not just width and capacity. |#
(define (fifo-transactions-for? result view)
  (define-values (definition ingress egress width sampler) (fifo_view_parts view))
  (and (fifo-transactions? result) (host-eq? definition (fifo-transactions-definition result))
       (host-equal? (fifo_view_layout view) (fifo-transactions-layout result))
       (host-equal? ingress (fifo-transactions-ingress result))
       (host-equal? egress (fifo-transactions-egress result))))

#| Allocate a candidate pending sequence; only its live prefix participates in the contract. |#
(define (fresh-fifo-history result)
  (define-symbolic* count integer?)
  (fifo-history count (for/list ([i (in-range (fifo-transactions-capacity result))])
                        (define-symbolic* word (bitvector (fifo-transactions-width result)))
                        word)))

#| Create a candidate contract; only successful certification returns it to callers. |#
(define (candidate-fifo view capacity)
  (define-values (definition ingress egress width sampler) (fifo_view_parts view))
  (fifo-transactions definition (fifo_view_layout view) ingress egress capacity width
                     (spec_snapshot sampler) (spec_snapshot (fifo_monitor capacity width))))

#| Interpret the declared passive sampler. Its pin-domain predicates are obligations on the
   sampled hardware, never constraints on the cycle states or inputs the solver explores. |#
(define (sampled-fifo-batch contract pins)
  (define sampler (fifo-transactions-sampler contract))
  (define e (spec-edge-at sampler (hash) pins))
  (define events (edge-events e))
  (define accepted (hash-ref events "accepted"))
  (define retired (hash-ref events "retired"))
  (values (fifo-events (observation-care (hash-ref events "reset"))
                       (observation-care accepted) (observation-value accepted)
                       (observation-care retired) (observation-value retired))
          (&& (edge-legal e) (edge-defined e)
              (allowed-inputs? sampler pins))))

#| Apply the same monitor as rspecsim. Bridge mathematical occupancies to finite sequence
   counts without admitting wraparound, and compare only the successor's live prefix. |#
(define (fifo-allows? evidence old events next)
  (define capacity (fifo-transactions-capacity evidence))
  (define width (fifo-transactions-width evidence))
  (match-define (fifo-events reset push in pop out) events)
  (match-define (fifo-history n slots) old)
  (match-define (fifo-history m new-slots) next)
  (unless (and (integer? n) (integer? m) (= capacity (length slots) (length new-slots))
               (andmap boolean? (list reset push pop))
               (andmap (bitvector width) (list* in out (append slots new-slots))))
    (error 'fifo-transactions "mistyped FIFO history or events"))
  (define count-width (integer-length capacity))
  (define count (for/fold ([v (bv 0 count-width)]) ([i (in-range 1 (add1 capacity))])
                  (if (= n i) (bv i count-width) v)))
  (define e (spec-edge-at (fifo-transactions-monitor evidence) (hash "q" (sequence-value count slots))
                          (hash "reset" reset "push" push "in" in "pop" pop "out" out)))
  (match-define (sequence-value after words) (hash-ref (edge-next e) "q"))
  (&& (|| reset (<= 0 n capacity)) (edge-defined e)
      (observation-value (hash-ref (edge-outputs e) "allowed"))
      (= m (bitvector->natural after))
      (for/fold ([ok #t]) ([i (in-naturals)] [actual new-slots] [expected words])
        (&& ok (=> (< i m) (bveq actual expected))))))

#| Weaken a proved, unconditional cycle contract to its sampled FIFO transfers. Only the
   certified snapshot is stepped; a cycle output outside care is an arbitrary physical value. |#
(define (derive-fifo-transactions cycle view history-field)
  (define-values (definition ingress egress width sampler) (fifo_view_parts view))
  (unless (and (refinement-result? cycle) (host-eq? definition (refinement-result-definition cycle))
               (host-equal? (fifo_view_layout view) (refinement-result-layout cycle))
               (host-equal? (refinement-result-protocol cycle) "single-clock-reset"))
    (error 'derive-fifo-transactions "expected cycle evidence for this exact definition and member layout"))
  (define model (refinement-result-model cycle))
  (unless (zero? (hash-count (hash-ref model "assumptions")))
    (error 'derive-fifo-transactions "conditional cycle evidence cannot export an unconditional FIFO contract"))
  (define capacity
    (match (hash-ref (hash-ref model "state") history-field #f)
      [(list "seq" n (list "bits" w)) #:when (= width w) n]
      [_ (error 'derive-fifo-transactions "expected a cycle-spec sequence of FIFO words: ~a" history-field)]))
  (define contract (candidate-fifo view capacity))
  (define e (symbolic-spec-edge model))
  (define inputs (edge-inputs e))
  (define reset (hash-ref inputs "reset"))
  (define-values (pins pins-ok)
    (for/fold ([pins inputs] [ok #t]) ([k (append (map second ingress) (list (first egress) (third egress)))])
      (unless (hash-has-key? (edge-outputs e) k)
        (error 'derive-fifo-transactions "cycle evidence does not cover output ~a" k))
      (define-values (v defined) (spec-pin-at model k (edge-state e) inputs))
      (values (hash-set pins k v) (&& ok defined))))
  (define-values (batch sampler-ok) (sampled-fifo-batch contract pins))
  #| Project the cycle spec's bounded sequence, never its unused slot padding or RTL registers. |#
  (define (history state)
    (match-define (sequence-value n slots) (hash-ref state history-field))
    (fifo-history (bitvector->natural n) slots))
  (define goals
    (list (cons 'sampler-domain sampler-ok)
          (cons 'defined (&& (edge-defined e) pins-ok (spec-state-legal? (edge-next e))))
          (cons 'event-relation (fifo-allows? contract (history (edge-state e)) batch (history (edge-next e))))))
  (check-phase 'cycle-to-fifo-reset (&& (edge-legal e) reset) goals)
  (check-phase 'cycle-to-fifo-step (&& (edge-legal e) (! reset)) goals)
  contract)

#| Prove a FIFO projection of an admitted synchronous cut using its bound transaction view.
   The proof author supplies the state abstraction, never the handshakes. |#
(define (prove-fifo-transactions plan capacity view project)
  (unless (exact-positive-integer? capacity) (error 'fifo-transactions "expected positive capacity"))
  (define contract (candidate-fifo view capacity))
  (unless (and (host-eq? (fifo-transactions-definition contract) (proved-cut-definition plan))
               (host-equal? (fifo_view_layout view) (proved-cut-layout plan)))
    (error 'fifo-transactions "view does not match the RTL cut"))
  (define ingress (single-ingress (fifo-transactions-ingress contract)))
  (define egress (fifo-transactions-egress contract))
  (define-symbolic* rst offered consuming boolean?)
  (define-symbolic* word (bitvector (fifo-transactions-width contract)))
  (define states (symbolic-cut-state plan))
  (define inputs (hash "reset" rst (first ingress) offered (third ingress) word (second egress) consuming))
  (define-values (pins edges settled) (proved-edge-at plan states inputs))
  (define sample (for/fold ([m inputs]) ([(k v) pins]) (hash-set m k v)))
  (define-values (events sample-ok) (sampled-fifo-batch contract sample))
  (define next (for/hash ([(site e) edges]) (values site (edge-next e))))
  (define legal (andmap spec-state-legal? (hash-values states)))
  (define goals
    (list (cons 'sampler sample-ok)
          (cons 'children (for/fold ([ok settled]) ([e (hash-values edges)])
                            (&& ok (edge-legal e) (edge-defined e) (spec-state-legal? (edge-next e)))))
          (cons 'event-relation (fifo-allows? contract (project states) events (project next)))))
  (check-phase 'transaction-reset (&& legal rst) goals)
  (check-phase 'transaction-step (&& legal (! rst)) goals)
  contract)

#| Direct cuts and the internal series handoff consume a single ingress handshake. |#
(define (single-ingress lanes)
  (unless (= (length lanes) 1) (error 'fifo-wiring "expected a single FIFO ingress"))
  (first lanes))

#| Check the two external handshakes and the internal valid/ready/payload paths against the actual
   parent drivers. Only this direct, shared-epoch cut licenses identifying the two middle events. |#
(define (check-series-wiring! top layout ingress egress a front b back)
  (unless (and (not (host-equal? a b)) (fifo-transactions? front) (fifo-transactions? back))
    (error 'fifo-series "transaction evidence does not match the RTL occurrences"))
  #| Only release the recorded physical-to-logical layout to its proved RTL occurrence. |#
  (define (layout-at site definition)
    (define child (cond [(host-equal? site a) front] [(host-equal? site b) back] [else #f]))
    (unless (and child (host-eq? definition (fifo-transactions-definition child)))
      (error 'fifo-series "transaction evidence does not match the RTL occurrences"))
    (fifo-transactions-layout child))
  ;; No unmodeled top pins, and no qualified parent member may alias an occurrence pin.
  (define top-pins
    (for*/list ([direction layout] [members (in-hash-values direction)] [member members]) (first member)))
  (define endpoints (append (list "reset") (append* ingress) egress))
  (unless (and (= (fifo-transactions-width front) (fifo-transactions-width back))
               (= (length ingress) (length (fifo-transactions-ingress front)))
               (host-equal? (sort top-pins string<?) (sort endpoints string<?))
               (for/and ([p top-pins])
                 (not (or (string-prefix? p (string-append a ".")) (string-prefix? p (string-append b "."))))))
    (error 'fifo-series "incompatible FIFO endpoints"))
  (define-values (_ wires) (series_wiring top layout layout-at))
  #| Qualify a physical pin at its actual occurrence. |#
  (define (pin site p) (string->immutable-string (string-append site "." p)))
  #| Preserve valid/ready/data order when qualifying a certified endpoint. |#
  (define (at site pins) (map (lambda (p) (pin site p)) pins))
  #| Check one driver, reporting the failing RTL sink before any proof hypotheses are built. |#
  (define (wire! sink source)
    (unless (host-equal? (hash-ref wires sink #f) source)
      (error 'fifo-series "expected ~a driven by ~a, got ~a" sink source (hash-ref wires sink #f))))
  #| Valid and data travel forward; the ready driver travels in the opposite direction. |#
  (define (link! from to)
    (match-define (list fv fr fd) from)
    (match-define (list tv tr td) to)
    (wire! tv fv) (wire! fr tr) (wire! td fd))
  (for* ([site (list a b)] [p '("clock" "reset")]) (wire! (pin site p) p))
  (for ([lane ingress] [front-lane (fifo-transactions-ingress front)])
    (link! lane (at a front-lane)))
  (link! (at a (fifo-transactions-egress front)) (at b (single-ingress (fifo-transactions-ingress back))))
  (link! (at b (fifo-transactions-egress back)) egress)
  ;; Four epoch drivers, three pins per ingress lane, one handoff and one egress.
  (unless (= (+ 4 (* 3 (+ 2 (length ingress)))) (hash-count wires))
    (error 'fifo-series "unexpected parent or child wiring")))

#| Place the downstream live prefix before the upstream live prefix; padding is unobservable.
   Splitting at concrete offsets keeps projection total even for arbitrary pre-reset counts. |#
(define (series-history downstream upstream)
  (match-define (fifo-history n back) downstream)
  (match-define (fifo-history m front) upstream)
  (fifo-history (+ n m)
                (for/fold ([slots (append back front)]) ([split (in-range (length back))])
                  (if (= split n) (append (take back split) front (drop back split)) slots))))

#| Certify the parent's transaction view over every joint step allowed by the two opaque theorems.
   No cycle offers are predicted; the checked wires justify sharing one transfer and word. |#
(define (compose-fifo-series view front-site front back-site back)
  (define-values (top ingress egress width sampler) (fifo_view_parts view))
  (check-series-wiring! top (fifo_view_layout view) ingress egress front-site front back-site back)
  (unless (= width (fifo-transactions-width front)) (error 'fifo-series "parent payload width mismatch"))
  (define capacity (+ (fifo-transactions-capacity front) (fifo-transactions-capacity back)))
  (define contract (candidate-fifo view capacity))
  (define a (fresh-fifo-history front))
  (define b (fresh-fifo-history back))
  (define an (fresh-fifo-history front))
  (define bn (fresh-fifo-history back))
  (define-symbolic* reset boolean?)
  #| Unconstrained observations, not offers driven by a child model. |#
  (define (fresh-link)
    (define-symbolic* valid ready boolean?)
    (define-symbolic* payload (bitvector width))
    (list valid ready payload))
  (define lanes (for/list ([_ ingress]) (fresh-link)))
  (define handoff (fresh-link))
  (define out (fresh-link))
  #| Bind independent ingress triples and the shared handoff only after checking their physical drivers. |#
  (define (sample child inputs outgoing)
    (define pins (append (append* (fifo-transactions-ingress child)) (fifo-transactions-egress child)))
    (sampled-fifo-batch child (for/fold ([m (hash "reset" reset)]) ([k pins] [v (append (append* inputs) outgoing)])
                                (hash-set m k v))))
  (define-values (front-batch front-ok) (sample front lanes handoff))
  (define-values (back-batch back-ok) (sample back (list handoff) out))
  (define-values (parent-batch parent-ok) (sample contract lanes out))
  ;; Every issuance path proves its own sampler domain. Only sealed child results supply
  ;; these guarantees at matched occurrences. Never assume the parent's domain or arbiter policy.
  (define child-domain (&& front-ok back-ok))
  (check-phase 'series-samplers child-domain (list (cons 'defined parent-ok)))
  (define allowed (&& child-domain (fifo-allows? front a front-batch an) (fifo-allows? back b back-batch bn)))
  (define parent-step
    (fifo-allows? contract (series-history b a) parent-batch (series-history bn an)))
  (define goals (list (cons 'external-history parent-step)))
  (check-phase 'series-reset (&& reset allowed) goals)
  (check-phase 'series-step (&& (! reset) allowed) goals)
  contract)

;; One symbolic edge, not a refinement certificate. Only parent pins/local edges and the
;; verified child's transfer batch/domain escape; there is no child cycle state.
(struct fifo-cut (inputs outputs locals batch domain))

#| Read one passive batch from the physical drivers of this occurrence, sharing their values. |#
(define (sample-cut-pins evidence inputs outputs)
  (sampled-fifo-batch evidence
    (for/hash ([k (hash-keys (hash-ref (fifo-transactions-sampler evidence) "inputs"))])
      (values k (hash-ref inputs k (lambda () (hash-ref outputs k)))))))

#| Sample both sides of an exact one-child RTL boundary. Child outputs are arbitrary;
   execute and prove defined only the parent wiring and registers. Issues no evidence. |#
(define (symbolic-wrapper-cut top layout bool site expected child-layout visible)
  #| Bind this pin layout only at the requested occurrence of the expected definition. |#
  (define (layout-at name definition)
    (unless (and (host-equal? name site) (host-eq? definition expected))
      (error 'wrapper-cut "transaction evidence does not match the RTL occurrence"))
    child-layout)
  (define graph (wrapper_wiring top layout site layout-at visible bool))
  (define locals (hash-ref graph "children"))
  (define states (for/hash ([(k model) locals]) (values k (symbolic-spec-state model))))
  (define inputs (fresh-fields (hash-ref graph "inputs")))
  (define-values (outputs edges settled) (wired-edge-at graph locals states inputs))
  (define local-ok (for/fold ([ok settled]) ([e (in-hash-values edges)])
                     (&& ok (edge-legal e) (edge-defined e) (spec-state-legal? (edge-next e)))))
  (check-phase 'wrapper-rtl #t (list (cons 'defined local-ok)))
  (define prefix (string-append site "."))
  #| Strip the site qualifier only on the child side; at that boundary graph directions reverse. |#
  (define (side pins child?)
    (for/hash ([(k v) pins] #:when (host-equal? child? (string-prefix? k prefix)))
      (values (if child? (string->immutable-string (substring k (string-length prefix))) k) v)))
  (values (side inputs #f) (side outputs #f) edges (side outputs #t) (side inputs #t)))

#| Sample the verified FIFO on its actual occurrence without opening its cycle implementation. |#
(define (symbolic-fifo-cut top layout bool site child)
  (unless (fifo-transactions? child) (error 'fifo-wrapper "expected FIFO transaction evidence"))
  (define-values (inputs outputs edges ci co)
    (symbolic-wrapper-cut top layout bool site (fifo-transactions-definition child) (fifo-transactions-layout child)
                          (hash-keys (hash-ref (fifo-transactions-sampler child) "inputs"))))
  (define-values (inside child-ok) (sample-cut-pins child ci co))
  (fifo-cut inputs outputs edges inside child-ok))

#| Transfer an opaque FIFO theorem through a single wrapper whose RTL preserves entire event
   batches. The bounded history is the child's transaction history, not operational state. |#
(define (prove-fifo-wrapper view site child)
  (define-values (top ingress egress width sampler) (fifo_view_parts view))
  (unless (and (fifo-transactions? child) (= width (fifo-transactions-width child)))
    (error 'fifo-wrapper "expected matching-width FIFO transaction evidence"))
  (match-define (fifo-cut inputs outputs _ inside child-ok)
    (symbolic-fifo-cut top (fifo_view_layout view) (fifo_view_bool view) site child))
  (define reset (hash-ref inputs "reset"))
  (define result (candidate-fifo view (fifo-transactions-capacity child)))
  (define-values (outside parent-ok) (sample-cut-pins result inputs outputs))
  #| Inactive payload is unconstrained. An active transfer must agree in both directions. |#
  (define (same-transfer? fires word)
    (&& (equal? (fires inside) (fires outside))
        (=> (fires inside) (bveq (word inside) (word outside)))))
  (define goals
    (list (cons 'sampler-domain parent-ok)
          (cons 'epoch (equal? (fifo-events-reset inside) (fifo-events-reset outside)))
          (cons 'accepted (same-transfer? fifo-events-accepted? fifo-events-accepted))
          (cons 'retired (same-transfer? fifo-events-retired? fifo-events-retired))))
  (check-phase 'wrapper-reset (&& reset child-ok) goals)
  (check-phase 'wrapper-step (&& (! reset) child-ok) goals)
  ;; Same capacity, monitor, and live history. Transport every allowed child successor.
  (define old (fresh-fifo-history child))
  (define next (fresh-fifo-history child))
  (define allowed (&& child-ok (fifo-allows? child old inside next)))
  (define history (list (cons 'external-history (fifo-allows? result old outside next))))
  (check-phase 'wrapper-history-reset (&& reset allowed) history)
  (check-phase 'wrapper-history-step (&& (! reset) allowed) history)
  result)
