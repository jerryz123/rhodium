#lang rosette
;; Proves reset-scoped transaction ordering and capacity across two contract-level FIFO cuts.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "composition-checks.rkt"
         "../spec-engine.rkt" (only-in "shift-queue-induction.rkt" verified-fifo)
         (only-in "shift-queue-binding.rhm" pair_wires unit_fifo_snapshot))

;; The contract is specialized to this direct netlist, not inferred from module names.
;; Require every sink, including clock/reset; no parent logic or count/mask consumers.
(define expected-wires
  (hash "a.clock" "clock" "b.clock" "clock" "a.reset" "reset" "b.reset" "reset"
        "a.offer" "offer" "a.data" "data" "a.consume" "b.ready"
        "b.offer" "a.valid" "b.data" "a.head" "b.consume" "consume"
        "ready" "a.ready" "valid" "b.valid" "head" "b.head"))
(define cut (check-cut pair_wires expected-wires))

(define (fresh-states model)
  (for/hash ([name '("a" "b")]) (values name (symbolic-spec-state model))))


(struct pair-step (states a b offers-defined ready valid head) #:transparent)
(define (advance model states rst offer data consume)
  (define external (hash "reset" rst "offer" offer "data" data "consume" consume))
  (define (wire signals sink) (hash-ref signals (hash-ref cut sink)))
  ;; First resolve each child's pre-edge offers using only its own state and reset.
  ;; No same-edge equations or child guarantees are added as solver assumptions.
  (define-values (nets offers-ok)
    (for/fold ([nets external] [ok #t]) ([(name state) states])
      (define (k suffix) (string-append name suffix))
      (define-values (out defined) (spec-offers-at model state (hash "reset" (wire external (k ".reset")))))
      (values (hash-set* nets (k ".ready") (pin (hash-ref out "in.ready") boolean?)
                         (k ".valid") (pin (hash-ref out "out.valid") boolean?)
                         (k ".head") (pin (hash-ref out "out.data") (bitvector 2)))
              (&& ok defined))))
  (define (step name)
    (define (input suffix) (wire nets (string-append name suffix)))
    (spec-edge-at model (hash-ref states name)
                  (hash "reset" (input ".reset") "in.valid" (input ".offer")
                        "in.data" (input ".data") "out.ready" (input ".consume"))))
  (define a (step "a"))
  (define b (step "b"))
  (pair-step (hash "a" (edge-next a) "b" (edge-next b)) a b offers-ok
             (wire nets "ready") (wire nets "valid") (wire nets "head")))

(define (queues s)
  (map (lambda (name) (hash-ref (hash-ref s name) "q")) '("b" "a")))
(define (legal-states s)
  (for/fold ([ok #t]) ([q (queues s)])
    (define n (sequence-value-count q))
    (&& ok (bvule n (bv (length (sequence-value-slots q)) (type-of n))))))

;; Length and indexed live words define the abstract sequence B.q ++ A.q.
;; Use mathematical integers for lengths; no modular overflow or symbolic list access.
(define (size q) (bitvector->natural (sequence-value-count q)))
(define (occupancy s) (apply + (map size (queues s))))
(define (slot q i)
  (for/fold ([v (bv 0 2)]) ([word (sequence-value-slots q)] [k (in-naturals)])
    (if (= i k) word v)))
(define (word-at s i)
  (define qs (queues s))
  (define n (size (first qs)))
  (if (< i n) (slot (first qs) i) (slot (second qs) (- i n))))
(define (holds-words s words)
  (for/fold ([ok (= (occupancy s) (length words))]) ([w words] [i (in-naturals)])
    (&& ok (bveq (word-at s i) w))))

(define states (fresh-states verified-fifo))
(define-symbolic* rst offer consume boolean?)
(define-symbolic* data (bitvector 2))
(define p (advance verified-fifo states rst offer data consume))
(define a (pair-step-a p))
(define b (pair-step-b p))
(define next (pair-step-states p))
(define before (occupancy states))
(define after (occupancy next))
(define push (&& (! rst) offer (pair-step-ready p)))
(define pop (&& (! rst) consume (pair-step-valid p)))
(define valid (legal-states states))
(define middle (event a "dequeue"))
(define removed (if pop 1 0))
(define kept (- before removed))

;; Discharge against unrestricted external pins *before* using either leaf, including
;; the legal-state part of its contract. There are no extra leaf traffic assumptions.
(define (admit-children! a-environment b-environment)
  (require-feasible valid)
  (discharge 'a-environment valid (&& (edge-legal a) a-environment))
  (discharge 'b-environment valid (&& (edge-legal b) b-environment))
  (discharge 'defined valid (&& (pair-step-offers-defined p) (edge-defined a) (edge-defined b))))
(admit-children! #t #t)

(test-case "verified two-child cut: independent states, feasible environment and legal wired inputs"
  (check-equal? (vc) vc-true)
  (check-equal? (sort (for*/list ([(site state) states] [field (hash-keys state)])
                        (string-append site "." field)) string<?) '("a.q" "b.q"))
  (require-feasible (&& valid rst))
  (require-feasible (&& valid (= (size (first (queues states))) 0) (= (size (second (queues states))) 2)))
  (require-feasible (&& valid push pop (observation-care middle)))
  ;; A proposed narrower child assumption is satisfiable but not implied by this wiring.
  (check-exn #rx"not discharged" (lambda ()
    (admit-children! (bveq (hash-ref (edge-inputs a) "in.data") (bv 0 2)) #t)))
  (check-exn #rx"outside the verified" (lambda () (check-cut (hash-set expected-wires "b.data" "a.mask") expected-wires))))

(test-case "explicit spec bindings reject missing dependencies and ill-typed wires"
  (define state (hash-ref states "a"))
  (check-exn #rx"no value found" (lambda () (spec-offers-at verified-fifo state (hash))))
  (check-exn #rx"spec binding" (lambda () (spec-edge-at verified-fifo state (hash "reset" rst))))
  (check-exn #rx"spec binding" (lambda ()
    (spec-edge-at verified-fifo state (hash-set (edge-inputs a) "in.data" (bv 0 1))))))

(test-case "joint handshakes, reset epochs, FIFO order, and four-word capacity"
  (discharge 'epoch valid (&& (matches? (event a "reset") rst #t) (matches? (event b "reset") rst #t)
                              (=> rst (= after 0))))
  (discharge 'middle valid (same-event middle (event b "enqueue")))
  (discharge 'external valid (&& (matches? (event a "enqueue") push data)
                                 (matches? (event b "dequeue") pop (pair-step-head p))))
  (discharge 'underflow-and-oldest valid (=> pop (&& (> before 0) (bveq (pair-step-head p) (word-at states 0)))))
  (discharge 'conservation valid (= after (if rst 0 (+ kept (if push 1 0)))))
  (for ([i (in-range 4)])
    (discharge (list 'ordered-slot i) valid
               (=> (< i after) (bveq (word-at next i) (if (< i kept) (word-at states (+ i removed)) data)))))
  (discharge 'capacity valid (&& (legal-states next) (<= after 4) (=> (&& push (! pop)) (< before 4)))))

(test-case "four distinct words fit and drain from a sampled reset"
  (define empty (pair-step-states (advance verified-fifo states #t #f (bv 0 2) #f)))
  (define words (map (lambda (i) (bv i 2)) '(0 1 2 3)))
  (define full (for/fold ([s empty]) ([w words])
                 (define e (advance verified-fifo s #f #t w #f))
                 (discharge 'accept-fill valid (pair-step-ready e))
                 (pair-step-states e)))
  (discharge 'all-four-live valid (holds-words full words))
  ;; Popping from full leaves A full and B with one free slot. Even this
  ;; proved instance is not an ideal FIFO that admits whenever space exists.
  (define one-pop (advance verified-fifo full #f #f (bv 0 2) #t))
  (define bubble (advance verified-fifo (pair-step-states one-pop) #f #t (bv 0 2) #f))
  (discharge 'space-without-admission valid
             (&& (= 3 (occupancy (pair-step-states one-pop))) (! (pair-step-ready bubble))))
  (define drained (for/fold ([s full]) ([w words])
                    (define e (advance verified-fifo s #f #f (bv 0 2) #t))
                    (discharge 'drain-oldest valid (&& (pair-step-valid e) (bveq (pair-step-head e) w)))
                    (pair-step-states e)))
  (discharge 'drained valid (= 0 (occupancy drained))))

(test-case "two depth-one specs are not an always-ready-when-space ideal FIFO"
  ;; Spec-only illustration: the depth-two leaf proof is not reused for depth one.
  (define s (fresh-states unit_fifo_snapshot))
  (define empty (pair-step-states (advance unit_fifo_snapshot s #t #f (bv 0 2) #f)))
  (define first (advance unit_fifo_snapshot empty #f #t (bv 1 2) #f))
  (define second (advance unit_fifo_snapshot (pair-step-states first) #f #t (bv 2 2) #f))
  (require-feasible (legal-states s))
  (discharge 'readiness-counterexample (legal-states s)
             (&& (pair-step-ready first) (= (occupancy (pair-step-states first)) 1)
                 (! (pair-step-ready second)) (observation-care (event (pair-step-a second) "dequeue"))
                 (holds-words (pair-step-states second) (list (bv 1 2))))))
