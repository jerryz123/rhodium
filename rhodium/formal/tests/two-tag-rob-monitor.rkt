#lang rosette
;; Compare the spec-IR monitor to the existing sealed ROB step on arbitrary joint event batches.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "composition-checks.rkt" "two-tag-rob-transactions.rkt"
         "../../../examples/formal/two-tag-rob-contract.rkt"
         (only-in "../../../examples/formal/two-tag-rob-monitor.rhm" two_tag_rob_monitor)
         (only-in "../spec.rhm" spec_snapshot)
         "../spec-engine.rkt")

(define model (spec_snapshot (two_tag_rob_monitor)))

;; Transport fields only. In particular, do not suppress events on reset, discard duplicates,
;; derive a retirement from a completion, or assume that a batch will be accepted.
(define (pins e)
  (define a (rob-events-a e))
  (define b (rob-events-b e))
  (hash "reset" (rob-events-reset e) "grant" (rob-events-grant e)
        "grant_tag" (rob-events-grant-tag e) "grant_gen" (rob-events-grant-gen e)
        "complete_a" (response-present a) "echo_a" (response-gen a) "data_a" (response-data a)
        "complete_b" (response-present b) "echo_b" (response-gen b) "data_b" (response-data b)
        "retire" (rob-events-retire e) "retire_tag" (rob-events-retire-tag e) "retire_data" (rob-events-retire-data e)))

;; Encode the live reference entries into otherwise independent symbolic IR storage.
;; Even the data of an unfinished live allocation remains unconstrained.
(define (encode n h)
  (define free (symbolic-spec-state model))
  (define slots (list (rob-history-head h) (rob-history-tail h)))
  (define q (sequence-value (bv n 2)
              (for/list ([s slots] [pad (sequence-value-slots (hash-ref free "order"))] [i (in-naturals)])
                (if (< i n) (entry-tag s) pad))))
  (for/fold ([regs (hash-set free "order" q)]) ([s (take slots n)])
    (for/fold ([m regs]) ([tag '(#f #t)] [lane '("a" "b")])
      (define mine (equal? tag (entry-tag s)))
      (for/fold ([m m]) ([key '("gen_" "done_" "saved_")] [v (list (entry-gen s) (entry-done s) (entry-data s))]
                         [care (list mine mine (&& mine (entry-done s)))])
        (define k (string->immutable-string (string-append key lane)))
        (hash-set m k (if care v (hash-ref m k)))))))

;; Read independently in the opposite direction: compare occupancy, each live slot in order,
;; and only those payloads made observable by a completed allocation.
(define (same-live? regs h)
  (define q (hash-ref regs "order"))
  (define n (rob-history-count h))
  (&& (= (bitvector->natural (sequence-value-count q)) n)
      (for/fold ([ok #t]) ([tag (sequence-value-slots q)]
                            [s (list (rob-history-head h) (rob-history-tail h))] [i (in-naturals)])
        (define (bank a b) (if tag (hash-ref regs b) (hash-ref regs a)))
        (&& ok (=> (< i n)
                   (&& (equal? tag (entry-tag s))
                       (equal? (bank "gen_a" "gen_b") (entry-gen s))
                       (equal? (bank "done_a" "done_b") (entry-done s))
                       (=> (entry-done s) (bveq (bank "saved_a" "saved_b") (entry-data s)))))))))

(define (compare-at-count n)
  (define h (fresh-rob-history))
  (define e (fresh-rob-events))
  (define domain (= (rob-history-count h) n))
  (define-values (legal next) (rob-step verified-rob-transactions h e))
  (define step (spec-edge-at model (encode n h) (pins e)))
  (define decision (hash-ref (edge-outputs step) "allowed"))
  (require-feasible (&& domain legal))
  (require-feasible (&& domain (! legal)))
  ;; The verdict is total, including on illegal batches. Definedness, care, and
  ;; valid sequence bounds are conclusions, not restrictions on the comparison.
  (discharge 'total-verdict domain
             (&& (edge-legal step) (edge-defined step) (observation-care decision)
                 (spec-state-legal? (edge-next step)) (equal? (observation-value decision) legal)))
  (discharge 'same-live-successor (&& domain legal) (same-live? (edge-next step) next))
  ;; One grant, the other tag's retirement and both completion pulses may coincide.
  (when (= n 1)
    (require-feasible (&& domain legal (rob-events-grant e) (rob-events-retire e)
                          (response-present (rob-events-a e)) (response-present (rob-events-b e))))))

(module+ test
  (test-case "empty initial history and no assumptions on the public batch"
    (check-equal? (hash-ref (hash-ref model "initial") "order") '())
    (check-equal? (hash-count (hash-ref model "assumptions")) 0))
  (for ([n (in-range 3)])
    (test-case (format "ROB monitor: all batches and live successors at occupancy ~a" n)
      (compare-at-count n))))
