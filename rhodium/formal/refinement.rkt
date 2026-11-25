#lang rosette
#| Proves leaf and parent refinement across verified child boundaries and admits their results at exact RTL child occurrences. |#
;; SPDX-License-Identifier: Apache-2.0

(require racket/match (only-in racket/base [equal? host-equal?] [eq? host-eq?])
         "engine.rkt" "spec-engine.rkt" "transaction-induction.rkt" (only-in "snapshot.rhm" formal_snapshot)
         (only-in "spec.rhm" spec_snapshot monitor_projection)
         (only-in "fifo-monitor.rhm" fifo_monitor)
         (only-in "fifo-view.rhm" fifo_view_parts fifo_view_layout fifo_view_bool fifo_view_behavior fifo_series_parts)
         (only-in "wiring.rhm" cut_parts leaf_protocol leaf_layout check_fifo_series_wiring))
(provide prove-leaf prove-parent prepare-proved-wiring proved-child-model symbolic-cut-state proved-edge-at view-agrees? pack-pins unpack-pins)
(provide exn:fail:input-contract? exn:fail:input-contract-goal exn:fail:input-contract-inputs)
(provide check-batch-total! check-cycle-monitor! (rename-out [check-phase check-refinement-phase!]))
(provide prove-fifo-series-refinement fifo-monitor-site)
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
  (define declared (fifo_view_behavior view))
  (and (fifo-transactions? result) (host-eq? definition (fifo-transactions-definition result))
       (host-equal? (fifo_view_layout view) (fifo-transactions-layout result))
       (host-equal? ingress (fifo-transactions-ingress result))
       (host-equal? egress (fifo-transactions-egress result))
       (or (not declared) (host-equal? (spec_snapshot declared) (fifo-transactions-monitor result)))))

#| Allocate a candidate pending sequence; only its live prefix participates in the contract. |#
(define (fresh-fifo-history result)
  (define-symbolic* count integer?)
  (fifo-history count (for/list ([i (in-range (fifo-transactions-capacity result))])
                        (define-symbolic* word (bitvector (fifo-transactions-width result)))
                        word)))

#| Create a candidate contract; only successful certification returns it to callers. |#
(define (candidate-fifo view capacity [monitor #f])
  (define-values (definition ingress egress width sampler) (fifo_view_parts view))
  (fifo-transactions definition (fifo_view_layout view) ingress egress capacity width
                     (spec_snapshot sampler) (or monitor (spec_snapshot (fifo_monitor capacity width)))))

#| A candidate may test a different capacity, but cannot be published as the declared behavior. |#
(define (checked-fifo-result view candidate)
  (unless (fifo-transactions-for? candidate view) (error 'fifo-transactions "proof does not match declared FIFO behavior"))
  candidate)

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

#| Admit only the certified unconditional clocked specialization, never a replacement model. |#
(define (certified-cycle-model cycle definition layout)
  (unless (and (refinement-result? cycle) (host-eq? definition (refinement-result-definition cycle))
               (host-equal? layout (refinement-result-layout cycle))
               (host-equal? (refinement-result-protocol cycle) "single-clock-reset"))
    (error 'cycle-view "expected cycle evidence for this exact definition and member layout"))
  (define model (refinement-result-model cycle))
  (unless (zero? (hash-count (hash-ref model "assumptions")))
    (error 'cycle-view "conditional cycle evidence cannot export an unconditional transaction contract"))
  model)

#| Prove a public monitor using only certified cycle state and public pre-edge pins. Both
   issuers keep the checked sampler with the evidence; this low-level check issues none. |#
(define (check-cycle-monitor! cycle definition layout monitor-model relation ins outs sample)
  (define model (certified-cycle-model cycle definition layout))
  (define monitor (spec_snapshot monitor-model))
  (unless (zero? (hash-count (hash-ref monitor "assumptions"))) (error 'cycle-view "conditional monitor"))
  (define projection (monitor_projection monitor-model relation))
  (for ([names (list ins outs)] [direction layout])
    (define members (map car (append* (hash-values direction))))
    (for ([name names])
      (unless (member name members) (error 'cycle-view "unknown pin or wrong direction: ~a" name))))
  (define sources (hash-ref projection "inputs"))
  (for ([(name sort) sources])
    (unless (host-equal? sort (hash-ref (hash-ref model "state") name #f))
      (error 'cycle-view "missing or mistyped cycle state: ~a" name)))
  #| Re-evaluate the same IR over old, next and initial cycle state, never monitor or RTL state. |#
  (define (project state)
    (spec-edge-at projection (hash) (for/hash ([k (hash-keys sources)]) (values k (hash-ref state k)))))
  (define e (symbolic-spec-edge model))
  (define inputs (edge-inputs e))
  (define-values (outputs pins-ok)
    (for/fold ([pins (hash)] [ok #t]) ([k (remove-duplicates outs)])
      (unless (hash-has-key? (edge-outputs e) k)
        (error 'cycle-view "cycle evidence does not cover output ~a" k))
      ;; Allocate one physical value per output, shared by all reads even when outside care.
      (define-values (v defined) (spec-pin-at model k (edge-state e) inputs))
      (values (hash-set pins k v) (&& ok defined))))
  (define-values (batch sampler-ok)
    (sample (for/hash ([k ins]) (values k (hash-ref inputs k))) outputs))
  (check-monitor-edge! (lambda (phase premise goals) (check-phase (format "cycle-view-~a" phase) premise goals))
                       monitor batch (hash-ref inputs "reset") (edge-legal e)
                       (project (edge-state e)) (project (edge-next e)) (project (spec-initial-state model))
                       (list (cons 'sampler-domain sampler-ok)
                             (cons 'defined (&& (edge-defined e) pins-ok (spec-state-legal? (edge-next e)))))))

#| Preserve the FIFO event API and canonical history monitor; induction is protocol independent. |#
(define (derive-fifo-transactions cycle view history-field)
  (define-values (definition ingress egress width sampler) (fifo_view_parts view))
  (define layout (fifo_view_layout view))
  (define model (certified-cycle-model cycle definition layout))
  (define capacity
    (match (hash-ref (hash-ref model "state") history-field #f)
      [(list "seq" n (list "bits" w)) #:when (= width w) n]
      [_ (error 'derive-fifo-transactions "expected a cycle-spec sequence of FIFO words: ~a" history-field)]))
  (define monitor (fifo_monitor capacity width))
  (define contract (candidate-fifo view capacity (spec_snapshot monitor)))
  (define outs (append (map second ingress) (list (first egress) (third egress))))
  (define ins (remove* outs (hash-keys (hash-ref (fifo-transactions-sampler contract) "inputs"))))
  (check-cycle-monitor! cycle definition layout monitor (hash "q" history-field) ins outs
                        (lambda (inputs outputs) (fifo-monitor-batch contract inputs outputs)))
  (checked-fifo-result view contract))

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
  (checked-fifo-result view contract))

#| Direct cuts and the internal series handoff consume a single ingress handshake. |#
(define (single-ingress lanes)
  (unless (= (length lanes) 1) (error 'fifo-wiring "expected a single FIFO ingress"))
  (first lanes))

#| Only sealed FIFO evidence may supply occurrence identity and endpoints for a handoff proof. |#
(define (check-series-wiring! top layout ingress egress a front b back)
  (unless (and (fifo-transactions? front) (fifo-transactions? back))
    (error 'fifo-series "transaction evidence does not match the RTL occurrences"))
  #| Project only concrete interface metadata, never the symbolic transition or issuance authority. |#
  (define (endpoint child)
    (hash "definition" (fifo-transactions-definition child) "layout" (fifo-transactions-layout child)
          "width" (fifo-transactions-width child) "ingress" (fifo-transactions-ingress child)
          "egress" (fifo-transactions-egress child)))
  (check_fifo_series_wiring top layout ingress egress a (endpoint front) b (endpoint back)))

#| Place the downstream live prefix before the upstream live prefix; padding is unobservable.
   Splitting at concrete offsets keeps projection total even for arbitrary pre-reset counts. |#
(define (series-history downstream upstream)
  (match-define (fifo-history n back) downstream)
  (match-define (fifo-history m front) upstream)
  (fifo-history (+ n m)
                (for/fold ([slots (append back front)]) ([split (in-range (length back))])
                  (if (= split n) (append (take back split) front (drop back split)) slots))))

#| Build an edge, not evidence. Match both opaque contracts to actual occurrences and verify all
   member drivers before sharing the handoff. The two child history transitions remain independent. |#
(define (fifo-series-edge view front-site front back-site back)
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
  (values contract a b an bn parent-batch reset allowed))

#| Handwritten induction: combine live child prefixes in retirement order on both sides of the edge. |#
(define (compose-fifo-series view front-site front back-site back)
  (define-values (contract a b an bn parent-batch reset allowed) (fifo-series-edge view front-site front back-site back))
  (define parent-step
    (fifo-allows? contract (series-history b a) parent-batch (series-history bn an)))
  (define goals (list (cons 'external-history parent-step)))
  (check-phase 'series-reset (&& reset allowed) goals)
  (check-phase 'series-step (&& (! reset) allowed) goals)
  (checked-fifo-result view contract))

#| Encode FIFO history without truncating an invalid count or silently treating a non-tag word as false. |#
(define (fifo-value q sort child)
  (match-define (list "seq" capacity element) sort)
  (define width (fifo-transactions-width child))
  (unless (and (= capacity (fifo-transactions-capacity child))
               (or (host-equal? element '("bool")) (host-equal? element (list "bits" width))))
    (error 'refinement "FIFO evidence does not match the declared sequence"))
  (define n (fifo-history-count q))
  (define words (fifo-history-slots q))
  (define tags? (host-equal? element '("bool")))
  (values (sequence-value (integer->bitvector n (bitvector (integer-length capacity)))
                          (if tags? (map (lambda (w) (bveq w (bv 1 width))) words) words))
          (&& (<= 0 n capacity)
              (for/fold ([ok #t]) ([w words] [i (in-naturals)])
                (&& ok (=> (&& tags? (< i n)) (|| (bveq w (bv 0 width)) (bveq w (bv 1 width)))))))))

#| Adapt a certified FIFO to an ordinary monitor cut; the mathematical count and optional
   tag encoding are checked before and after each admitted transition. |#
(define (fifo-monitor-site child key sort)
  (unless (fifo-transactions? child) (error 'refinement "expected FIFO transaction evidence"))
  #| Allocate fresh live histories for this occurrence and sample its actual directional drivers. |#
  (define (advance ci co _inputs _outputs)
    (define-values (batch sampled) (sample-cut-pins child ci co))
    (define old (fresh-fifo-history child))
    (define next (fresh-fifo-history child))
    (define-values (pre ok) (fifo-value old sort child))
    (define-values (post next-ok) (fifo-value next sort child))
    (define start (hash-ref (spec-initial-state (fifo-transactions-monitor child)) "q"))
    (define-values (empty _) (fifo-value (fifo-history 0 (sequence-value-slots start)) sort child))
    (values (&& sampled (fifo-allows? child old batch next)) (hash key pre) ok (hash key post) next-ok (hash key empty)))
  (monitor-site (fifo-transactions-definition child) (fifo-transactions-layout child)
                (hash-keys (hash-ref (fifo-transactions-sampler child) "inputs")) advance))

#| The FIFO protocol checks its direct handoff and adapts sparse events to its canonical
   batch. Wiring evaluation, independent child steps, projection and induction use the shared checker. |#
(define (prove-fifo-series-refinement view children)
  (match-define (list (list a b) cuts relation checks) (fifo_series_parts view))
  (unless (and (hash? children) (host-equal? (sort (hash-keys children) string<?) (sort (list a b) string<?)))
    (error 'fifo-series "expected evidence for both declared occurrences"))
  (define-values (top ingress egress width sampler) (fifo_view_parts view))
  (define layout (fifo_view_layout view))
  (check-series-wiring! top layout ingress egress a (hash-ref children a) b (hash-ref children b))
  (define monitor (spec_snapshot (fifo_view_behavior view)))
  (match-define (list "seq" capacity (list "bits" _)) (hash-ref (hash-ref monitor "state") "q"))
  (define contract (candidate-fifo view capacity monitor))
  (define sites
    (for/hash ([(k child) children])
      (define key (hash-ref cuts k))
      (values k (fifo-monitor-site child key (hash-ref (hash-ref relation "inputs") key)))))
  (check-monitor-refinement! check-phase top layout (fifo_view_bool view) sites monitor
                             (lambda (i o) (fifo-monitor-batch contract i o)) relation checks)
  (checked-fifo-result view contract))

#| Feed the canonical FIFO monitor its dense batch while retaining the sampler's domain obligation. |#
(define (fifo-monitor-batch contract inputs outputs)
  (define-values (batch ok) (sample-cut-pins contract inputs outputs))
  (match-define (fifo-events reset push in pop out) batch)
  (values (hash "reset" reset "push" push "in" in "pop" pop "out" out) ok))

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
  (define-values (inputs outputs edges pins)
    (open-monitor-sites top layout bool (hash site (monitor-site expected child-layout visible #f)) check-phase))
  (match-define (list ci co) (hash-ref pins site))
  (values inputs outputs edges ci co))

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
  (checked-fifo-result view result))
