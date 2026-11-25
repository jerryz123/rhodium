#lang rosette
;; Check ordinal grant-to-record readback on the joint trace using only a parent seal.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "composition-checks.rkt" "ordered-retirement-observer.rkt"
         (only-in "../../../examples/formal/logged-two-tag-rob.rspl" LoggedTwoTagROB)
         (only-in "../../../rspecl/proof.rkt" transaction-step)
         (only-in "../../../rspecl/rtl-boundary.rhm" transaction_parts)
         (only-in "../spec.rhm" spec_snapshot)
         (only-in "../spec-engine.rkt" fresh-fields symbolic-spec-state spec-initial-state))

(define view (LoggedTwoTagROB #:transaction_view #t))
(define-values (_ monitor _i _o) (transaction_parts view))
(define model (spec_snapshot monitor))
(define schema (hash-ref model "inputs"))

;; This theorem has one argument: evidence for the public parent contract.
(define (check-readback evidence)
  (define old (symbolic-spec-state model))
  (define events (fresh-fields schema))
  (define-values (ok after) (transaction-step evidence view old events))
  (define-values (kth tag data)
    (check-ordinal-edge old events ok after))
  (define readback (fresh-fields schema))
  (define-values (ok2 _next) (transaction-step evidence view after readback))
  (define read? (&& kth ok2 (! (hash-ref readback "reset"))))
  ;; An immediately following retirement can replace the record only *after* this sample.
  (for* ([t '(#f #t)] [another '(#f #t)])
    (require-feasible (&& read? (equal? tag t) (equal? another (hash-ref readback "retire")))))
  (discharge 'kth-grant-readback read?
             (&& (hash-ref readback "valid") (equal? (hash-ref readback "last_tag") tag)
                 (bveq (hash-ref readback "last_payload") data))))

(module+ test
  (require (only-in "logged-refinement.rkt" verified-logged-transactions verified-renamed-transactions))
  (test-case "the same public theorem holds for both implementations"
    (for ([evidence (list verified-logged-transactions verified-renamed-transactions)])
      (check-readback evidence)))
  (test-case "a completed head may idle indefinitely or retire on the same parent contract"
    (define quiet
      (for/hash ([(name sort) schema])
        (values name (if (equal? sort '("bool")) #f (bv 0 (second sort))))))
    (define (take old overrides)
      (define sample (for/fold ([sample quiet]) ([(name v) overrides]) (hash-set sample name v)))
      (define-values (ok after) (transaction-step verified-logged-transactions view old sample))
      (check-true ok)
      after)
    (define empty (take (spec-initial-state model) (hash "reset" #t)))
    (define live (take empty (hash "grant" #t "grant_tag" #t)))
    (define done (take live (hash "complete_b" #t "data_b" (bv 3 2))))
    (check-equal? (take done (hash)) done)
    (define logged (take done (hash "retire" #t "retire_tag" #t "retire_data" (bv 3 2))))
    (check-true (hash-ref logged "occupied"))
    (check-equal? (hash-ref logged "data") (bv 3 2))))
