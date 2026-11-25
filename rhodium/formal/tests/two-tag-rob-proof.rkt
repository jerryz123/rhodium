#lang rosette
;; Certify out-of-order completion and in-order retirement with only local state and the queue contract.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "../refinement.rkt" "../spec-engine.rkt" "composition-checks.rkt"
         (only-in "../engine.rkt" prepare_transition_snapshot symbolic_transition_state step_transition
                  transition_step_state transition_step_validity transition_step_outputs)
         (only-in "arbiter-queue-binding.rhm" rob_case rob_replay)
         (only-in "arbiter-queue-order.rkt" verified-arbiter-queue-at))

(define (live? seq i)
  (define n (sequence-value-count seq))
  (bvugt n (bv i (type-of n))))

;; Only the live prefix matters. Its encoding is exact: no aliases for A or B.
(define (related states ref)
  (define regs (hash-ref states "@local"))
  (define tags (hash-ref (hash-ref states "order") "pending"))
  (define history (hash-ref ref "history"))
  (define h (sequence-value-slots history))
  (define (contains? b)
    (for/fold ([found #f]) ([x h] [i (in-naturals)])
      (|| found (&& (live? history i) (equal? x b)))))
  (define a? (contains? #f))
  (define b? (contains? #t))
  (&& (bveq (sequence-value-count tags) (sequence-value-count history))
      (=> (live? history 1) (! (equal? (first h) (second h))))
      (for/fold ([ok #t]) ([x h] [w (sequence-value-slots tags)] [i (in-naturals)])
        (&& ok (=> (live? history i) (bveq w (if x (bv 1 2) (bv 0 2))))))
      (equal? (hash-ref regs "busy_a") a?) (equal? (hash-ref regs "busy_b") b?)
      (equal? (hash-ref regs "done_a") (hash-ref ref "finished_a"))
      (equal? (hash-ref regs "done_b") (hash-ref ref "finished_b"))
      (=> (hash-ref ref "finished_a") a?) (=> (hash-ref ref "finished_b") b?)))

(define (observe p x)
  (define active (! (hash-ref x "reset")))
  (define take-a (&& (hash-ref x "issue_a") (hash-ref p "grant_a")))
  (define take-b (&& (hash-ref x "issue_b") (hash-ref p "grant_b")))
  (define valid (hash-ref p "retire.valid"))
  (values (for/hash ([(k v) p])
            (values k (observation (if (equal? k "retire.bits") (&& active valid) active) v)))
          (hash "issued" (observation (&& active (|| take-a take-b)) take-b)
                "retired" (observation (&& active valid (hash-ref x "retire.ready")) (hash-ref p "retire.bits")))))

(test-case "a tag cannot be reissued while live, and only its own completion unlocks the oldest tag"
  (define-values (correct reference incorrect) (rob_case))
  (define (admit cut) (prepare-proved-wiring cut (list (verified-arbiter-queue-at 2))))
  (check-exn #rx"no evidence for RTL specialization" (lambda () (prepare-proved-wiring correct '())))
  (define plan (admit correct))
  (define s (symbolic-cut-state plan))
  (check-equal? (sort (hash-keys s) string<?) '("@local" "order"))
  (check-equal? (sort (hash-keys (hash-ref s "@local")) string<?) '("busy_a" "busy_b" "done_a" "done_b"))
  (check-equal? (hash-keys (hash-ref s "order")) '("pending"))
  (prove-parent plan reference observe #:relation related)
  (check-exn #rx"preservation/successor not proved"
             (lambda () (prove-parent (admit incorrect) reference observe #:relation related))))

;; Independent physical RTL replay; the inductive proof above never snapshots the child's registers.
(define (runner wrong?)
  (define-values (snapshot in out) (rob_replay wrong?))
  (define rtl (prepare_transition_snapshot snapshot "clock"))
  (define state (symbolic_transition_state rtl))
  (lambda (#:reset [reset #f] #:a [a #f] #:b [b #f] #:ca [ca #f] #:cb [cb #f] #:ready [ready #f])
    (define x (hash "reset" reset "issue_a" a "issue_b" b "complete_a" ca "complete_b" cb "retire.ready" ready))
    (define e (step_transition rtl state (pack-pins in x)))
    (set! state (transition_step_state e))
    (discharge 'rtl-defined #t (transition_step_validity e))
    (for/hash ([(k v) (unpack-pins out (transition_step_outputs e))]) (values k (bveq v (bv 1 1))))))

(define (port p k) (hash-ref p k))
(define (offer p tag-b?) (&& (port p "retire.valid") (equal? (port p "retire.bits") tag-b?)))

(test-case "A then B; B completes first and cannot retire A"
  (for ([faulty? '(#f #t)])
    (define tick (runner faulty?))
    (tick #:reset #t)
    (define first (tick #:a #t #:b #t))
    (define second (tick #:a #t #:b #t))
    (define completing (tick #:cb #t #:ready #t))
    (define waiting (tick #:ready #t))
    (discharge 'issue #t (&& (port first "grant_a") (! (port first "grant_b"))
                            (! (port second "grant_a")) (port second "grant_b")))
    (discharge 'completion-not-bypassed #t (! (port completing "retire.valid")))
    (if faulty?
        (discharge 'early-retirement-of-A #t (offer waiting #f))
        (let* ([mark (tick #:ca #t #:ready #t)] [stall (tick #:a #t #:b #t)]
               [a (tick #:a #t #:ready #t)] [b (tick #:a #t #:ready #t)] [again (tick #:ready #t)])
          (discharge 'blocked-until-head-completes #t (&& (! (port waiting "retire.valid")) (! (port mark "retire.valid"))))
          (discharge 'ordered-stall-drain-reuse #t
                     (&& (offer stall #f) (! (port stall "grant_a")) (! (port stall "grant_b"))
                         (offer a #f) (! (port a "grant_a")) (offer b #t) (port b "grant_a")
                         (! (port again "retire.valid"))))))))
