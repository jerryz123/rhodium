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
(define routed-evidence
  (cons (prove-leaf router router_spec interface_prefixes (flags-view '("route_a" "route_b"))) evidence))

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
