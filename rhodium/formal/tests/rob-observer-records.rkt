#lang rosette
;; Historical record representation used by the differential oracle and ordinal log observer only.
;; SPDX-License-Identifier: Apache-2.0

(require racket/match "../../../rspecl/proof.rkt" "../spec-engine.rkt"
         (only-in "../../../examples/formal/two-tag-rob.rspl" TwoTagROB))
(provide rob-step fresh-rob-history fresh-rob-events public-batch
         (struct-out response) (struct-out rob-events) (struct-out entry) (struct-out rob-history))

(define view (TwoTagROB #:transaction_view #t))
(struct response (present gen data) #:transparent)
(struct rob-events (reset grant grant-tag grant-gen a b retire retire-tag retire-data) #:transparent)
(struct entry (tag gen done data) #:transparent)
(struct rob-history (count head tail) #:transparent)
(define unused (entry #f #f #f (bv 0 2)))
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

#| Step a certified event contract. The successor is usable only if the first result is true. |#
(define (rob-step evidence old e)
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
  (define n (rob-history-count old))
  (define (at count)
    (define-values (ok next) (transaction-step evidence view (monitor-state old count) pins))
    (list (&& (|| reset (<= 0 n 2)) ok) (public-history next)))
  (apply values (cond [(= n 1) (at 1)] [(= n 2) (at 2)] [else (at 0)])))

#| Package already projected fields for existing public event consumers, without decoding any sparse events. |#
(define (public-batch fields)
  #| Read a dense batch field, including a present value of false. |#
  (define (v k) (hash-ref fields k))
  (rob-events (v "reset") (v "grant") (v "grant_tag") (v "grant_gen")
              (response (v "complete_a") (v "echo_a") (v "data_a"))
              (response (v "complete_b") (v "echo_b") (v "data_b"))
              (v "retire") (v "retire_tag") (v "retire_data")))
