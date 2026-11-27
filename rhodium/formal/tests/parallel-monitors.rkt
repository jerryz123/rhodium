#lang rosette
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "../../../rspecl/proof.rkt" "../../../rspecl/runner.rkt" "parallel-monitors.rspl"
         (only-in "composition-binding.rhm" mark_leaf toggle_leaf mark_pair crossed_marks))

(module+ test
  (define leaf (prove-transaction mark_leaf (StickyMark mark_leaf #:transaction_view #t) (hash)))
  (define toggle (prove-transaction toggle_leaf (ToggleMark toggle_leaf #:transaction_view #t) (hash)))
  (define children (hash "a" leaf "b" toggle))
  (define view (ParallelMarks mark_pair #:transaction_view #t))
  (define result (prove-transaction mark_pair view children))
  (test-case "the design runner needs neither FIFO-specific state nor an external child map"
    (check-true (transaction-for? (prove-design mark_pair ParallelMarks) mark_pair view))
    (define correct (clocked_mark #f #f))
    (check-true (transaction-for? (prove-design mark_leaf correct) mark_leaf (correct mark_leaf #:transaction_view #t)))
    (check-exn #rx"reset/successor not proved" (lambda () (prove-design mark_leaf (clocked_mark #t #f))))
    (check-exn #rx"reset/outputs not proved" (lambda () (prove-design mark_leaf (clocked_mark #f #t)))))
  (test-case "independent public histories: either or both occurrences can be marked after reset"
    (check-true (transaction-for? result mark_pair view))
    (for* ([a '(#f #t)] [b '(#f #t)])
      (define (batch reset a b marked-a marked-b)
        (hash "reset" reset "mark_a" a "mark_b" b "marked_a" marked-a "marked_b" marked-b))
      (define-values (init? empty)
        (transaction-step result view (hash "left_seen" #t "right_seen" #t) (batch #t #f #f #t #t)))
      (define-values (mark? next) (transaction-step result view empty (batch #f a b #f #f)))
      (define-values (read? held) (transaction-step result view next (batch #f #f #f a b)))
      (check-true (&& init? mark? read?))
      (check-equal? held (hash "left_seen" a "right_seen" b))
      (define-values (again? twice) (transaction-step result view held (batch #f a b a b)))
      (check-true again?)
      (check-equal? twice (hash "left_seen" a "right_seen" #f))))
  (test-case "wiring the right toggle to mark_a breaks its next-history relation"
    (check-exn #rx"step/successor not proved"
               (lambda () (prove-transaction crossed_marks (ParallelMarks crossed_marks #:transaction_view #t) children))))
  (test-case "a seal for the other monitor is not interchangeable despite identical pins"
    (check-exn #rx"expected certified evidence" (lambda () (prove-transaction mark_pair view (hash "a" toggle "b" leaf)))))
  (test-case "every actual occurrence needs evidence"
    (check-exn #rx"evidence for each declared occurrence"
               (lambda () (prove-transaction mark_pair view (hash "a" leaf))))))
