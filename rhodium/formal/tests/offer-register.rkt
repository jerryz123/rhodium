#lang rosette
;; SPDX-License-Identifier: Apache-2.0

(require rackunit racket/match "composition-checks.rkt" "stall-replay.rhm"
         (only-in "offer-register-claim.rspl" dut cycle_row [OfferRegister StableClaim])
         (only-in "../../../examples/formal/offer-register.rspl" OfferRegister)
         (only-in "../../../rspecl/runner.rkt" prove-design)
         (only-in "../../../rspecl/proof.rkt" transaction-step)
         (only-in "../../../rspecl/rtl-boundary.rhm" transaction_parts interface_prefixes)
         (only-in "../spec.rhm" spec_snapshot)
         (only-in "../spec-engine.rkt" spec-initial-state)
         (only-in "../snapshot.rhm" formal_snapshot)
         (only-in "../wiring.rhm" leaf_layout)
         (only-in "../refinement.rkt" pack-pins unpack-pins)
         (only-in "../engine.rkt" prepare_transition_snapshot transition_registers step_transition
                  transition_step_validity transition_step_outputs transition_step_state))

(module+ test
  (define view (OfferRegister dut #:transaction_view #t))
  (define seal (prove-design dut OfferRegister))
  (define bad-view (StableClaim dut #:transaction_view #t))
  (test-case "simultaneous delivery and replacement, using only the public seal"
    (define-values (_ monitor _i _o) (transaction_parts view))
    (define empty (spec-initial-state (spec_snapshot monitor)))
    (define-symbolic* a b first second (bitvector 2))
    (define zero (bv 0 2))
    (define (batch install incoming fire word)
      (hash "rst" #f "install" install "new_word" incoming "offered" fire "offer_word" word
            "transfer" fire "old_word" word))
    (define-values (set? live) (transaction-step seal view empty (batch #t a #f zero)))
    (define-values (take? replaced) (transaction-step seal view live (batch #t b #t first)))
    (define-values (drain? _drained) (transaction-step seal view replaced (batch #f zero #t second)))
    ;; Equivalence proves the matching deliveries are admitted as well as excluding wrong ones.
    (discharge 'old-then-new #t
               (equal? (&& set? take? drain?) (&& (bveq first a) (bveq second b)))))

  (test-case "OfferRegister cannot issue the stable-offer seal"
    (check-exn #rx"adjacent-pair/.*not proved" (lambda () (prove-design dut StableClaim))))

  (test-case "reset, install A, replace stalled A with B, then deliver B and replace it with C"
    (define plan (prepare_transition_snapshot (formal_snapshot dut) "clock"))
    (match-define (list ins outs) (leaf_layout dut (interface_prefixes dut)))
    (define start (for/hash ([r (transition_registers plan)]) (values (hash-ref r "name") 0)))
    ;; Reset, update?, update bits, ready, expected pre-edge word (#f = absent), stable payload verdict.
    (define rows '((#t #t 3 #t #f #t) (#f #t 1 #f #f #t)
                   (#f #t 2 #f 1 #t) (#f #f 0 #f 2 #f)
                   (#f #t 3 #t 2 rejected) (#f #f 0 #t 3 rejected) (#f #f 0 #t #f rejected)))
    (for/fold ([regs start] [state #f] [history #f] #:result (void)) ([row rows])
      (match-define (list rst put incoming ready expected stable?) row)
      (define pins (hash "reset" rst "update.valid" put "update.bits" incoming "egress.ready" ready))
      (define edge (step_transition plan regs (pack-pins ins pins)))
      (check-true (transition_step_validity edge))
      (define out (unpack-pins outs (transition_step_outputs edge)))
      (define raw-valid (= 1 (bitvector->natural (hash-ref out "egress.valid"))))
      (define word (bitvector->natural (hash-ref out "egress.bits")))
      (define offered (and (not rst) raw-valid))
      (check-equal? (and offered word) expected)
      (define-values (observations events next) (cycle_row state pins))
      (define (plain h) (for/hash ([(k v) h]) (values k v)))
      (check-equal? (plain observations) (cond [rst (hash)] [offered (hash "egress.valid" #t "egress.bits" word)]
                                              [else (hash "egress.valid" #f)]))
      (check-equal? (plain events)
                    (let ([updates (if (and (not rst) put) (hash "installed" incoming) (hash))])
                      (if (and offered ready) (hash-set updates "delivered" word) updates)))
      ;; The strong claim has no legal continuation after it rejects B. Finish replaying
      ;; the real RTL and the valid spec, without treating rejected history as a predecessor.
      (define after
        (unless (eq? stable? 'rejected)
          (define-values (h verdict)
            (replay_sample bad-view history (hash "reset" rst "egress.ready" ready "egress.valid" raw-valid "egress.bits" word)))
          (check-true (hash-ref verdict "stable-valid"))
          (check-equal? (hash-ref verdict "stable-payload") stable?)
          h))
      (values (transition_step_state edge) next after))))
