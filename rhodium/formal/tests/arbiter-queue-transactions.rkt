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
           (prefix-in generic: "fifo-observer-input.rkt")
           (only-in "../../../rspecl/runner.rkt" prove-design)
           (only-in "../../../rspecl/proof.rkt" prove-transaction sample-transaction transaction-step)
           (only-in "../../../examples/formal/arbiter.rspl" Arbiter arbitrated_transfers)
           (only-in "../../../examples/formal/shift-queue.rspl" ShiftQueue)
           (only-in "composition-binding.rhm" arbiter_definition fifo_at wrong_lane_at undersized)
           (only-in "../spec-engine.rkt" sequence-value)
           (only-in "arbiter-queue-variants.rspl" candidate)
           (only-in "arbiter-queue-binding.rhm" crossed_sampler))
  (define arb (prove-design arbiter_definition arbitrated_transfers))
  (for ([depth proof_depths])
    (test-case (format "generic child contracts order the accepted winners, depth ~a" depth)
      (define top (parent_at depth))
      (define view (ArbiterQueue top #:transaction_view #t))
      (define seal (prove-design top ArbiterQueue))
      (define result (generic:observe-fifo-seal seal top view))
      (check-equal? (generic:fifo-transactions-capacity result) depth)
      (define behind-head (solve (assert (check-retirement-order result))))
      (check-true (if (= depth 1) (unsat? behind-head) (sat? behind-head)))
      (check-ready-not-entailed result)
      (define children (hash "arbiter_stage" arb "shift_queue_stage" (prove-design (fifo_at depth) ShiftQueue)))
      ;; The FIFO stores lane one's bits even when lane zero wins, so its next history can be wrong.
      (check-exn #rx"step/successor not proved"
                 (lambda () (prove-transaction (wrong_lane_at depth) view children)))
      (when (= depth 2)
        (check-exn #rx"expected certified evidence for this transaction view"
                   (lambda () (prove-transaction top view (hash-set children "arbiter_stage" (prove-design arbiter_definition Arbiter)))))
        (check-exn #rx"does not match the RTL occurrence|expected certified evidence for this transaction view"
                   (lambda () (prove-transaction undersized view children)))
        (for ([fault '("word-unconstrained" "payload" "ready")]
              [goal '(#rx"step/successor not proved" #rx"step/successor not proved"
                      #rx"step/@domain/exclusive-ingress not proved")])
          (check-exn goal (lambda () (prove-design top (candidate fault)))))
        ;; Sample impossible physical pins too: a conflict is rejected, not decoded by priority.
        (define in (hash "reset" #f "egress.ready" #f "ingress[0].valid" #t "ingress[1].valid" #t
                         "ingress[0].bits" (bv 2 2) "ingress[1].bits" (bv 3 2)))
        (define out (hash "egress.valid" #f "egress.bits" (bv 0 2) "ingress[0].ready" #t "ingress[1].ready" #t))
        (define empty (hash "q" (sequence-value (bv 0 2) (list (bv 0 2) (bv 0 2)))))
        (define (ok? pins)
          (define-values (allowed _) (transaction-step seal view empty (sample-transaction seal view pins out)))
          allowed)
        (check-false (ok? in))
        (check-true (ok? (hash-set in "reset" #t))))))
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
