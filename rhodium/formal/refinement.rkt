#lang rosette
#| Proves leaf and parent refinement across verified child boundaries and admits their results at exact RTL child occurrences. |#
;; SPDX-License-Identifier: Apache-2.0

(require racket/match (only-in racket/base [equal? host-equal?] [eq? host-eq?])
         "engine.rkt" "spec-engine.rkt" (only-in "snapshot.rhm" formal_snapshot) (only-in "spec.rhm" spec_snapshot)
         (only-in "wiring.rhm" cut_parts leaf_protocol leaf_layout))
(provide prove-leaf prove-parent prepare-proved-wiring proved-child-model symbolic-cut-state proved-edge-at view-agrees? pack-pins unpack-pins)
(provide exn:fail:input-contract? exn:fail:input-contract-goal exn:fail:input-contract-inputs)

;; Private constructors. These are live elaboration identities, not transferable certificates.
;; A conditional leaf result carries its current-input premises in the exact model snapshot.
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
  (define allowed (andmap identity (hash-values (spec-input-assumptions model inputs))))
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

#| Before any parent query, resolve each site's exact definition and full spec (start, sorts,
   care, values, events and updates). One proof may cover several occurrences, not their states. |#
(define (prepare-proved-wiring cut results)
  (define-values (definition layout graph definitions layouts locals) (cut_parts cut))
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
    (discharge-wired-inputs! graph all
                            (lambda (inputs legal goals) (check-phase 'child-input legal goals #:inputs inputs))))
  (proved-cut definition layout graph all))

#| Access the admitted operational model for one occurrence, e.g. to allocate its abstract state. |#
(define (proved-child-model plan site) (hash-ref (proved-cut-models plan) site))

#| Allocate one independent abstract state per child and the actual local RTL register fields, if any. |#
(define (symbolic-cut-state plan)
  (for/hash ([(site model) (proved-cut-models plan)]) (values site (symbolic-spec-state model))))

#| Step an admitted cut without inspecting child RTL or accepting replacement models from the caller. |#
(define (proved-edge-at plan states inputs)
  (wired-edge-at (proved-cut-graph plan) (proved-cut-models plan) states inputs))

#| Prove the external contract of an admitted synchronous cut. The plan owns the implementation
   identity; the caller supplies only the reference model, external observer and state relation. |#
(define (prove-parent plan spec-model observe #:relation relation)
  (clean-vc!)
  (define model (spec_snapshot spec-model))
  (define graph (proved-cut-graph plan))
  (unless (zero? (hash-count (hash-ref model "assumptions")))
    (error 'prove-parent "parent contracts must be unconditional"))
  (unless (host-equal? (hash-ref graph "inputs") (hash-ref model "inputs"))
    (error 'prove-parent "parent input schema mismatch"))
  (for ([(k field) (hash-ref model "outputs")])
    (unless (host-equal? (hash-ref (hash-ref graph "output-types") k #f) (second (second field)))
      (error 'prove-parent "parent output schema mismatch: ~a" k)))
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
                      (relation states (edge-state spec)) (relation after (edge-next spec)) common)
  (refinement-result (proved-cut-definition plan) model "single-clock-reset" (proved-cut-layout plan)))
