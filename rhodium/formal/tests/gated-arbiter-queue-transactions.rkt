#lang rosette
;; Check that the actual gate preserves an opaque child's accepted/retired FIFO batches.
;; SPDX-License-Identifier: Apache-2.0

(require (only-in "../refinement.rkt" prove-fifo-wrapper)
         (only-in "../../../examples/formal/gated-arbiter-queue.rspl" GatedArbiterQueue)
         (only-in "composition-binding.rhm" gated_at proof_depths)
         (only-in "arbiter-queue-transactions.rkt" verified-arbiter-queue-transactions-at))
(provide verified-gated-transactions-at)

(define (view depth) (GatedArbiterQueue (gated_at depth) #:event_view #t))
(define verified
  (for/hash ([depth proof_depths])
    (values depth (prove-fifo-wrapper (view depth) "queue" (verified-arbiter-queue-transactions-at depth)))))
(define (verified-gated-transactions-at depth) (hash-ref verified depth))

(module+ test
  (require rackunit
           (only-in "../refinement.rkt" fifo-transactions-for? fifo-transactions-capacity)
           (only-in "composition-binding.rhm" leaky_at)
           (only-in "leaky-gate.rspl" LeakyGate)
           (only-in "queue-pair-transaction-order.rkt" check-retirement-order check-ready-not-entailed))
  (for ([depth proof_depths])
    (test-case (format "opaque gate: transfer preservation and FIFO order at depth ~a" depth)
      (define result (verified-gated-transactions-at depth))
      (check-true (fifo-transactions-for? result (view depth)))
      (check-equal? (fifo-transactions-capacity result) depth)
      (define cover (check-retirement-order result))
      (define behind-head (solve (assert cover)))
      (check-true (if (= depth 1) (unsat? behind-head) (sat? behind-head)))
      (check-ready-not-entailed result))
    (test-case (format "LeakyGate can admit a private word at depth ~a" depth)
      (check-exn #rx"wrapper-step/accepted not proved"
                 (lambda () (prove-fifo-wrapper (LeakyGate (leaky_at depth) #:event_view #t) "queue"
                                                (verified-arbiter-queue-transactions-at depth))))))
  (test-case "a matching-width contract for a different child specialization cannot justify the gate"
    (check-exn #rx"evidence does not match the RTL occurrence"
               (lambda () (prove-fifo-wrapper (view 2) "queue" (verified-arbiter-queue-transactions-at 1))))))
