#lang rosette
#| Certifies an event-only TwoTagROB monitor and exposes its transition to downstream proofs. |#
;; SPDX-License-Identifier: Apache-2.0

(require racket/match (only-in racket/base [eq? same-definition?] [equal? same-layout?])
         (only-in "../../rhodium/formal/refinement.rkt" symbolic-fifo-cut fifo-cut-inputs fifo-cut-outputs
                  fifo-cut-locals fifo-cut-batch fifo-cut-domain fresh-fifo-history fifo-allows?
                  fifo-history-count fifo-history-slots fifo-transactions-width fifo-transactions-capacity
                  check-refinement-phase!)
         (only-in "../../rhodium/formal/spec-engine.rkt" edge-state edge-next)
         (only-in "../../rhodium/formal/port-types.rhm" leaf_layout)
         (only-in "../../rspecl/rtl-boundary.rhm" interface_prefixes)
         (only-in "../../rhodium/frontend/layers/bool.rhm" Bool))
(provide certify-rob rob-contract-for? rob-step fresh-rob-history fresh-rob-events
         (struct-out response) (struct-out rob-events)
         rob-history-count rob-history-head rob-history-tail
         entry-tag entry-gen entry-done entry-data)

;; This module owns exactly one event view. A seal names its exact RTL and physical layout,
;; not a child proof, cut, graph or executable cycle model.
(struct seal (definition layout))
(struct response (present gen data) #:transparent)
(struct rob-events (reset grant grant-tag grant-gen a b retire retire-tag retire-data) #:transparent)
(struct entry (tag gen done data) #:transparent)
(struct rob-history (count head tail) #:transparent)
(define unused (entry #f #f #f (bv 0 2)))
(define empty-history (rob-history 0 unused unused))

#| Match the canonical public event view of this exact elaborated specialization. |#
(define (rob-contract-for? evidence top)
  (and (seal? evidence) (same-definition? top (seal-definition evidence))
       (same-layout? (leaf_layout top (interface_prefixes top)) (seal-layout evidence))))

#| Allocate two independent abstract slots; only the live prefix is meaningful. |#
(define (fresh-rob-history)
  (define-symbolic* n integer?)
  #| Create an arbitrary public allocation, ignoring data until its first match. |#
  (define (slot)
    (define-symbolic* tag gen done boolean?)
    (define-symbolic* data (bitvector 2))
    (entry tag gen done data))
  (rob-history n (slot) (slot)))

#| Allocate an arbitrary joint public sample, including simultaneous responses for both tags. |#
(define (fresh-rob-events)
  (define-symbolic* reset grant tag gen retire retired-tag boolean?)
  (define-symbolic* data (bitvector 2))
  #| Allocate a completion pulse independent of all other events. |#
  (define (pulse)
    (define-symbolic* present gen boolean?)
    (define-symbolic* word (bitvector 2))
    (response present gen word))
  (rob-events reset grant tag gen (pulse) (pulse) retire retired-tag data))

#| Slot values outside the live prefix and data before completion are unobservable. |#
(define (same-history? x y)
  (define n (rob-history-count x))
  (&& (= n (rob-history-count y))
      (for/fold ([ok #t]) ([slot (list rob-history-head rob-history-tail)] [i (in-naturals)])
        (define a (slot x))
        (define b (slot y))
        (&& ok (=> (< i n) (&& (equal? (entry-tag a) (entry-tag b)) (equal? (entry-gen a) (entry-gen b))
                                (equal? (entry-done a) (entry-done b))
                                (=> (entry-done a) (bveq (entry-data a) (entry-data b)))))))))

#| Interpret one batch. Retirement reads the old head, completions update old live allocations,
   and a new grant is appended last. A ready head does not oblige a retirement. |#
(define (transition old e)
  (match-define (rob-history n head tail) old)
  (match-define (rob-events reset grant tag gen a b pop out word) e)
  #| Ignore duplicates, the other tag, and a reused tag's mismatching generation. |#
  (define (complete slot)
    (define p (if (entry-tag slot) b a))
    (define hit (&& (! (entry-done slot)) (response-present p) (equal? (entry-gen slot) (response-gen p))))
    (entry (entry-tag slot) (entry-gen slot) (|| (entry-done slot) hit)
           (if hit (response-data p) (entry-data slot))))
  (define left (- n (if pop 1 0)))
  (define new (entry tag gen #f (bv 0 2)))
  (define next
    (if reset empty-history
        (rob-history (+ left (if grant 1 0))
                     (if (&& grant (= left 0)) new (complete (if pop tail head)))
                     (if (&& grant (= left 1)) new (complete tail)))))
  (values
   (list (cons 'epoch (=> reset (! (|| grant pop (response-present a) (response-present b)))))
         (cons 'history (=> (! reset) (&& (<= 0 n 2) (=> (= n 2) (! (equal? (entry-tag head) (entry-tag tail)))))))
         (cons 'fresh-grant (=> grant (&& (< left 2) (=> (> n 0) (! (equal? tag (entry-tag head))))
                                          (=> (= n 2) (! (equal? tag (entry-tag tail)))))))
         (cons 'oldest-tag (=> pop (&& (> n 0) (equal? out (entry-tag head)))))
         (cons 'completed-before-retirement (=> pop (entry-done head)))
         (cons 'retired-payload (=> pop (bveq word (entry-data head)))))
   next))

#| Step a certified event contract. The successor is usable only if the first result is true. |#
(define (rob-step evidence old e)
  (unless (seal? evidence) (error 'rob-step "expected certified TwoTagROB transaction evidence"))
  (match-define (rob-events reset grant tag gen a b pop out word) e)
  (unless (and (andmap boolean? (list reset grant tag gen pop out)) ((bitvector 2) word)
               (for/and ([r (list a b)])
                 (and (response? r) (boolean? (response-present r)) (boolean? (response-gen r))
                      ((bitvector 2) (response-data r)))))
    (error 'rob-step "mistyped public ROB batch"))
  (define-values (guards next) (transition old e))
  (values (andmap cdr guards) next))

#| Project only the child transaction history and the actual parent-local registers.
   Canonical encodings and busy membership are induction obligations, never child promises. |#
(define (projection q regs)
  (define n (fifo-history-count q))
  (define words (fifo-history-slots q))
  (define slots
    (for/list ([w words])
      (define b (bveq w (bv 1 2)))
      (entry b (if b (hash-ref regs "gen_b") (hash-ref regs "gen_a"))
             (if b (hash-ref regs "done_b") (hash-ref regs "done_a"))
             (if b (hash-ref regs "saved_b") (hash-ref regs "saved_a")))))
  (values
   (&& (<= 0 n 2) (=> (= n 2) (! (bveq (first words) (second words))))
       (for/fold ([ok #t]) ([w words] [i (in-naturals)])
         (&& ok (=> (< i n) (|| (bveq w (bv 0 2)) (bveq w (bv 1 2)))))))
   (for/fold ([ok #t]) ([i '(0 1)] [busy '("busy_a" "busy_b")] [done '("done_a" "done_b")])
     (define live (for/fold ([present #f]) ([w words] [j (in-naturals)])
                    (|| present (&& (< j n) (bveq w (bv i 2))))))
     (&& ok (equal? live (hash-ref regs busy)) (=> (hash-ref regs done) live)))
   (rob-history n (first slots) (second slots))))

#| Issue evidence only after reset and every allowed child successor refine the exact monitor
   used by rob-step. There is no caller-supplied decoder, proof callback, or constructor. |#
(define (certify-rob top child)
  (define layout (leaf_layout top (interface_prefixes top)))
  (define cut (symbolic-fifo-cut top layout Bool "order" child))
  (unless (and (= (fifo-transactions-width child) 2) (= (fifo-transactions-capacity child) 2)
               (same-layout? (hash-keys (fifo-cut-locals cut)) '("@local")))
    (error 'certify-rob "expected depth-two two-bit FIFO and local ROB logic"))
  (define local (hash-ref (fifo-cut-locals cut) "@local"))
  #| Read an external input of the fixed public event view. |#
  (define (in k) (hash-ref (fifo-cut-inputs cut) k))
  #| Read a parent output, never a child offer or cycle prediction. |#
  (define (out k) (hash-ref (fifo-cut-outputs cut) k))
  (define reset (in "reset"))
  (define a (&& (! reset) (out "grant_a")))
  (define b (&& (! reset) (out "grant_b")))
  #| Reset cancels both completion pulses in the public epoch. |#
  (define (pulse present gen data) (response (&& (! reset) (in present)) (in gen) (in data)))
  (define events
    (rob-events reset (|| a b) b (out "issue_gen")
                (pulse "complete_a" "complete_gen_a" "result_a") (pulse "complete_b" "complete_gen_b" "result_b")
                (&& (! reset) (in "retire.ready") (out "retire.valid")) (out "retire.bits") (out "retire_result")))
  (define q (fresh-fifo-history child))
  (define next (fresh-fifo-history child))
  (define allowed (&& (fifo-cut-domain cut) (fifo-allows? child q (fifo-cut-batch cut) next)))
  (define-values (legal members old) (projection q (edge-state local)))
  (define-values (legal-next members-next actual) (projection next (edge-next local)))
  (define-values (guards expected) (transition old events))
  (define goals
    (append (list (cons 'grants (&& (! (&& a b)) (=> a (in "issue_a")) (=> b (in "issue_b"))))
                  (cons 'invariant (&& legal-next members-next))
                  (cons 'successor (same-history? expected actual)))
            guards))
  (define step (&& (! reset) allowed legal members))
  (check-refinement-phase! 'rob-reset (&& reset allowed) goals)
  (check-refinement-phase! 'rob-step step goals)
  ;; Retain the producer proof's independent nonvacuity checks. These are feasible
  ;; inductive samples, not timing premises or promised paths from reset.
  (for* ([tag '(#f #t)] [gen '(#f #t)])
    (check-refinement-phase! 'retirement-coverage
      (&& step (rob-events-retire events) (equal? tag (rob-events-retire-tag events))
          (equal? gen (entry-gen (rob-history-head old)))) '()))
  (check-refinement-phase! 'younger-only-coverage
    (&& step (= (rob-history-count old) 2) (! (entry-tag (rob-history-head old)))
        (! (entry-done (rob-history-head old))) (entry-done (rob-history-tail old))
        (in "retire.ready") (! (rob-events-retire events))) '())
  (seal top layout))
