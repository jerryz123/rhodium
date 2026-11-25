#lang rosette
;; Proves combinational fixed-priority arbitration and replays a reversed-priority defect.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit racket/match (only-in racket/base [equal? host-equal?])
         "../refinement.rkt" "../engine.rkt" "../spec-engine.rkt" "arbiter-binding.rhm")
(provide verified-arbiter)

(define (pins in) (pack-pins in_layout in))
(define (bit v) (bveq v (bv 1 1)))
(define spec (symbolic-spec-edge model))
(define in (edge-inputs spec))
(define rtl (combinational_sample correct (pins in)))
(define bad (combinational_sample faulty (pins in)))

;; Decode the RTL observation independently: care and transfers come from actual pins.
(define (views ports in)
  (define r0 (bit (hash-ref ports "ingress[0].ready")))
  (define r1 (bit (hash-ref ports "ingress[1].ready")))
  (define valid (bit (hash-ref ports "egress.valid")))
  (define data (hash-ref ports "egress.bits"))
  (values (hash "egress.valid" (observation #t valid) "ingress[0].ready" (observation #t r0) "ingress[1].ready" (observation #t r1)
                "egress.bits" (observation valid data) "chosen" (observation valid (hash-ref ports "chosen")))
          (hash "in0" (observation (&& (hash-ref in "ingress[0].valid") r0) (hash-ref in "ingress[0].bits"))
                "in1" (observation (&& (hash-ref in "ingress[1].valid") r1) (hash-ref in "ingress[1].bits"))
                "out" (observation (&& valid (hash-ref in "egress.ready")) data))))

(define (observe rtl inputs) (views (unpack-pins out_layout (transition_step_outputs rtl)) inputs))
(define-values (out events) (observe rtl in))
(define-values (bad-out bad-events) (views (unpack-pins out_layout (transition_step_outputs bad)) in))
(define (transfer k) (observation-care (hash-ref events k)))
(define (word k) (observation-value (hash-ref events k)))
(define (prove name goal)
  (unless (host-equal? (vc) vc-true) (error 'arbiter-proof "~a: unexpected ambient constraint" name))
  (define result (solve (assert (! goal))))
  (unless (unsat? result) (error 'arbiter-proof "~a: counterexample or unknown: ~a" name result)))

;; Release this specific spec only after every obligation holds for arbitrary input pins.
(define verified-arbiter
  (begin
    (prove 'one-accepted-source
           (&& (! (&& (transfer "in0") (transfer "in1")))
               (equal? (transfer "out") (|| (transfer "in0") (transfer "in1")))
               (=> (transfer "in0") (bveq (word "in0") (word "out")))
               (=> (transfer "in1") (bveq (word "in1") (word "out")))))
    (prove-leaf arbiter_definition arbiter_spec interface_prefixes views)))

(module+ test
  (check-exn #rx"combinational/outputs not proved"
             (lambda () (prove-leaf bad_definition arbiter_spec interface_prefixes views)))
  ;; An observer that reports the spec's answer cannot hide the faulty RTL's actual pin values.
  (check-exn #rx"combinational/physical-pins not proved"
             (lambda () (prove-leaf bad_definition arbiter_spec interface_prefixes
                                    (lambda (_ inputs)
                                      (define ref (spec-edge-at model (hash) inputs))
                                      (values (edge-outputs ref) (edge-events ref))))))
  (define (sample v0 d0 v1 d1 accept)
    (hash "ingress[0].valid" v0 "ingress[1].valid" v1 "egress.ready" accept "ingress[0].bits" d0 "ingress[1].bits" d1))
  (define (plain h) (for/hash ([(k v) h]) (values k v)))
  (define (concrete-spec s) (map plain (concrete_row s)))
  (define (concrete-rtl dut s)
    (define raw (interpret_snapshot dut (for/hash ([(k v) (pins s)]) (values k (bitvector->natural v)))))
    (define p (unpack-pins out_layout raw))
    (define (flag k) (= (hash-ref p k) 1))
    (define v (flag "egress.valid"))
    (define o (hash "ingress[0].ready" (flag "ingress[0].ready") "ingress[1].ready" (flag "ingress[1].ready") "egress.valid" v))
    (list (if v (hash-set* o "egress.bits" (hash-ref p "egress.bits") "chosen" (hash-ref p "chosen")) o)
          (for/hash ([k '("in0" "in1" "out")]
                     [fire (list (and (hash-ref s "ingress[0].valid") (flag "ingress[0].ready"))
                                 (and (hash-ref s "ingress[1].valid") (flag "ingress[1].ready")) (and v (hash-ref s "egress.ready")))]
                     [w (list (hash-ref s "ingress[0].bits") (hash-ref s "ingress[1].bits") (hash-ref p "egress.bits"))] #:when fire)
            (values k w))))

  (test-case "all 128 concrete samples replay against rspecsim, including absent idle payload"
    (for* ([v0 '(#f #t)] [v1 '(#f #t)] [accept '(#f #t)] [d0 (in-range 4)] [d1 (in-range 4)])
      (define s (sample v0 d0 v1 d1 accept))
      (check-equal? (concrete-rtl correct s) (concrete-spec s))))

  (test-case "a stalled low-priority offer can be superseded before the first transfer"
    (for ([s (list (sample #f 2 #t 1 #f) (sample #t 2 #t 1 #f) (sample #t 2 #t 1 #t))]
          [chosen '(1 0 0)] [data '(1 2 2)] [events (list (hash) (hash) (hash "in0" 2 "out" 2))])
      (match-define (list o ev) (concrete-rtl correct s))
      (check-equal? (list o ev) (concrete-spec s))
      (check-equal? (list (hash-ref o "chosen") (hash-ref o "egress.bits") ev) (list chosen data events))))

  (test-case "SAT wrong-winner witness replays on defective RTL and rspecsim"
    (prove 'faulty-mux-domain (transition_step_validity bad))
    (define result
      (solve (assert (&& (hash-ref in "ingress[0].valid") (hash-ref in "ingress[1].valid") (hash-ref in "egress.ready")
                         (! (bveq (hash-ref in "ingress[0].bits") (hash-ref in "ingress[1].bits")))
                         (! (view-agrees? bad-out (edge-outputs spec))) (! (view-agrees? bad-events (edge-events spec)))))))
    (check-true (sat? result))
    (define full (complete-solution result (symbolics (hash-values in))))
    (define s (for/hash ([(k x) in])
                (define v (evaluate x full))
                (values k (if (boolean? v) v (bitvector->natural v)))))
    (match-define (list good eg) (concrete-spec s))
    (match-define (list wrong ew) (concrete-rtl faulty s))
    (check-equal? (concrete-rtl correct s) (list good eg))
    (check-equal? (list (hash-ref good "chosen") (hash-ref wrong "chosen")) '(0 1))
    (check-equal? eg (hash "in0" (hash-ref s "ingress[0].bits") "out" (hash-ref s "ingress[0].bits")))
    (check-equal? ew (hash "in1" (hash-ref s "ingress[1].bits") "out" (hash-ref s "ingress[1].bits")))
    (check-not-equal? (hash-ref eg "out") (hash-ref ew "out")))

  (test-case "clockless sample requires exact names and widths"
    (check-exn #rx"exactly the declared names" (lambda () (combinational_sample correct (hash))))
    (check-exn #rx"exact-width bitvector" (lambda ()
      (combinational_sample correct (for/hash ([(k v) (pins in)]) (values k (bv 0 8))))))))
