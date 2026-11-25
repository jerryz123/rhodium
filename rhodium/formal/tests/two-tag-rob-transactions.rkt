#lang rosette
;; Induct over public grants/completions, local ROB RTL and the opaque FIFO event relation.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "composition-checks.rkt"
         (only-in "../refinement.rkt" symbolic-fifo-cut fifo-cut-inputs fifo-cut-outputs fifo-cut-locals
                  fifo-cut-batch fifo-cut-domain fresh-fifo-history fifo-events fifo-allows?
                  fifo-history-count fifo-history-slots fifo-transactions-width fifo-transactions-capacity)
         (only-in "../spec-engine.rkt" edge-state edge-next)
         (only-in "../../frontend/layers/bool.rhm" Bool)
         (only-in "composition-binding.rhm" rob faulty_rob unchecked_rob swapped_rob pin_layout)
         (only-in "arbiter-queue-transactions.rkt" verified-arbiter-queue-transactions-at))

;; Independent observer for a tag. Remember the generation actually advertised on its
;; grant; a completion counts only for a previously live allocation with that token.
(struct ticket (live gen completed payload) #:transparent)
(define (fresh-ticket)
  (define-symbolic* live gen completed boolean?)
  (define-symbolic* payload (bitvector 2))
  (ticket live gen completed payload))
(define (advance t reset grant advertised retire complete echo data)
  (define matched (&& (ticket-live t) complete (equal? echo (ticket-gen t))))
  (ticket (&& (! reset) (|| grant (&& (ticket-live t) (! retire))))
          (if grant advertised (ticket-gen t))
          (&& (! reset) (! grant) (! retire) (|| (ticket-completed t) matched))
          (if (&& matched (! (ticket-completed t))) data (ticket-payload t))))

(define A (bv 0 2))
(define B (bv 1 2))
(define (tag b?) (if b? B A))

;; Relate the public tickets to local registers and *transaction* history, not the
;; queue's cycle model. The tag-only invariant makes no assumptions about the result banks.
(define (related q regs tickets)
  (define n (fifo-history-count q))
  (define words (fifo-history-slots q))
  (&& (<= 0 n 2)
      (=> (= n 2) (! (bveq (first words) (second words))))
      (for/fold ([ok #t]) ([w words] [i (in-naturals)])
        (&& ok (=> (< i n) (|| (bveq w A) (bveq w B)))))
      (for/fold ([ok #t]) ([t tickets] [w (list A B)] [lane '("a" "b")])
        (define live (for/fold ([found #f]) ([slot words] [i (in-naturals)])
                       (|| found (&& (< i n) (bveq slot w)))))
        (define (reg prefix) (hash-ref regs (string->immutable-string (string-append prefix lane))))
        (&& ok (equal? live (ticket-live t)) (equal? live (reg "busy_"))
            (=> live (equal? (reg "gen_") (ticket-gen t)))
            (equal? (reg "done_") (ticket-completed t)) (=> (ticket-completed t) live)))))

;; The RTL banks need no reset value: only a completed allocation owns a saved result.
(define (payloads-related regs tickets)
  (for/fold ([ok #t]) ([t tickets] [bank '("saved_a" "saved_b")])
    (&& ok (=> (ticket-completed t) (bveq (ticket-payload t) (hash-ref regs bank))))))

(define (prove-retirements top child)
  (define cut (symbolic-fifo-cut top (pin_layout top) Bool "order" child))
  (check-equal? (list (fifo-transactions-width child) (fifo-transactions-capacity child)) '(2 2))
  (check-equal? (hash-keys (fifo-cut-locals cut)) '("@local"))
  (define local (hash-ref (fifo-cut-locals cut) "@local"))
  (define (in k) (hash-ref (fifo-cut-inputs cut) k))
  (define (out k) (hash-ref (fifo-cut-outputs cut) k))
  (define reset (in "reset"))
  (define a (&& (! reset) (out "grant_a")))
  (define b (&& (! reset) (out "grant_b")))
  (define pop (&& (! reset) (in "retire.ready") (out "retire.valid")))
  (define tag-b? (out "retire.bits"))
  (define public-batch (fifo-events reset (|| a b) (tag b) pop (tag tag-b?)))
  (define q (fresh-fifo-history child))
  (define next (fresh-fifo-history child))
  (define allowed (&& (fifo-cut-domain cut) (fifo-allows? child q (fifo-cut-batch cut) next)))
  (define tickets (list (fresh-ticket) (fresh-ticket)))
  (define after
    (for/list ([t tickets] [lane '("a" "b")] [grant (list a b)] [lane-b? '(#f #t)])
      (define (input prefix) (in (string->immutable-string (string-append prefix lane))))
      (advance t reset grant (out "issue_gen") (&& pop (equal? tag-b? lane-b?))
               (input "complete_") (input "complete_gen_") (input "result_"))))
  (define base (&& allowed reset))
  (define step (&& allowed (! reset) (related q (edge-state local) tickets)))
  (define successor (related next (edge-next local) after))
  (require-feasible base)
  (require-feasible step)
  (discharge 'reset base (&& (= (fifo-history-count next) 0) successor))
  (discharge 'grants step
             (&& (! (&& a b)) (=> a (&& (in "issue_a") (! (ticket-live (first tickets)))))
                              (=> b (&& (in "issue_b") (! (ticket-live (second tickets)))))))
  ;; The same history starts empty and advances by the *public* FIFO rule every edge.
  (discharge 'public-history step (fifo-allows? child q public-batch next))
  (discharge 'oldest-tag step
             (=> pop (&& (> (fifo-history-count q) 0)
                         (bveq (tag tag-b?) (first (fifo-history-slots q))))))
  (discharge 'two-tag-bound step (<= 0 (fifo-history-count next) 2))
  ;; Establish induction before checking the stronger safety claim: the any-done
  ;; mutant still preserves the public order, bound and ticket correspondence.
  (discharge 'ticket-preservation step successor)
  (discharge 'completed-before-retirement step
             (=> pop (if tag-b? (ticket-completed (second tickets)) (ticket-completed (first tickets)))))
  ;; Strengthen the established tag induction only for data. Compare the external result
  ;; on a retirement handshake, not on a merely valid or stalled offer.
  (define data-step (&& step (payloads-related (edge-state local) tickets)))
  (define data-next (payloads-related (edge-next local) after))
  (discharge 'payload-reset base data-next)
  (discharge 'payload-preservation data-step data-next)
  (discharge 'retired-payload data-step
             (=> pop (bveq (out "retire_result")
                           (if tag-b? (ticket-payload (second tickets)) (ticket-payload (first tickets))))))
  ;; Nonvacuity is independent of induction: each generation of each tag can retire,
  ;; and a full ROB can have just its younger tag completed while the consumer is ready.
  (for* ([lane-b? '(#f #t)] [gen '(#f #t)])
    (require-feasible (&& data-step pop (equal? tag-b? lane-b?)
                          (equal? gen (ticket-gen (if lane-b? (second tickets) (first tickets)))))))
  (require-feasible (&& step (= (fifo-history-count q) 2) (bveq A (first (fifo-history-slots q)))
                        (! (ticket-completed (first tickets))) (ticket-completed (second tickets))
                        (in "retire.ready") (! pop))))

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
  (define child (verified-arbiter-queue-transactions-at 2))
  (test-case "TwoTagROB retirement safety without child cycle state or offers"
    (prove-retirements rob child))
  ;; This mutant preserves FIFO order: it can retire the oldest A merely because B completed.
  (test-case "any-done fails the head's own prior completion, not oldest-first order"
    (check-exn #rx"completed-before-retirement not discharged" (lambda () (prove-retirements faulty_rob child))))
  (test-case "reset, issue A, issue B, complete only B, then try to retire A"
    (younger-only-trace rob child #f)
    (younger-only-trace faulty_rob child #t))
  (test-case "a wrong-generation pulse cannot be recorded as completion"
    (check-exn #rx"ticket-preservation not discharged" (lambda () (prove-retirements unchecked_rob child))))
  (test-case "swapped banks preserve tag safety but return the other allocation's payload"
    (check-exn #rx"retired-payload not discharged" (lambda () (prove-retirements swapped_rob child))))
  (test-case "reverse completions survive matching duplicates during a stall"
    (reverse-payload-trace rob child #f)
    (reverse-payload-trace swapped_rob child #t)))
