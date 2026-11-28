#lang rosette
;; Exercise the runner-issued recorder seal, candidate RTL faults, and occurrence-bound evidence.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "composition-checks.rkt" "two-tag-rob-transactions.rkt"
         (only-in "composition-binding.rhm" logged_rob valid_only_logged_rob miswired_logged_rob renamed_logged_rob rob)
         (only-in "../../../examples/formal/logged-two-tag-rob.rspl" LoggedTwoTagROB)
         (only-in "../../../examples/formal/two-tag-rob.rspl" TwoTagROB)
         (only-in "../../../rspecl/runner.rkt" prove-design)
         "../../../rspecl/proof.rkt" (only-in "../../../rspecl/rtl-boundary.rhm" transaction_parts)
         (only-in "../spec.rhm" spec_snapshot)
         (only-in "../spec-engine.rkt" fresh-fields symbolic-spec-state))
(provide verified-logged-transactions verified-renamed-transactions)

(define view (LoggedTwoTagROB logged_rob #:transaction_view #t))
(define verified-logged-transactions (prove-design logged_rob LoggedTwoTagROB))

(define verified-renamed-transactions
  (prove-transaction renamed_logged_rob view verified-rob-transactions #:child-site "engine"))

(module+ test
  (require (only-in "stall-replay.rhm" sample_events))
  (test-case "record and record_data report the old record independently of the current retirement"
    (define pins (hash "reset" #f "grant_a" #f "grant_b" #f "issue_gen" #f
                       "complete_a" #f "complete_gen_a" #f "result_a" 0
                       "complete_b" #f "complete_gen_b" #f "result_b" 0
                       "retire.valid" #t "retire.bits" #t "retire_result" 1
                       "last_tag" #f "last_payload" 3))
    (define (events recorded? ready?)
      (for/hash ([(k v) (sample_events view (hash-set* pins "has_commit" recorded? "retire.ready" ready?))])
        (values k v)))
    (check-equal? (events #f #f) (hash))
    (check-equal? (events #t #f) (hash "record" #f "record_data" 3))
    (check-equal? (events #f #t) (hash "retired" #t "retired_value" 1))
    (check-equal? (events #t #t) (hash "record" #f "record_data" 3 "retired" #t "retired_value" 1)))
  ;; The mutants are distinct RTL families. Apply the good candidate plan without its parent
  ;; seal: the runner must check the actual occurrence, forwarding and recorder state anew.
  (define plan (LoggedTwoTagROB logged_rob #:proof_plan #t))
  (define (candidate _top #:proof_plan _requested) plan)
  (test-case "renaming changes only the private occurrence binding"
    (check-exn #rx"transaction evidence does not match the RTL occurrence"
               (lambda () (prove-design renamed_logged_rob candidate)))
    (check-true (transaction-for? verified-renamed-transactions renamed_logged_rob view))
    (check-false (transaction-for? verified-logged-transactions renamed_logged_rob view)))
  (test-case "parent seal is specific to this occurrence and this public contract"
    (check-true (transaction-for? verified-logged-transactions logged_rob view))
    (check-false (transaction-for? verified-logged-transactions valid_only_logged_rob view))
    (check-exn #rx"expected certified evidence for this transaction view"
               (lambda () (transaction-step verified-logged-transactions (TwoTagROB #:transaction_view #t) (hash) (hash))))
    ;; The view matches the supplied evidence in both cases; it is the physical occurrence that is wrong.
    (check-exn #rx"transaction evidence does not match the RTL occurrence"
               (lambda () (prove-transaction rob view verified-rob-transactions)))
    (check-exn #rx"transaction evidence does not match the RTL occurrence"
               (lambda () (transaction-cut logged_rob "rob" verified-logged-transactions view))))
  (test-case "an offered but stalled retirement cannot advance the recorder"
    (check-exn #rx"step/successor not proved"
               (lambda () (prove-design valid_only_logged_rob candidate))))
  (test-case "dropping a forwarded generation fails wiring even with correct capture logic"
    (check-exn #rx"child-wiring/forwarded-batch not proved"
               (lambda () (prove-design miswired_logged_rob candidate))))
  (test-case "two-edge consumer: read back a captured retirement using only the parent seal"
    (define-values (_ mon _i _o) (transaction_parts view))
    (define m (spec_snapshot mon))
    (define old (symbolic-spec-state m))
    (define e (fresh-fields (hash-ref m "inputs")))
    (define f (fresh-fields (hash-ref m "inputs")))
    (define-values (accept after) (transaction-step verified-logged-transactions view old e))
    (define-values (read-back unused-next) (transaction-step verified-logged-transactions view after f))
    (define capture (&& accept (hash-ref e "retire") read-back (! (hash-ref f "reset"))))
    (for ([tag '(#f #t)]) (require-feasible (&& capture (equal? (hash-ref e "retire_tag") tag))))
    (discharge 'captured-retirement capture
               (&& (hash-ref f "valid") (equal? (hash-ref f "last_tag") (hash-ref e "retire_tag"))
                   (bveq (hash-ref f "last_payload") (hash-ref e "retire_data"))))))
