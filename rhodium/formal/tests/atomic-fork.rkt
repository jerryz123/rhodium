#lang rosette
;; SPDX-License-Identifier: Apache-2.0

(require rackunit racket/match "one-sided-fork.rhdl" "stall-replay.rhm"
         (only-in "../../../examples/formal/atomic-fork.rspl" AtomicFork)
         (only-in "../../../rspecl/runner.rkt" prove-design)
         (only-in "../../../rspecl/proof.rkt" transaction-step)
         (only-in "../../../rspecl/rtl-boundary.rhm" interface_prefixes)
         (only-in "../wiring.rhm" leaf_layout)
         (only-in "../engine.rkt" interpret_snapshot)
         (only-in "../snapshot.rhm" formal_snapshot)
         (only-in "../refinement.rkt" pack-pins unpack-pins)
         (only-in "composition-checks.rkt" discharge))

(module+ test
  (define view (AtomicFork good_top #:transaction_view #t))
  (test-case "the seal admits exactly idle batches or three simultaneous transfers of the same word"
    (define seal (prove-design good_top AtomicFork))
    (define-symbolic* i l r boolean?)
    (define-symbolic* a b c (bitvector 2))
    (define-values (ok _) (transaction-step seal view (hash)
      (hash "take" i "send_left" l "send_right" r "incoming" a "left_word" b "right_word" c)))
    (discharge 'atomic-fork #t
      (equal? ok (|| (&& (! i) (! l) (! r)) (&& i l r (bveq a b) (bveq a c))))))

  (test-case "the same leaf proof plan fails on the one-sided RTL output"
    (define plan (AtomicFork good_top #:proof_plan #t))
    (check-exn #rx"combinational/outputs not proved"
      (lambda () (prove-design bad_top (lambda (_top #:proof_plan _request) plan)))))

  (define (rtl-simulator top)
    (define snapshot (formal_snapshot top))
    (match-define (list ins outs) (leaf_layout top (interface_prefixes top)))
    (lambda (inputs)
      (define raw (interpret_snapshot snapshot
                    (for/hash ([(k v) (pack-pins ins inputs)]) (values k (bitvector->natural v)))))
      (for/fold ([pins inputs]) ([(k v) (unpack-pins outs raw)])
        (hash-set pins k (if (regexp-match? #rx"[.](valid|ready)$" k) (= v 1) v)))))
  (define simulate (rtl-simulator good_top))
  (define simulate-mutant (rtl-simulator bad_top))
  (define (inputs valid word left right)
    (hash "ingress.valid" valid "ingress.bits" word "egress[0].ready" left "egress[1].ready" right))
  (define (rtl-events pins)
    (for/hash ([port '("ingress" "egress[0]" "egress[1]")] [event '("accepted" "left" "right")]
               #:when (and (hash-ref pins (string-append port ".valid"))
                           (hash-ref pins (string-append port ".ready"))))
      (values event (hash-ref pins (string-append port ".bits")))))
  (define (rspecsim-events input)
    (for/hash ([(k v) (spec_events input)]) (values k v)))

  (test-case "all 32 concrete inputs match rspecsim, including transfer of zero"
    (for* ([valid '(#f #t)] [left '(#f #t)] [right '(#f #t)] [word (in-range 4)])
      (define in (inputs valid word left right))
      (define expected (if (and valid left right) (hash "accepted" word "left" word "right" word) (hash)))
      (check-equal? (rtl-events (simulate in)) expected)
      (check-equal? (rspecsim-events in) expected)))

  (test-case "one-edge replay: left transfers 2, but neither ingress nor right transfers"
    (define in (inputs #t 2 #t #f))
    (define wrong (simulate-mutant in))
    (check-equal? (rtl-events wrong) (hash "left" 2))
    (check-equal? (for/hash ([(k v) (sample_events view wrong)]) (values k v)) (hash "left" 2))
    (check-equal? (rtl-events (simulate in)) (hash))
    (check-equal? (rspecsim-events in) (hash))
    ;; Replay the *mutant's* pin values through the same executable transaction monitor.
    (define-values (_ verdict) (replay_sample view #f wrong))
    (check-false (hash-ref verdict "atomic-transfer"))
    (check-false (hash-ref verdict "allowed"))))
