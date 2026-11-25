#lang rosette
;; Proves reset-scoped transaction ordering and capacity across two verified FIFO cuts.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "../refinement.rkt" "composition-checks.rkt" "../spec-engine.rkt"
         (only-in "shift-queue-induction.rkt" verified-fifo-at)
         (only-in "shift-queue-binding.rhm" pair_proof_cut))

(struct pair-step (states a b defined ready valid head) #:transparent)
(define (advance plan states rst offer data consume)
  (define-values (pins edges defined)
    (proved-edge-at plan states (hash "reset" rst "offer" offer "data" data "consume" consume)))
  (pair-step (for/hash ([(site e) edges]) (values site (edge-next e)))
             (hash-ref edges "a") (hash-ref edges "b") defined
             (hash-ref pins "ready") (hash-ref pins "valid") (hash-ref pins "head")))

;; B.q ++ A.q is the transaction history, oldest first. Ignore all unused slots.
(define (queues s) (map (lambda (site) (hash-ref (hash-ref s site) "q")) '("b" "a")))
(define (legal-states s) (andmap spec-state-legal? (hash-values s)))
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

(define (check-pair depth)
  (define capacity (* 2 depth))
  (define cut (pair_proof_cut depth))
  (define plan (prepare-proved-wiring cut (list (verified-fifo-at depth))))
  (define states (symbolic-cut-state plan))
  (define-symbolic* rst offer consume boolean?)
  (define-symbolic* data (bitvector 2))
  (define p (advance plan states rst offer data consume))
  (define a (pair-step-a p))
  (define b (pair-step-b p))
  (define next (pair-step-states p))
  (define before (occupancy states))
  (define after (occupancy next))
  (define push (&& (! rst) offer (pair-step-ready p)))
  (define pop (&& (! rst) consume (pair-step-valid p)))
  (define middle (event a "dequeue"))
  (define valid (legal-states states))
  (define removed (if pop 1 0))
  (define kept (- before removed))

  (test-case "both real occurrences require the matching leaf proof and have independent abstract states"
    (define other-depth (if (= depth 1) 2 1))
    (check-exn #rx"no evidence for RTL specialization"
               (lambda () (prepare-proved-wiring cut (list (verified-fifo-at other-depth)))))
    (check-eq? (proved-child-model plan "a") (proved-child-model plan "b"))
    (check-equal? (sort (for*/list ([(site state) states] [field (hash-keys state)])
                          (string-append site "." field)) string<?) '("a.q" "b.q"))
    (require-feasible (&& valid rst))
    (require-feasible (&& valid push pop))
    (require-feasible (&& valid (! push) (! pop) (observation-care middle)))
    (when (= depth 2) (require-feasible (&& valid push pop (observation-care middle)))))

  (test-case "both child input domains hold without assuming traffic or either guarantee"
    (discharge 'admitted valid (&& (edge-legal a) (edge-legal b) (pair-step-defined p)
                                   (edge-defined a) (edge-defined b)))
    (check-exn #rx"not discharged" (lambda ()
      (discharge 'narrower-domain valid (bveq (hash-ref (edge-inputs a) "ingress.bits") (bv 0 2))))))

  (test-case "explicit spec bindings reject missing dependencies and ill-typed wires"
    (define fifo (proved-child-model plan "a"))
    (define state (hash-ref states "a"))
    (check-exn #rx"no value found" (lambda () (spec-offers-at fifo state (hash))))
    (check-exn #rx"spec binding" (lambda () (spec-edge-at fifo state (hash "reset" rst))))
    (check-exn #rx"spec binding" (lambda ()
      (spec-edge-at fifo state (hash-set (edge-inputs a) "ingress.bits" (bv 0 1))))))

  (test-case "reset, handoffs and external transactions preserve order and capacity on every edge"
    (discharge 'epoch valid (&& (matches? (event a "reset") rst #t) (matches? (event b "reset") rst #t)
                                (=> rst (= after 0))))
    (discharge 'middle valid (same-event middle (event b "enqueue")))
    (discharge 'external valid (&& (matches? (event a "enqueue") push data)
                                   (matches? (event b "dequeue") pop (pair-step-head p))))
    (discharge 'oldest valid (=> pop (&& (> before 0) (bveq (pair-step-head p) (word-at states 0)))))
    (discharge 'conservation valid (= after (if rst 0 (+ kept (if push 1 0)))))
    (for ([i (in-range capacity)])
      (discharge (list 'ordered-slot i) valid
                 (=> (< i after) (bveq (word-at next i) (if (< i kept) (word-at states (+ i removed)) data)))))
    (discharge 'capacity valid (&& (legal-states next) (<= after capacity)
                                   (=> (&& push (! pop)) (< before capacity)))))

  (test-case "capacity is attainable and all accepted words drain in order from reset"
    (define empty (pair-step-states (advance plan states #t #f (bv 0 2) #f)))
    (define words (for/list ([i (in-range capacity)]) (bv (add1 i) 2)))
    (define (fill s w)
      (define e (advance plan s #f #t w #f))
      (discharge 'accept-fill valid (pair-step-ready e))
      (pair-step-states e))
    (define (drain s w)
      (define e (advance plan s #f #f (bv 0 2) #t))
      (discharge 'drain-oldest valid (&& (pair-step-valid e) (bveq (pair-step-head e) w)))
      (pair-step-states e))
    (define (handoff s offer w consume)
      (define e (advance plan s #f offer w consume))
      (discharge 'internal-only valid
                 (&& (= (occupancy s) 1) (! (pair-step-ready e)) (! (pair-step-valid e))
                     (observation-care (event (pair-step-a e) "dequeue"))
                     (holds-words (pair-step-states e) (list (word-at s 0)))))
      (pair-step-states e))
    ;; At depth one the offered second word is refused on the handoff edge.
    (define first-in (fill empty (first words)))
    (define rest-start (if (= depth 1) (handoff first-in #t (second words) #f) first-in))
    (define full (foldl (lambda (w s) (fill s w)) rest-start (rest words)))
    (discharge 'all-live valid (holds-words full words))
    (define first-out (drain full (first words)))
    (define bubble (advance plan first-out #f #t (bv 0 2) #f))
    (discharge 'space-without-admission valid (! (pair-step-ready bubble)))
    (define rest-out (if (= depth 1) (handoff first-out #f (bv 0 2) #t) first-out))
    (define drained (foldl (lambda (w s) (drain s w)) rest-out (rest words)))
    (discharge 'drained valid (= 0 (occupancy drained)))))

(for ([depth '(1 2)])
  (with-check-info (['depth depth]) (check-pair depth)))
