#lang rosette
;; Prove the two- and four-word QueuePair contracts from generic ShiftQueue seals.
;; SPDX-License-Identifier: Apache-2.0

(require (only-in "../../../rspecl/proof.rkt" prove-transaction transaction-for?) rackunit "fifo-observer-input.rkt" "composition-checks.rkt"
         (only-in "../../../rspecl/runner.rkt" prove-design)
         (only-in "composition-binding.rhm" pair_at faulty_pair)
         (only-in "../../../examples/formal/queue-pair.rspl" QueuePair)
         (only-in "queue-pair.rspl" reversed_pair)
         (only-in "shift-queue-transactions.rkt" shift-queue-seal-at)
         (only-in "queue-pair-transaction-order.rkt" check-retirement-order check-ready-not-entailed))
(provide queue-pair-seal-at verified-depth-one-queue-pair)

(define (view top) (QueuePair top #:transaction_view #t))
(define depths '(1 2))
;; Evidence can be reused; the two occurrences do not share an abstract queue state.
(define (children-at depth)
  (define leaf (shift-queue-seal-at depth))
  (hash "a" leaf "b" leaf))
(define seals
  (for/hash ([depth depths])
    (define top (pair_at depth))
    (values depth (prove-design top QueuePair))))
(define (queue-pair-seal-at depth) (hash-ref seals depth))
(define verified-depth-one-queue-pair (queue-pair-seal-at 1))

(module+ test
  (for ([depth depths])
    (define top (pair_at depth))
    (define public (view top))
    (define seal (queue-pair-seal-at depth))
    (define result (observe-fifo-seal seal top public))
    (define children (children-at depth))
    (define capacity (* 2 depth))
    (test-case (format "QueuePair(~a): ordered retirements, capacity ~a, no ready guarantee" depth capacity)
      (check-true (transaction-for? seal top public))
      (check-equal? (fifo-transactions-capacity result) capacity)
      (require-feasible (check-retirement-order result))
      (check-ready-not-entailed result))
    (test-case (format "QueuePair(~a): reset and fill to ~a; no further acceptance without retirement" depth capacity)
      (define (fresh) (fresh-fifo-history result))
      (define pad (bv 0 2))
      (define empty (fresh))
      (define reset (fifo-allows? result (fresh) (fifo-events #t #f pad #f pad) empty))
      (define-values (full prefix)
        (for/fold ([old empty] [legal reset]) ([i (in-range capacity)])
          (define next (fresh))
          (values next (&& legal (fifo-allows? result old (fifo-events #f #t (bv i 2) #f pad) next)))))
      ;; This is feasibility in the event contract; the independent cycle test witnesses RTL fill/drain.
      (require-feasible prefix)
      (define-symbolic* offered (bitvector 2))
      (discharge 'full-limit prefix
                 (&& (= (fifo-history-count full) capacity)
                     (! (fifo-allows? result full (fifo-events #f #t offered #f pad) (fresh))))))
    (test-case (format "QueuePair(~a): reject same-width miswires on the actual RTL" depth)
      (define (reject fault message)
        (define bad (faulty_pair fault depth))
        ;; The correct declaration is nominal; test the bad RTL using the known-good public view.
        (check-exn message (lambda () (prove-transaction bad public children))))
      (reject "ready" #rx"expected a[.]egress[.]ready driven by b[.]ingress[.]ready, got consume")
      (reject "payload" #rx"expected b[.]ingress[.]bits driven by a[.]egress[.]bits, got data")
      (when (= depth 2)
        (reject "mask" #rx"expected head driven by b[.]egress[.]bits, got b[.]mask")))
    (test-case (format "QueuePair(~a): wrong child depth or reversed history cannot issue evidence" depth)
      (define wrong-leaf (shift-queue-seal-at (if (= depth 1) 2 1)))
      (check-exn #rx"expected certified evidence for this transaction view"
                 (lambda () (prove-transaction top public (hash-set children "a" wrong-leaf))))
      (check-exn #rx"step/(allowed|retire-oldest|successor) not proved"
                 (lambda () (prove-transaction top (reversed_pair top) children))))))
