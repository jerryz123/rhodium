#lang rosette
;; Supply the proved ROB contract and exercise its physical bindings and negative fixtures.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "composition-checks.rkt"
         "../../../rspecl/proof.rkt" (only-in "../../../examples/formal/two-tag-rob.rspl" TwoTagROB)
         (only-in "refinement-hints.rspl" bad_hint)
         (only-in "arbiter-queue-transactions.rkt" verified-arbiter-queue-transactions-at)
         (only-in "../refinement.rkt" symbolic-fifo-cut fifo-cut-inputs fifo-cut-outputs fifo-cut-locals
                  fifo-cut-batch fifo-cut-domain fresh-fifo-history fifo-allows?)
         (only-in "../spec-engine.rkt" edge-state edge-next)
         (only-in "../../frontend/layers/bool.rhm" Bool)
         (only-in "composition-binding.rhm" rob faulty_rob unchecked_rob swapped_rob pin_layout))
(provide verified-rob-transactions)

(define child (verified-arbiter-queue-transactions-at 2))
(define view (TwoTagROB rob #:transaction_view #t))
(define verified-rob-transactions (prove-transaction rob view child))

;; Chain actual local edges and allowed FIFO successors, starting with arbitrary state
;; and a sampled reset. Request specific grants as coverage, never as induction premises.
(define (fifo-trace top child length)
  (define cuts (for/list ([i (in-range length)]) (symbolic-fifo-cut top (pin_layout top) Bool "order" child)))
  (define qs (for/list ([i (in-range (add1 length))]) (fresh-fifo-history child)))
  (define (local c) (hash-ref (fifo-cut-locals c) "@local"))
  (values cuts
    (for/fold ([ok #t]) ([c cuts] [prev (cons #f cuts)] [q qs] [next (cdr qs)])
      (define linked
        (or (not prev)
            (for/fold ([same #t]) ([(k v) (edge-state (local c))])
              (&& same (equal? v (hash-ref (edge-next (local prev)) k))))))
      (&& ok linked (fifo-cut-domain c) (fifo-allows? child q (fifo-cut-batch c) next)))))

(define (younger-only-trace top child any-done?)
  (define-values (cuts allowed) (fifo-trace top child 5))
  (define b-token (hash-ref (fifo-cut-outputs (third cuts)) "issue_gen"))
  (define trace
    (for/fold ([ok allowed]) ([c cuts] [i (in-naturals)])
      (define x (fifo-cut-inputs c))
      (define y (fifo-cut-outputs c))
      (&& ok
          (equal? (hash-ref x "reset") (= i 0))
          (equal? (hash-ref x "issue_a") (= i 1)) (equal? (hash-ref x "issue_b") (= i 2))
          (=> (= i 1) (hash-ref y "grant_a")) (=> (= i 2) (hash-ref y "grant_b"))
          (! (hash-ref x "complete_a")) (equal? (hash-ref x "complete_b") (= i 3))
          (=> (= i 3) (equal? (hash-ref x "complete_gen_b") b-token))
          (equal? (hash-ref x "retire.ready") (= i 4)))))
  (define last-pins (fifo-cut-outputs (last cuts)))
  (define pop (hash-ref last-pins "retire.valid")) ; ready on the final edge
  (require-feasible trace)
  (if any-done?
      (require-feasible (&& trace pop (! (hash-ref last-pins "retire.bits"))))
      (discharge 'younger-cannot-unlock-head trace (! pop))))

;; Reset; grant A then B; complete B=3 then A=1; offer the head while stalled and
;; send matching duplicates A=2/B=0; drain twice. Transfers are coverage choices,
;; not promises about when a FIFO must make progress. Do not constrain tag or data.
(define (reverse-payload-trace top child swapped?)
  (define-values (cuts allowed) (fifo-trace top child 8))
  (define (output i k) (hash-ref (fifo-cut-outputs (list-ref cuts i)) k))
  (define trace
    (for/fold ([ok allowed]) ([c cuts] [i (in-naturals)])
      (define (in k) (hash-ref (fifo-cut-inputs c) k))
      (define dup (= i 5))
      (&& ok (equal? (in "reset") (= i 0))
          (equal? (in "issue_a") (= i 1)) (equal? (in "issue_b") (= i 2))
          (=> (= i 1) (output i "grant_a")) (=> (= i 2) (output i "grant_b"))
          (equal? (in "complete_b") (or (= i 3) dup))
          (equal? (in "complete_a") (or (= i 4) dup))
          (=> (in "complete_b") (&& (equal? (in "complete_gen_b") (output 2 "issue_gen"))
                                    (bveq (in "result_b") (bv (if dup 0 3) 2))))
          (=> (in "complete_a") (&& (equal? (in "complete_gen_a") (output 1 "issue_gen"))
                                    (bveq (in "result_a") (bv (if dup 2 1) 2))))
          (equal? (in "retire.ready") (>= i 6))
          (=> (>= i 5) (output i "retire.valid")))))
  (require-feasible trace)
  (discharge 'reverse-tags trace (&& (! (output 6 "retire.bits")) (output 7 "retire.bits")))
  (discharge 'reverse-data trace
             (&& (bveq (output 6 "retire_result") (bv (if swapped? 3 1) 2))
                 (bveq (output 7 "retire_result") (bv (if swapped? 1 3) 2)))))

(module+ test
  (test-case "the same real RTL cannot certify an incorrect reset or successor relation"
    (define other (bad_hint #f))
    (check-exn #rx"reset/initial not proved" (lambda () (prove-transaction rob (bad_hint #t) child)))
    (check-exn #rx"step/successor not proved" (lambda () (prove-transaction rob other child)))
    (check-exn #rx"expected certified evidence" (lambda () (transaction-step verified-rob-transactions other (hash) (hash)))))
  (test-case "TwoTagROB retirement safety without child cycle state or offers"
    (check-true (transaction-for? verified-rob-transactions rob view))
    (check-false (transaction-for? verified-rob-transactions swapped_rob view))
    (check-false (transaction-for? child rob view))
    (check-exn #rx"expected certified evidence"
               (lambda () (transaction-step child view (hash) (hash)))))
  ;; This mutant preserves FIFO order: it can retire the oldest A merely because B completed.
  (test-case "any-done fails the head's own prior completion, not oldest-first order"
    (check-exn #rx"step/completed-before-retirement not proved" (lambda () (prove-transaction faulty_rob view child))))
  (test-case "reset, issue A, issue B, complete only B, then try to retire A"
    (younger-only-trace rob child #f)
    (younger-only-trace faulty_rob child #t))
  (test-case "a wrong-generation pulse cannot be recorded as completion"
    (check-exn #rx"step/successor not proved" (lambda () (prove-transaction unchecked_rob view child))))
  (test-case "swapped banks preserve tag safety but return the other allocation's payload"
    (check-exn #rx"step/retired-payload not proved" (lambda () (prove-transaction swapped_rob view child))))
  (test-case "reverse completions survive matching duplicates during a stall"
    (reverse-payload-trace rob child #f)
    (reverse-payload-trace swapped_rob child #t)))
