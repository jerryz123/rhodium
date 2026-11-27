#lang rosette
;; Check that the actual gate preserves an opaque child's accepted/retired FIFO batches.
;; SPDX-License-Identifier: Apache-2.0

(require (only-in "../refinement.rkt" prove-fifo-wrapper)
         (only-in "../../../examples/formal/gated-arbiter-queue.rspl" GatedArbiterQueue)
         (only-in "composition-binding.rhm" gated_at proof_depths)
         (only-in "arbiter-queue-transactions.rkt" verified-arbiter-queue-transactions-at))
(define (view depth) (GatedArbiterQueue (gated_at depth) #:event_view #t))

(module+ test
  (require rackunit
           (only-in "../refinement.rkt" fifo-transactions-for?)
           (only-in "../../../rspecl/runner.rkt" prove-design)
           (only-in "fifo-observer-input.rkt" observe-fifo-seal fifo-transactions-capacity)
           (only-in "composition-binding.rhm" leaky_at)
           (only-in "leaky-gate.rspl" LeakyGate)
           (only-in "queue-pair-transaction-order.rkt" check-retirement-order check-ready-not-entailed))
  (define (check-order result depth)
    (check-equal? (fifo-transactions-capacity result) depth)
    (define cover (check-retirement-order result))
    (define behind-head (solve (assert cover)))
    (check-true (if (= depth 1) (unsat? behind-head) (sat? behind-head)))
    (check-ready-not-entailed result))
  (for ([depth proof_depths])
    (test-case (format "exact forwarding preserves FIFO order at depth ~a" depth)
      (define top (gated_at depth))
      (define seal (prove-design top GatedArbiterQueue))
      (check-order (observe-fifo-seal seal top (GatedArbiterQueue top #:transaction_view #t)) depth))
    (test-case (format "independent FIFO wrapper oracle at depth ~a" depth)
      (define result (prove-fifo-wrapper (view depth) "queue" (verified-arbiter-queue-transactions-at depth)))
      (check-true (fifo-transactions-for? result (view depth)))
      (check-order result depth))
    (test-case (format "LeakyGate can admit a private word at depth ~a" depth)
      ;; Exact forwarding compares every field even before assuming the child verdict.
      (check-exn #rx"forward/@domain/exclusive-ingress not proved"
                 (lambda () (prove-design (leaky_at depth) LeakyGate)))
      (check-exn #rx"wrapper-step/accepted not proved"
                 (lambda () (prove-fifo-wrapper (LeakyGate (leaky_at depth) #:event_view #t) "queue"
                                                (verified-arbiter-queue-transactions-at depth))))))
  (test-case "a matching-width contract for a different child specialization cannot justify the gate"
    (check-exn #rx"evidence does not match the RTL occurrence"
               (lambda () (prove-fifo-wrapper (view 2) "queue" (verified-arbiter-queue-transactions-at 1))))))
