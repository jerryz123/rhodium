#lang rosette
;; Compose sealed child transaction results across the actual buffered arbiter wiring.
;; SPDX-License-Identifier: Apache-2.0

(require (only-in "../refinement.rkt" compose-fifo-series)
         (only-in "../../../examples/formal/buffered-arbiter-queue.rspl" BufferedArbiterQueue)
         (only-in "composition-binding.rhm" outer_at proof_depths)
         (only-in "arbiter-queue-transactions.rkt" verified-arbiter-queue-transactions-at)
         (only-in "shift-queue-transactions.rkt" verified-shift-queue-transactions-at))
(provide verified-buffered-transactions-at)

(define (certify view depth)
  (compose-fifo-series view "front" (verified-arbiter-queue-transactions-at depth)
                       "tail_fifo" (verified-shift-queue-transactions-at 1)))
(define (parent-view depth) (BufferedArbiterQueue (outer_at depth) #:event_view #t))
(define verified
  (for/hash ([depth proof_depths]) (values depth (certify (parent-view depth) depth))))
(define (verified-buffered-transactions-at depth) (hash-ref verified depth))

(module+ test
  (require rackunit
           (only-in racket/base hashalw)
           (only-in "../refinement.rkt" fifo-transactions-for? fifo-transactions-capacity)
           (only-in "../fifo-view.rhm" sample_fifo_events)
           (only-in "composition-binding.rhm" faulty_buffered)
           (only-in "faulty-buffered.rspl" FaultyBuffered)
           (only-in "queue-pair-transaction-order.rkt" check-retirement-order check-ready-not-entailed)
           (only-in "composition-checks.rkt" require-feasible))
  (for ([depth proof_depths])
    (test-case (format "buffered accepted winners retire in order, capacity ~a" (add1 depth))
      (define contract (verified-buffered-transactions-at depth))
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
  (test-case "width-correct crossed lane and independent handoff ready fail structural admission"
    (define (bad cross?) (lambda () (certify (FaultyBuffered (faulty_buffered cross?) #:event_view #t) 2)))
    (check-exn #rx"expected front.ingress\\[0\\].bits driven by ingress\\[0\\].bits, got ingress\\[1\\].bits" (bad #t))
    (check-exn #rx"expected front.egress.ready driven by tail_fifo.ingress.ready, got egress.ready" (bad #f))))
