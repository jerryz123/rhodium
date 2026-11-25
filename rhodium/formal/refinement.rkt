#lang rosette
#| Checks fixed leaf refinement obligations and admits their results at exact RTL child occurrences. |#
;; SPDX-License-Identifier: Apache-2.0

(require racket/match (only-in racket/base [equal? host-equal?] [eq? host-eq?])
         "engine.rkt" "spec-engine.rkt" (only-in "snapshot.rhm" formal_snapshot) (only-in "spec.rhm" spec_snapshot)
         (only-in "wiring.rhm" cut_parts leaf_protocol leaf_layout))
(provide prove-leaf prepare-proved-wiring proved-child-model proved-edge-at view-agrees? pack-pins unpack-pins)

;; Private constructors. These are live elaboration identities, not transferable certificates.
;; Both supported protocols quantify over unrestricted external input samples.
(struct leaf-result (definition model protocol layout))
(struct proved-cut (graph models))

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
  (unless (host-equal? (vc) vc-true) (error 'prove-leaf "unexpected ambient verification condition")))

#| Never issue evidence from an infeasible premise, failed obligation, or unknown solver answer. |#
(define (check-phase name premise goals)
  (clean-vc!)
  (unless (sat? (solve (assert premise))) (error 'prove-leaf "~a: infeasible or unknown domain" name))
  (for ([goal goals])
    (define answer (solve (assert (&& premise (! (cdr goal))))))
    (unless (unsat? answer) (error 'prove-leaf "~a/~a not proved: ~a" name (car goal) answer))))

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
     (define reset (hash-ref inputs "reset"))
     (unless (boolean? reset) (error 'prove-leaf "expected Boolean synchronous reset"))
     (define next (edge-next spec))
     (define related-next (cons 'successor (relation (transition_step_state rtl) next)))
     (define start (for/fold ([ok #t]) ([(k type) (hash-ref model "state")])
                     (&& ok (at-start? (hash-ref next k) type (hash-ref (hash-ref model "initial") k)))))
     (define related-old (relation old (edge-state spec)))
     (check-phase 'reset (&& (edge-legal spec) reset) (list* (cons 'initial start) related-next common))
     (check-phase 'preservation (&& (edge-legal spec) related-old) (cons related-next common))]
    [else
     (unless (zero? (hash-count (edge-state spec))) (error 'prove-leaf "expected a stateless model"))
     (check-phase 'combinational #t (cons (cons 'domain (edge-legal spec)) common))])
  (leaf-result definition model protocol layout))

#| Before any parent query, resolve each site's exact definition and full spec (start, sorts,
   care, values, events and updates). One proof may cover several occurrences, not their states. |#
(define (prepare-proved-wiring cut results)
  (define-values (graph definitions layouts) (cut_parts cut))
  (unless (and (list? results) (andmap leaf-result? results))
    (error 'prepare-proved-wiring "expected completed leaf proof results"))
  (define models
    (for/hash ([(site definition) definitions])
      (define candidates (filter (lambda (r) (host-eq? definition (leaf-result-definition r))) results))
      (when (null? candidates)
        (error 'prepare-proved-wiring "no evidence for RTL specialization at ~a" site))
      (define target (hash-ref (hash-ref graph "children") site))
      (define protocol (leaf_protocol definition))
      (define matched (findf (lambda (r) (and (host-equal? protocol (leaf-result-protocol r))
                                              (host-equal? (hash-ref layouts site) (leaf-result-layout r))
                                              (host-equal? target (leaf-result-model r)))) candidates))
      (unless matched (error 'prepare-proved-wiring "evidence does not cover contract at ~a" site))
      (values site (leaf-result-model matched))))
  (proved-cut graph models))

#| Access the admitted operational model for one occurrence, e.g. to allocate its abstract state. |#
(define (proved-child-model plan site) (hash-ref (proved-cut-models plan) site))

#| Step an admitted cut without inspecting child RTL or accepting replacement models from the caller. |#
(define (proved-edge-at plan states inputs)
  (wired-edge-at (proved-cut-graph plan) (proved-cut-models plan) states inputs))
