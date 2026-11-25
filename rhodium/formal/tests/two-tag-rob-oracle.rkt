#lang rosette
;; Independent pre-IR ROB transition retained only as an oracle for the sealed public step.
;; SPDX-License-Identifier: Apache-2.0

(require racket/match rackunit "composition-checks.rkt" "two-tag-rob-transactions.rkt"
         "../../../examples/formal/two-tag-rob-contract.rkt"
         (submod "../../../examples/formal/two-tag-rob-contract.rkt" history-types))

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
