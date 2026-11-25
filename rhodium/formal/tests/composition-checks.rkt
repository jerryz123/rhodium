#lang rosette
;; Shared wiring-boundary checks for same-run compositional proof fixtures.
;; SPDX-License-Identifier: Apache-2.0

(require (only-in racket/base [equal? host-equal?]) "../spec-engine.rkt")
(provide check-cut pin event matches? same-event discharge require-feasible)

(define (check-cut wires expected-wires)
  (define netlist (for/hash ([(k v) wires]) (values k v)))
  (unless (equal? netlist expected-wires)
    (error 'check-cut "wiring outside the verified child contract"))
  netlist)

;; On an uncared output the implementation may drive *any* physical value.
;; In particular, the spec's totalized front(empty) is not a wiring guarantee.
(define (pin field sort)
  (define-symbolic* hidden sort)
  (if (observation-care field) (observation-value field) hidden))

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
