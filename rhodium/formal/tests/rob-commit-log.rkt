#lang rosette
;; Prove an ordinal commit-log property using only the sealed ROB contract and public events.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "composition-checks.rkt" "rob-observer-records.rkt")

;; Observe arbitrary zero-based k. No slot or hardware value is read to update log state:
;; abstract slots are used only to state the induction invariant.
(define (check-commit-log evidence)
  (define-symbolic* k grants retirements integer?)
  (define-symbolic* tag generation matched boolean?)
  (define-symbolic* first-data (bitvector 2))
  (define old (fresh-rob-history))
  (define e (fresh-rob-events))
  (define-values (allowed next) (rob-step evidence old e))
  (define grant (rob-events-grant e))
  (define pop (rob-events-retire e))
  (define kth-grant (&& grant (= k grants)))
  ;; The log appends exactly (retire-tag, retire-data) per retirement; this is
  ;; the write-enable for entry k, so no unbounded array is needed.
  (define kth-log (&& pop (= k retirements)))
  (define tracked (<= retirements k (sub1 grants)))
  (define pulse (if tag (rob-events-b e) (rob-events-a e)))
  (define capture (&& tracked (! matched) (response-present pulse) (equal? generation (response-gen pulse))))
  (define (inv h g r t gen done word)
    (&& (<= 0 r g) (= (rob-history-count h) (- g r))
        (for/fold ([ok #t]) ([slot (list (rob-history-head h) (rob-history-tail h))] [i (in-naturals)])
          (&& ok (=> (&& (= k (+ r i)) (< k g))
                     (&& (equal? t (entry-tag slot)) (equal? gen (entry-gen slot))
                         (equal? done (entry-done slot)) (=> done (bveq word (entry-data slot)))))))))
  (define base (&& (>= k 0) allowed (rob-events-reset e)))
  (define step (&& (>= k 0) allowed (! (rob-events-reset e))
                   (inv old grants retirements tag generation matched first-data)))
  (require-feasible (&& base (= (rob-history-count old) 2)))
  (discharge 'log-reset base (&& (! grant) (! pop) (inv next 0 0 #f #f #f (bv 0 2))))
  (discharge 'log-preserved step
             (inv next (+ grants (if grant 1 0)) (+ retirements (if pop 1 0))
                  (if kth-grant (rob-events-grant-tag e) tag)
                  (if kth-grant (rob-events-grant-gen e) generation)
                  (if kth-grant #f (|| matched capture))
                  (if capture (response-data pulse) first-data)))
  (discharge 'kth-commit step
             (=> kth-log (&& tracked matched (equal? (rob-events-retire-tag e) tag)
                             (bveq (rob-events-retire-data e) first-data))))
  ;; Grants, completions and retirement are not serialized by the induction premises.
  (require-feasible (&& step kth-grant pop))
  (require-feasible (&& step capture (< retirements k) pop))
  (for* ([t '(#f #t)] [gen '(#f #t)])
    (require-feasible (&& step kth-log grant (equal? tag t) (equal? generation gen)))))

;; Reach a completed head from reset. An idle self-loop can repeat any number of times;
;; retirement from that very same state is also permitted, not required.
(define (check-no-retirement-deadline evidence)
  (define z (bv 0 2))
  (define data (bv 3 2))
  (define quiet (response #f #f z))
  (define idle (rob-events #f #f #f #f quiet quiet #f #f z))
  (define (take h e)
    (define-values (ok next) (rob-step evidence h e))
    (check-true ok)
    next)
  (define empty (take (fresh-rob-history) (struct-copy rob-events idle [reset #t])))
  (define live (take empty (struct-copy rob-events idle [grant #t] [grant-gen #t])))
  (define complete (take live (struct-copy rob-events idle [a (response #t #t data)])))
  (check-equal? (rob-history-count complete) 1)
  (check-true (entry-done (rob-history-head complete)))
  (check-equal? (take complete idle) complete)
  (check-equal? (rob-history-count (take complete (struct-copy rob-events idle [retire #t] [retire-data data]))) 0))

(module+ test
  (require (only-in "two-tag-rob-transactions.rkt" verified-rob-transactions))
  (test-case "kth log entry since reset: granted tag and first matching-generation response"
    (check-commit-log verified-rob-transactions))
  (test-case "no retirement deadline follows from a completed head"
    (check-no-retirement-deadline verified-rob-transactions)))
