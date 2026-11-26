#lang rosette
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "../refinement.rkt"
         (only-in "../../../rspecl/proof.rkt" derive-transaction transaction-for?)
         (only-in "composition-binding.rhm" fifo_at)
         (only-in "shift-queue-induction.rkt" verified-fifo-at proof_depths)
         (only-in "../../../examples/formal/shift-queue.rspl" ShiftQueue))
(provide verified-shift-queue-transactions-at shift-queue-seal-at)

;; Only store a result after both implication queries pass.
(define proved
  (for/hash ([depth proof_depths])
    (define top (fifo_at depth))
    (values depth (derive-transaction (verified-fifo-at depth) top ShiftQueue))))
(define (shift-queue-seal-at depth) (hash-ref proved depth))

;; Unmigrated parents require the old evidence type. Prove it separately on demand;
;; never rebrand the generic seal or use the old transition to justify the leaf observer.
(define legacy (make-hash))
(define (verified-shift-queue-transactions-at depth)
  (hash-ref! legacy depth
             (lambda () (derive-fifo-transactions (verified-fifo-at depth)
                         (ShiftQueue (fifo_at depth) #:event_view #t) "q"))))

(module+ test
  (require (only-in "queue-pair-transaction-order.rkt" check-retirement-order check-ready-not-entailed)
           (prefix-in observer: "fifo-observer-input.rkt"))
  (for ([depth proof_depths])
    (test-case (format "ShiftQueue's proved cycle model implies FIFO ordering at depth ~a" depth)
      (define top (fifo_at depth))
      (define view (ShiftQueue top #:transaction_view #t))
      (define seal (shift-queue-seal-at depth))
      (define other (if (= depth 1) 2 1))
      (define transactions (observer:observe-fifo-seal seal top view))
      (check-true (transaction-for? seal top view))
      (check-false (transaction-for? seal (fifo_at other) view))
      (check-exn #rx"expected an unconditional seal"
                 (lambda () (observer:observe-fifo-seal seal top (ShiftQueue (fifo_at other) #:transaction_view #t))))
      (check-equal? (observer:fifo-transactions-capacity transactions) depth)
      (check-exn #rx"missing or mistyped cycle state: mask"
                 (lambda () (derive-transaction (verified-fifo-at depth) top view (hash "q" "mask"))))
      (check-exn #rx"exact definition and member layout"
                 (lambda () (derive-transaction (verified-fifo-at other) top ShiftQueue)))
      (define coverage (check-retirement-order transactions))
      (define behind-head (solve (assert coverage)))
      (check-true (if (= depth 1) (unsat? behind-head) (sat? behind-head)))
      (check-ready-not-entailed transactions))))
