#lang rosette
;; Issue reusable recorder evidence and exercise the public monitor without a second ROB proof.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "composition-checks.rkt" "two-tag-rob-transactions.rkt"
         (only-in "composition-binding.rhm" logged_rob valid_only_logged_rob miswired_logged_rob renamed_logged_rob rob)
         (only-in "../../../examples/formal/logged-two-tag-rob.rspl" LoggedTwoTagROB)
         (only-in "../../../examples/formal/two-tag-rob.rspl" TwoTagROB)
         "../../../rspecl/proof.rkt" (only-in "../../../rspecl/rtl-boundary.rhm" transaction_parts)
         (only-in "../spec.rhm" spec_snapshot)
         (only-in "../spec-engine.rkt" fresh-fields symbolic-spec-state))
(provide verified-logged-transactions verified-renamed-transactions)

(define view (LoggedTwoTagROB logged_rob #:transaction_view #t))
(define verified-logged-transactions (prove-transaction logged_rob view verified-rob-transactions))

(define verified-renamed-transactions
  (prove-transaction renamed_logged_rob view verified-rob-transactions #:child-site "engine"))

(module+ test
  (test-case "renaming changes only the private occurrence binding"
    (check-exn #rx"transaction evidence does not match the RTL occurrence"
               (lambda () (prove-transaction renamed_logged_rob view verified-rob-transactions)))
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
               (lambda () (prove-transaction valid_only_logged_rob view verified-rob-transactions))))
  (test-case "dropping a forwarded generation fails wiring even with correct capture logic"
    (check-exn #rx"child-wiring/forwarded-batch not proved"
               (lambda () (prove-transaction miswired_logged_rob view verified-rob-transactions))))
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
