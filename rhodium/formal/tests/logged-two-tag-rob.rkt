#lang rosette
;; Check the last-commit register at a sealed ROB boundary; replay the stalled-offer fault independently.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "composition-checks.rkt"
         "two-tag-rob-transactions.rkt" "../../../examples/formal/two-tag-rob-contract.rkt"
         (only-in "composition-binding.rhm" logged_rob valid_only_logged_rob rob pin_layout)
         (only-in "../spec-engine.rkt" edge-state edge-next)
         (only-in "../snapshot.rhm" formal_snapshot)
         (only-in "../engine.rkt" prepare_transition_snapshot symbolic_transition_state step_transition
                  transition_step_state transition_step_validity transition_step_outputs)
         (only-in "../refinement.rkt" pack-pins unpack-pins check-refinement-phase!))

(define (prove-recorder top seal)
  (define-values (inputs outputs locals child) (symbolic-rob-cut top "rob" seal))
  (define public (sample-rob-events inputs outputs))
  ;; Check the parent boundary before using any history or protocol assumptions.
  (check-refinement-phase! 'recorder-wiring #t (list (cons 'public-events (equal? public child))))
  (define local (hash-ref locals "@local"))
  (define old (edge-state local))
  (define next (edge-next local))
  (define (valid r) (hash-ref r "occupied"))
  (define (tag r) (bveq (hash-ref r "tag") (bv 1 1)))
  (define (data r) (hash-ref r "payload"))
  (check-refinement-phase! 'recorder-outputs #t
    (list (cons 'record (&& (equal? (hash-ref outputs "has_commit") (valid old))
                            (equal? (hash-ref outputs "last_tag") (tag old))
                            (bveq (hash-ref outputs "last_payload") (data old))))))
  (define h (fresh-rob-history))
  (define-values (allowed _) (rob-step seal h child))
  (define reset (hash-ref inputs "reset"))
  (define fire (&& (hash-ref outputs "retire.valid") (hash-ref inputs "retire.ready")))
  (define same-word (&& (equal? (tag next) (tag old)) (bveq (data next) (data old))))
  (check-refinement-phase! 'recorder-reset (&& allowed reset)
    (list (cons 'empty (! (valid next))) (cons 'no-capture same-word)))
  (define capture (&& allowed (! reset) fire))
  (define head (rob-history-head h))
  (check-refinement-phase! 'recorder-capture capture
    (list (cons 'recorded (&& (valid next) (equal? (tag next) (hash-ref outputs "retire.bits"))
                              (bveq (data next) (hash-ref outputs "retire_result"))))
          (cons 'completed-oldest (&& (> (rob-history-count h) 0) (entry-done head)
                                      (equal? (tag next) (entry-tag head)) (bveq (data next) (entry-data head))))))
  (define no-fire (&& allowed (! reset) (! fire)))
  (check-refinement-phase! 'recorder-no-transfer no-fire
    (list (cons 'hold (&& (equal? (valid next) (valid old)) same-word))))
  ;; Neither an empty first stall nor a stall over a previous commit is vacuous.
  (for ([occupied '(#f #t)])
    (require-feasible (&& no-fire (hash-ref outputs "retire.valid") (equal? occupied (valid old)))))
  (for ([t '(#f #t)])
    (require-feasible (&& capture (equal? (entry-tag head) t) (! (bveq (data old) (entry-data head))))))
  (require-feasible (&& allowed reset fire (valid old))))

;; Independent full-RTL replay, not a premise of the inductive proof. Outputs are pre-edge.
(define (replay-first-stall top faulty?)
  (define layouts (pin_layout top))
  (define rtl (prepare_transition_snapshot (formal_snapshot top) "clock"))
  (define state (symbolic_transition_state rtl))
  (define (tick #:reset [rst #f] #:issue [issue #f] #:complete [complete #f] #:ready [ready #f])
    (define inputs (hash "reset" rst "issue_a" #f "issue_b" issue "complete_a" #f "complete_b" complete
                         "complete_gen_a" #f "complete_gen_b" #f "result_a" (bv 0 2) "result_b" (bv 3 2)
                         "retire.ready" ready))
    (define e (step_transition rtl state (pack-pins (first layouts) inputs)))
    (set! state (transition_step_state e))
    (discharge 'replay-defined #t (transition_step_validity e))
    (unpack-pins (second layouts) (transition_step_outputs e)))
  (define (bit out k) (bveq (hash-ref out k) (bv 1 1)))
  (tick #:reset #t)
  (define issue (tick #:issue #t))
  (discharge 'grant-b #t (&& (bit issue "grant_b") (! (bit issue "issue_gen"))))
  (tick #:complete #t)
  (define stall (tick))
  (discharge 'first-stall #t (&& (bit stall "retire.valid") (! (bit stall "has_commit"))
                                 (bit stall "retire.bits") (bveq (hash-ref stall "retire_result") (bv 3 2))))
  (define transfer (tick #:ready #t))
  (discharge 'before-handshake #t (&& (bit transfer "retire.valid") (equal? faulty? (bit transfer "has_commit"))))
  (define after (tick))
  (discharge 'after-handshake #t (&& (bit after "has_commit") (bit after "last_tag")
                                     (bveq (hash-ref after "last_payload") (bv 3 2)))))

(module+ test
  (test-case "logged ROB: reset, capture completed oldest on transfer, otherwise hold"
    (prove-recorder logged_rob verified-rob-transactions))
  (test-case "valid-only recorder violates hold on a non-transfer"
    (check-exn #rx"recorder-no-transfer/hold not proved"
               (lambda () (prove-recorder valid_only_logged_rob verified-rob-transactions))))
  (test-case "a seal for a ROB cannot substitute for its internal order queue"
    (check-exn #rx"transaction evidence does not match the RTL occurrence"
               (lambda () (symbolic-rob-cut rob "order" verified-rob-transactions))))
  (test-case "physical replay from reset: first stalled B=3 is not a commit"
    (replay-first-stall logged_rob #f)
    (replay-first-stall valid_only_logged_rob #t)))
