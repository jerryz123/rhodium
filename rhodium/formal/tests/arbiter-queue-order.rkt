#lang rosette
;; Prove the external ArbiterQueue contract for the RTL-derived graph with both leaves substituted.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit racket/match "../refinement.rkt" "composition-checks.rkt" "../spec-engine.rkt"
         (only-in "../spec.rhm" spec_snapshot)
         (only-in "../engine.rkt" prepare_transition_snapshot step_transition symbolic_transition_state
                  transition_step_state transition_step_outputs transition_step_validity)
         (only-in "arbiter-refinement.rkt" verified-arbiter)
         (only-in "shift-queue-induction.rkt" verified-fifo-at proof_depths)
         (only-in "arbiter-queue-binding.rhm" ordering_case separate_reset_cut alternate_cut swapped_view_cut fresh_cut
                  undersized_rtl correct_rtl one_slot_rtl depth_two_contract))

(provide verified-arbiter-queue-at (rename-out [observe observe-arbiter-queue]))

(define (inputs reset v0 d0 v1 d1 consume)
  (hash "reset" reset "ingress[0].valid" v0 "ingress[0].bits" d0 "ingress[1].valid" v1 "ingress[1].bits" d1 "egress.ready" consume))
(struct sample (ports events arb fifo take0 take1 safe) #:transparent)
(define (admit candidate results)
  (match-define (list cut arb-site fifo-site) candidate)
  (list (prepare-proved-wiring cut results) arb-site fifo-site))

(define (boundary p x)
  (define rst (hash-ref x "reset"))
  (define take0 (&& (! rst) (hash-ref x "ingress[0].valid") (hash-ref p "ingress[0].ready")))
  (define take1 (&& (! rst) (hash-ref x "ingress[1].valid") (hash-ref p "ingress[1].ready")))
  (define pop (&& (! rst) (hash-ref x "egress.ready") (hash-ref p "egress.valid")))
  ;; Decode the actual transfers only at the external boundary, never from FIFO enqueue/dequeue.
  (define events (hash "reset" (observation rst #t)
                       "accepted" (observation (|| take0 take1) (if take0 (hash-ref x "ingress[0].bits") (hash-ref x "ingress[1].bits")))
                       "retired" (observation pop (hash-ref p "egress.bits"))))
  (define outputs (for/hash ([(k v) p])
                    (values k (observation (if (equal? k "egress.bits") (&& (! rst) (hash-ref p "egress.valid")) (! rst)) v))))
  (values outputs events take0 take1))
(define (observe p x)
  (define-values (out events _a _b) (boundary p x))
  (values out events))
(define (advance candidate state x)
  (match-define (list plan arb-site fifo-site) candidate)
  (define-values (p edges ok) (proved-edge-at plan (hash arb-site (hash) fifo-site state) x))
  (define-values (_out events take0 take1) (boundary p x))
  (define arb (hash-ref edges arb-site))
  (define fifo (hash-ref edges fifo-site))
  (sample p events arb fifo take0 take1
          (&& ok (edge-legal arb) (edge-defined arb) (edge-legal fifo) (edge-defined fifo))))

;; Compare all external values cared about by the contract, and presence as well as payload for events.
(define (agrees? impl ref)
  (define (keys h) (sort (hash-keys h) string<?))
  (unless (and (equal? (keys (sample-ports impl)) (keys (edge-outputs ref)))
               (equal? (keys (sample-events impl)) (keys (edge-events ref))))
    (error 'arbiter-queue "external schemas differ"))
  (&& (for/fold ([ok #t]) ([(k expected) (edge-outputs ref)])
        (&& ok (=> (observation-care expected)
                   (equal? (hash-ref (sample-ports impl) k) (observation-value expected)))))
      (for/fold ([ok #t]) ([(k expected) (edge-events ref)])
        (&& ok (same-event (hash-ref (sample-events impl) k) expected)))))

;; The proof relation mentions only abstract live words, not either child's RTL state or padding.
(define (related depth state reference)
  (define q (hash-ref state "q"))
  (define history (hash-ref reference "pending"))
  (define n (sequence-value-count q))
  (&& (bvule n (bv depth (type-of n))) (bveq n (sequence-value-count history))
      (for/fold ([ok #t]) ([w (sequence-value-slots q)] [p (sequence-value-slots history)] [i (in-naturals)])
        (&& ok (=> (bvugt n (bv i (type-of n))) (bveq w p))))))
(define-symbolic* d0 d1 (bitvector 2))

(define (check-ordering depth)
  (define-values (candidate reference wrong) (ordering_case depth))
  (define contract (spec_snapshot reference))
  (define (certify graph)
    (prove-parent (first graph) reference observe
                  #:relation (lambda (states parent) (related depth (hash-ref states (third graph)) parent))))
  (define results (list verified-arbiter (verified-fifo-at depth)))
  (define graph (admit candidate results))
  (define wrong-graph (admit wrong results))
  (define fifo (proved-child-model (first graph) (third graph)))
  (define state (symbolic-spec-state fifo))
  (define history (symbolic-spec-state contract))
  (define-symbolic* rst v0 v1 consume boolean?)
  (define x (inputs rst v0 d0 v1 d1 consume))
  (define impl (advance graph state x))
  (define ref (spec-edge-at contract history x))
  (define n (sequence-value-count (hash-ref state "q")))
  (unless (and (= depth (length (sequence-value-slots (hash-ref state "q"))))
               (= depth (length (sequence-value-slots (hash-ref history "pending")))))
    (error 'arbiter-queue "child and parent must model the requested depth ~a" depth))
  (define legal (&& (bvule n (bv depth (type-of n))) (edge-legal ref)))
  (define base (&& legal rst))
  (define step (&& legal (related depth state history)))
  (define pop (observation-care (event ref "retired")))

  (test-case "each occupancy is feasible and concurrent transfers respect this depth"
    (require-feasible (&& base (bveq n (bv depth (type-of n))) (! (related depth state history))))
    (for ([k (in-range (add1 depth))])
      (require-feasible (&& step (bveq n (bv k (type-of n))))))
    (if (= depth 1)
        ;; A single non-pipelined slot cannot accept and retire on the same edge.
        (discharge 'one-slot-no-replacement step (! (&& pop (|| (sample-take0 impl) (sample-take1 impl)))))
        (require-feasible (&& step v0 v1 (sample-take0 impl) pop)))
    (require-feasible (&& step (sample-take1 impl)))
    (require-feasible (&& step v0 v1 pop (bveq n (bv depth (type-of n))) (! (sample-take0 impl)) (! (sample-take1 impl))))
    (require-feasible (&& base (observation-care (event (sample-arb impl) "out")))))

  (test-case "the children exchange exactly one winner"
    (discharge 'single-winner legal (! (&& (sample-take0 impl) (sample-take1 impl))))
    (discharge 'internal-handoff legal
               (=> (! rst) (same-event (event (sample-arb impl) "out") (event (sample-fifo impl) "accepted")))))

  (test-case "actual wrong-lane RTL admits the winner but violates the same contract on drain"
    (check-exn #rx"preservation/successor not proved" (lambda () (certify wrong-graph)))
    (define reset (inputs #t #f d0 #f d1 #f))
    (define tie (inputs #f #t d0 #t d1 #f))
    (define drain (inputs #f #f d0 #f d1 #t))
    (define (replay candidate)
      (define cleared (advance candidate state reset))
      (define accepted (advance candidate (edge-next (sample-fifo cleared)) tie))
      (define retired (advance candidate (edge-next (sample-fifo accepted)) drain))
      (values accepted retired (&& (sample-safe cleared) (sample-safe accepted) (sample-safe retired))))
    (define-values (ga gr gs) (replay graph))
    (define-values (ba br bs) (replay wrong-graph))
    (define c0 (spec-edge-at contract history reset))
    (define c1 (spec-edge-at contract (edge-next c0) tie))
    (define c2 (spec-edge-at contract (edge-next c1) drain))
    (define distinct (&& legal (! (bveq d0 d1))))
    (require-feasible (&& distinct (! (agrees? br c2))))
    (discharge 'replay distinct
               (&& gs bs (edge-defined c0) (edge-defined c1) (edge-defined c2)
                   (agrees? ga c1) (agrees? ba c1) (sample-take0 ba) (agrees? gr c2) (! (agrees? br c2))
                   (matches? (event c2 "retired") #t d0)
                   (matches? (hash-ref (sample-events br) "retired") #t d1))))
  (certify graph))

(test-case "preflight rejects unrelated or absent evidence before a parent edge is constructed"
  (define-values (candidate _ wrong) (ordering_case 2))
  (check-exn #rx"no evidence for RTL specialization"
             (lambda () (admit candidate (list verified-arbiter (verified-fifo-at 1)))))
  (check-exn #rx"no evidence for RTL specialization"
             (lambda () (admit candidate (list (verified-fifo-at 2)))))
  (check-exn #rx"expected completed refinement results"
             (lambda () (admit wrong (list verified-arbiter (hash)))))
  (check-exn #rx"expected direct parent reset at storage" separate_reset_cut)
  (define right (list verified-arbiter (verified-fifo-at 2)))
  (for ([change-start? '(#t #f)])
    (check-exn #rx"evidence does not cover contract"
               (lambda () (prepare-proved-wiring (alternate_cut change-start?) right))))
  (check-exn #rx"evidence does not cover contract"
             (lambda () (prepare-proved-wiring (swapped_view_cut) right)))
  (check-exn #rx"no evidence for RTL specialization"
             (lambda () (prepare-proved-wiring (fresh_cut) right))))

(define verified-parents
  (for/hash ([depth proof_depths])
    (values depth (with-check-info (['depth depth]) (check-ordering depth)))))
(define (verified-arbiter-queue-at depth) (hash-ref verified-parents depth))

(test-case "depth-two parent with a one-entry RTL FIFO rejects the second uncontested offer"
  ;; Compare the faulty RTL with the correct depth-two declaration's contract. Its leaf
  ;; certificate does not cover this child. Observe only the external handshake.
  (define reset (inputs #t #f d0 #f d1 #f))
  (define first (inputs #f #t d0 #f d1 #f))
  (define second (inputs #f #t d1 #f d0 #f))
  (define (rtl-trace design)
    (define-values (snapshot inputs outputs) (design))
    (define p (prepare_transition_snapshot snapshot "clock"))
    (define (tick s x) (step_transition p s (pack-pins inputs x)))
    (define z (tick (symbolic_transition_state p) reset))
    (define a (tick (transition_step_state z) first))
    (define b (tick (transition_step_state a) second))
    (define (grant e) (bveq (hash-ref (unpack-pins outputs (transition_step_outputs e)) "ingress[0].ready") (bv 1 1)))
    (values (grant a) (grant b) (&& (transition_step_validity z)
                                   (transition_step_validity a) (transition_step_validity b))))
  (define-values (g1 g2 good-ok) (rtl-trace correct_rtl))
  (define-values (b1 b2 bad-ok) (rtl-trace undersized_rtl))
  ;; With no injected fault, requesting depth one must really specialize the RTL too.
  (define-values (s1 s2 single-ok) (rtl-trace one_slot_rtl))
  (define z (spec-edge-at depth_two_contract (symbolic-spec-state depth_two_contract) reset))
  (define a (spec-edge-at depth_two_contract (edge-next z) first))
  (define b (spec-edge-at depth_two_contract (edge-next a) second))
  (define want (event b "accepted"))
  (require-feasible (&& (edge-legal z) good-ok bad-ok (observation-care want) (! b2)))
  (discharge 'public-depth (edge-legal z)
             (&& good-ok bad-ok (edge-defined z) (edge-defined a) (edge-defined b)
                 (matches? (event a "accepted") #t d0) (matches? want #t d1)
                 g1 b1 g2 (! b2) single-ok s1 (! s2))))
