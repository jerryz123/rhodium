#lang rosette
;; SPDX-License-Identifier: Apache-2.0

(require rackunit racket/match "../spec-engine.rkt")
(provide check_fifo check_guard check_concat)

;; For every legal split of asymmetric banks, unconstrained padding must never precede a live word.
(define (check_concat snapshot)
  (define e (symbolic-spec-edge snapshot))
  (define older (hash-ref (edge-inputs e) "older"))
  (define younger (hash-ref (edge-inputs e) "younger"))
  (define actual (observation-value (hash-ref (edge-outputs e) "joined")))
  (for* ([n (in-range 3)] [m (in-range 2)])
    (define row (&& (bveq (sequence-value-count older) (bv n 2))
                    (bveq (sequence-value-count younger) (bv m 1))))
    (define words (append (take (sequence-value-slots older) n) (take (sequence-value-slots younger) m)))
    (define ok (&& (edge-defined e) (= (bitvector->natural (sequence-value-count actual)) (+ n m))
                   (equal? (take (sequence-value-slots actual) (+ n m)) words)))
    (check-true (unsat? (solve (assert (&& row (! ok))))))))

;; Match only the count and live prefix; unused symbolic cells may contain arbitrary bits.
(define (matches-queue? q words)
  (define n (sequence-value-count q))
  (&& (bveq n (bv (length words) (type-of n)))
      (for/fold ([ok #t]) ([value (sequence-value-slots q)] [expected words])
        (&& ok (bveq value (bv expected 2))))))

(define (matches-view? fields expected)
  (&& (for/and ([name (hash-keys expected)]) (hash-has-key? fields name))
      (for/fold ([ok #t]) ([(name field) fields])
        (&& ok
            (if (hash-has-key? expected name)
                (let ([v (hash-ref expected name)])
                  (&& (observation-care field)
                      (equal? (observation-value field) (if (boolean? v) v (bv v 2)))))
                (! (observation-care field)))))))

(define (check_fifo snapshot concrete-row)
  (define e (symbolic-spec-edge snapshot))
  (define q (hash-ref (edge-state e) "q"))
  (define next (hash-ref (edge-next e) "q"))
  (define in (edge-inputs e))
  (define words (range 4))
  (define queues (append '(()) (map list words) (for*/list ([a words] [b words]) (list a b))))
  (define cases 0)
  (define-values (covered mismatch)
    (for*/fold ([covered #f] [mismatch #f])
               ([old queues] [reset '(#f #t)] [offer '(#f #t)] [consume '(#f #t)] [word words])
      (set! cases (add1 cases))
      (define row (&& (matches-queue? q old) (equal? (hash-ref in "reset") reset)
                      (equal? (hash-ref in "ingress.valid") offer) (equal? (hash-ref in "egress.ready") consume)
                      (bveq (hash-ref in "ingress.bits") (bv word 2))))
      ;; Exhibit a witness per row: a generator that aliases two inputs must not
      ;; get a vacuous agreement result for combinations it can no longer represent.
      (define binding (hash (sequence-value-count q) (bv (length old) 2)
                            (hash-ref in "reset") reset (hash-ref in "ingress.valid") offer
                            (hash-ref in "egress.ready") consume (hash-ref in "ingress.bits") (bv word 2)))
      (define witness (sat (for/fold ([h binding]) ([slot (sequence-value-slots q)] [v old])
                             (hash-set h slot (bv v 2)))))
      (check-true (evaluate (&& row (edge-legal e)) witness))
      (match-define (list outputs events after) (concrete-row old reset offer consume word))
      (define agrees (&& (matches-view? (edge-outputs e) outputs) (matches-view? (edge-events e) events)
                         (matches-queue? next after)))
      (values (|| covered row) (|| mismatch (&& row (! agrees))))))
  (check-equal? cases 672)
  (check-true (sat? (solve (assert (edge-legal e)))))
  ;; No encodings outside the 672 rows are legal, nor are any row encodings excluded.
  (check-true (unsat? (solve (assert (! (equal? covered (edge-legal e)))))))
  (check-true (unsat? (solve (assert (&& (edge-legal e) (! (edge-defined e)))))))
  (check-true (unsat? (solve (assert mismatch)))))

(define (check_guard snapshot)
  (define e (symbolic-spec-edge snapshot))
  (define gate (hash-ref (edge-inputs e) "gate"))
  (check-true (sat? (solve (assert (&& (edge-legal e) (! (edge-defined e)))))))
  (check-true (unsat? (solve (assert (&& (edge-legal e) (! (equal? (edge-defined e) (! gate)))))))))
