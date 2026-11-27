#lang rosette
;; SPDX-License-Identifier: Apache-2.0

(require rackunit racket/match "composition-checks.rkt" "stall-replay.rhm" "clear-both-broadcast.rhdl"
         (only-in "../../../examples/formal/broadcast.rspl" Broadcast)
         (only-in "../../../examples/formal/queued-fork.rspl" QueuedFork)
         (only-in "../../../examples/formal/atomic-fork.rspl" AtomicFork)
         (only-in "../../../examples/formal/shift-queue.rspl" ShiftQueue)
         (only-in "../../../rspecl/runner.rkt" prove-design child_definition)
         (only-in "../../../rspecl/proof.rkt" transaction-step transaction-for?)
         (only-in "../../../rspecl/rtl-boundary.rhm" transaction_parts interface_prefixes)
         (only-in "../spec.rhm" spec_snapshot)
         (only-in "../spec-engine.rkt" spec-initial-state)
         (only-in "../snapshot.rhm" formal_snapshot)
         (only-in "../wiring.rhm" leaf_layout)
         (only-in "../refinement.rkt" pack-pins unpack-pins)
         (only-in "../engine.rkt" prepare_transition_snapshot transition_registers step_transition
                  transition_step_validity transition_step_outputs transition_step_state))

(module+ test
  (define view (Broadcast good_top #:transaction_view #t))
  (define seal (prove-design good_top Broadcast))
  (define-values (_sampler monitor _i _o) (transaction_parts view))
  (define empty (spec-initial-state (spec_snapshot monitor)))
  (define z (bv 0 2))
  (define (edge state take incoming l lw r rw)
    (transaction-step seal view state
      (hash "rst" #f "take" take "incoming" incoming "pop_l" l "out_l" lw "pop_r" r "out_r" rw)))

  (for ([left-first? '(#t #f)])
    (test-case (format "recipient left-first=~a: no lost, duplicate, or overwritten word" left-first?)
      (define-symbolic* a b first last next-l next-r (bitvector 2))
      (define-values (install both) (edge empty #t a #f z #f z))
      (define-values (one other) (edge both #f z left-first? first (not left-first?) first))
      ;; Once one recipient has drained, it cannot take A again. B cannot overwrite the peer's copy.
      (define-values (duplicate _dup) (edge other #f z left-first? a (not left-first?) a))
      (define-values (overwrite _lost) (edge other #t b #f z #f z))
      (define-values (replace new) (edge other #t b (not left-first?) last left-first? last))
      (define-values (drain done) (edge new #f z #t next-l #t next-r))
      (define-values (extra _extra) (edge done #f z #t b #f z))
      (discharge 'independent-retirements #t
                 (equal? (&& install one (! duplicate) (! overwrite) replace drain (! extra))
                         (&& (bveq first a) (bveq last a) (bveq next-l b) (bveq next-r b))))))

  (test-case "clearing both pending bits on a single retirement fails preservation"
    (match-define (list "leaf" _root cycle public projection hints) (Broadcast good_top #:proof_plan #t))
    ;; Only the physical register prefix changes; check the same reference model, relation and care.
    (check-exn #rx"preservation/successor not proved"
               (lambda () (prove-design bad_top
                            (lambda (_ #:proof_plan request)
                              (list "leaf" bad_root cycle public projection hints))))))

  ;; Read the uncut RTL's actual output pins before each tick. Only those public samples
  ;; drive the monitor; a matching transaction history need not have matching input-ready cycles.
  (define (replay top public rows)
    (define plan (prepare_transition_snapshot (formal_snapshot top) "clock"))
    (match-define (list ins outs) (leaf_layout top (interface_prefixes top)))
    (define start (for/hash ([r (transition_registers plan)]) (values (hash-ref r "name") 0)))
    (for/fold ([regs start] [history #f] [log '()] #:result (reverse log)) ([row rows])
      (match-define (list rst offered data l r) row)
      (define pins (hash "reset" rst "ingress.valid" offered "ingress.bits" data
                         "egress[0].ready" l "egress[1].ready" r))
      (define e (step_transition plan regs (pack-pins ins pins)))
      (check-true (transition_step_validity e))
      (define all
        (for/fold ([p pins]) ([(k v) (unpack-pins outs (transition_step_outputs e))])
          (define n (bitvector->natural v))
          (hash-set p k (if (string-suffix? k ".bits") n (= n 1)))))
      (define-values (h verdict) (replay_sample public history all))
      (define events (for/hash ([(k v) (sample_events public all)]) (values k v)))
      (define failed (for/list ([(k ok) verdict] #:unless (or ok (equal? k "allowed"))) k))
      (check-equal? (hash-ref verdict "allowed") (null? failed))
      (values (transition_step_state e) h (cons (list events failed) log))))

  (test-case "replay: stalled right retires A as B arrives; clear-both instead loses A"
    (define prefix '((#t #t 3 #t #t) (#f #t 1 #f #f) (#f #t 2 #f #f)
                     (#f #t 2 #t #f) (#f #t 2 #f #t)))
    (define trace (append prefix '((#f #f 0 #f #f) (#f #f 0 #f #t) (#f #f 0 #t #f))))
    (define expected (list (hash "reset" #t) (hash "accepted" 1) (hash) (hash "left" 1)
                           (hash "accepted" 2 "right" 1) (hash) (hash "right" 2) (hash "left" 2)))
    ;; The monolithic Broadcast also has a cycle spec: check it from inputs alone.
    (for/fold ([state #f]) ([row trace] [events expected])
      (match-define (list rst offered data l r) row)
      (define-values (next observed) (cycle_row state (hash "reset" rst "ingress.valid" offered "ingress.bits" data
                                                            "egress[0].ready" l "egress[1].ready" r)))
      (check-equal? (for/hash ([(k v) observed]) (values k v)) events)
      next)
    (define good (replay good_top view trace))
    (check-equal? good (for/list ([events expected]) (list events '())))
    (define bad (replay bad_top view prefix))
    (check-equal? (drop-right bad 1) (take good 4))
    (check-equal? (last bad) (list (hash "accepted" 2) '("right-space"))))

  (define queued-view (QueuedFork queued_top #:transaction_view #t))
  (test-case "compose from supplied seals only, with independent histories for the identical queue occurrences"
    (define fork (prove-design (child_definition queued_top "fork") AtomicFork))
    (define fifo (prove-design (child_definition queued_top "left") ShiftQueue))
    (define parent (without-child-proofs QueuedFork queued_top))
    (define queues (hash "left" fifo "right" fifo))
    (check-exn #rx"expected occurrences" (lambda () (prove-design queued_top parent #:children queues)))
    (check-true (transaction-for? (prove-design queued_top parent #:children (hash-set queues "fork" fork))
                                  queued_top queued-view)))

  (for ([left-first? '(#t #f)])
    (test-case (format "same broadcast contract, different admission edge; left first: ~a" left-first?)
      (define first (if left-first? "left" "right"))
      (define last (if left-first? "right" "left"))
      ;; Offer B continuously through the final retirement of A and the following stalled edge.
      (define rows (list '(#t #f 0 #f #f) '(#f #t 0 #f #f)
                         (list #f #t 3 left-first? (not left-first?))
                         (list #f #t 3 (not left-first?) left-first?)
                         '(#f #t 3 #f #f)
                         (list #f #f 0 (not left-first?) left-first?)
                         (list #f #f 0 left-first? (not left-first?))))
      (define (expected early?)
        (for/list ([events (list (hash "reset" #t) (hash "accepted" 0) (hash first 0)
                                 (if early? (hash last 0 "accepted" 3) (hash last 0))
                                 (if early? (hash) (hash "accepted" 3))
                                 (hash last 3) (hash first 3))])
          (list events '())))
      (check-equal? (replay good_top view rows) (expected #t))
      (check-equal? (replay queued_top queued-view rows) (expected #f)))))
