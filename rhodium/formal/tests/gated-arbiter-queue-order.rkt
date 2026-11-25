#lang rosette
;; Check registered admission by induction over local RTL and one verified opaque ArbiterQueue.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "../refinement.rkt" "../spec-engine.rkt" "composition-checks.rkt"
         (only-in "../engine.rkt" prepare_transition_snapshot symbolic_transition_state step_transition
                  transition_step_state transition_step_validity transition_step_outputs)
         (only-in "arbiter-queue-binding.rhm" gated_case gated_replay)
         (only-in "arbiter-queue-order.rkt" verified-arbiter-queue-at observe-arbiter-queue)
         (only-in "shift-queue-induction.rkt" proof_depths))

(define (related states reference)
  (define rtl (hash-ref states "@local"))
  (define child (hash-ref (hash-ref states "queue") "pending"))
  (define words (hash-ref reference "pending"))
  (define n (sequence-value-count child))
  (&& (equal? (hash-ref rtl "gate_open") (hash-ref reference "admitting"))
      (bveq n (sequence-value-count words))
      (for/fold ([ok #t]) ([a (sequence-value-slots child)] [b (sequence-value-slots words)] [i (in-naturals)])
        (&& ok (=> (bvugt n (bv i (type-of n))) (bveq a b))))))

(define (input reset enable v0 d0 v1 d1 take)
  (hash "reset" reset "enable" enable "ingress[0].valid" v0 "ingress[0].bits" d0
        "ingress[1].valid" v1 "ingress[1].bits" d1 "egress.ready" take))

(for ([depth proof_depths])
  (test-case (format "registered gate refines external admission and ordered drain at depth ~a" depth)
    (define-values (good reference faulty) (gated_case depth))
    (check-exn #rx"no evidence for RTL specialization" (lambda () (prepare-proved-wiring good '())))
    (define results (list (verified-arbiter-queue-at depth)))
    (define plan (prepare-proved-wiring good results))
    (define fields (for*/list ([(s state) (symbolic-cut-state plan)] [k (hash-keys state)]) (list s k)))
    (check-equal? (sort fields string<? #:key car) '(("@local" "gate_open") ("queue" "pending")))
    (define (certify p) (prove-parent p reference observe-arbiter-queue #:relation related))
    (certify plan)
    (check-exn #rx"preservation/successor not proved" (lambda () (certify (prepare-proved-wiring faulty results))))))

;; Separate reachable replay against the complete RTL, not an input to either refinement query.
(test-case "reset closes the gate: a leaky wrapper retires an unacknowledged word"
  (define-symbolic* word other (bitvector 2))
  (define reset (input #t #t #f word #f other #f))
  (define closed-offer (input #f #f #t word #f other #f))
  (define drain (input #f #f #f word #f other #t))
  (define (runner wrong?)
    (define-values (snapshot in out) (gated_replay 2 wrong?))
    (define rtl (prepare_transition_snapshot snapshot "clock"))
    (define state (symbolic_transition_state rtl))
    (lambda (x)
      (define e (step_transition rtl state (pack-pins in x)))
      (set! state (transition_step_state e))
      (discharge 'rtl-defined #t (transition_step_validity e))
      (unpack-pins out (transition_step_outputs e))))
  (define (high p k) (bveq (hash-ref p k) (bv 1 1)))
  (for ([faulty? '(#f #t)])
    (define tick (runner faulty?))
    (tick reset)
    (define offered (tick closed-offer))
    (define retired (tick drain))
    (discharge 'closed-no-grant #t (! (high offered "ingress[0].ready")))
    (discharge 'unexpected-retirement #t (equal? (high retired "egress.valid") faulty?))
    (when faulty? (discharge 'phantom-word #t (bveq (hash-ref retired "egress.bits") word))))
  ;; The correct gate opens one edge later; it can close again without trapping pending data.
  (define tick (runner #f))
  (tick reset)
  (define opening (tick (hash-set closed-offer "enable" #t)))
  (define accepted (tick closed-offer))
  (define stalled (tick closed-offer))
  (define retired (tick drain))
  (discharge 'open-close-stall-drain #t
             (&& (! (high opening "ingress[0].ready")) (high accepted "ingress[0].ready")
                 (! (high stalled "ingress[0].ready")) (high stalled "egress.valid")
                 (high retired "egress.valid") (bveq (hash-ref retired "egress.bits") word))))
