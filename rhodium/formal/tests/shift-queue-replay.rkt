#lang rosette
;; SPDX-License-Identifier: Apache-2.0

(require racket/match rackunit (only-in "../refinement.rkt" pack-pins unpack-pins) "../engine.rkt" "shift-queue-binding.rhm")
(provide fault-state occ payload plan faulty pins sample reset start replay (struct-out mismatch))

(define occ (string-append fifo_root ".occupied"))
(define payload (string-append fifo_root ".payload"))
(define plan (prepare_transition_snapshot snapshot "clock"))
(define faulty (prepare_transition_snapshot faulty_snapshot "clock"))

;; Slot zero is the least-significant packed element and the oldest item.
;; Mask 10 is a hole, not a queue of length one. Inactive payloads are irrelevant.
(define (contents state)
  (define n (case (bitvector->natural (hash-ref state occ))
              [(0) 0] [(1) 1] [(3) 2] [else (error 'contents "noncompact occupied slots")]))
  (define bits (bitvector->natural (hash-ref state payload)))
  (for/list ([i (in-range n)]) (bitwise-bit-field bits (* 2 i) (* 2 (add1 i)))))

(define (sample reset offer data consume)
  (hash "reset" reset "ingress.valid" offer "ingress.bits" data "egress.ready" consume))

(define (pins s) (pack-pins in_layout s))
(define (rename-state state from to)
  (for/hash ([suffix '(".occupied" ".payload")])
    (values (string-append to suffix) (hash-ref state (string-append from suffix)))))
(define (fault-state state) (rename-state state fifo_root bad_root))

;; Derive presence and events solely from RTL ports, never from the spec's care bits.
(define (rtl-view ports s)
  (define (port k) (bitvector->natural (hash-ref ports k)))
  (define ready (= 1 (port "ingress.ready")))
  (define valid (= 1 (port "egress.valid")))
  (define cycle (hash "ingress.ready" ready "egress.valid" valid))
  (define en (if (and ready (hash-ref s "ingress.valid")) (hash "accepted" (hash-ref s "ingress.bits")) (hash)))
  (if (hash-ref s "reset")
      (values (hash) (hash "reset" #t))
      (values (if valid (hash-set cycle "egress.bits" (port "egress.bits")) cycle)
              (if (and valid (hash-ref s "egress.ready")) (hash-set en "retired" (port "egress.bits")) en))))

;; Zero-based edge index; retain the startup bits and entire input prefix for replay.
(struct mismatch (start prefix edge field expected actual) #:transparent)
(define (replay p start trace)
  (unless (and (pair? trace) (hash-ref (car trace) "reset")) (error 'replay "expected initial reset edge"))
  (let/ec stop
    (for/fold ([rtl start] [q '()] #:result 'matched)
              ([s trace] [index (in-naturals)])
      (define (compare field expected actual)
        (unless (equal? expected actual)
          (stop (mismatch start (take trace (add1 index)) index field expected actual))))
      (define edge (step_transition p (if (eq? p faulty) (fault-state rtl) rtl) (pins s)))
      (for ([o (transition_step_obligations edge)])
        (compare (hash-ref o "diagnostic") #t (hash-ref o "condition")))
      (define outputs (transition_step_outputs edge))
      (define-values (cycle events) (rtl-view (unpack-pins out_layout outputs) s))
      (match-define (list want-cycle want-events next-q) (spec_row q s))
      ;; Compare field names and values, not the host hash table's equality mode.
      (compare 'outputs (for/hash ([(k v) want-cycle]) (values k v)) cycle)
      (compare 'events (for/hash ([(k v) want-events]) (values k v)) events)
      ;; Additional binding diagnostics, not extra outputs in the abstract cycle contract.
      (unless (hash-ref s "reset")
        (compare 'count (length q) (bitvector->natural (hash-ref outputs "count")))
        (compare 'mask (sub1 (arithmetic-shift 1 (length q))) (bitvector->natural (hash-ref outputs "mask"))))
      (define after (if (eq? p faulty) (rename-state (transition_step_state edge) bad_root fifo_root) (transition_step_state edge)))
      (compare 'occupied-prefix (sub1 (arithmetic-shift 1 (length next-q))) (bitvector->natural (hash-ref after occ)))
      (compare 'contents next-q (contents after))
      (values after next-q))))

(define reset (sample #t #t 3 #t))
(define start (hash occ 2 payload 15))
(module+ test
  (check-equal? (sort (map (lambda (r) (hash-ref r "name")) (transition_registers plan)) string<?)
                (list occ payload))
  ;; Start even from malformed masks; do not assume compactness or cleared data before reset.
  (for* ([mask (in-range 4)] [bits (in-range 16)])
    (check-equal? (replay plan (hash occ mask payload bits) (list reset)) 'matched))

  ;; Reach each abstract queue by genuine enqueue edges; never inject its desired state.
  (define words (range 4))
  (define queues (append '(()) (map list words) (for*/list ([a words] [b words]) (list a b))))
  (define cases 0)
  (for* ([q queues] [rst '(#f #t)] [offer '(#f #t)] [consume '(#f #t)] [word words])
    (define prefix (cons reset (map (lambda (x) (sample #f #t x #f)) q)))
    (check-equal? (replay plan start (append prefix (list (sample rst offer word consume)))) 'matched)
    (set! cases (add1 cases)))
  (check-equal? cases 672)

  ;; A deliberately unreachable hole violates the real assertion when active, not on reset.
  (for ([rst '(#f #t)])
    (define edge (step_transition plan start (pins (sample rst #f 0 #f))))
    (match-define (list o) (transition_step_obligations edge))
    (check-equal? (hash-ref o "label") "shift_queue_valids_are_compact")
    (check-equal? (hash-ref (hash-ref o "diagnostic") "module_path") fifo_root)
    (check-equal? (transition_step_validity edge) rst))

  ;; Fault in executable RTL, not in the observer. A stalled valid offer is already observable.
  (define bad-trace (list reset (sample #f #t 2 #f) (sample #f #f 0 #f)))
  (define failure (replay faulty start bad-trace))
  (check-true (mismatch? failure))
  (check-equal? (mismatch-edge failure) 2)
  (check-equal? (mismatch-field failure) 'outputs)
  (check-equal? (hash-ref (mismatch-expected failure) "egress.bits") 2)
  (check-equal? (hash-ref (mismatch-actual failure) "egress.bits") 0)
  (check-equal? (replay faulty (mismatch-start failure) (mismatch-prefix failure)) failure)
  (check-equal? (replay plan start bad-trace) 'matched)

  ;; No register or user output is needed to keep a guarded verification effect alive.
  (define guarded (prepare_transition_snapshot guarded_snapshot "clock"))
  (for* ([rst '(0 1)] [g '(0 1)] [c '(0 1)])
    (define edge (step_transition guarded (hash) (hash "reset" rst "enabled" g "condition" c)))
    (check-equal? (transition_step_validity edge) (or (= rst 1) (= g 0) (= c 1)))))
