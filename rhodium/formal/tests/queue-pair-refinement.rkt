#lang rosette
;; Certify the RSPECL two-word contract from opaque ShiftQueue proofs and exercise its public observer.
;; SPDX-License-Identifier: Apache-2.0

(require (only-in "../../../rspecl/proof.rkt" prove-transaction) rackunit "../refinement.rkt" "composition-checks.rkt"
         (only-in "composition-binding.rhm" pair_at faulty_pair)
         (only-in "queue-pair.rspl" QueuePair reversed_pair)
         (only-in "shift-queue-transactions.rkt" verified-shift-queue-transactions-at)
         (only-in "queue-pair-transaction-order.rkt" check-retirement-order check-ready-not-entailed))
(provide (rename-out [result verified-depth-one-queue-pair]))

(define (view top) (QueuePair top #:event_view #t))
(define leaf (verified-shift-queue-transactions-at 1))
;; Evidence can be reused; the two occurrences do not share an abstract queue state.
(define children (hash "a" leaf "b" leaf))
(define public (view (pair_at 1)))
(define result (prove-transaction (pair_at 1) public children))

(module+ test
  (test-case "the RSPECL parent alone proves ordering and a two-word bound, but not ready when space remains"
    (check-true (fifo-transactions-for? result public))
    (check-equal? (fifo-transactions-capacity result) 2)
    (require-feasible (check-retirement-order result))
    (check-ready-not-entailed result))
  (test-case "both words can be pending in a reset epoch; the public contract forbids accepting a third without retirement"
    (define (fresh) (fresh-fifo-history result))
    (define old (fresh)) (define empty (fresh)) (define one (fresh)) (define two (fresh))
    (define x (bv 1 2)) (define y (bv 2 2))
    (define prefix (&& (fifo-allows? result old (fifo-events #t #f x #f x) empty)
                       (fifo-allows? result empty (fifo-events #f #t x #f x) one)
                       (fifo-allows? result one (fifo-events #f #t y #f x) two)))
    (require-feasible prefix)
    (discharge 'two-word-limit prefix
               (&& (= (fifo-history-count two) 2) (! (fifo-allows? result two (fifo-events #f #t x #f x) (fresh))))))
  (test-case "a same-width ready or payload miswire fails before substitution"
    (for ([fault '("ready" "payload")] [message (list #rx"expected a.egress.ready" #rx"expected b.ingress.bits")])
      (define top (faulty_pair fault 1))
      (check-exn message (lambda () (prove-transaction top (view top) children)))))
  (test-case "wrong child specialization or wrong history order cannot issue a parent seal"
    (check-exn #rx"evidence does not match the RTL occurrences"
               (lambda () (prove-transaction (pair_at 1) public (hash-set children "a" (verified-shift-queue-transactions-at 2)))))
    (check-exn #rx"step/(allowed|successor) not proved"
               (lambda () (prove-transaction (pair_at 1) (reversed_pair (pair_at 1)) children)))))
