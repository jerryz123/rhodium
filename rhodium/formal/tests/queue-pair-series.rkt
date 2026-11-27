#lang rosette
;; Compares the spec-authored six-word proof with the handwritten series oracle and replays real transfers.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit
         (only-in "../refinement.rkt" compose-fifo-series fifo-transactions-for? fifo-transactions-capacity)
         (only-in "composition-binding.rhm" pair_series wrong_ready_series)
         (only-in "../../../examples/formal/queue-pair-series.rspl" QueuePairSeries)
         (only-in "queue-pair.rspl" WrongReadySeries reversed_series)
         (only-in "queue-pair-grouped.rkt" grouped-pair-at)
         (only-in "queue-pair-transaction-order.rkt" check-retirement-order check-ready-not-entailed))
(define series-view (QueuePairSeries pair_series #:event_view #t))
(define front (grouped-pair-at 1))
(define back (grouped-pair-at 2))
(define (certify view a b) (compose-fifo-series view "front" a "back" b))
(define oracle-series (certify series-view front back))

(module+ test
  (require (only-in "../refinement.rkt" fresh-fifo-history fifo-history-count fifo-history-slots fifo-events fifo-allows?)
           (only-in "../../../rspecl/runner.rkt" prove-design)
           (only-in "../../../rspecl/proof.rkt" prove-transaction transaction-for?)
           (prefix-in observer: "fifo-observer-input.rkt")
           (only-in "queue-pair-refinement.rkt" queue-pair-seal-at)
           (only-in "composition-checks.rkt" discharge require-feasible)
           (only-in racket/treelist treelist treelist->list)
           (only-in "../fifo-view.rhm" sample_fifo_events)
           (only-in "../fifo-monitor.rhm" fifo_monitor step_fifo_monitor))

  (define public (QueuePairSeries pair_series #:transaction_view #t))
  (test-case "the generic six-word seal proves the same ordinal theorem and does not promise ready"
    (define seal (prove-design pair_series QueuePairSeries))
    (check-true (transaction-for? seal pair_series public))
    (define reader (observer:observe-fifo-seal seal pair_series public))
    (check-equal? (observer:fifo-transactions-capacity reader) 6)
    (require-feasible (check-retirement-order reader))
    (check-ready-not-entailed reader))

  (test-case "generic induction rejects the miswire, exchanged seals, and upstream-first history"
    (define small (queue-pair-seal-at 1))
    (define large (queue-pair-seal-at 2))
    (define children (hash "front" small "back" large))
    (check-exn #rx"expected front[.]consume driven by back[.]ready, got consume"
               (lambda () (prove-transaction wrong_ready_series public children)))
    (check-exn #rx"expected certified evidence for this transaction view"
               (lambda () (prove-transaction pair_series public (hash "front" large "back" small))))
    (check-exn #rx"step/(allowed|retire-oldest|successor) not proved"
               (lambda () (prove-transaction pair_series (reversed_series pair_series) children))))

  ;; Independent arithmetic relation from before the executable monitor. Quantify over
  ;; arbitrary counts and slot padding, not just well-formed, reachable concrete states.
  (for ([cert (list front back oracle-series)] [capacity '(2 4 6)])
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

  (test-case "handwritten series oracle binds the actual parent and passes the unchanged ordering observer"
    (check-true (fifo-transactions-for? oracle-series series-view))
    (check-equal? (fifo-transactions-capacity oracle-series) 6)
    (require-feasible (check-retirement-order oracle-series))
    (check-ready-not-entailed oracle-series))

  (test-case "an independent ready driver breaks the handoff, and same-shaped children are not interchangeable"
    (check-exn #rx"expected front.consume driven by back.ready, got consume"
               (lambda () (certify (WrongReadySeries wrong_ready_series #:event_view #t) front back)))
    (check-exn #rx"evidence does not match the RTL occurrences"
               (lambda () (certify series-view back front))))

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
      (define inputs (hash "reset" rst "offer" offer "data" data "consume" consume))
      (define e (step_transition plan state inputs))
      (check-true (transition_step_validity e))
      (define pins (transition_step_outputs e))
      (define sample (for/fold ([m inputs]) ([(k v) pins]) (hash-set m k (bitvector->natural v))))
      (define batch (sample_fifo_events series-view sample))
      (when (and (hash-has-key? batch "retired") (not checked-corruption?))
        (define wrong (modulo (add1 (hash-ref batch "retired")) 4))
        (check-false (step_fifo_monitor monitor pending (hash-set batch "retired" wrong)))
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
