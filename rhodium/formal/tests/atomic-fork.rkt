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

  (for ([n '(1 3)] [w '(1 8)])
    (test-case (format "AtomicFork(~a outputs, ~a bits): each lane transfers exactly the ingress word" n w)
      (define top (fork_top n w))
      (define v (AtomicFork top #:transaction_view #t))
      (define seal (prove-design top AtomicFork))
      (define-symbolic* take boolean?)
      (define-symbolic* incoming (bitvector w))
      (define-values (batch expected)
        (for/fold ([batch (hash "take" take "incoming" incoming)] [expected #t]) ([i (in-range n)])
          (define-symbolic* sent boolean?)
          (define-symbolic* word (bitvector w))
          (define send-key (string->immutable-string (format "send_lane~a" i)))
          (define word-key (string->immutable-string (format "lane~a_word" i)))
          (values (hash-set (hash-set batch send-key sent) word-key word)
                  (&& expected (equal? take sent) (=> take (bveq incoming word))))))
      (define-values (allowed _) (transaction-step seal v (hash) batch))
      (discharge 'all-lanes #t (equal? allowed expected))))

  (test-case "only lane 2 corrupts bit 7: handshakes agree, but its payload guarantee fails"
    (define top (fork_top 3 8))
    (define plan (AtomicFork top #:proof_plan #t))
    (check-exn #rx"combinational/outputs not proved"
               (lambda () (prove-design high_bit_top (lambda (_ #:proof_plan _plan?) plan))))
    (define pins ((rtl-simulator high_bit_top)
                  (hash "ingress.valid" #t "ingress.bits" #x01
                        "egress[0].ready" #t "egress[1].ready" #t "egress[2].ready" #t)))
    (define v (AtomicFork top #:transaction_view #t))
    (check-equal? (for/hash ([(k x) (sample_events v pins)]) (values k x))
                  (hash "accepted" #x01 "lane0" #x01 "lane1" #x01 "lane2" #x81))
    (define-values (_ verdict) (replay_sample v #f pins))
    (check-equal? (for/hash ([(k x) verdict]) (values k x))
                  (hash "atomic-transfer" #t "lane0-payload" #t "lane1-payload" #t
                        "lane2-payload" #f "allowed" #f)))

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
    (check-equal? (for/hash ([(k v) verdict]) (values k v))
                  (hash "atomic-transfer" #f "left-payload" #t "right-payload" #t "allowed" #f))))
