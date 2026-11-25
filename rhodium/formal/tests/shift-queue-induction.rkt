#lang rosette
;; Proves the fixed ShiftQueue leaf refines its executable FIFO and replays a faulty RTL witness.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "../engine.rkt" "../spec-engine.rkt" "shift-queue-replay.rkt"
         (only-in "shift-queue-binding.rhm" fifo_snapshot initial_contents))
(provide verified-fifo)

;; No relation on stale words in either representation. Compactness and a legal
;; count are conclusions of R(next), not separate assumptions about the RTL.
(define (related rtl q)
  (define n (sequence-value-count q))
  (define occupied (hash-ref rtl occ))
  (define packed (hash-ref rtl payload))
  (for/fold ([ok (bvule n (bv 2 2))]) ([i (in-range 2)] [slot (sequence-value-slots q)])
    (define live (bvugt n (bv i 2)))
    (define lo (* 2 i))
    (&& ok (equal? live (bveq (extract i i occupied) (bv 1 1)))
        (=> live (bveq slot (extract (add1 lo) lo packed))))))

;; Derive both care and values from the RTL interface, not from the spec's decisions.
(define (rtl-observations rtl inputs)
  (define port (transition_step_outputs rtl))
  (define rst (hash-ref inputs "reset"))
  (define ready (bveq (hash-ref port "ready") (bv 1 1)))
  (define valid (bveq (hash-ref port "valid") (bv 1 1)))
  (define head (hash-ref port "head"))
  (values
   (hash "in.ready" (observation (! rst) ready) "out.valid" (observation (! rst) valid)
         "out.data" (observation (&& (! rst) valid) head))
   (hash "reset" (observation rst #t)
         "enqueue" (observation (&& (! rst) ready (hash-ref inputs "in.valid")) (hash-ref inputs "in.data"))
         "dequeue" (observation (&& (! rst) valid (hash-ref inputs "out.ready")) head))))

(define (same-view a b)
  (unless (equal? (sort (hash-keys a) string<?) (sort (hash-keys b) string<?))
    (error 'same-view "observation schemas differ"))
  (for/fold ([ok #t]) ([(name x) a])
    (define y (hash-ref b name))
    (&& ok (equal? (observation-care x) (observation-care y))
        (=> (observation-care x) (equal? (observation-value x) (observation-value y))))))

(define spec (symbolic-spec-edge fifo_snapshot))
(define inputs (edge-inputs spec))
(define old-q (hash-ref (edge-state spec) "q"))
(define next-q (hash-ref (edge-next spec) "q"))
(define old-rtl (symbolic_transition_state plan))
(define rtl (step_transition plan old-rtl (pins inputs)))
(define-values (outputs events) (rtl-observations rtl inputs))
(define outputs-ok (same-view outputs (edge-outputs spec)))
(define events-ok (same-view events (edge-events spec)))
(define next-ok (related (transition_step_state rtl) next-q))

;; These are the only premises. The base case has NO pre-edge RTL invariant.
(define base (&& (edge-legal spec) (hash-ref inputs "reset")))
(define step (&& (edge-legal spec) (related old-rtl old-q)))
(define (counterexample premise conclusion) (solve (assert (&& premise (! conclusion)))))
(define (prove name premise conclusion)
  (with-check-info (['obligation name])
    (check-equal? (counterexample premise conclusion) (unsat))))

(define (check-premises)
  (check-equal? (vc) vc-true) ; no ambient assumptions from either translation
  (check-equal? initial_contents '())
  (check-equal? (hash-keys (edge-state spec)) '("q"))
  (check-equal? (hash-keys (edge-next spec)) '("q"))
  (check-equal? (length (sequence-value-slots old-q)) 2)
  (check-equal? (sort (hash-keys inputs) string<?) '("in.data" "in.valid" "out.ready" "reset"))
  (check-true (sat? (solve (assert (&& base (bveq (hash-ref old-rtl occ) (bv 2 2)))))))
  (for ([n (in-range 3)])
    (check-true (sat? (solve (assert (&& step (bveq (sequence-value-count old-q) (bv n 2)))))))))

(define (check-initialization)
  (prove 'rtl-obligations base (transition_step_validity rtl))
  (prove 'spec-defined base (edge-defined spec))
  (prove 'initial-spec-state base (bveq (sequence-value-count next-q) (bv 0 2)))
  (prove 'post-reset-relation base next-ok)
  (prove 'reset-cycle-view base outputs-ok)
  (prove 'reset-event base events-ok))

(define (check-preservation)
  (prove 'rtl-obligations step (transition_step_validity rtl))
  (prove 'spec-defined step (edge-defined spec))
  (prove 'pre-edge-cycle-view step outputs-ok)
  (prove 'joint-transfer-event step events-ok)
  (prove 'successor-relation step next-ok))

;; A same-run dependency, not a cached verdict: do not release this exact spec
;; if a proof fails, is vacuous, or is unknown. No RTL state crosses the cut.
(define verified-fifo
  (parameterize ([current-check-handler raise])
    (check-premises)
    (check-initialization)
    (check-preservation)
    fifo_snapshot))

(module+ test
  (test-case "a symbolic payload defect yields a reachable mismatch, not just an induction witness"
    (define bad (step_transition faulty old-rtl (pins inputs)))
    (define-values (bad-outputs bad-events) (rtl-observations bad inputs))
    (check-true (sat? (counterexample step (same-view bad-events (edge-events spec)))))
    ;; Ask specifically for a stalled singleton so its abstract prestate has a
    ;; one-enqueue reset prefix. Do not treat arbitrary induction states as reachable.
    (define model (counterexample (&& step (bveq (sequence-value-count old-q) (bv 1 2))
                                      (! (hash-ref inputs "out.ready")))
                                  (same-view bad-outputs (edge-outputs spec))))
    (check-true (sat? model))
    (define full (complete-solution model (symbolics (cons (sequence-value-slots old-q) (hash-values inputs)))))
    (define (concrete term) (evaluate term full))
    (define word (bitvector->natural (concrete (first (sequence-value-slots old-q)))))
    (define last-sample
      (for/hash ([(k term) inputs])
        (values k (if (equal? k "in.data") (bitvector->natural (concrete term)) (concrete term)))))
    (define trace (list reset (sample #f #t word #f) last-sample))
    (define failure (replay faulty start trace))
    (check-true (mismatch? failure))
    (check-equal? (mismatch-prefix failure) trace)
    (check-equal? (mismatch-edge failure) 2)
    (check-equal? (mismatch-field failure) 'outputs)
    (check-equal? (hash-ref (mismatch-expected failure) "out.data") word)
    (check-equal? (hash-ref (mismatch-actual failure) "out.data") (hash-ref last-sample "in.data"))
    (check-equal? (replay plan start trace) 'matched)))
