#lang rosette
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "selector-binding.rhm" "arbiter-refinement.rkt" "../refinement.rkt" "../spec-engine.rkt")

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

(test-case "conditional wrapper needs its declared premise and both feasible clock phases"
  (check-exn #rx"lanes/exclusive not proved" (lambda () (prepare-proved-wiring (gate_cut) (list verified-register))))
  (check-exn #rx"lanes/exclusive not proved" (lambda () (certify-gate (gate_variant "unrestricted"))))
  (check-exn #rx"reset: infeasible or unknown domain" (lambda () (certify-gate (gate_variant "never-reset"))))
  (check-exn #rx"preservation: infeasible or unknown domain" (lambda () (certify-gate (gate_variant "only-reset")))))

(test-case "outer parent uses only arbiter and wrapper evidence, with the wrapper premise intact"
  (check-not-exn (lambda () (certify-outer #f)))
  (check-exn #rx"evidence does not cover contract at gate"
             (lambda () (prepare-proved-wiring (capture_cut #f (gate_variant "unrestricted")) capture-evidence)))
  (define both (hash "reset" #f "capture" #t "request_a" #t "request_b" #t "word_a" 1 "word_b" 2))
  (define clear (hash-set both "reset" #t))
  (define hold (hash-set both "capture" #f))
  (define b (hash-set both "request_a" #f))
  (check-equal? (capture_trace #f (list clear both hold b hold clear hold)) '(#f 0 1 1 2 #f 0)))

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
