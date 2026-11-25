#lang rosette
;; Track one arbitrary grant ordinal from public events; only its invariant reads public spec history.
;; SPDX-License-Identifier: Apache-2.0

(require "composition-checks.rkt" (only-in "../spec-engine.rkt" sequence-value-count sequence-value-slots))
(provide check-ordinal-edge)

(define (check-ordinal-edge old e allowed next)
  (define-symbolic* k grants retirements integer?)
  (define-symbolic* tag generation matched boolean?)
  (define-symbolic* first-data (bitvector 2))
  (define (event key) (hash-ref e key))
  (define grant (event "grant"))
  (define pop (event "retire"))
  (define kth-grant (&& grant (= k grants)))
  ;; The log appends exactly (retire-tag, retire-data) per retirement; this is
  ;; the write-enable for entry k, so no unbounded array is needed.
  (define kth-log (&& pop (= k retirements)))
  (define tracked (<= retirements k (sub1 grants)))
  (define (lane a b) (if tag (event b) (event a)))
  (define capture (&& tracked (! matched) (lane "complete_a" "complete_b") (equal? generation (lane "echo_a" "echo_b"))))
  (define (count h) (bitvector->natural (sequence-value-count (hash-ref h "order"))))
  (define (inv h g r t gen done word)
    (&& (<= 0 r g) (= (count h) (- g r))
        (for/fold ([ok #t]) ([slot (sequence-value-slots (hash-ref h "order"))] [i (in-naturals)])
          (define (bank a b) (if slot (hash-ref h b) (hash-ref h a)))
          (&& ok (=> (&& (= k (+ r i)) (< k g))
                     (&& (equal? t slot) (equal? gen (bank "gen_a" "gen_b"))
                         (equal? done (bank "done_a" "done_b")) (=> done (bveq word (bank "saved_a" "saved_b")))))))))
  (define base (&& (>= k 0) allowed (event "reset")))
  (define step (&& (>= k 0) allowed (! (event "reset"))
                   (inv old grants retirements tag generation matched first-data)))
  (require-feasible (&& base (= (count old) 2)))
  (discharge 'log-reset base (&& (! grant) (! pop) (inv next 0 0 #f #f #f (bv 0 2))))
  (discharge 'log-preserved step
             (inv next (+ grants (if grant 1 0)) (+ retirements (if pop 1 0))
                  (if kth-grant (event "grant_tag") tag)
                  (if kth-grant (event "grant_gen") generation)
                  (if kth-grant #f (|| matched capture))
                  (if capture (lane "data_a" "data_b") first-data)))
  (discharge 'kth-commit step
             (=> kth-log (&& tracked matched (equal? (event "retire_tag") tag)
                             (bveq (event "retire_data") first-data))))
  ;; Grants, completions and retirement are not serialized by the induction premises.
  (require-feasible (&& step kth-grant pop))
  (require-feasible (&& step capture (< retirements k) pop))
  (for* ([t '(#f #t)] [gen '(#f #t)])
    (require-feasible (&& step kth-log grant (equal? tag t) (equal? generation gen))))
  (values (&& step kth-log) tag first-data))
