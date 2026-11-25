#lang rosette
;; Proves reset-scoped transaction ordering and capacity across two verified FIFO cuts.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "../refinement.rkt" "composition-checks.rkt" "../spec-engine.rkt"
         (only-in "shift-queue-induction.rkt" verified-fifo-at)
         (only-in "shift-queue-binding.rhm" pair_proof_cut)
         (only-in "composition-binding.rhm" pair_at))
(provide verified-queue-pair-transactions-at)

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
  (define children (list (verified-fifo-at depth)))
  (define plan (prepare-proved-wiring cut children))
  (define states (symbolic-cut-state plan))
  (define-symbolic* rst offer consume boolean?)
  (define-symbolic* data (bitvector 2))
  (define p (advance plan states rst offer data consume))
  (define a (pair-step-a p))
  (define b (pair-step-b p))
  (define push (&& (! rst) offer (pair-step-ready p)))
  (define pop (&& (! rst) consume (pair-step-valid p)))
  (define middle (event a "dequeue"))
  (define valid (legal-states states))
  (define (project s [bound capacity])
    (fifo-history (occupancy s) (for/list ([i (in-range bound)]) (word-at s i))))
  (define ingress '("offer" "ready" "data"))
  (define egress '("valid" "consume" "head"))
  (define (certify projection [bound capacity])
    (prove-fifo-transactions plan bound ingress egress projection))
  ;; Issue evidence outside rackunit: a failing obligation must abort, not just record a test failure.
  (define transactions (certify project))

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

  (test-case "the internal handoff agrees and only external transfers appear in the public batch"
    (discharge 'epoch valid (&& (matches? (event a "reset") rst #t) (matches? (event b "reset") rst #t)))
    (discharge 'middle valid (same-event middle (event b "enqueue")))
    (discharge 'external valid (&& (matches? (event a "enqueue") push data)
                                   (matches? (event b "dequeue") pop (pair-step-head p)))))

  (test-case "evidence is specialization-bound; a false capacity or payload projection cannot certify"
    (check-true (fifo-transactions-for? transactions (pair_at depth) ingress egress))
    (check-false (fifo-transactions-for? transactions (pair_at (if (= depth 1) 2 1)) ingress egress))
    (check-false (fifo-transactions-for? transactions (pair_at depth) egress ingress))
    (define too-small (sub1 capacity))
    (check-exn #rx"transaction-step/event-relation not proved"
               (lambda () (certify (lambda (s) (project s too-small)) too-small)))
    (check-exn #rx"transaction-step/event-relation not proved"
               (lambda () (certify (lambda (s)
                 (fifo-history (occupancy s) (make-list capacity (bv 0 2))))))))

  (test-case "a cycle-sensitive parent cannot substitute the event-only proof for a pin model"
    (check-exn #rx"transaction-only evidence cannot supply cycle pins"
               (lambda () (prepare-proved-wiring cut (list transactions)))))

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
    (discharge 'drained valid (= 0 (occupancy drained))))
  transactions)

(define proved-pairs
  (for/hash ([depth '(1 2)])
    (values depth (with-check-info (['depth depth]) (check-pair depth)))))
(define (verified-queue-pair-transactions-at depth) (hash-ref proved-pairs depth))
