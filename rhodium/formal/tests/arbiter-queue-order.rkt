#lang rosette
;; Prove the external ArbiterQueue contract for the RTL-derived graph with both leaves substituted.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit racket/match "member-pins.rkt" "composition-checks.rkt" "../spec-engine.rkt"
         (only-in "../engine.rkt" prepare_transition_snapshot step_transition symbolic_transition_state
                  transition_step_state transition_step_outputs transition_step_validity)
         (only-in "arbiter-refinement.rkt" verified-arbiter)
         (only-in "shift-queue-induction.rkt" verified-fifo-at proof_depths)
         (only-in "arbiter-queue-binding.rhm" ordering_case
                  undersized_rtl correct_rtl one_slot_rtl depth_two_contract))

(define (inputs reset v0 d0 v1 d1 consume)
  (hash "reset" reset "ingress[0].valid" v0 "ingress[0].bits" d0 "ingress[1].valid" v1 "ingress[1].bits" d1 "egress.ready" consume))
(struct sample (ports events arb fifo take0 take1 safe) #:transparent)
(define (advance fifo-model candidate state x)
  (match-define (list graph arb-site fifo-site) candidate)
  (define certified (hash arb-site verified-arbiter fifo-site fifo-model))
  (define-values (p edges ok) (wired-edge-at graph certified (hash arb-site (hash) fifo-site state) x))
  (define rst (hash-ref x "reset"))
  (define take0 (&& (! rst) (hash-ref x "ingress[0].valid") (hash-ref p "ingress[0].ready")))
  (define take1 (&& (! rst) (hash-ref x "ingress[1].valid") (hash-ref p "ingress[1].ready")))
  (define pop (&& (! rst) (hash-ref x "egress.ready") (hash-ref p "egress.valid")))
  ;; Decode the actual transfers only at the external boundary, never from FIFO enqueue/dequeue.
  (define events (hash "reset" (observation rst #t)
                       "accepted" (observation (|| take0 take1) (if take0 (hash-ref x "ingress[0].bits") (hash-ref x "ingress[1].bits")))
                       "retired" (observation pop (hash-ref p "egress.bits"))))
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
  (define-values (graph contract wrong-graph) (ordering_case depth))
  (define fifo (verified-fifo-at depth))
  (define state (symbolic-spec-state fifo))
  (define history (symbolic-spec-state contract))
  (define-symbolic* rst v0 v1 consume boolean?)
  (define x (inputs rst v0 d0 v1 d1 consume))
  (define impl (advance fifo graph state x))
  (define ref (spec-edge-at contract history x))
  (define n (sequence-value-count (hash-ref state "q")))
  (unless (and (= depth (length (sequence-value-slots (hash-ref state "q"))))
               (= depth (length (sequence-value-slots (hash-ref history "pending")))))
    (error 'arbiter-queue "child and parent must model the requested depth ~a" depth))
  (define legal (&& (bvule n (bv depth (type-of n))) (edge-legal ref)))
  (define base (&& legal rst))
  (define step (&& legal (related depth state history)))
  (define next (edge-next (sample-fifo impl)))
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

  (test-case "reset establishes and each edge preserves the independently authored parent contract"
    (discharge 'domains legal (&& (sample-safe impl) (edge-defined ref)))
    (discharge 'reset base (&& (agrees? impl ref) (related depth next (edge-next ref))
                               (bveq (sequence-value-count (hash-ref next "q")) (bv 0 (type-of n)))))
    (discharge 'public-observations step (agrees? impl ref))
    (discharge 'ordered-live-words step (related depth next (edge-next ref)))
    (discharge 'single-winner legal (! (&& (sample-take0 impl) (sample-take1 impl))))
    (discharge 'internal-handoff legal
               (=> (! rst) (same-event (event (sample-arb impl) "out") (event (sample-fifo impl) "enqueue")))))

  (test-case "actual wrong-lane RTL admits the winner but violates the same contract on drain"
    (define reset (inputs #t #f d0 #f d1 #f))
    (define tie (inputs #f #t d0 #t d1 #f))
    (define drain (inputs #f #f d0 #f d1 #t))
    (define (replay candidate)
      (define cleared (advance fifo candidate state reset))
      (define accepted (advance fifo candidate (edge-next (sample-fifo cleared)) tie))
      (define retired (advance fifo candidate (edge-next (sample-fifo accepted)) drain))
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
                   (matches? (hash-ref (sample-events br) "retired") #t d1)))))

(for ([depth proof_depths])
  (with-check-info (['depth depth]) (check-ordering depth)))

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
