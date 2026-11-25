#lang rosette
;; Proves each elaborated ShiftQueue specialization refines its FIFO, and replays a faulty leaf.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "member-pins.rkt" "../engine.rkt" "../spec-engine.rkt" "shift-queue-replay.rkt"
         (only-in "composition-checks.rkt" discharge require-feasible)
         (only-in "shift-queue-binding.rhm" proof_depths fifo_case fifo_snapshot out_layout))
(provide verified-fifo verified-fifo-at proof_depths)

;; No relation on stale words in either representation. Compactness and a legal
;; count are conclusions of R(next), not separate assumptions about the RTL.
(define (related depth occ-key payload-key rtl q)
  (define n (sequence-value-count q))
  (define occupied (hash-ref rtl occ-key))
  (define packed (hash-ref rtl payload-key))
  (for/fold ([ok (bvule n (bv depth (type-of n)))]) ([i (in-range depth)] [slot (sequence-value-slots q)])
    (define live (bvugt n (bv i (type-of n))))
    (define lo (* 2 i))
    (&& ok (equal? live (bveq (extract i i occupied) (bv 1 1)))
        (=> live (bveq slot (extract (add1 lo) lo packed))))))

;; Derive both care and values from the RTL interface, not from the spec's decisions.
(define (rtl-observations rtl inputs layout)
  (define port (unpack-pins layout (transition_step_outputs rtl)))
  (define rst (hash-ref inputs "reset"))
  (define ready (bveq (hash-ref port "ingress.ready") (bv 1 1)))
  (define valid (bveq (hash-ref port "egress.valid") (bv 1 1)))
  (define head (hash-ref port "egress.bits"))
  (values
   (hash "ingress.ready" (observation (! rst) ready) "egress.valid" (observation (! rst) valid)
         "egress.bits" (observation (&& (! rst) valid) head))
   (hash "reset" (observation rst #t)
         "enqueue" (observation (&& (! rst) ready (hash-ref inputs "ingress.valid")) (hash-ref inputs "ingress.bits"))
         "dequeue" (observation (&& (! rst) valid (hash-ref inputs "egress.ready")) head))))

(define (same-view a b)
  (unless (equal? (sort (hash-keys a) string<?) (sort (hash-keys b) string<?))
    (error 'same-view "observation schemas differ"))
  (for/fold ([ok #t]) ([(name x) a])
    (define y (hash-ref b name))
    (&& ok (equal? (observation-care x) (observation-care y))
        (=> (observation-care x) (equal? (observation-value x) (observation-value y))))))

;; No ambient RTL invariant on reset. The inductive edge starts with independently
;; allocated RTL and abstract states, related only by length and the live payloads.
(define (verify-fifo depth)
  (define-values (snapshot root layout model initial) (fifo_case depth))
  (define p (prepare_transition_snapshot snapshot "clock"))
  (define occ-key (string-append root ".occupied"))
  (define payload-key (string-append root ".payload"))
  (define spec (symbolic-spec-edge model))
  (define inputs (edge-inputs spec))
  (define q (hash-ref (edge-state spec) "q"))
  (define n (sequence-value-count q))
  (define old (symbolic_transition_state p))
  (define rtl (step_transition p old (pins inputs)))
  (define-values (outs events) (rtl-observations rtl inputs layout))
  (define next-q (hash-ref (edge-next spec) "q"))
  (define (rel s q) (related depth occ-key payload-key s q))
  (define next-ok (rel (transition_step_state rtl) next-q))
  (define base (&& (edge-legal spec) (hash-ref inputs "reset")))
  (define step (&& (edge-legal spec) (rel old q)))
  (unless (and (null? initial) (equal? (hash-keys (edge-state spec)) '("q"))
               (equal? (hash-keys (edge-next spec)) '("q"))
               (= depth (length (sequence-value-slots q)))
               (equal? (type-of (hash-ref old occ-key)) (bitvector depth))
               (equal? (type-of (hash-ref old payload-key)) (bitvector (* 2 depth)))
               (equal? (sort (hash-keys inputs) string<?) '("egress.ready" "ingress.bits" "ingress.valid" "reset")))
    (error 'verify-fifo "unexpected RTL/spec shape at depth ~a" depth))
  ;; A hole is possible before reset only when there are at least two slots.
  (require-feasible (&& base (bveq (hash-ref old occ-key) (bv (if (= depth 1) 1 2) depth))))
  (for ([k (in-range (add1 depth))])
    (require-feasible (&& step (bveq n (bv k (type-of n))))))
  (discharge 'reset-empty base (bveq (sequence-value-count next-q) (bv 0 (type-of n))))
  (for ([premise (list base step)] [phase '(reset preservation)])
    (define (prove name goal) (discharge (list 'fifo depth phase name) premise goal))
    (prove 'rtl-obligations (transition_step_validity rtl))
    (prove 'defined (edge-defined spec))
    (prove 'outputs (same-view outs (edge-outputs spec)))
    (prove 'transfers (same-view events (edge-events spec)))
    (prove 'successor next-ok))
  model)

;; Only completed, non-vacuous proofs enter this run-local table. QueuePair uses depth two.
(define verified (for/hash ([depth proof_depths]) (values depth (verify-fifo depth))))
(define (verified-fifo-at depth) (hash-ref verified depth))
(define verified-fifo (verified-fifo-at 2))

(module+ test
  (test-case "a symbolic payload defect yields a reachable mismatch, not just an induction witness"
    (define spec (symbolic-spec-edge fifo_snapshot))
    (define inputs (edge-inputs spec))
    (define old-q (hash-ref (edge-state spec) "q"))
    (define old-rtl (symbolic_transition_state plan))
    (define step (&& (edge-legal spec) (related 2 occ payload old-rtl old-q)))
    (define (counterexample premise goal) (solve (assert (&& premise (! goal)))))
    (define bad (step_transition faulty (fault-state old-rtl) (pins inputs)))
    (define-values (bad-outputs bad-events) (rtl-observations bad inputs out_layout))
    (check-true (sat? (counterexample step (same-view bad-events (edge-events spec)))))
    ;; Ask specifically for a stalled singleton so its abstract prestate has a
    ;; one-enqueue reset prefix. Do not treat arbitrary induction states as reachable.
    (define model (counterexample (&& step (bveq (sequence-value-count old-q) (bv 1 2))
                                      (! (hash-ref inputs "egress.ready")))
                                  (same-view bad-outputs (edge-outputs spec))))
    (check-true (sat? model))
    (define full (complete-solution model (symbolics (cons (sequence-value-slots old-q) (hash-values inputs)))))
    (define (concrete term) (evaluate term full))
    (define word (bitvector->natural (concrete (first (sequence-value-slots old-q)))))
    (define last-sample
      (for/hash ([(k term) inputs])
        (values k (if (equal? k "ingress.bits") (bitvector->natural (concrete term)) (concrete term)))))
    (define trace (list reset (sample #f #t word #f) last-sample))
    (define failure (replay faulty start trace))
    (check-true (mismatch? failure))
    (check-equal? (mismatch-prefix failure) trace)
    (check-equal? (mismatch-edge failure) 2)
    (check-equal? (mismatch-field failure) 'outputs)
    (check-equal? (hash-ref (mismatch-expected failure) "egress.bits") word)
    (check-equal? (hash-ref (mismatch-actual failure) "egress.bits") (hash-ref last-sample "ingress.bits"))
    (check-equal? (replay plan start trace) 'matched)))
