#lang rosette
;; Certifies the six-word series FIFO using only the two QueuePair event contracts.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit
         (only-in "../refinement.rkt" compose-fifo-series fifo-transactions-for? fifo-transactions-capacity)
         (only-in "composition-binding.rhm" pair_series wrong_ready_series)
         (only-in "shift-queue-pair.rkt" verified-queue-pair-transactions-at)
         (only-in "queue-pair-transaction-order.rkt" check-retirement-order check-ready-not-entailed))
(provide verified-queue-pair-series)

(define ingress '("offer" "ready" "data"))
(define egress '("valid" "consume" "head"))
(define front (verified-queue-pair-transactions-at 1))
(define back (verified-queue-pair-transactions-at 2))
(define (certify top a b) (compose-fifo-series top ingress egress "front" a "back" b))
(define verified-queue-pair-series (certify pair_series front back))

(module+ test
  (require (only-in "../refinement.rkt" fresh-fifo-history fifo-history-count fifo-history-slots fifo-events fifo-allows?)
           (only-in "composition-checks.rkt" discharge)
           (only-in racket/treelist treelist treelist->list)
           (only-in "../fifo-monitor.rhm" fifo_monitor step_fifo_monitor))

  ;; Independent arithmetic relation from before the executable monitor. Quantify over
  ;; arbitrary counts and slot padding, not just well-formed, reachable concrete states.
  (for ([cert (list front back verified-queue-pair-series)] [capacity '(2 4 6)])
    (test-case (format "executable FIFO agrees in both directions at capacity ~a" capacity)
      (define old (fresh-fifo-history cert))
      (define next (fresh-fifo-history cert))
      (define n (fifo-history-count old))
      (define m (fifo-history-count next))
      (define slots (fifo-history-slots old))
      (define-symbolic* reset push pop boolean?)
      (define-symbolic* in out (bitvector 2))
      (define kept (- n (if pop 1 0)))
      (define expected
        (&& (<= 0 m capacity)
            (if reset (&& (! push) (! pop) (= m 0))
                (&& (<= 0 n capacity) (=> pop (&& (> n 0) (bveq out (car slots))))
                    (= m (+ kept (if push 1 0)))
                    (for/fold ([ok #t]) ([i (in-naturals)] [word (fifo-history-slots next)]
                                          [prior slots] [following (append (cdr slots) (list (last slots)))])
                      (&& ok (=> (< i m) (bveq word (if (< i kept) (if pop following prior) in)))))))))
      (discharge 'monitor-equivalence #t
                 (equal? expected (fifo-allows? cert old (fifo-events reset push in pop out) next)))))

  (test-case "series evidence binds the actual parent and suffices for the unchanged ordering observer"
    (check-true (fifo-transactions-for? verified-queue-pair-series pair_series ingress egress))
    (check-equal? (fifo-transactions-capacity verified-queue-pair-series) 6)
    (check-retirement-order verified-queue-pair-series)
    (check-ready-not-entailed verified-queue-pair-series))

  (test-case "an independent ready driver breaks the handoff, and same-shaped children are not interchangeable"
    (check-exn #rx"expected front.consume driven by back.ready, got consume"
               (lambda () (certify wrong_ready_series front back)))
    (check-exn #rx"evidence does not match the RTL occurrences"
               (lambda () (certify pair_series back front))))

  ;; Independent concrete replay only. The certification above does not import an RTL stepper.
  (require (only-in "../snapshot.rhm" formal_snapshot)
           (only-in "../engine.rkt" prepare_transition_snapshot transition_registers step_transition
                    transition_step_outputs transition_step_validity transition_step_state))
  (test-case "from reset the RTL refuses the second offer, then fills and drains six words"
    (define plan (prepare_transition_snapshot (formal_snapshot pair_series) "clock"))
    (define state (for/hash ([r (transition_registers plan)]) (values (hash-ref r "name") 0)))
    (define monitor (fifo_monitor 6 2))
    (define pending (treelist))
    (define checked-corruption? #f)
    (define (tick rst offer data consume)
      (define e (step_transition plan state (hash "reset" rst "offer" offer "data" data "consume" consume)))
      (check-true (transition_step_validity e))
      (define pins (transition_step_outputs e))
      (define accepted? (and (= rst 0) (= offer 1) (ready? pins)))
      (define retired? (and (= rst 0) (= consume 1) (bveq (hash-ref pins "valid") (bv 1 1))))
      (define word (bitvector->natural (hash-ref pins "head")))
      (define arrivals (if accepted? (hash "accepted" data) (hash)))
      (define batch (cond [(= rst 1) (hash "reset" #t)]
                          [retired? (hash-set arrivals "retired" word)] [else arrivals]))
      (when (and retired? (not checked-corruption?))
        (check-false (step_fifo_monitor monitor pending (hash-set batch "retired" (modulo (add1 word) 4))))
        (set! checked-corruption? #t))
      (set! pending (step_fifo_monitor monitor pending batch))
      (check-not-false pending)
      (set! state (transition_step_state e))
      pins)
    (define (ready? e) (bveq (hash-ref e "ready") (bv 1 1)))
    (tick 1 0 0 0)
    (check-true (ready? (tick 0 1 1 0)))
    (check-false (ready? (tick 0 1 2 0))) ; one pending, five free
    (define accepted
      (for/fold ([words '(1)]) ([i (in-range 16)])
        (define offered (modulo (add1 (length words)) 4))
        (if (ready? (tick 0 1 offered 0)) (append words (list offered)) words)))
    (check-equal? (length accepted) 6)
    (check-equal? (treelist->list pending) accepted)
    (define retired
      (for/list ([i (in-range 16)]
                 #:do [(define e (tick 0 0 0 1))]
                 #:when (bveq (hash-ref e "valid") (bv 1 1)))
        (bitvector->natural (hash-ref e "head"))))
    (check-equal? retired accepted)
    (check-equal? (treelist->list pending) '())
    (check-true checked-corruption?)))
