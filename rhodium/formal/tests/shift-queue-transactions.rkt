#lang rosette
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "../refinement.rkt"
         (only-in "composition-binding.rhm" fifo_at)
         (only-in "shift-queue-induction.rkt" verified-fifo-at proof_depths)
         (only-in "../../../examples/formal/shift-queue-transactions.rspl" ShiftQueue)
         (only-in "queue-pair-transaction-order.rkt" check-retirement-order check-ready-not-entailed))
(provide verified-shift-queue-transactions-at)

;; Only store a result after both implication queries pass.
(define proved
  (for/hash ([depth proof_depths])
    (values depth (derive-fifo-transactions (verified-fifo-at depth) (ShiftQueue (fifo_at depth)) "q"))))
(define (verified-shift-queue-transactions-at depth) (hash-ref proved depth))

(for ([depth proof_depths])
  (test-case (format "ShiftQueue's proved cycle model implies FIFO ordering at depth ~a" depth)
    (define view (ShiftQueue (fifo_at depth)))
    (define transactions (verified-shift-queue-transactions-at depth))
    (check-true (fifo-transactions-for? transactions view))
    (check-equal? (fifo-transactions-capacity transactions) depth)
    (check-exn #rx"expected a cycle-spec sequence of FIFO words: mask"
               (lambda () (derive-fifo-transactions (verified-fifo-at depth) view "mask")))
    (check-exn #rx"exact definition and member layout"
               (lambda () (derive-fifo-transactions (verified-fifo-at (if (= depth 1) 2 1)) view "q")))
    (define coverage (check-retirement-order transactions))
    (define behind-head (solve (assert coverage)))
    (check-true (if (= depth 1) (unsat? behind-head) (sat? behind-head)))
    (check-ready-not-entailed transactions)))
