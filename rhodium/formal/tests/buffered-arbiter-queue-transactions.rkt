#lang rosette
;; Check the spec-authored buffered arbiter proof against its independent series oracle and wiring mutants.
;; SPDX-License-Identifier: Apache-2.0

(require (only-in "../refinement.rkt" compose-fifo-series)
         (only-in "../../../examples/formal/buffered-arbiter-queue.rspl" BufferedArbiterQueue)
         (only-in "composition-binding.rhm" outer_at proof_depths)
         (only-in "arbiter-queue-transactions.rkt" verified-arbiter-queue-transactions-at)
         (only-in "shift-queue-transactions.rkt" verified-shift-queue-transactions-at))
(define (series-oracle view depth)
  (compose-fifo-series view "front" (verified-arbiter-queue-transactions-at depth)
                       "tail_fifo" (verified-shift-queue-transactions-at 1)))
(define (parent-view depth) (BufferedArbiterQueue (outer_at depth) #:event_view #t))

(module+ test
  (require rackunit
           (only-in racket/base hashalw)
           (only-in "../refinement.rkt" fifo-transactions-for? fifo-transactions-capacity)
           (only-in "../fifo-view.rhm" sample_fifo_events)
           (only-in "composition-binding.rhm" faulty_buffered parent_at fifo_at)
           (only-in "../../../rspecl/runner.rkt" prove-design)
           (only-in "../../../rspecl/proof.rkt" prove-transaction)
           (only-in "../../../examples/formal/arbiter-queue.rspl" ArbiterQueue)
           (only-in "../../../examples/formal/shift-queue.rspl" ShiftQueue)
           (prefix-in observer: "fifo-observer-input.rkt")
           (only-in "faulty-buffered.rspl" FaultyBuffered)
           (only-in "queue-pair-transaction-order.rkt" check-retirement-order check-ready-not-entailed)
           (only-in "composition-checks.rkt" require-feasible))
  (for ([depth proof_depths])
    (test-case (format "generic buffered seal: kth retirement matches kth acceptance, capacity ~a" (add1 depth))
      (define top (outer_at depth))
      (define view (BufferedArbiterQueue top #:transaction_view #t))
      (define contract (observer:observe-fifo-seal (prove-design top BufferedArbiterQueue) top view))
      (check-equal? (observer:fifo-transactions-capacity contract) (add1 depth))
      (require-feasible (check-retirement-order contract))
      (check-ready-not-entailed contract))
    (test-case (format "independent series oracle, capacity ~a" (add1 depth))
      (define contract (series-oracle (parent-view depth) depth))
      (check-true (fifo-transactions-for? contract (parent-view depth)))
      (check-equal? (fifo-transactions-capacity contract) (add1 depth))
      (require-feasible (check-retirement-order contract))
      (check-ready-not-entailed contract)))
  (test-case "simultaneous offers are legal; the passive view records the lane that transferred"
    (define view (parent-view 2))
    (define (sample r0 r1 reset)
      (sample_fifo_events view
        (hash "reset" reset "ingress[0].valid" #t "ingress[0].bits" 1 "ingress[0].ready" r0
              "ingress[1].valid" #t "ingress[1].bits" 3 "ingress[1].ready" r1
              "egress.valid" #f "egress.ready" #t "egress.bits" 0)))
    (check-equal? (sample #t #f #f) (hashalw "accepted" 1))
    (check-equal? (sample #f #t #f) (hashalw "accepted" 3))
    (check-equal? (sample #t #t #t) (hashalw "reset" #t))
    (check-exn #rx"exclusive-ingress" (lambda () (sample #t #t #f))))
  (test-case "crossed payload and miswired ready violate the parent FIFO contract"
    ;; Bind the intended contract on the correct declaration, then check the mutant RTL with
    ;; the same two real child seals. Neither a nominal mismatch nor a fresh child is the fault.
    (define view (BufferedArbiterQueue (outer_at 2) #:transaction_view #t))
    (define children (hash "front" (prove-design (parent_at 2) ArbiterQueue)
                           "tail_fifo" (prove-design (fifo_at 1) ShiftQueue)))
    (define (reject cross? obligation driver)
      (define top (faulty_buffered cross?))
      (check-exn obligation (lambda () (prove-transaction top view children)))
      (check-exn driver (lambda () (series-oracle (FaultyBuffered top #:event_view #t) 2))))
    (reject #t #rx"step/successor not proved"
            #rx"expected front[.]ingress\\[0\\][.]bits driven by ingress\\[0\\][.]bits, got ingress\\[1\\][.]bits")
    (reject #f #rx"step/space-for-accept not proved"
            #rx"expected front[.]egress[.]ready driven by tail_fifo[.]ingress[.]ready, got egress[.]ready")))
