#lang rosette
;; Reads a generic transaction seal or a legacy FIFO result for the same ordering observer.
;; SPDX-License-Identifier: Apache-2.0

(require racket/match
         (prefix-in old: "../refinement.rkt")
         (only-in "../refinement.rkt" fifo-history fifo-history-count fifo-history-slots fifo-events)
         "../spec-engine.rkt" (only-in "../transaction-induction.rkt" equivalent-value?)
         (only-in "../spec.rhm" spec_snapshot)
         (only-in "../../../rspecl/rtl-boundary.rhm" transaction_parts)
         (only-in "../../../rspecl/proof.rkt" transaction-for? transaction-step))
(provide observe-fifo-seal fresh-fifo-history fifo-events fifo-allows?
         fifo-history-count fifo-history-slots fifo-transactions-capacity fifo-transactions-width)

;; This is a reader, not a certificate. The observer still proves the FIFO properties.
(struct reader (seal view model capacity width))
(define (observe-fifo-seal seal top view)
  (unless (transaction-for? seal top view) (error 'observer "expected an unconditional seal for this top and view"))
  (define-values (_ model _ins _outs) (transaction_parts view))
  (define snapshot (spec_snapshot model))
  (match-define (list "seq" capacity (list "bits" width)) (hash-ref (hash-ref snapshot "state") "q"))
  (reader seal view snapshot capacity width))

(define (fifo-transactions-capacity c) (if (reader? c) (reader-capacity c) (old:fifo-transactions-capacity c)))
(define (fifo-transactions-width c) (if (reader? c) (reader-width c) (old:fifo-transactions-width c)))
(define (fresh-fifo-history c)
  (cond [(reader? c)
         (match-define (sequence-value n slots) (hash-ref (symbolic-spec-state (reader-model c)) "q"))
         (fifo-history (bitvector->natural n) slots)]
        [else (old:fresh-fifo-history c)]))

(define (fifo-allows? c old events next)
  (cond [(reader? c)
         (define count-sort (bitvector (integer-length (reader-capacity c))))
         (define (seq h) (sequence-value (integer->bitvector (fifo-history-count h) count-sort) (fifo-history-slots h)))
         (match-define (fifo-events rst push in pop out) events)
         (define fifo-batch (hash "reset" rst "push" push "in" in "pop" pop "out" out))
         ;; Quantify additional batch fields rather than fixing a sampler-domain verdict to true.
         (define extra (fresh-fields (for/hash ([(k t) (hash-ref (reader-model c) "inputs")]
                                                #:unless (hash-has-key? fifo-batch k))
                                      (values k t))))
         (define-values (allowed after)
           (transaction-step (reader-seal c) (reader-view c) (hash "q" (seq old))
                             (for/fold ([b extra]) ([(k v) fifo-batch]) (hash-set b k v))))
         (&& (<= 0 (fifo-history-count old) (reader-capacity c))
             (<= 0 (fifo-history-count next) (reader-capacity c))
             allowed (equivalent-value? (hash-ref after "q") (seq next)))]
        [else (old:fifo-allows? c old events next)]))
