#lang rosette
;; Independent pre-IR ROB transition retained only as an oracle for the sealed public step.
;; SPDX-License-Identifier: Apache-2.0

(require racket/match rackunit "composition-checks.rkt" "two-tag-rob-transactions.rkt"
         (only-in "../../../examples/formal/two-tag-rob.rspl" TwoTagROB)
         (only-in "../../../rspecl/rtl-boundary.rhm" transaction_parts)
         (only-in "../spec.rhm" spec_snapshot)
         (only-in "../spec-engine.rkt" fresh-fields spec-edge-at edge-events edge-defined observation-care observation-value)
         (only-in "../refinement.rkt" check-batch-total!)
         (only-in "rob-sampler-fixture.rhm" dropped_generation)
         "../../../rspecl/proof.rkt" "rob-observer-records.rkt")

(define unused (entry #f #f #f (bv 0 2)))
(define empty-history (rob-history 0 unused unused))

#| Interpret one batch. Retirement reads the old head, completions update old live allocations,
   and a new grant is appended last. A ready head does not oblige a retirement. |#
(define (transition old e)
  (match-define (rob-history n head tail) old)
  (match-define (rob-events reset grant tag gen a b pop out word) e)
  #| Ignore duplicates, the other tag, and a reused tag's mismatching generation. |#
  (define (complete slot)
    (define p (if (entry-tag slot) b a))
    (define hit (&& (! (entry-done slot)) (response-present p) (equal? (entry-gen slot) (response-gen p))))
    (entry (entry-tag slot) (entry-gen slot) (|| (entry-done slot) hit)
           (if hit (response-data p) (entry-data slot))))
  (define left (- n (if pop 1 0)))
  (define new (entry tag gen #f (bv 0 2)))
  (define next
    (if reset empty-history
        (rob-history (+ left (if grant 1 0))
                     (if (&& grant (= left 0)) new (complete (if pop tail head)))
                     (if (&& grant (= left 1)) new (complete tail)))))
  (values
   (list (cons 'epoch (=> reset (! (|| grant pop (response-present a) (response-present b)))))
         (cons 'history (=> (! reset) (&& (<= 0 n 2) (=> (= n 2) (! (equal? (entry-tag head) (entry-tag tail)))))))
         (cons 'fresh-grant (=> grant (&& (< left 2) (=> (> n 0) (! (equal? tag (entry-tag head))))
                                          (=> (= n 2) (! (equal? tag (entry-tag tail)))))))
         (cons 'oldest-tag (=> pop (&& (> n 0) (equal? out (entry-tag head)))))
         (cons 'completed-before-retirement (=> pop (entry-done head)))
         (cons 'retired-payload (=> pop (bveq word (entry-data head)))))
   next))

;; Only allocated slots are public, and a slot's data matters only after completion.
(define (same-live? x y)
  (define n (rob-history-count x))
  (&& (= n (rob-history-count y))
      (for/fold ([ok #t]) ([get (list rob-history-head rob-history-tail)] [i (in-naturals)])
        (define a (get x))
        (define b (get y))
        (&& ok (=> (< i n)
                   (&& (equal? (entry-tag a) (entry-tag b)) (equal? (entry-gen a) (entry-gen b))
                       (equal? (entry-done a) (entry-done b))
                       (=> (entry-done a) (bveq (entry-data a) (entry-data b)))))))))

(module+ test
  (test-case "declared batch agrees with the former sparse Racket adapter on every directional pin sample"
    (define-values (source _monitor ins outs) (transaction_parts (TwoTagROB #:transaction_view #t)))
    (define raw (spec_snapshot source))
    (define pins (fresh-fields (hash-ref raw "inputs")))
    (define edge (spec-edge-at raw (hash) pins))
    (define fields (edge-events edge))
    (define (present k) (observation-care (hash-ref fields k)))
    (define (value k) (observation-value (hash-ref fields k)))
    ;; This is the old adapter, independent of the new field-to-event bindings.
    (define expected
      (rob-events (present "reset") (present "issued") (value "issued") (value "issued_gen")
                  (response (present "echo_a") (value "echo_a") (value "data_a"))
                  (response (present "echo_b") (value "echo_b") (value "data_b"))
                  (present "retired") (value "retired") (value "retired_value")))
    (define (direction names) (for/hash ([k names]) (values k (hash-ref pins k))))
    (define actual (public-batch (sample-transaction verified-rob-transactions (TwoTagROB #:transaction_view #t)
                                                  (direction ins) (direction outs))))
    ;; No restriction on reset, simultaneous events, stalled offers, or uncared pin values.
    (define (agrees? presence payloads a b)
      (&& (equal? (presence a) (presence b))
          (=> (presence a) (andmap (lambda (get) (equal? (get a) (get b))) payloads))))
    (discharge 'batch-agreement #t
      (&& (edge-defined edge) (equal? (rob-events-reset expected) (rob-events-reset actual))
          (agrees? rob-events-grant (list rob-events-grant-tag rob-events-grant-gen) expected actual)
          (agrees? rob-events-retire (list rob-events-retire-tag rob-events-retire-data) expected actual)
          (for/fold ([ok #t]) ([lane (list rob-events-a rob-events-b)])
            (&& ok (agrees? response-present (list response-gen response-data) (lane expected) (lane actual)))))))
  (test-case "the pre-seal totality check rejects a sampler that loses generation on a grant of A"
    (check-exn #rx"batch-sampler/total not proved"
               (lambda () (check-batch-total! (spec_snapshot dropped_generation)))))
  (test-case "sealed IR step agrees with the old Racket transition, without constraining history or batch"
    (define old (fresh-rob-history))
    (define e (fresh-rob-events))
    (define-values (guards expected) (transition old e))
    (define permitted (andmap cdr guards))
    (define-values (actual next) (rob-step verified-rob-transactions old e))
    (discharge 'batch-legality #t (equal? actual permitted))
    (discharge 'live-successor permitted (same-live? expected next))
    ;; In particular, resetting an out-of-range count works; non-reset cannot wrap it into range.
    (define outside (! (<= 0 (rob-history-count old) 2)))
    (require-feasible (&& outside permitted))
    (require-feasible (&& outside (! (rob-events-reset e)) (! permitted)))))
