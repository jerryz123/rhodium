#lang rosette
;; SPDX-License-Identifier: Apache-2.0

(require rackunit racket/match "composition-checks.rkt" "stall-replay.rhm" "bypass-offer-queue.rhdl"
         (only-in "../../../examples/formal/offer-queue.rspl" OfferQueue)
         (only-in "../../../rspecl/runner.rkt" prove-design)
         (only-in "../../../rspecl/proof.rkt" transaction-step)
         (only-in "../../../rspecl/rtl-boundary.rhm" transaction_parts child_definition interface_prefixes)
         (only-in "../spec.rhm" spec_snapshot)
         (only-in "../spec-engine.rkt" spec-initial-state)
         (only-in "../snapshot.rhm" formal_snapshot)
         (only-in "../wiring.rhm" leaf_layout)
         (only-in "../refinement.rkt" pack-pins unpack-pins)
         (only-in "../engine.rkt" prepare_transition_snapshot transition_registers step_transition
                  transition_step_validity transition_step_outputs transition_step_state))

(module+ test
  (define view (OfferQueue good_top #:transaction_view #t))
  (define seal (prove-design good_top OfferQueue))
  (test-case "handoff of A with installation of B: only A then B may retire without another update"
    (define-values (_ monitor _i _o) (transaction_parts view))
    (define-symbolic* a b first second (bitvector 2))
    (define z (bv 0 2))
    (define (edge state install incoming ready retire outgoing)
      (transaction-step seal view state
        (hash "rst" #f "install" install "incoming" incoming "ready" ready "retire" retire "outgoing" outgoing)))
    (define empty (spec-initial-state (spec_snapshot monitor)))
    (define-values (i s1) (edge empty #t a #t #f z))
    (define-values (h s2) (edge s1 #t b #t #f z))
    (define-values (r s3) (edge s2 #f z #f #t first))
    (define-values (h2 s4) (edge s3 #f z #t #f z))
    (define-values (r2 _end) (edge s4 #f z #f #t second))
    (discharge 'old-then-replacement #t
               (equal? (&& i h r h2 r2) (&& (bveq first a) (bveq second b)))))

  (test-case "bypass cannot obtain the parent seal"
    ;; Apply the same nominal declaration to the alternate top, resolving its own occurrences.
    (match-define (list "parent" public children req suppliers) (OfferQueue good_top #:proof_plan #t))
    (define (candidate top #:proof_plan _)
      (list "parent" public (for/hash ([(site binding) children])
                              (values site (list (child_definition top site) (second binding))))
            req suppliers))
    (check-exn #rx"expected fifo[.]ingress[.]bits driven by offer[.]egress[.]bits, got update[.]bits"
               (lambda () (prove-design bypass_top candidate))))

  ;; Replay the uncut RTL, then feed only its parent pins to the executable public monitor.
  (define (replay top rows)
    (define plan (prepare_transition_snapshot (formal_snapshot top) "clock"))
    (match-define (list ins outs) (leaf_layout top (interface_prefixes top)))
    (define start (for/hash ([r (transition_registers plan)]) (values (hash-ref r "name") 0)))
    (for/fold ([regs start] [history #f] [words '()] [failure #f] #:result (list (reverse words) failure))
              ([row rows])
      (match-define (list rst put incoming drain) row)
      (define pins (hash "reset" rst "update.valid" put "update.bits" incoming "egress.ready" drain))
      (define edge (step_transition plan regs (pack-pins ins pins)))
      (check-true (transition_step_validity edge))
      (define out (unpack-pins outs (transition_step_outputs edge)))
      (define (nat k) (bitvector->natural (hash-ref out k)))
      (define valid (= 1 (nat "egress.valid")))
      (define word (nat "egress.bits"))
      (define-values (next failed)
        (if failure (values history failure)
            (let-values ([(h verdict)
                          (replay_sample view history
                            (hash-set* pins "handoff_ready" (= 1 (nat "handoff_ready"))
                                       "egress.valid" valid "egress.bits" word))])
              (define violations
                (sort (for/list ([(k ok) verdict] #:unless (or ok (equal? k "allowed"))) k) string<?))
              (check-equal? (hash-ref verdict "allowed") (null? violations))
              (values h (and (pair? violations) violations)))))
      (values (transition_step_state edge) next
              (if (and (not rst) valid drain) (cons word words) words) failed)))

  (test-case "RTL and rspecsim distinguish committed words from stalled replacements"
    (define prefix '((#t #f 0 #f) (#f #t 1 #f) (#f #t 2 #f)))
    (define drain '((#f #f 2 #t) (#f #f 2 #t) (#f #f 2 #t)))
    (check-equal? (replay good_top (append prefix drain)) '((1 2) #f))
    (check-equal? (replay good_top (append prefix '((#f #t 3 #f)) drain)) '((1 3) #f))
    (check-equal? (replay bypass_top (append prefix drain)) '((2 2) ("retire-committed-head")))))
