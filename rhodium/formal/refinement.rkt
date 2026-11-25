#lang rosette
#| Proves leaf and parent refinement across verified child boundaries and admits their results at exact RTL child occurrences. |#
;; SPDX-License-Identifier: Apache-2.0

(require racket/match (only-in racket/base [equal? host-equal?] [eq? host-eq?])
         "engine.rkt" "spec-engine.rkt" (only-in "snapshot.rhm" formal_snapshot) (only-in "spec.rhm" spec_snapshot)
         (only-in "fifo-monitor.rhm" fifo_monitor)
         (only-in "fifo-view.rhm" fifo_view_parts)
         (only-in "wiring.rhm" cut_parts leaf_protocol leaf_layout scalar_series_wiring))
(provide prove-leaf prove-parent prepare-proved-wiring proved-child-model symbolic-cut-state proved-edge-at view-agrees? pack-pins unpack-pins)
(provide exn:fail:input-contract? exn:fail:input-contract-goal exn:fail:input-contract-inputs)
(provide prove-fifo-transactions compose-fifo-series fifo-transactions-for? fifo-transactions-capacity fifo-transactions-width
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
(struct fifo-transactions (definition ingress egress capacity width sampler monitor))
(struct fifo-history (count slots) #:transparent)
(struct fifo-events (reset accepted? accepted retired? retired) #:transparent)

#| Match the exact run-local definition and ordered endpoint mapping, not just width and capacity. |#
(define (fifo-transactions-for? result view)
  (define-values (definition ingress egress width sampler) (fifo_view_parts view))
  (and (fifo-transactions? result) (host-eq? definition (fifo-transactions-definition result))
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
  (fifo-transactions definition ingress egress capacity width
                     (spec_snapshot sampler) (spec_snapshot (fifo_monitor capacity width))))

#| Interpret the declared passive sampler, preserving presence even for a zero payload. |#
(define (sampled-fifo-batch contract pins)
  (define e (spec-edge-at (fifo-transactions-sampler contract) (hash) pins))
  (define events (edge-events e))
  (define accepted (hash-ref events "accepted"))
  (define retired (hash-ref events "retired"))
  (values (fifo-events (observation-care (hash-ref events "reset"))
                       (observation-care accepted) (observation-value accepted)
                       (observation-care retired) (observation-value retired))
          (&& (edge-legal e) (edge-defined e))))

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

#| Prove a FIFO projection of an admitted synchronous cut using its bound transaction view.
   The proof author supplies the state abstraction, never the handshakes. |#
(define (prove-fifo-transactions plan capacity view project)
  (unless (exact-positive-integer? capacity) (error 'fifo-transactions "expected positive capacity"))
  (define contract (candidate-fifo view capacity))
  (unless (host-eq? (fifo-transactions-definition contract) (proved-cut-definition plan))
    (error 'fifo-transactions "view does not match the RTL cut"))
  (define ingress (fifo-transactions-ingress contract))
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

#| Check the two external handshakes and the internal valid/ready/payload paths against the actual
   parent drivers. Only this direct, shared-epoch cut licenses identifying the two middle events. |#
(define (check-series-wiring! top ingress egress a front b back)
  (define-values (children wires) (scalar_series_wiring top))
  (unless (and (not (host-equal? a b))
               (for/and ([site (list a b)] [child (list front back)])
                 (and (fifo-transactions? child)
                      (host-eq? (hash-ref children site #f) (fifo-transactions-definition child)))))
    (error 'fifo-series "transaction evidence does not match the RTL occurrences"))
  (unless (and (= (fifo-transactions-width front) (fifo-transactions-width back))
               (andmap (lambda (p) (and (string? p) (not (regexp-match? #rx"[.]" p)))) (append ingress egress))
               (= 8 (length (remove-duplicates (append '("clock" "reset") ingress egress)))))
    (error 'fifo-series "incompatible FIFO endpoints"))
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
  (link! ingress (at a (fifo-transactions-ingress front)))
  (link! (at a (fifo-transactions-egress front)) (at b (fifo-transactions-ingress back)))
  (link! (at b (fifo-transactions-egress back)) egress)
  ;; Four epoch drivers and three links of three wires each; no additional sinks.
  (unless (= 13 (hash-count wires)) (error 'fifo-series "unexpected parent or child wiring")))

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
  (check-series-wiring! top ingress egress front-site front back-site back)
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
  (define in (fresh-link))
  (define handoff (fresh-link))
  (define out (fresh-link))
  #| Wire one declared sampler to the shared triples justified by the structural check. |#
  (define (sample child incoming outgoing)
    (define pins (append (fifo-transactions-ingress child) (fifo-transactions-egress child)))
    (sampled-fifo-batch child (for/fold ([m (hash "reset" reset)]) ([k pins] [v (append incoming outgoing)])
                                (hash-set m k v))))
  (define-values (front-batch front-ok) (sample front in handoff))
  (define-values (back-batch back-ok) (sample back handoff out))
  (define-values (parent-batch parent-ok) (sample contract in out))
  (check-phase 'series-samplers #t (list (cons 'defined (&& front-ok back-ok parent-ok))))
  (define allowed (&& (fifo-allows? front a front-batch an) (fifo-allows? back b back-batch bn)))
  (define parent-step
    (fifo-allows? contract (series-history b a) parent-batch (series-history bn an)))
  (define goals (list (cons 'external-history parent-step)))
  (check-phase 'series-reset (&& reset allowed) goals)
  (check-phase 'series-step (&& (! reset) allowed) goals)
  contract)
