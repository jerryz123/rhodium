#lang rosette
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "selector-binding.rhm" "arbiter-refinement.rkt" "../refinement.rkt" "../spec-engine.rkt"
         "../../../rspecl/proof.rkt"
         (only-in "../../../rspecl/rtl-boundary.rhm" transaction_parts)
         (only-in "../spec.rhm" spec_snapshot)
         (only-in "composition-binding.rhm" arbitrated_capture capture_gate)
         (only-in "../../../examples/formal/gated-capture.rspl" ArbitratedCapture)
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
  (define result (derive-transaction verified-capture arbitrated_capture v (faulty:capture_relation #f)))
  (check-true (transaction-for? result arbitrated_capture v))
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
             (lambda () (derive-transaction verified-capture arbitrated_capture wrong (hash "previous" "saved"))))
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
             (lambda () (derive-transaction verified-capture arbitrated_capture v (faulty:capture_relation #t))))
  (check-exn #rx"missing or mistyped cycle state: word_a"
             (lambda () (derive-transaction verified-capture arbitrated_capture v (hash "previous" "word_a"))))
  (check-exn #rx"conditional cycle evidence"
             (lambda () (derive-transaction verified-gate capture_gate v (hash "previous" "held")))))

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
