#lang rosette
;; Proves reset-scoped ordering of accepted lane payloads using only the two verified child specs.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "composition-checks.rkt" "../spec-engine.rkt"
         (only-in "arbiter-refinement.rkt" verified-arbiter)
         (only-in "shift-queue-induction.rkt" verified-fifo)
         (only-in "composition-binding.rhm" mixed_wires))

(define topology
  (hash "arb.v0" "v0" "arb.d0" "d0" "arb.v1" "v1" "arb.d1" "d1"
        "arb.accept" "fifo.ready" "fifo.offer" "arb.valid" "fifo.data" "arb.data"
        "fifo.clock" "clock" "fifo.reset" "reset" "fifo.consume" "consume"
        "r0" "arb.r0" "r1" "arb.r1" "valid" "fifo.valid" "head" "fifo.head"))
(define cut (check-cut mixed_wires topology))
(define (inputs reset v0 d0 v1 d1 consume)
  (hash "reset" reset "v0" v0 "d0" d0 "v1" v1 "d1" d1 "consume" consume))
(struct sample (arb fifo offers-ok take0 take1 pop valid head) #:transparent)

;; No fixed-point assumption: FIFO offers depend only on q/reset, then the
;; arbiter is fully bound, then the FIFO can commit its simultaneous step.
(define (advance state x [wires cut])
  (define (wire nets sink) (hash-ref nets (hash-ref wires sink)))
  (define rst (wire x "fifo.reset"))
  (define-values (offer ok) (spec-offers-at verified-fifo state (hash "reset" rst)))
  (define f-nets (hash-set* x "fifo.ready" (pin (hash-ref offer "in.ready") boolean?)
                            "fifo.valid" (pin (hash-ref offer "out.valid") boolean?)
                            "fifo.head" (pin (hash-ref offer "out.data") (bitvector 2))))
  (define arb (spec-edge-at verified-arbiter (hash)
                            (for/hash ([k '("v0" "d0" "v1" "d1" "accept")])
                              (values k (wire f-nets (string-append "arb." k))))))
  (define nets
    (for/fold ([ns f-nets]) ([k '("r0" "r1" "valid" "data")])
      (hash-set ns (string-append "arb." k)
                (pin (hash-ref (edge-outputs arb) k) (if (equal? k "data") (bitvector 2) boolean?)))))
  (define fifo (spec-edge-at verified-fifo state
                             (hash "reset" rst "in.valid" (wire nets "fifo.offer")
                                   "in.data" (wire nets "fifo.data") "out.ready" (wire nets "fifo.consume"))))
  (sample arb fifo ok
          (&& (! rst) (hash-ref x "v0") (wire nets "r0"))
          (&& (! rst) (hash-ref x "v1") (wire nets "r1"))
          (&& (! rst) (hash-ref x "consume") (wire nets "valid"))
          (wire nets "valid") (wire nets "head")))

;; Independent transaction oracle: it sees only accepted external words, not
;; chosen, arbiter-to-FIFO events, or the child implementation's queue update.
(struct history (n first second) #:transparent)
(define (history-word h i) (if (= i 0) (history-first h) (history-second h)))
(define (related state h)
  (define q (hash-ref state "q"))
  (define n (history-n h))
  (&& (<= 0 n 2) (= n (bitvector->natural (sequence-value-count q)))
      (for/fold ([ok #t]) ([w (sequence-value-slots q)] [i (in-naturals)])
        (&& ok (=> (< i n) (bveq w (history-word h i)))))))
(define empty (history 0 (bv 0 2) (bv 0 2)))
(define (update h x e)
  (define drop (if (sample-pop e) 1 0))
  (define kept (- (history-n h) drop))
  (define push (|| (sample-take0 e) (sample-take1 e)))
  (define word (if (sample-take0 e) (hash-ref x "d0") (hash-ref x "d1")))
  (define (at i) (if (< i kept) (history-word h (+ i drop)) word))
  (history (if (hash-ref x "reset") 0 (+ kept (if push 1 0))) (at 0) (at 1)))

(define state (symbolic-spec-state verified-fifo))
(define-symbolic* rst v0 v1 consume boolean?)
(define-symbolic* d0 d1 first-word second-word (bitvector 2))
(define-symbolic* n integer?)
(define h (history n first-word second-word))
(define x (inputs rst v0 d0 v1 d1 consume))
(define e (advance state x))
(define a (sample-arb e))
(define f (sample-fifo e))
(define legal (bvule (sequence-value-count (hash-ref state "q")) (bv 2 2)))
(define step (related state h))
(define base (&& legal rst))
(define room (< n 2))
(define push (|| (sample-take0 e) (sample-take1 e)))
(define accepted-data (if (sample-take0 e) d0 d1))

(test-case "actual mixed cut; feasible reset, contests, stalls, and simultaneous transfers"
  (check-equal? (hash-keys state) '("q"))
  (check-equal? (hash-count (edge-state a)) 0)
  (check-exn #rx"outside the verified" (lambda () (check-cut (hash-set cut "fifo.data" "d1") topology)))
  (require-feasible (&& base (bveq (sequence-value-count (hash-ref state "q")) (bv 2 2))))
  (require-feasible (&& step v0 v1 (sample-take0 e) (sample-pop e)))
  (require-feasible (&& step (sample-take1 e)))
  (require-feasible (&& step (! rst) (= n 2) v0 v1 (! push)))
  ;; No reset on the arbiter: don't assume its electrical handshake disappears.
  (require-feasible (&& base (observation-care (event a "out")))))

(test-case "reset empties the history; legal wired child steps for all external pins"
  (discharge 'domains legal (&& (sample-offers-ok e) (edge-legal a) (edge-legal f) (edge-defined a) (edge-defined f)))
  (discharge 'reset-start base (&& (related (edge-next f) empty) (! push) (! (sample-pop e))))
  (discharge 'reset-event legal (matches? (event f "reset") rst #t)))

(test-case "external acceptances, internal handoff and inductive ordered output"
  (discharge 'lane0 step (equal? (sample-take0 e) (&& (! rst) room v0)))
  (discharge 'lane1 step (equal? (sample-take1 e) (&& (! rst) room (! v0) v1)))
  (discharge 'one-source legal (! (&& (sample-take0 e) (sample-take1 e))))
  (discharge 'arbiter-handoffs legal
             (=> (! rst) (&& (matches? (event a "in0") (sample-take0 e) d0)
                             (matches? (event a "in1") (sample-take1 e) d1)
                             (same-event (event a "out") (event f "enqueue")))))
  (discharge 'fifo-handoffs legal (&& (matches? (event f "enqueue") push accepted-data)
                                      (matches? (event f "dequeue") (sample-pop e) (sample-head e))))
  (discharge 'visible-head step (=> (! rst) (&& (equal? (sample-valid e) (> n 0))
                                                (=> (> n 0) (bveq (sample-head e) first-word)))))
  (discharge 'accepted-history-preserved step (related (edge-next f) (update h x e))))

(test-case "a full FIFO can retire without accepting the stalled winner; a later tie selects anew"
  (define (check-edge s rst v0 d0 v1 d1 ready take0 take1 out)
    (define row (advance s (inputs rst v0 (bv d0 2) v1 (bv d1 2) ready)))
    (discharge 'trace legal
               (&& (equal? (sample-take0 row) take0) (equal? (sample-take1 row) take1)
                   (matches? (event (sample-fifo row) "dequeue") (number? out) (bv (or out 0) 2))))
    (edge-next (sample-fifo row)))
  (define s0 (check-edge state #t #t 0 #t 1 #t #f #f #f))
  (define s1 (check-edge s0 #f #f 0 #t 1 #f #f #t #f))
  (define s2 (check-edge s1 #f #t 2 #t 3 #f #t #f #f))
  (define s3 (check-edge s2 #f #f 0 #t 3 #t #f #f 1))
  (define s4 (check-edge s3 #f #t 0 #t 3 #t #t #f 2))
  (define s5 (check-edge s4 #f #f 0 #f 3 #t #f #f 0))
  (discharge 'drained legal (related s5 empty)))

(test-case "ordering oracle detects a losing-lane payload substituted at the handoff"
  ;; Bypass only the structural check for this deliberately faulty spec wiring.
  (define bad-cut (hash-set cut "fifo.data" "d1"))
  (define (observe wires)
    (define cleared (advance state (inputs #t #f d0 #f d1 #f) wires))
    (define tied (advance (edge-next (sample-fifo cleared)) (inputs #f #t d0 #t d1 #f) wires))
    (define drain (advance (edge-next (sample-fifo tied)) (inputs #f #f d0 #f d1 #t) wires))
    (values tied drain))
  (define-values (good g) (observe cut))
  (define-values (bad b) (observe bad-cut))
  (define distinct (&& legal (! (bveq d0 d1))))
  (require-feasible distinct)
  (discharge 'wrong-lane-replay distinct
             (&& (sample-take0 good) (sample-take0 bad) (sample-pop g) (sample-pop b)
                 (bveq (sample-head g) d0) (bveq (sample-head b) d1) (! (bveq (sample-head b) d0)))))
