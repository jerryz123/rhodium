#lang rosette
;; Proves the wiring-only QueuePair parent from opaque ShiftQueue transaction results.
;; SPDX-License-Identifier: Apache-2.0

(require (only-in "../refinement.rkt" compose-fifo-series)
         (only-in "queue-pair.rspl" QueuePair)
         (only-in "composition-binding.rhm" pair_at)
         (only-in "shift-queue-transactions.rkt" verified-shift-queue-transactions-at))
(provide grouped-pair-at)

(define (certify top front back)
  (compose-fifo-series (QueuePair top #:event_view #t) "a" front "b" back))
(define proved
  (for/hash ([depth '(1 2)])
    (define leaf (verified-shift-queue-transactions-at depth))
    (values depth (certify (pair_at depth) leaf leaf))))
(define (grouped-pair-at depth) (hash-ref proved depth))

(module+ test
  (require rackunit
           (only-in "../refinement.rkt" fifo-transactions-capacity fifo-transactions-for?)
           (only-in "composition-binding.rhm" faulty_pair)
           (only-in "composition-checks.rkt" require-feasible)
           (only-in "queue-pair-transaction-order.rkt" check-retirement-order check-ready-not-entailed))
  (for ([depth '(1 2)])
    (test-case (format "two grouped depth-~a FIFO contracts imply order and capacity" depth)
      (define result (grouped-pair-at depth))
      (check-true (fifo-transactions-for? result (QueuePair (pair_at depth) #:event_view #t)))
      (check-equal? (fifo-transactions-capacity result) (* 2 depth))
      (require-feasible (check-retirement-order result))
      (check-ready-not-entailed result)))
  (test-case "member drivers are checked before handoff events are shared"
    (define leaf (verified-shift-queue-transactions-at 2))
    (define (fault kind) (lambda () (certify (faulty_pair kind) leaf leaf)))
    (check-exn #rx"expected a.egress.ready driven by b.ingress.ready, got consume" (fault "ready"))
    (check-exn #rx"expected b.ingress.bits driven by a.egress.bits, got data" (fault "payload"))
    (check-exn #rx"expected head driven by b.egress.bits, got b.mask" (fault "mask"))
    (check-exn #rx"evidence does not match the RTL occurrences"
               (lambda () (certify (pair_at 2) leaf (verified-shift-queue-transactions-at 1))))))
