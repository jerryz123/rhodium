#lang rosette
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "selector-binding.rhm" "arbiter-refinement.rkt" "../refinement.rkt" "../spec-engine.rkt")

(define (view pins _) (values (hash "selected" (observation #t (hash-ref pins "selected"))) (hash)))
(define (certify model) (prove-leaf selector model interface_prefixes view))
(define evidence (list verified-arbiter (certify leaf)))

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

(test-case "same-edge conditional guarantees cannot establish each other's input assumptions"
  (define-values (_ legal goals) (wired-input-obligations circular_graph (hash-ref circular_graph "children")))
  (check-true (sat? (solve (assert (&& legal (! (cdr (assoc "a/enabled" goals)))
                                       (! (cdr (assoc "b/enabled" goals)))))))))
