#lang rosette
;; Shared wiring-boundary checks for same-run compositional proof fixtures.
;; SPDX-License-Identifier: Apache-2.0

(require (only-in racket/base [equal? host-equal?]) racket/match "../spec-engine.rkt")
(provide event matches? same-event discharge require-feasible without-child-proofs)

;; Keep the real parent plan and public child views. Any recursive proof request is a test failure.
(define (without-child-proofs spec top)
  (define ((public-only child-spec) child #:transaction_view [public? #f] #:proof_plan [plan? #f])
    (unless (and public? (not plan?)) (error 'without-child-proofs "attempted to reprove a child"))
    (child-spec child #:transaction_view #t))
  (define plan
    (match (spec top #:proof_plan #t)
      [(list "parent" view bindings req links domain)
       (list "parent" view
             (for/hash ([(site binding) bindings])
               (values site (list (first binding) (public-only (second binding)))))
             req links domain)]
      [(list "forward" contract site child req)
       (list "forward" contract site (public-only child) req)]))
  (lambda (_ #:proof_plan requested?) plan))

(define (event e name) (hash-ref (edge-events e) name))
(define (matches? field present value)
  (&& (equal? (observation-care field) present) (=> present (equal? (observation-value field) value))))
(define (same-event x y) (matches? x (observation-care y) (observation-value y)))

;; Stop on SAT or UNKNOWN, rather than restricting the search by an unproved assumption.
(define (discharge name premise goal)
  (unless (host-equal? (vc) vc-true) (error 'composition-proof "~a: unexpected ambient VC" name))
  (define answer (solve (assert (&& premise (! goal)))))
  (unless (unsat? answer) (error 'composition-proof "~a not discharged: ~a" name answer)))
(define (require-feasible premise)
  (unless (sat? (solve (assert premise))) (error 'composition-proof "infeasible or unknown proof domain")))
