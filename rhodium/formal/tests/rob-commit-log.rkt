#lang rosette
;; Prove an ordinal commit-log property using only the sealed ROB contract and public events.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "composition-checks.rkt" "rob-observer-records.rkt" "ordered-retirement-observer.rkt"
         (only-in "../../../examples/formal/two-tag-rob.rspl" TwoTagROB)
         (only-in "../../../rspecl/proof.rkt" transaction-step)
         (only-in "../../../rspecl/rtl-boundary.rhm" transaction_parts)
         (only-in "../spec.rhm" spec_snapshot)
         (only-in "../spec-engine.rkt" fresh-fields symbolic-spec-state))

(define (check-commit-log evidence)
  (define view (TwoTagROB #:transaction_view #t))
  (define-values (_ monitor _i _o) (transaction_parts view))
  (define model (spec_snapshot monitor))
  (define old (symbolic-spec-state model))
  (define batch (fresh-fields (hash-ref model "inputs")))
  (define-values (ok next) (transaction-step evidence view old batch))
  (define-values (_capture _tag _data) (check-ordinal-edge old batch ok next))
  (void))

;; Reach a completed head from reset. An idle self-loop can repeat any number of times;
;; retirement from that very same state is also permitted, not required.
(define (check-no-retirement-deadline evidence)
  (define z (bv 0 2))
  (define data (bv 3 2))
  (define quiet (response #f #f z))
  (define idle (rob-events #f #f #f #f quiet quiet #f #f z))
  (define (take h e)
    (define-values (ok next) (rob-step evidence h e))
    (check-true ok)
    next)
  (define empty (take (fresh-rob-history) (struct-copy rob-events idle [reset #t])))
  (define live (take empty (struct-copy rob-events idle [grant #t] [grant-gen #t])))
  (define complete (take live (struct-copy rob-events idle [a (response #t #t data)])))
  (check-equal? (rob-history-count complete) 1)
  (check-true (entry-done (rob-history-head complete)))
  (check-equal? (take complete idle) complete)
  (check-equal? (rob-history-count (take complete (struct-copy rob-events idle [retire #t] [retire-data data]))) 0))

(module+ test
  (require (only-in "two-tag-rob-transactions.rkt" verified-rob-transactions))
  (test-case "kth log entry since reset: granted tag and first matching-generation response"
    (check-commit-log verified-rob-transactions))
  (test-case "no retirement deadline follows from a completed head"
    (check-no-retirement-deadline verified-rob-transactions)))
