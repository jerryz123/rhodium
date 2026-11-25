#lang rosette
;; Derive FIFO transactions from the verified parent cycle spec; no child cut or state is used here.
;; SPDX-License-Identifier: Apache-2.0

(require (only-in "../refinement.rkt" derive-fifo-transactions)
         (only-in "../../../examples/formal/arbiter-queue.rspl" ArbiterQueue)
         (only-in "composition-binding.rhm" parent_at proof_depths)
         (only-in "arbiter-queue-order.rkt" verified-arbiter-queue-at))
(provide verified-arbiter-queue-transactions-at)

(define proved
  (for/hash ([depth proof_depths])
    (values depth (derive-fifo-transactions (verified-arbiter-queue-at depth)
                                            (ArbiterQueue (parent_at depth) #:event_view #t) "pending"))))
(define (verified-arbiter-queue-transactions-at depth) (hash-ref proved depth))

(module+ test
  (require rackunit
           (only-in "../refinement.rkt" fifo-transactions-for? fifo-transactions-capacity)
           (only-in "queue-pair-transaction-order.rkt" check-retirement-order check-ready-not-entailed)
           (only-in "arbiter-queue-binding.rhm" crossed_sampler))
  (for ([depth proof_depths])
    (test-case (format "ordering of accepted winners at depth ~a" depth)
      (define result (verified-arbiter-queue-transactions-at depth))
      (check-true (fifo-transactions-for? result (ArbiterQueue (parent_at depth) #:event_view #t)))
      (check-equal? (fifo-transactions-capacity result) depth)
      (define cover (check-retirement-order result))
      (define behind-head (solve (assert cover)))
      (check-true (if (= depth 1) (unsat? behind-head) (sat? behind-head)))
      (check-ready-not-entailed result)))
  (test-case "a well-typed but incorrect sampler cannot export reusable evidence"
    (define cycle (verified-arbiter-queue-at 2))
    (for ([fault '("payload" "ready")] [goal '(#rx"cycle-view-step/(allowed|successor) not proved"
                                                #rx"cycle-view-step/sampler-domain not proved")])
      (define bad (crossed_sampler fault))
      (check-false (fifo-transactions-for? (verified-arbiter-queue-transactions-at 2) bad))
      (check-exn goal (lambda () (derive-fifo-transactions cycle bad "pending"))))))
