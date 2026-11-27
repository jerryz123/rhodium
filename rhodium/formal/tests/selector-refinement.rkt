#lang rosette
;; SPDX-License-Identifier: Apache-2.0

(require rackunit racket/match "selector-binding.rhm" "arbiter-refinement.rkt" "../refinement.rkt" "../spec-engine.rkt"
         "../../../rspecl/proof.rkt"
         (only-in "../../../rspecl/runner.rkt" prove-design)
         (only-in "../../../rspecl/rtl-boundary.rhm" transaction_parts)
         (only-in "../spec.rhm" spec_snapshot step_spec)
         (only-in "composition-binding.rhm" arbitrated_capture capture_gate raw_capture duplicate_capture arbiter_definition circular_grants)
         (only-in "../../../examples/formal/arbiter.rspl" Arbiter)
         (only-in "../../../examples/formal/granted-selector.rspl" RegisteredLaneSelector)
         (only-in "../../../examples/formal/gated-capture.rspl" GatedCapture ArbitratedCapture previous_zero_view
                  arbitrated_capture_zero transaction_zero_view zero_from_grants zero_gate
                  arbitrated_capture_from_bit zero_from_bit_view)
         (prefix-in faulty: "capture-derivation.rspl"))

(define (view pins _) (values (hash "selected" (observation #t (hash-ref pins "selected"))) (hash)))
(define (certify model) (prove-leaf selector model interface_prefixes view))
(define evidence (list verified-arbiter (certify leaf)))
(define ((flags-view names) pins _)
  (values (for/hash ([name names])
            (values name (observation #t (bveq (hash-ref pins name) (bv 1 1)))))
          (hash)))
(define verified-router (prove-leaf router router_spec interface_prefixes (flags-view '("route_a" "route_b"))))
(define routed-evidence (cons verified-router evidence))

(test-case "selector is conditional, not priority RTL; idle and either single lane are legal"
  (check-equal? (map replay '(#f #t #f) '(#f #f #t)) '(0 1 2))
  (check-exn #rx"violated spec input assumption: exclusive" (lambda () (replay #t #t)))
  (check-exn #rx"combinational/outputs not proved" (lambda () (certify without_assumption)))
  (check-exn #rx"infeasible or unknown domain" (lambda () (certify impossible))))

(test-case "unrestricted parent establishes exclusivity before substituting selector"
  (define admitted (prepare-proved-wiring (cut #f leaf) evidence))
  (check-not-exn (lambda () (prove-parent admitted parent view #:relation (lambda (_ __) #t))))
  (check-exn #rx"evidence does not cover contract" (lambda () (prepare-proved-wiring (cut #f without_assumption) evidence))))

(test-case "raw requests cannot admit the cut: the witness has both requests asserted"
  (define failure
    (with-handlers ([exn:fail:input-contract? values]) (prepare-proved-wiring (cut #t leaf) evidence)))
  (check-equal? (exn:fail:input-contract-goal failure) "lanes/exclusive")
  (define sample (exn:fail:input-contract-inputs failure))
  (check-true (and (hash-ref sample "request_a") (hash-ref sample "request_b")))
  (check-exn #rx"violated spec input assumption: exclusive" (lambda () (replay_wiring #t sample)))
  (check-equal? (replay_wiring #f sample) (if (hash-ref sample "accept") (hash-ref sample "word_a") 0)))

(test-case "admitted router grants establish the selector's assumption under either routing"
  (define plan (prepare-proved-wiring (routed_cut #f) routed-evidence))
  (check-not-exn (lambda () (prove-parent plan routed_parent view #:relation (lambda (_ __) #t))))
  (for ([swap '(#f #t)] [word '(1 2)])
    (check-equal? (replay_routed #f (hash "reset" #f "accept" #t "request_a" #t "request_b" #t
                                         "swap" swap "word_a" 1 "word_b" 2)) word)))

(test-case "duplicating a routed grant violates the downstream contract on an actual parent input"
  (define failure
    (with-handlers ([exn:fail:input-contract? values]) (prepare-proved-wiring (routed_cut #t) routed-evidence)))
  (check-equal? (exn:fail:input-contract-goal failure) "lanes/exclusive")
  (define s (exn:fail:input-contract-inputs failure))
  (check-true (and (hash-ref s "accept")
                   (if (hash-ref s "swap") (and (not (hash-ref s "request_a")) (hash-ref s "request_b"))
                       (hash-ref s "request_a"))))
  (check-exn #rx"violated spec input assumption: exclusive" (lambda () (replay_routed #t s)))
  (check-equal? (replay_routed #f s) (hash-ref s "word_a")))

(test-case "two occurrences of one certified leaf cannot justify each other's premises"
  (define high (prove-leaf high_grant conditional interface_prefixes (flags-view '("grant"))))
  (check-exn #rx"circular same-edge input contract"
             (lambda () (prepare-proved-wiring (circular_cut) (list high)))))

;; Only the leaf relation knows the RTL register. The parent sees the child's spec state.
(define (registered-view pins inputs)
  (values (hash "selected" (observation (! (hash-ref inputs "reset")) (hash-ref pins "selected"))) (hash)))
(define (certify-registered model)
  (prove-leaf registered_lane model interface_prefixes registered-view
              #:relation (lambda (rtl spec) (bveq (hash-ref rtl registered_key) (hash-ref spec "last_word")))))
(define verified-register (certify-registered registered_spec))
(define clocked-evidence (list verified-arbiter verified-router verified-register))

(test-case "conditional clocked evidence needs feasible reset and active phases and a valid successor"
  (check-exn #rx"reset: infeasible or unknown domain" (lambda () (certify-registered (registered_variant "never-reset"))))
  (check-exn #rx"preservation: infeasible or unknown domain" (lambda () (certify-registered (registered_variant "only-reset"))))
  (check-exn #rx"preservation/successor not proved" (lambda () (certify-registered (registered_variant "unrestricted")))))

(test-case "unrestricted parent proves one-edge capture, hold, and reset through the child specs"
  (define plan (prepare-proved-wiring (registered_cut #f) clocked-evidence))
  (check-not-exn
   (lambda () (prove-parent plan registered_parent registered-view
                            #:relation (lambda (cut spec)
                                         (bveq (hash-ref (hash-ref cut "lanes") "last_word") (hash-ref spec "retained"))))))
  (define capture (hash "reset" #f "request_a" #t "request_b" #t "swap" #t "accept" #t "word_a" 1 "word_b" 2))
  (define clear (hash-set capture "reset" #t))
  (define hold (hash-set* capture "accept" #f "word_b" 3))
  (check-equal? (registered_trace #f (list clear capture hold hold (hash-set capture "swap" #f) hold clear hold))
                '(#f 0 2 2 2 1 #f 0)))

(test-case "duplicated clocked grant replays off reset; exactly the same enables are legal on reset"
  (define failure
    (with-handlers ([exn:fail:input-contract? values]) (prepare-proved-wiring (registered_cut #t) clocked-evidence)))
  (check-equal? (exn:fail:input-contract-goal failure) "lanes/exclusive")
  (define s (exn:fail:input-contract-inputs failure))
  (check-false (hash-ref s "reset"))
  (define clear (hash-set s "reset" #t))
  (define hold (hash-set s "accept" #f))
  (check-equal? (registered_trace #t (list clear hold)) '(#f 0))
  (check-exn #rx"violated spec input assumption: exclusive" (lambda () (registered_trace #t (list clear s))))
  (check-equal? (registered_trace #f (list clear s hold)) (list #f 0 (hash-ref s "word_a"))))

;; A parent is certified conditionally without handing the caller a conditionally steppable cut.
(define (certify-gate model)
  (prove-parent (gate_cut) model registered-view #:child-evidence (list verified-register)
                #:relation (lambda (cut spec) (bveq (hash-ref (hash-ref cut "lanes") "last_word") (hash-ref spec "held")))))
(define verified-gate (certify-gate gate_spec))
(define capture-evidence (list verified-arbiter verified-gate))
(define (certify-outer raw)
  (prove-parent (capture_cut raw) capture_spec registered-view #:child-evidence capture-evidence
                #:relation (lambda (cut spec) (bveq (hash-ref (hash-ref cut "gate") "held") (hash-ref spec "saved")))))
(define verified-capture (certify-outer #f))
;; The cycle-first oracle projects its own cycle state, not the generic runner's child history.
(define (cycle-capture-contract)
  (derive-transaction verified-capture arbitrated_capture (ArbitratedCapture #:transaction_view #t)
                      (hash "previous" "saved")))

(test-case "conditional wrapper needs its declared premise and both feasible clock phases"
  (check-exn #rx"lanes/exclusive not proved" (lambda () (prepare-proved-wiring (gate_cut) (list verified-register))))
  (check-exn #rx"lanes/exclusive not proved" (lambda () (certify-gate (gate_variant "unrestricted"))))
  (check-exn #rx"reset: infeasible or unknown domain" (lambda () (certify-gate (gate_variant "never-reset"))))
  (check-exn #rx"preservation: infeasible or unknown domain" (lambda () (certify-gate (gate_variant "only-reset")))))

(test-case "outer parent uses only arbiter and wrapper evidence, with the wrapper premise intact"
  (check-exn #rx"evidence does not cover contract at gate"
             (lambda () (prepare-proved-wiring (capture_cut #f (gate_variant "unrestricted")) capture-evidence)))
  (define both (hash "reset" #f "capture" #t "request_a" #t "request_b" #t "word_a" 1 "word_b" 2))
  (define clear (hash-set both "reset" #t))
  (define hold (hash-set both "capture" #f))
  (define b (hash-set both "request_a" #f))
  (check-equal? (capture_trace #f (list clear both hold b hold clear hold)) '(#f 0 1 1 2 #f 0)))

(test-case "derive pre-edge capture readback from the sealed cycle spec, then replay only public pins"
  (define v (ArbitratedCapture arbitrated_capture #:transaction_view #t))
  (define result (cycle-capture-contract))
  (check-true (transaction-for? result arbitrated_capture v))
  (check-true (transaction-for? result arbitrated_capture (ArbitratedCapture #:transaction_view #t)))
  (define (step old rst capture a b readback)
    (define batch (sample-transaction result v
                    (hash "reset" rst "capture" capture "request_a" a "request_b" b
                          "word_a" (bv 1 2) "word_b" (bv 2 2)) (hash "selected" (bv readback 2))))
    (define-values (ok next) (transaction-step result v old batch))
    (check-true ok)
    next)
  (define empty (step (hash "previous" (bv 3 2)) #t #t #t #t 3))
  (define one (step empty #f #t #t #t 0))
  (define held (step one #f #f #f #t 3)) ; an idle edge does not sample readback
  (define two (step held #f #t #f #t 1))
  (check-equal? two (hash "previous" (bv 2 2)))
  (check-equal? (step two #t #t #f #t 3) empty)
  (define wrong (faulty:ArbitratedCapture arbitrated_capture #:transaction_view #t))
  (check-exn #rx"cycle-view-step/new-on-capture not proved"
             (lambda () (derive-transaction verified-capture arbitrated_capture faulty:ArbitratedCapture)))
  ;; Reach the timing mismatch from reset. Replay the old readback from the existing wired cycle oracle.
  (define first (hash "reset" #f "capture" #t "request_a" #t "request_b" #t "word_a" 1 "word_b" 2))
  (define trace (capture_trace #f (list (hash-set first "reset" #t) first)))
  (check-equal? trace '(#f 0))
  (define seen (sample-transaction result v (hash-set* first "word_a" (bv 1 2) "word_b" (bv 2 2))
                                   (hash "selected" (bv (second trace) 2))))
  (define-values (_ bad-model _ins _outs) (transaction_parts wrong))
  (define snap (spec_snapshot bad-model))
  (define mismatch (spec-edge-at snap (spec-initial-state snap) seen))
  (check-true (edge-defined mismatch))
  (check-false (observation-value (hash-ref (edge-outputs mismatch) "new-on-capture")))
  (check-exn #rx"cycle-view-reset/cleared-output not proved"
             (lambda () (derive-transaction verified-capture arbitrated_capture
                          (faulty:reset_readback arbitrated_capture) (hash))))
  (check-exn #rx"expected Boolean monitor verdicts"
             (lambda () (derive-transaction verified-capture arbitrated_capture (faulty:packed_verdict) (hash))))
  (check-exn #rx"cycle-view-step/successor not proved"
             (lambda () (derive-transaction verified-capture arbitrated_capture (faulty:faulty_projection "invariant"))))
  (for ([kind '("pin" "care" "sort")]
        [message (list #rx"cannot read sampled pin: word_a" #rx"cannot read sampled pin: reset" #rx"projection type mismatch: previous")])
    (check-exn message (lambda () (derive-transaction verified-capture arbitrated_capture (faulty:faulty_projection kind)))))
  (check-exn #rx"missing or mistyped cycle state: word_a"
             (lambda () (derive-transaction verified-capture arbitrated_capture v (hash "previous" "word_a"))))
  (check-exn #rx"unknown pin or wrong direction"
             (lambda () (derive-transaction verified-gate capture_gate v (hash "previous" "held")))))

(define gate-events (GatedCapture #:transaction_view #t))
(define selector-events (RegisteredLaneSelector #:transaction_view #t))
(define selector-contract (derive-transaction verified-register registered_lane RegisteredLaneSelector))
(test-case "the standalone selector proof retains exactly off-reset exclusivity"
  (define result (prove-design registered_lane RegisteredLaneSelector))
  (define premises (transaction-input-premises result selector-events))
  (check-equal? premises (transaction-input-premises selector-contract selector-events))
  (check-equal? (hash-keys premises) '("exclusive"))
  (for* ([rst '(#f #t)] [a '(#f #t)] [b '(#f #t)])
    (define pins (hash "reset" rst "enable_a" a "enable_b" b))
    (check-equal? (hash-ref (spec-input-assumptions (hash "assumptions" premises) pins) "exclusive")
                  (or rst (not (and a b)))))
  (check-false (transaction-for? result registered_lane selector-events))
  (check-exn #rx"requires discharge on RTL input pins"
             (lambda () (transaction-step result selector-events (hash) (hash)))))

(test-case "removing only exclusivity breaks the runner's RTL successor proof"
  (define (unrestricted top #:proof_plan plan?)
    (define plan (RegisteredLaneSelector top #:proof_plan plan?))
    (match-define (list "leaf" root _cycle view projection hints) plan)
    (list "leaf" root (registered_variant "unrestricted") view projection hints))
  (check-exn #rx"preservation/successor not proved"
             (lambda () (prove-design registered_lane unrestricted))))

(define (prove-gate declaration)
  (prove-forwarded-transaction capture_gate declaration "lanes" selector-contract selector-events))
(define gate-contract (prove-design capture_gate GatedCapture))
(define ((gate-with mode) top #:transaction_view [tx? #f])
  (if tx? (GatedCapture top #:transaction_view #t) (gate_variant mode)))

(test-case "the composed gate retains exactly the independently cycle-derived pin contract"
  ;; Independent route: the handwritten cycle proof owns this projection, not the forwarding plan.
  (define cycle-gate (derive-transaction verified-gate capture_gate gate-events (hash "previous" "held")))
  (define premises (transaction-input-premises gate-contract gate-events))
  (check-equal? premises (transaction-input-premises cycle-gate gate-events))
  (check-equal? premises (transaction-input-premises (prove-gate GatedCapture) gate-events))
  (define (allows? rst capture a b)
    (define pins (hash "reset" rst "capture" capture "grant_a" a "grant_b" b))
    (hash-ref (spec-input-assumptions (hash "assumptions" premises) pins) "capture_exclusive"))
  (for* ([rst '(#f #t)] [capture '(#f #t)] [a '(#f #t)] [b '(#f #t)])
    (check-equal? (allows? rst capture a b) (or rst (not capture) (not (and a b))))))

(test-case "without the gate assumption the selector rejects an actual double enable"
  (define failure (with-handlers ([exn:fail:input-contract? values]) (prove-design capture_gate faulty:GatedCapture)))
  (check-exn #rx"lanes/exclusive not proved" (lambda () (prove-gate (gate-with "unrestricted"))))
  (check-equal? (exn:fail:input-contract-goal failure) "lanes/exclusive")
  (define w (exn:fail:input-contract-inputs failure))
  (for ([k '("grant_a" "grant_b" "capture" "lanes.enable_a" "lanes.enable_b")]) (check-true (hash-ref w k)))
  (check-false (hash-ref w "reset"))
  (check-false (hash-ref w "lanes.reset"))
  (define ci (for/hash ([k '("reset" "enable_a" "enable_b" "word_a" "word_b")])
               (values k (hash-ref w (string-append "lanes." k)))))
  (check-exn #rx"violated spec input assumption: exclusive"
             (lambda () (step_spec registered_spec (hash-ref (spec_snapshot registered_spec) "initial") ci)))
  ;; Passing the bare view likewise requests unconditional forwarding; it cannot inherit the old seal.
  (check-exn #rx"lanes/exclusive not proved" (lambda () (prove-gate gate-events))))

(test-case "a forwarded parent cannot export evidence on an infeasible reset or non-reset phase"
  (for ([mode '("only-reset" "never-reset")] [diagnostic '(#rx"parent-active: infeasible" #rx"parent-reset: infeasible")])
    (check-exn diagnostic (lambda () (prove-gate (gate-with mode))))))
(define parent-events (ArbitratedCapture #:transaction_view #t))
(define parent-contract
  (prove-forwarded-transaction arbitrated_capture parent-events "gate" gate-contract gate-events
                               #:combinational (hash "arb" verified-arbiter)))

(test-case "only a checked occurrence can use conditional monitor evidence"
  (check-equal? (hash-keys (transaction-input-premises gate-contract gate-events)) '("capture_exclusive"))
  (check-false (transaction-for? gate-contract capture_gate gate-events))
  (for ([bypass (list (lambda () (transaction-step gate-contract gate-events (hash) (hash)))
                      (lambda () (sample-transaction gate-contract gate-events (hash) (hash)))
                      (lambda () (transaction-cut raw_capture "gate" gate-contract gate-events)))])
    (check-exn #rx"requires discharge on RTL input pins" bypass))
  (check-exn #rx"unconditional combinational cycle evidence"
             (lambda () (prove-forwarded-transaction arbitrated_capture parent-events "gate" gate-contract gate-events
                                                     #:combinational (hash "arb" gate-contract)))))

(test-case "discharged exclusivity alone cannot certify a crossed parent payload"
  (check-exn #rx"forward/winner not proved"
             (lambda () (prove-forwarded-transaction arbitrated_capture (faulty:crossed_payload)
                           "gate" gate-contract gate-events #:combinational (hash "arb" verified-arbiter)))))

(test-case "generic parent, handwritten forwarder and cycle-first oracle agree on simultaneous requests"
  (for ([proof (list (prove-design arbitrated_capture ArbitratedCapture) parent-contract (cycle-capture-contract))])
    (check-true (transaction-for? proof arbitrated_capture parent-events))
    (check-equal? (hash-count (transaction-input-premises proof parent-events)) 0)
    (define (step old reset capture a b readback)
      (define batch (sample-transaction proof parent-events
                      (hash "reset" reset "capture" capture "request_a" a "request_b" b
                            "word_a" (bv 1 2) "word_b" (bv 2 2)) (hash "selected" (bv readback 2))))
      (define-values (ok next) (transaction-step proof parent-events old batch))
      (check-true ok)
      next)
    (define empty (step (hash "previous" (bv 3 2)) #t #t #t #t 3))
    (define first (step empty #f #t #t #t 0))
    (check-equal? first (hash "previous" (bv 1 2)))
    (check-equal? (step first #f #f #t #t 3) first)
    (check-equal? (step first #f #t #f #t 1) (hash "previous" (bv 2 2)))))

(define (prove-zero top view drivers)
  (prove-transaction top view (hash "gate" gate-contract) #:combinational drivers))

(define (check-zero-contract result view)
  (check-true (transaction-for? result arbitrated_capture view))
  (check-equal? (transaction-input-premises result view) (hash))
  (define (step old reset capture a b readback expected-ok)
    (define batch (sample-transaction result view
                    (hash "reset" reset "capture" capture "request_a" a "request_b" b
                          "word_a" (bv 1 2) "word_b" (bv 0 2)) (hash "selected" (bv readback 2))))
    (define-values (ok next) (transaction-step result view old batch))
    (check-equal? ok expected-ok)
    next)
  (define zero (hash "previous_was_zero" #t))
  (define nonzero (hash "previous_was_zero" #f))
  (check-equal? (step nonzero #t #t #t #t 3 #t) zero)
  (check-equal? (step zero #f #t #t #t 0 #t) nonzero) ; simultaneous requests capture A=1
  (check-equal? (step nonzero #f #f #t #t 0 #t) nonzero)
  (check-equal? (step nonzero #f #t #f #t 1 #t) zero)
  ;; Public evidence distinguishes zero from nonzero, not the identity of two nonzero words.
  (check-equal? (step nonzero #f #t #f #t 3 #t) zero)
  (void (step nonzero #f #t #f #t 0 #f)))

(test-case "general transaction induction proves one bit using only the gate contract and arbiter evidence"
  (check-zero-contract (prove-zero arbitrated_capture previous_zero_view (hash "arb" verified-arbiter)) previous_zero_view))

(define (grant-seal spec)
  (derive-transaction verified-arbiter arbiter_definition (spec arbiter_definition #:transaction_view #t) (hash)))
(define strong-grants (prove-design arbiter_definition Arbiter))
(test-case "correct grants cannot certify a transfer that reports the losing lane's payload"
  (check-true (transaction-for? (prove-design arbiter_definition (faulty:grant_with_transfer #f))
                                arbiter_definition (Arbiter #:transaction_view #t)))
  (check-exn #rx"combinational/transfers not proved"
             (lambda () (prove-design arbiter_definition (faulty:grant_with_transfer #t)))))
(define gate-bit-view (zero_gate capture_gate #:transaction_view #t))
(define (prove-gate-bit view under)
  (prove-transaction capture_gate view (hash "lanes" selector-contract) #:under under))
(define gate-bit-contract (prove-gate-bit gate-bit-view GatedCapture))
(define (prove-from-bit top view)
  (prove-transaction top view (hash "gate" gate-bit-contract "arb" strong-grants)))

(test-case "general induction seals only the declared gate premise and the outer proof discharges it"
  (for ([gate (list (prove-design capture_gate zero_gate) gate-bit-contract)])
    (define premises (transaction-input-premises gate gate-bit-view))
    (check-equal? (hash-keys premises) '("capture_exclusive"))
    (check-equal? premises (transaction-input-premises gate-contract gate-events))
    (check-false (transaction-for? gate capture_gate gate-bit-view))
    (check-exn #rx"requires discharge on RTL input pins"
               (lambda () (transaction-step gate gate-bit-view (hash) (hash)))))
  (check-zero-contract (prove-design arbitrated_capture arbitrated_capture_from_bit) zero_from_bit_view)
  (check-zero-contract (prove-from-bit arbitrated_capture zero_from_bit_view) zero_from_bit_view))

(test-case "conditional induction separates missing admission, false relation and infeasible coverage"
  (check-exn #rx"lanes/exclusive not proved" (lambda () (prove-gate-bit gate-bit-view #f)))
  (for ([invert? '(#t #f)] [failure (list #rx"reset/initial not proved" #rx"double_capture: infeasible")])
    (define candidate (faulty:bad_gate_spec invert? GatedCapture))
    (check-exn failure (lambda () (prove-design capture_gate candidate)))
    (check-exn failure (lambda () (prove-gate-bit (candidate #:transaction_view #t) GatedCapture))))
  (check-exn #rx"expected a completed specialization of RegisteredLaneSelector"
             (lambda () (prove-design capture_gate (faulty:bad_gate_spec #f RegisteredLaneSelector)))))

(test-case "the one-bit child contract cannot establish exact nonzero readback"
  (check-exn #rx"step/previous-on-capture not proved"
             (lambda () (prove-design arbitrated_capture faulty:exact_from_bit)))
  (check-exn #rx"step/previous-on-capture not proved"
             (lambda () (prove-from-bit arbitrated_capture (faulty:exact_from_bit #:transaction_view #t)))))

(define (prove-from-grants top view grants)
  (prove-transaction top view (hash "gate" gate-contract "arb" grants)))

(test-case "two transaction contracts alone prove the unchanged one-bit observer"
  (check-zero-contract (prove-design arbitrated_capture arbitrated_capture_zero) transaction_zero_view)
  (check-zero-contract (prove-from-grants arbitrated_capture transaction_zero_view strong-grants) transaction_zero_view))

(test-case "an exclusive but otherwise unconstrained producer admits the gate but cannot prove winner readback"
  (define weak (grant-seal faulty:Arbiter))
  (check-exn #rx"expected certified evidence for this transaction view"
             (lambda () (prove-from-grants arbitrated_capture transaction_zero_view weak)))
  (check-exn #rx"step/previous-was-zero not proved"
             (lambda () (prove-from-grants arbitrated_capture (zero_from_grants faulty:Arbiter) weak))))

(test-case "clockless derivation proves each grant rule before issuing a producer contract"
  (check-exn #rx"cycle-view-combinational/b-wins-contest not proved"
             (lambda () (derive-transaction verified-arbiter arbiter_definition faulty:bad_priority (hash)))))

(define (check-duplicated-grant prove!)
  (define failure (with-handlers ([exn:fail:input-contract? values]) (prove!)))
  (check-equal? (exn:fail:input-contract-goal failure) "gate/capture_exclusive")
  (define w (exn:fail:input-contract-inputs failure))
  (for ([k '("request_a" "capture" "arb.ingress[0].ready" "gate.grant_a" "gate.grant_b")])
    (check-true (hash-ref w k)))
  (check-false (hash-ref w "arb.ingress[1].ready"))
  (check-false (hash-ref w "reset"))
  (define pins (for/hash ([k '("request_a" "request_b" "word_a" "word_b" "capture" "reset")]) (values k (hash-ref w k))))
  (check-exn #rx"violated spec input assumption: capture_exclusive"
             (lambda () (duplicate_capture_trace (list (hash-set pins "reset" #t) pins)))))

(test-case "duplicating an actual arbiter grant fails before either gate contract can restrict the edge"
  ;; Same two children as the correct design: even proved arbiter exclusivity does not
  ;; establish gate exclusivity if the same winning pin drives both grants.
  (for ([spec (list ArbitratedCapture arbitrated_capture_from_bit)])
    (define plan (spec arbitrated_capture #:proof_plan #t))
    (define (candidate _ #:proof_plan _requested) plan)
    (check-duplicated-grant (lambda () (prove-design duplicate_capture candidate))))
  (check-duplicated-grant (lambda () (prove-from-grants duplicate_capture transaction_zero_view strong-grants)))
  (check-duplicated-grant (lambda () (prove-from-bit duplicate_capture zero_from_bit_view))))

(test-case "two conditional grant monitors cannot admit one another on the same edge"
  (define high (prove-leaf high_grant conditional interface_prefixes (flags-view '("grant"))))
  (define seal (derive-transaction high high_grant (faulty:HighGrant high_grant #:transaction_view #t) (hash)))
  (check-equal? (hash-keys (transaction-input-premises seal (faulty:HighGrant #:transaction_view #t))) '("enabled"))
  (check-exn #rx"circular same-edge transaction premise"
             (lambda () (prove-transaction circular_grants (faulty:CircularGrants circular_grants #:transaction_view #t)
                                           (hash "a" seal "b" seal)))))

(test-case "inverting the one-bit relation fails reset after the gate premise has been discharged"
  (check-exn #rx"reset/initial not proved"
             (lambda () (prove-design arbitrated_capture faulty:inverted_capture_zero)))
  (check-exn #rx"reset/initial not proved"
             (lambda () (prove-from-grants arbitrated_capture
                          (faulty:inverted_capture_zero #:transaction_view #t) strong-grants))))

(define (check-raw-discharge prove!)
  (define failure (with-handlers ([exn:fail:input-contract? values]) (prove!)))
  (check-equal? (exn:fail:input-contract-goal failure) "gate/capture_exclusive")
  (define witness (exn:fail:input-contract-inputs failure))
  (for ([k '("request_a" "request_b" "capture" "gate.capture" "gate.grant_a" "gate.grant_b")])
    (check-true (hash-ref witness k)))
  (check-false (hash-ref witness "reset"))
  (check-false (hash-ref witness "gate.reset"))
  (define pins (for/hash ([k '("request_a" "request_b" "word_a" "word_b" "capture" "reset")])
                 (values k (hash-ref witness k))))
  (define clear (hash-set pins "reset" #t))
  (check-exn #rx"violated spec input assumption: capture_exclusive" (lambda () (capture_trace #t (list clear pins))))
  (check-equal? (capture_trace #f (list clear pins (hash-set pins "capture" #f)))
                (list #f 0 (hash-ref pins "word_a"))))

(test-case "raw routing fails the gate pin promise before identical event batches can license reuse"
  (check-raw-discharge (lambda () (prove-design raw_capture faulty:RawCapture)))
  (check-raw-discharge (lambda () (prove-forwarded-transaction raw_capture parent-events "gate" gate-contract gate-events))))

(test-case "raw double grants fail on the actual gate inputs before one-bit monitor induction"
  (check-raw-discharge (lambda () (prove-zero raw_capture previous_zero_view (hash)))))

(test-case "raw requests fail at the wrapper boundary only when actively capturing off reset"
  (define failure (with-handlers ([exn:fail:input-contract? values]) (certify-outer #t)))
  (check-equal? (exn:fail:input-contract-goal failure) "gate/capture_exclusive")
  (define s (exn:fail:input-contract-inputs failure))
  (check-false (hash-ref s "reset"))
  (check-true (and (hash-ref s "capture") (hash-ref s "request_a") (hash-ref s "request_b")))
  (define clear (hash-set s "reset" #t))
  (define hold (hash-set s "capture" #f))
  (define b (hash-set* s "request_a" #f "word_b" 3))
  (check-exn #rx"violated spec input assumption: capture_exclusive" (lambda () (capture_trace #t (list clear s))))
  (check-equal? (capture_trace #t (list clear b hold hold clear hold)) '(#f 0 3 3 #f 0))
  (check-equal? (capture_trace #f (list clear s hold)) (list #f 0 (hash-ref s "word_a"))))
