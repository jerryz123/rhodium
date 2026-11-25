#lang rosette
#| Certifies the TwoTagROB spec-IR event monitor and steps that sealed monitor in downstream proofs. |#
;; SPDX-License-Identifier: Apache-2.0

(require racket/match (only-in racket/base [eq? same-definition?] [equal? same-layout?])
         (only-in "../../rhodium/formal/refinement.rkt" symbolic-wrapper-cut symbolic-fifo-cut fifo-cut-inputs fifo-cut-outputs
                  fifo-cut-locals fifo-cut-batch fifo-cut-domain fresh-fifo-history fifo-allows?
                  fifo-history-count fifo-history-slots fifo-transactions-width fifo-transactions-capacity
                  check-batch-total! check-refinement-phase!)
         (only-in "two-tag-rob.rspl" TwoTagROB)
         (only-in "../../rhodium/formal/spec.rhm" spec_snapshot)
         (only-in "../../rhodium/formal/spec-engine.rkt" spec-edge-at edge-state edge-next edge-outputs
                  edge-defined edge-legal spec-state-legal? sequence-value sequence-value-slots sequence-value-count
                  observation-care observation-value)
         (only-in "../../rhodium/formal/port-types.rhm" leaf_layout)
         (only-in "../../rspecl/rtl-boundary.rhm" interface_prefixes transaction_parts transaction_batch_model)
         (only-in "../../rhodium/frontend/layers/bool.rhm" Bool))
(provide certify-rob rob-contract-for? rob-step sample-rob-events symbolic-rob-cut fresh-rob-history fresh-rob-events
         (struct-out response) (struct-out rob-events)
         rob-history-count rob-history-head rob-history-tail
         entry-tag entry-gen entry-done entry-data)

;; This module owns exactly one event view. A seal names its exact RTL and physical layout,
;; not a child proof, cut, graph or executable cycle model.
(struct seal (definition layout sampler monitor))
(struct response (present gen data) #:transparent)
(struct rob-events (reset grant grant-tag grant-gen a b retire retire-tag retire-data) #:transparent)
(struct entry (tag gen done data) #:transparent)
(struct rob-history (count head tail) #:transparent)
(module+ history-types (provide (struct-out entry) (struct-out rob-history))) ; test oracle data, never evidence
(define unused (entry #f #f #f (bv 0 2)))
(define view (TwoTagROB #:transaction_view #t))
(define-values (_raw-sampler monitor-model sampled-ins sampled-outs) (transaction_parts view))
(define monitor (spec_snapshot monitor-model))
(define sampler (spec_snapshot (transaction_batch_model view)))
(unless (zero? (hash-count (hash-ref monitor "assumptions")))
  (error 'certify-rob "the public transaction monitor must judge every batch without assumptions"))

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

#| Encode the live prefix of a given length as an IR tag sequence and two banks. |#
(define (monitor-state old n)
  (match-define (rob-history _ head tail) old)
  #| Only an old live allocation owns a bank; an idle bank can be encoded with any bits. |#
  (define (bank tag)
    (cond [(&& (> n 0) (equal? tag (entry-tag head))) head]
          [(&& (= n 2) (equal? tag (entry-tag tail))) tail]
          [else unused]))
  (define a (bank #f))
  (define b (bank #t))
  (hash "order" (sequence-value (bv n 2) (list (entry-tag head) (entry-tag tail)))
        "gen_a" (entry-gen a) "done_a" (entry-done a) "saved_a" (entry-data a)
        "gen_b" (entry-gen b) "done_b" (entry-done b) "saved_b" (entry-data b)))

#| Expose only the live entries of an IR successor. Padding and unfinished data are not promises. |#
(define (public-history state)
  (define q (hash-ref state "order"))
  (define n (bitvector->natural (sequence-value-count q)))
  (define slots
    (for/list ([tag (sequence-value-slots q)] [i (in-naturals)])
      (define gen (if tag (hash-ref state "gen_b") (hash-ref state "gen_a")))
      (define done (if tag (hash-ref state "done_b") (hash-ref state "done_a")))
      (define data (if tag (hash-ref state "saved_b") (hash-ref state "saved_a")))
      (if (< i n) (entry tag gen done (if done data (bv 0 2))) unused)))
  (rob-history n (first slots) (second slots)))

#| Run one spec-IR edge over a dense batch, without reinterpreting the sampler or filtering joint events. |#
(define (monitor-step snapshot old pins)
  (define reset (hash-ref pins "reset"))
  (define n (rob-history-count old))
  #| Evaluate at each possible length so an unconditional reset produces a concrete verdict
     even from unknown occupancy. Never truncate an out-of-range count into a live allocation. |#
  (define (at count)
    (define step (spec-edge-at snapshot (monitor-state old count) pins))
    #| A missing or uncared verdict cannot authorize a transition. |#
    (define (verdict key)
      (define obs (hash-ref (edge-outputs step) key))
      (&& (observation-care obs) (observation-value obs)))
    (define allowed (&& (|| reset (<= 0 n 2))
                        (edge-legal step) (edge-defined step) (spec-state-legal? (edge-next step))
                        (verdict "allowed")))
    (define diagnostics
      (for/list ([name '(epoch history fresh-grant oldest-tag completed-before-retirement retired-payload)])
        (cons name (verdict (symbol->string name)))))
    (list allowed (public-history (edge-next step)) diagnostics))
  (apply values (cond [(= n 1) (at 1)] [(= n 2) (at 2)] [else (at 0)])))

#| Step a certified event contract. The successor is usable only if the first result is true. |#
(define (rob-step evidence old e)
  (unless (seal? evidence) (error 'rob-step "expected certified TwoTagROB transaction evidence"))
  (match-define (rob-events reset grant tag gen a b pop out word) e)
  (unless (and (andmap boolean? (list reset grant tag gen pop out)) ((bitvector 2) word)
               (for/and ([r (list a b)])
                 (and (response? r) (boolean? (response-present r)) (boolean? (response-gen r))
                      ((bitvector 2) (response-data r)))))
    (error 'rob-step "mistyped public ROB batch"))
  (define pins (hash "reset" reset "grant" grant "grant_tag" tag "grant_gen" gen
                     "complete_a" (response-present a) "echo_a" (response-gen a) "data_a" (response-data a)
                     "complete_b" (response-present b) "echo_b" (response-gen b) "data_b" (response-data b)
                     "retire" pop "retire_tag" out "retire_data" word))
  (define-values (allowed next _) (monitor-step (seal-monitor evidence) old pins))
  (values allowed next))

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

#| Feed the declared projection's dense output record directly to the monitor. Before sealing
   we prove each output is present and defined for every physical sample. |#
(define (sample-fields snapshot inputs outputs)
  (define pins (for/fold ([pins (for/hash ([k sampled-ins]) (values k (hash-ref inputs k)))])
                         ([k sampled-outs]) (hash-set pins k (hash-ref outputs k))))
  (for/hash ([(name obs) (edge-outputs (spec-edge-at snapshot (hash) pins))])
    (values name (observation-value obs))))

#| Package already projected fields for existing public event consumers, without decoding any sparse events. |#
(define (public-batch fields)
  #| Read a dense batch field, including a present value of false. |#
  (define (v k) (hash-ref fields k))
  (rob-events (v "reset") (v "grant") (v "grant_tag") (v "grant_gen")
              (response (v "complete_a") (v "echo_a") (v "data_a"))
              (response (v "complete_b") (v "echo_b") (v "data_b"))
              (v "retire") (v "retire_tag") (v "retire_data")))

#| Sample the wrapper's own public pins for comparison with its sealed child's batch. |#
(define (sample-rob-events inputs outputs) (public-batch (sample-fields sampler inputs outputs)))

#| Bind a seal to the wrapper's actual child and sample its events without opening the ROB. |#
(define (symbolic-rob-cut top site evidence)
  (unless (seal? evidence) (error 'symbolic-rob-cut "expected certified TwoTagROB transaction evidence"))
  (define layout (seal-layout evidence))
  (define outputs (for*/list ([fields (in-hash-values (second layout))] [f fields]) (car f)))
  (define-values (ins outs edges ci co)
    (symbolic-wrapper-cut top (leaf_layout top (interface_prefixes top)) Bool site
                          (seal-definition evidence) layout outputs))
  (values ins outs edges (public-batch (sample-fields (seal-sampler evidence) ci co))))

#| Issue evidence only after reset and every allowed child successor refine the exact monitor
   used by rob-step. There is no caller-supplied decoder, proof callback, or constructor. |#
(define (certify-rob top child)
  (check-batch-total! sampler)
  (define layout (leaf_layout top (interface_prefixes top)))
  (define cut (symbolic-fifo-cut top layout Bool "order" child))
  (unless (and (= (fifo-transactions-width child) 2) (= (fifo-transactions-capacity child) 2)
               (same-layout? (hash-keys (fifo-cut-locals cut)) '("@local")))
    (error 'certify-rob "expected depth-two two-bit FIFO and local ROB logic"))
  (define local (hash-ref (fifo-cut-locals cut) "@local"))
  (define inputs (fifo-cut-inputs cut))
  (define outputs (fifo-cut-outputs cut))
  (define fields (sample-fields sampler inputs outputs))
  (define reset (hash-ref fields "reset"))
  (define a (&& (! reset) (hash-ref outputs "grant_a")))
  (define b (&& (! reset) (hash-ref outputs "grant_b")))
  (define q (fresh-fifo-history child))
  (define next (fresh-fifo-history child))
  (define allowed (&& (fifo-cut-domain cut) (fifo-allows? child q (fifo-cut-batch cut) next)))
  (define-values (legal members old) (projection q (edge-state local)))
  (define-values (legal-next members-next actual) (projection next (edge-next local)))
  (define-values (permitted expected diagnostics) (monitor-step monitor old fields))
  (define goals
    (append (list (cons 'grants (&& (! (&& a b)) (=> a (hash-ref inputs "issue_a")) (=> b (hash-ref inputs "issue_b"))))
                  (cons 'invariant (&& legal-next members-next)))
            diagnostics
            (list (cons 'allowed permitted) (cons 'successor (same-history? expected actual)))))
  (define step (&& (! reset) allowed legal members))
  (check-refinement-phase! 'rob-reset (&& reset allowed) goals)
  (check-refinement-phase! 'rob-step step goals)
  ;; Retain the producer proof's independent nonvacuity checks. These are feasible
  ;; inductive samples, not timing premises or promised paths from reset.
  (for* ([tag '(#f #t)] [gen '(#f #t)])
    (check-refinement-phase! 'retirement-coverage
      (&& step (hash-ref fields "retire") (equal? tag (hash-ref fields "retire_tag"))
          (equal? gen (entry-gen (rob-history-head old)))) '()))
  (check-refinement-phase! 'younger-only-coverage
    (&& step (= (rob-history-count old) 2) (! (entry-tag (rob-history-head old)))
        (! (entry-done (rob-history-head old))) (entry-done (rob-history-tail old))
        (hash-ref inputs "retire.ready") (! (hash-ref fields "retire"))) '())
  (seal top layout sampler monitor))
