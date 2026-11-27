#lang rosette
;; SPDX-License-Identifier: Apache-2.0

(require rackunit racket/match "both-only-receipt.rhdl" "composition-checks.rkt" "stall-replay.rhm"
         "weak-broadcast.rspl"
         (only-in "../../../examples/formal/broadcast-receipt.rspl" BroadcastReceipt)
         (only-in "../../../examples/formal/broadcast.rspl" Broadcast)
         (only-in "../../../examples/formal/queued-fork.rspl" QueuedFork)
         (only-in "../../../rspecl/runner.rkt" child_definition prove-design)
         (only-in "../../../rspecl/proof.rkt" transaction-for? transaction-step)
         (only-in "../../../rspecl/rtl-boundary.rhm" interface_prefixes transaction_batch_model transaction_parts)
         (only-in "../spec.rhm" spec_snapshot)
         (only-in "../spec-engine.rkt" spec-initial-state)
         (only-in "../wiring.rhm" leaf_layout)
         (only-in "../refinement.rkt" pack-pins unpack-pins)
         (only-in "../snapshot.rhm" formal_snapshot)
         (only-in "../engine.rkt" prepare_transition_snapshot transition_registers step_transition
                  transition_step_validity transition_step_outputs transition_step_state))

(module+ test
  (define (child-seal top spec) (prove-design (child_definition top "fanout") spec))
  (define direct-seal (child-seal direct Broadcast))
  (define queued-seal (child-seal queued QueuedFork))
  (define (view top) (BroadcastReceipt top #:transaction_view #t))
  (test-case "a shared contract specifies behavior; it cannot discover or prove its implementation"
    (check-exn #rx"shared child contract requires supplied evidence" (lambda () (prove-design direct BroadcastReceipt))))
  (for ([top (list direct queued)] [seal (list direct-seal queued-seal)] [wrong (list queued-seal direct-seal)])
    (test-case "only the seal for the actual occurrence can prove the unchanged parent"
      (define parent (without-child-proofs BroadcastReceipt top))
      (check-true (transaction-for? (prove-design top parent #:children (hash "fanout" seal)) top (view top)))
      (check-exn #rx"actual RTL occurrence" (lambda () (prove-design top parent #:children (hash "fanout" wrong))))))

  (test-case "same physical fanout and pin-to-batch mapping, but weaker delivery promise"
    (define child (child_definition direct "fanout"))
    (define plan (weak_leaf child #:proof_plan #t))
    (define weak (fourth plan))
    (define strong (Broadcast child #:transaction_view #t))
    (define evidence (prove-design child (lambda (_ #:proof_plan _plan) plan)))
    (check-true (transaction-for? evidence child weak))
    (check-equal? (spec_snapshot (transaction_batch_model weak)) (spec_snapshot (transaction_batch_model strong)))
    ;; This edge retires left without ever admitting a word: only the weakened seal permits it.
    (define-values (_sampler monitor _ins _outs) (transaction_parts weak))
    (define empty-history (spec-initial-state (spec_snapshot monitor)))
    (define batch (hash "rst" #f "take" #f "incoming" (bv 0 2)
                        "pop_l" #t "out_l" (bv 1 2) "pop_r" #f "out_r" (bv 0 2)))
    (define-values (weak-ok _) (transaction-step evidence weak empty-history batch))
    (define-values (strong-ok _next) (transaction-step direct-seal strong empty-history batch))
    (check-true weak-ok)
    (check-false strong-ok)
    (check-exn #rx"exact public contract"
               (lambda () (prove-design direct BroadcastReceipt #:children (hash "fanout" evidence)))))

  (test-case "both-only recorder fails the named last-delivery obligation"
    (define plan (without-child-proofs BroadcastReceipt direct))
    (check-eq? (child_definition direct "fanout") (child_definition bad_top "fanout"))
    (check-exn #rx"step/last-delivery not proved"
               (lambda () (prove-design bad_top plan #:children (hash "fanout" direct-seal)))))

  ;; Run the real, uncut RTL, then pass its pins to rspecsim. Compare both the pulse
  ;; and the surrounding transfers, so an inactive input cannot make a case pass.
  (define (replay top public rows)
    (define rtl (prepare_transition_snapshot (formal_snapshot top) "clock"))
    (match-define (list ins outs) (leaf_layout top (interface_prefixes top)))
    (define initial (for/hash ([reg (transition_registers rtl)]) (values (hash-ref reg "name") 0)))
    (for/fold ([regs initial] [history #f] [log '()] #:result (reverse log)) ([row rows])
      (match-define (list rst put data l r) row)
      (define inputs (hash "reset" rst "ingress.valid" put "ingress.bits" data
                           "egress[0].ready" l "egress[1].ready" r))
      (define edge (step_transition rtl regs (pack-pins ins inputs)))
      (check-true (transition_step_validity edge))
      (define pins (for/fold ([pins inputs]) ([(k v) (unpack-pins outs (transition_step_outputs edge))])
                     (define n (bitvector->natural v))
                     (hash-set pins k (if (string-suffix? k ".bits") n (= n 1)))))
      (define events (for/hash ([(k v) (sample_events public pins)]) (values k v)))
      (define-values (next verdict) (replay_sample public history pins))
      (define ok (hash-ref verdict "last-delivery"))
      (check-equal? (hash-ref verdict "allowed") ok)
      (values (transition_step_state edge) next (cons (list events ok) log))))

  (for* ([q? '(#f #t)] [first-left? '(#t #f)])
    (test-case (format "queued=~a, left-first=~a: split A, simultaneous B, idle" q? first-left?)
      (define top (if q? queued direct))
      (define first (if first-left? "left" "right"))
      (define last-lane (if first-left? "right" "left"))
      (define rows (list '(#t #f 0 #t #t) '(#f #t 1 #f #f)
                         (list #f #f 0 first-left? (not first-left?)) '(#f #t 2 #f #f)
                         (list #f #t 2 (not first-left?) first-left?)
                         '(#f #t 2 #f #f) '(#f #f 0 #t #t) '(#f #f 0 #t #t)))
      (define retirement (hash last-lane 1 "pulse" #t))
      (define events (list (hash "reset" #t) (hash "accepted" 1) (hash first 1) (hash)
                           (if q? retirement (hash-set retirement "accepted" 2))
                           (if q? (hash "accepted" 2) (hash)) (hash "left" 2 "right" 2 "pulse" #t) (hash)))
      (check-equal? (replay top (view top) rows) (map (lambda (e) (list e #t)) events))
      (unless q?
        (define bad (replay bad_top (view direct) (take rows 5)))
        (check-equal? (take bad 4) (map (lambda (e) (list e #t)) (take events 4)))
        (check-equal? (last bad) (list (hash-remove (list-ref events 4) "pulse") #f)))))

  (for ([top (list direct queued)])
    (test-case "reset cancels a split word even if its final recipient is ready"
      (define rows '((#t #f 0 #f #f) (#f #t 1 #f #f) (#f #f 0 #t #f)
                     (#t #t 3 #t #t) (#f #t 0 #t #t) (#f #f 0 #f #t)
                     (#f #f 0 #t #f) (#f #f 0 #t #t)))
      (define events (list (hash "reset" #t) (hash "accepted" 1) (hash "left" 1)
                           (hash "reset" #t) (hash "accepted" 0) (hash "right" 0)
                           (hash "left" 0 "pulse" #t) (hash)))
      (check-equal? (replay top (view top) rows) (map (lambda (e) (list e #t)) events)))))
