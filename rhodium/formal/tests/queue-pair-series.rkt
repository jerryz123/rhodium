#lang rosette
;; Checks parameterized series ordering and compares the default specialization with an oracle and replay.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit
         (only-in "../refinement.rkt" compose-fifo-series fifo-transactions-for? fifo-transactions-capacity)
         (only-in "composition-binding.rhm" pair_series series_at wrong_ready_series)
         (only-in "../../../examples/formal/queue-pair-series.rspl" QueuePairSeries)
         (only-in "queue-pair.rspl" WrongReadySeries reversed_series wrong_capacity_series)
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
           (only-in "../../../rspecl/proof.rkt" transaction-for?)
           (prefix-in observer: "fifo-observer-input.rkt")
           (only-in "queue-pair-refinement.rkt" queue-pair-seal-at)
           (only-in "composition-checks.rkt" discharge require-feasible without-child-proofs)
           (only-in racket/treelist treelist treelist->list)
           (only-in "../fifo-view.rhm" sample_fifo_events)
           (only-in "../fifo-monitor.rhm" fifo_monitor step_fifo_monitor))

  (define cut (without-child-proofs QueuePairSeries pair_series))
  (define (series-children f b)
    (hash "front" (queue-pair-seal-at f) "back" (queue-pair-seal-at b)))
  (define children (series-children 1 2))
  (for ([f '(1 1 2)] [b '(1 2 1)] [capacity '(4 6 6)])
    (test-case (format "QueuePairSeries(~a, ~a) proves ordering and the ~a-word bound" f b capacity)
      (define top (series_at f b))
      (define evidence (series-children f b))
      (define seal (prove-design top QueuePairSeries #:children evidence))
      (define public (QueuePairSeries top #:transaction_view #t))
      (check-true (transaction-for? seal top public))
      (define reader (observer:observe-fifo-seal seal top public))
      (check-equal? (observer:fifo-transactions-capacity reader) capacity)
      (require-feasible (check-retirement-order reader))
      (check-ready-not-entailed reader)
      ;; Correct seals at their actual sites, but the expected capacities are exchanged.
      (unless (= f b)
        (check-exn #rx"expected certified evidence for the exact public contract"
                   (lambda () (prove-design top wrong_capacity_series #:children evidence))))))

  (test-case "shared contracts require the declared sites and actual occurrences"
    (define small (queue-pair-seal-at 1))
    (define large (queue-pair-seal-at 2))
    (check-exn #rx"shared child contract requires supplied evidence" (lambda () (prove-design pair_series QueuePairSeries)))
    (for ([invalid (list (hash) (hash-remove children "back") (hash-set children "extra" small))])
      (check-exn #rx"#:children expected occurrences" (lambda () (prove-design pair_series QueuePairSeries #:children invalid))))
    (check-exn #rx"expected certified evidence for the actual RTL occurrence"
               (lambda () (prove-design pair_series QueuePairSeries #:children (hash "front" large "back" small)))))

  (test-case "with the right contracts, induction still rejects broken wiring and upstream-first history"
    ;; The broken handoff must violate the FIFO proof, not just a prescribed topology.
    (check-exn #rx"step/space-for-accept not proved"
               (lambda () (prove-design wrong_ready_series cut #:children children)))
    (check-exn #rx"step/(allowed|retire-oldest|successor) not proved"
               (lambda () (prove-design pair_series reversed_series #:children children))))

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
