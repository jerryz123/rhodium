#lang rosette
;; Certify out-of-order completion and in-order retirement with only local state and the queue contract.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "../refinement.rkt" "../spec-engine.rkt" "composition-checks.rkt"
         (only-in "../spec.rhm" spec_snapshot)
         (only-in "../engine.rkt" prepare_transition_snapshot symbolic_transition_state step_transition
                  transition_step_state transition_step_validity transition_step_outputs)
         (only-in "arbiter-queue-binding.rhm" rob_case rob_replay)
         (only-in "arbiter-queue-order.rkt" verified-arbiter-queue-at))

(define (live? seq i)
  (define n (sequence-value-count seq))
  (bvugt n (bv i (type-of n))))

;; Only the live prefix matters. Its encoding is exact: no aliases for A or B.
(define (tag-related states ref)
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
      ;; RTL toggles on retirement; the spec advances its next-issue bit on allocation.
      (equal? (hash-ref regs "gen_a") (if a? (! (hash-ref ref "next_gen_a")) (hash-ref ref "next_gen_a")))
      (equal? (hash-ref regs "gen_b") (if b? (! (hash-ref ref "next_gen_b")) (hash-ref ref "next_gen_b")))
      (equal? (hash-ref regs "busy_a") a?) (equal? (hash-ref regs "busy_b") b?)
      (equal? (hash-ref regs "done_a") (hash-ref ref "finished_a"))
      (equal? (hash-ref regs "done_b") (hash-ref ref "finished_b"))
      (=> (hash-ref ref "finished_a") a?) (=> (hash-ref ref "finished_b") b?)))

(define (related states ref)
  (define regs (hash-ref states "@local"))
  (&& (tag-related states ref)
      (=> (hash-ref ref "finished_a") (bveq (hash-ref regs "saved_a") (hash-ref ref "value_a")))
      (=> (hash-ref ref "finished_b") (bveq (hash-ref regs "saved_b") (hash-ref ref "value_b")))))

(define (observe p x)
  (define active (! (hash-ref x "reset")))
  (define take-a (&& (hash-ref x "issue_a") (hash-ref p "grant_a")))
  (define take-b (&& (hash-ref x "issue_b") (hash-ref p "grant_b")))
  (define issue (&& active (|| take-a take-b)))
  (define valid (hash-ref p "retire.valid"))
  (values (for/hash ([(k v) p])
            (values k (observation (cond [(equal? k "issue_gen") issue]
                                         [(member k '("retire.bits" "retire_result")) (&& active valid)]
                                         [else active]) v)))
          (hash "issued" (observation issue take-b)
                "issued_gen" (observation issue (hash-ref p "issue_gen"))
                "retired_value" (observation (&& active valid (hash-ref x "retire.ready")) (hash-ref p "retire_result"))
                "retired" (observation (&& active valid (hash-ref x "retire.ready")) (hash-ref p "retire.bits")))))

(define (observe-tags p x)
  (define-values (outputs events) (observe p x))
  (values (hash-remove outputs "retire_result") (hash-remove events "retired_value")))

(test-case "a tag cannot be reissued while live, and only its own completion unlocks the oldest tag"
  (define-values (correct reference incorrect swapped tags unchecked) (rob_case))
  (define (admit cut) (prepare-proved-wiring cut (list (verified-arbiter-queue-at 2))))
  (check-exn #rx"no evidence for RTL specialization" (lambda () (prepare-proved-wiring correct '())))
  (define plan (admit correct))
  (define s (symbolic-cut-state plan))
  (check-equal? (sort (hash-keys s) string<?) '("@local" "order"))
  (check-equal? (sort (hash-keys (hash-ref s "@local")) string<?) '("busy_a" "busy_b" "done_a" "done_b" "gen_a" "gen_b" "saved_a" "saved_b"))
  (check-equal? (hash-keys (hash-ref s "order")) '("pending"))
  ;; The banks have no reset mux: on the reset edge their physical D can hold or
  ;; capture according to the old enable, even though both next values are uncared.
  (define x (hash-set (edge-inputs (symbolic-spec-edge (spec_snapshot reference))) "reset" #t))
  (define before (hash-ref s "@local"))
  (define-values (_pins edges ok) (proved-edge-at plan s x))
  (define local (hash-ref edges "@local"))
  (define cap (&& (hash-ref before "busy_a") (! (hash-ref before "done_a")) (hash-ref x "complete_a")
                  (equal? (hash-ref x "complete_gen_a") (hash-ref before "gen_a"))))
  (define legal (andmap spec-state-legal? (hash-values s)))
  (require-feasible (&& legal cap (! (bveq (hash-ref x "result_a") (hash-ref before "saved_a")))))
  (require-feasible (&& legal (! cap) (bveq (hash-ref before "saved_a") (bv 3 2))))
  (discharge 'unreset-d legal (&& ok (edge-defined local)
                                  (bveq (hash-ref (edge-next local) "saved_a")
                                        (if cap (hash-ref x "result_a") (hash-ref before "saved_a")))))
  (prove-parent plan reference observe #:relation related)
  (check-exn #rx"preservation/successor not proved"
             (lambda () (prove-parent (admit unchecked) reference observe #:relation related)))
  ;; Same faulty implementation and child proof. Only the observable contract changes.
  (define swapped-plan (admit swapped))
  (prove-parent swapped-plan tags observe-tags #:relation tag-related)
  (check-exn #rx"preservation/outputs not proved"
             (lambda () (prove-parent swapped-plan reference observe #:relation related)))
  (check-exn #rx"preservation/successor not proved"
             (lambda () (prove-parent (admit incorrect) reference observe #:relation related))))

;; Independent physical RTL replay; the inductive proof above never snapshots the child's registers.
(define (runner variant)
  (define-values (snapshot in out) (rob_replay variant))
  (define rtl (prepare_transition_snapshot snapshot "clock"))
  (define state (symbolic_transition_state rtl))
  (lambda (#:reset [reset #f] #:a [a #f] #:b [b #f] #:ca [ca #f] #:cb [cb #f] #:ga [ga #f] #:gb [gb #f]
           #:va [va (bv 0 2)] #:vb [vb (bv 0 2)] #:ready [ready #f])
    (define x (hash "reset" reset "issue_a" a "issue_b" b "complete_a" ca "complete_b" cb "complete_gen_a" ga "complete_gen_b" gb
                    "result_a" va "result_b" vb "retire.ready" ready))
    (define e (step_transition rtl state (pack-pins in x)))
    (set! state (transition_step_state e))
    (discharge 'rtl-defined #t (transition_step_validity e))
    (for/hash ([(k v) (unpack-pins out (transition_step_outputs e))]) (values k (if (equal? k "retire_result") v (bveq v (bv 1 1)))))))

(define (port p k) (hash-ref p k))
(define (offer p tag-b?) (&& (port p "retire.valid") (equal? (port p "retire.bits") tag-b?)))

(test-case "A then B; B completes first and cannot retire A"
  (for ([faulty? '(#f #t)])
    (define tick (runner (if faulty? "any-done" "correct")))
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

(test-case "a stalled completed head keeps its tag and first result for arbitrary next traffic"
  (define-values (_impl model _early _swap _tags _unchecked) (rob_case))
  (define ref (spec_snapshot model))
  (define old (symbolic-spec-edge ref))
  (define inputs (edge-inputs (symbolic-spec-edge ref)))
  (define next (spec-edge-at ref (edge-next old) inputs))
  (define (pin edge name) (observation-value (hash-ref (edge-outputs edge) name)))
  (define stall (&& (edge-legal old) (! (hash-ref (edge-inputs old) "reset"))
                    (pin old "retire.valid") (! (hash-ref (edge-inputs old) "retire.ready"))
                    (! (hash-ref inputs "reset"))))
  (require-feasible stall)
  (discharge 'stable-offer stall
             (&& (edge-defined old) (edge-defined next) (pin next "retire.valid")
                 (equal? (pin old "retire.bits") (pin next "retire.bits"))
                 (bveq (pin old "retire_result") (pin next "retire_result")))))

(test-case "reverse completions B=3 then A=1; stalls, duplicate completions and swapped output banks"
  (for ([swapped? '(#f #t)])
    (define tick (runner (if swapped? "swapped" "correct")))
    (tick #:reset #t)
    (tick #:a #t)
    (tick #:b #t)
    (tick #:cb #t #:vb (bv 3 2))
    (define blocked (tick #:ca #t #:va (bv 1 2) #:ready #t))
    (define stalled (tick #:ca #t #:va (bv 2 2) #:cb #t #:vb (bv 0 2)))
    (define held (tick))
    (define a (tick #:ready #t))
    (define b (tick #:ready #t))
    (discharge 'tags-and-results #t
               (&& (! (port blocked "retire.valid"))
                   (for/fold ([ok #t]) ([p (list stalled held a)])
                     (&& ok (offer p #f) (bveq (port p "retire_result") (bv (if swapped? 3 1) 2))))
                   (offer b #t) (bveq (port b "retire_result") (bv (if swapped? 1 3) 2))))))

(test-case "per-tag issue bits alternate; a mismatching completion is equivalent to silence"
  (define-values (_impl model _early _swap _tags _unchecked) (rob_case))
  (define ref (spec_snapshot model))
  (define e (symbolic-spec-edge ref))
  (define x (edge-inputs e))
  (define s (edge-state e))
  (define active (&& (edge-legal e) (! (hash-ref x "reset"))))
  (for ([lane '("a" "b")])
    (define (k prefix) (string->immutable-string (string-append prefix lane)))
    (define bit (hash-ref s (k "next_gen_")))
    (define grant (observation-value (hash-ref (edge-outputs e) (k "grant_"))))
    (define advertised (hash-ref (edge-events e) "issued_gen"))
    (discharge 'advance-only-on-own-grant active
               (&& (=> grant (&& (observation-care advertised) (equal? bit (observation-value advertised))))
                   (equal? (hash-ref (edge-next e) (k "next_gen_")) (if grant (! bit) bit))))
    (define history (hash-ref s "history"))
    (define live (for/fold ([found #f]) ([tag (sequence-value-slots history)] [i (in-naturals)])
                   (|| found (&& (live? history i) (equal? tag (equal? lane "b"))))))
    ;; For a live issue, its advertised bit is the opposite of the next allocation's bit.
    (define miss (&& active live (hash-ref x (k "complete_")) (equal? bit (hash-ref x (k "complete_gen_")))))
    (define quiet (spec-edge-at ref s (hash-set x (k "complete_") #f)))
    (for ([parity '(#f #t)])
      (require-feasible (&& miss (! (hash-ref s (k "finished_"))) (equal? bit parity))))
    (discharge 'stale-is-inert miss
               (&& (edge-defined e) (edge-defined quiet)
                   (view-agrees? (edge-outputs e) (edge-outputs quiet))
                   (view-agrees? (edge-events e) (edge-events quiet))
                   (for/fold ([ok #t]) ([(key value) (edge-next e)])
                     (&& ok (equal? value (hash-ref (edge-next quiet) key))))))))

(test-case "late A0 cannot complete A1; identical wire tokens alias after wrap"
  (for ([broken? '(#f #t)])
    (define tick (runner (if broken? "ignore-generation" "correct")))
    (tick #:reset #t)
    (define a0 (tick #:a #t))
    (tick #:ca #t #:ga #f #:va (bv 2 2))
    (define first (tick #:ready #t))
    (define a1 (tick #:a #t))
    (tick #:ca #t #:ga #f #:va (bv 3 2))
    (define late (tick #:ready #t))
    (discharge 'adjacent-tokens #t
               (&& (port a0 "grant_a") (! (port a0 "issue_gen"))
                   (offer first #f) (bveq (port first "retire_result") (bv 2 2))
                   (port a1 "grant_a") (port a1 "issue_gen")))
    (if broken?
        (discharge 'stale-corrupts-reused-tag #t (&& (offer late #f) (bveq (port late "retire_result") (bv 3 2))))
        (let* ([matching (tick #:ca #t #:ga #t #:va (bv 1 2))]
               [good (tick #:ready #t)] [a2 (tick #:a #t)]
               [_echo (tick #:ca #t #:ga #f #:va (bv 3 2))] [wrap (tick #:ready #t)])
          (discharge 'reject-before-match #t
                     (&& (! (port late "retire.valid")) (! (port matching "retire.valid"))
                         (offer good #f) (bveq (port good "retire_result") (bv 1 2))))
          ;; A duplicate from the original A0 has exactly the bits of a current A2 response.
          (discharge 'one-bit-alias #t
                     (&& (port a2 "grant_a") (! (port a2 "issue_gen"))
                         (offer wrap #f) (bveq (port wrap "retire_result") (bv 3 2))))))))
