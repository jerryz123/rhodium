#lang rosette
;; SPDX-License-Identifier: Apache-2.0
(require rackunit racket/match "../../../rspecl/proof.rkt" "../engine.rkt"
         (only-in "../snapshot.rhm" formal_snapshot)
         (only-in "../../../rspecl/rtl-boundary.rhm" transaction_parts)
         (only-in "../spec.rhm" spec_snapshot)
         (prefix-in alternative: "stall-relations.rspl")
         (only-in "composition-binding.rhm" fifo_at stall_parent stall_sink stall_wrapper changing_offer transfer_only_wrapper)
         "stall-replay.rhm" "shift-queue-induction.rkt" "shift-queue-transactions.rkt"
         "../../../examples/formal/stalled-offer.rspl")

(define fifo (fifo_at 1))
(define offer (ShiftQueue fifo #:transaction_view #t))
(define receipt (StallConsumer stall_sink #:transaction_view #t))
(define wrapped (StallConsumerWrapper stall_wrapper #:transaction_view #t))
(define outside (BufferedStallConsumer stall_parent #:transaction_view #t))
(define fifo-only (verified-shift-queue-transactions-at 1))
(define producer (derive-transaction (verified-fifo-at 1) fifo offer 'adjacent-samples))
(define consumer (prove-transaction stall_sink receipt (hash) #:requires required_offer))
(define (forward top requirement)
  (prove-forwarded-transaction top wrapped "recorder" consumer receipt #:requires requirement))
(define conditional-wrapper (forward stall_wrapper wrapper_offer))
(define sites (hash "fifo" producer "sink" conditional-wrapper))
(define links (hash "sink" "fifo"))

(test-case "only pending is public; the private stalled-word relation is an induction goal"
  (define (state-fields view)
    (define-values (_ model _i _o) (transaction_parts view))
    (sort (hash-keys (hash-ref (spec_snapshot model) "state")) string<?))
  (check-equal? (state-fields required_offer) '("last" "pending"))
  (check-equal? (state-fields receipt) '("pending"))
  (check-equal? (state-fields wrapped) '("pending"))
  (check-equal? (state-fields outside) '("pending"))
  (check-exn #rx"unknown or mistyped child monitor state: last" alternative:parent_reads_private)
  ;; Even the correct input guarantee cannot identify the RTL's saved word without a relation.
  (check-exn #rx"step/first-stalled-word not proved"
             (lambda () (prove-transaction stall_sink alternative:unrestricted (hash) #:requires required_offer)))
  (check-exn #rx"step/successor not proved"
             (lambda () (prove-transaction stall_sink (alternative:StallConsumer #:transaction_view #t)
                                           (hash) #:requires required_offer))))

(test-case "forwarding proves the pin mapping, not the validity of the environment"
  (check-false (transaction-for? conditional-wrapper stall_wrapper wrapped))
  (check-exn #rx"requires discharge" (lambda () (transaction-step conditional-wrapper wrapped (hash) (hash))))
  (check-exn #rx"missing temporal supplier" (lambda () (forward stall_wrapper #f)))
  (check-exn #rx"matching temporal input monitors" (lambda () (forward stall_wrapper alternative:other_epoch)))
  (check-exn #rx"forward-requirement/word not proved" (lambda () (forward transfer_only_wrapper wrapper_offer))))

(test-case "only a proved offer at the wrapper's own pins discharges its temporal premise"
  (check-true (transaction-for? producer fifo offer))
  (check-false (transaction-for? consumer stall_sink receipt))
  (check-exn #rx"requires discharge" (lambda () (transaction-step consumer receipt (hash) (hash))))
  (check-exn #rx"unknown or unavailable hint source: required\\."
             (lambda () (prove-transaction stall_sink receipt (hash))))
  (check-exn #rx"step/first-stalled-word not proved"
             (lambda () (prove-transaction stall_sink alternative:unrestricted (hash))))
  (check-true (transaction-for? (prove-transaction stall_parent outside sites #:suppliers links) stall_parent outside))
  (check-exn #rx"expected certified evidence for this transaction view"
             (lambda () (prove-transaction stall_parent outside (hash-set sites "sink" consumer) #:suppliers links)))
  (check-exn #rx"missing temporal supplier" (lambda () (prove-transaction stall_parent outside sites)))
  (check-exn #rx"matching temporal supplier"
             (lambda () (prove-transaction stall_parent outside sites #:suppliers (hash "sink" "sink"))))
  (check-exn #rx"input-only history requirement" (lambda () (prove-transaction stall_sink receipt (hash) #:requires receipt)))
  (check-exn #rx"expected cycle evidence" (lambda () (derive-transaction fifo-only fifo offer 'adjacent-samples)))
  (check-exn #rx"matching temporal supplier"
             (lambda () (prove-transaction stall_parent outside (hash-set sites "fifo" fifo-only) #:suppliers links)))
  (check-exn #rx"history-link/word not proved"
             (lambda () (prove-transaction changing_offer outside sites #:suppliers links))))

(define (pins rst valid ready data) (hash "reset" rst "valid" valid "ready" ready "word" data))
(test-case "the next sample can continue the stall, handshake, or cancel on reset"
  (define-values (pending _) (replay_sample required_offer #f (pins #f #t #f 1)))
  (for ([ready '(#t #f)])
    (define-values (_ok unchanged) (replay_sample required_offer pending (pins #f #t ready 1)))
    (check-true (hash-ref unchanged "allowed"))
    (define-values (_bad changed) (replay_sample required_offer pending (pins #f #t ready 2)))
    (check-false (hash-ref changed "stable-payload"))
    (define-values (_idle withdrawn) (replay_sample required_offer pending (pins #f #f ready 1)))
    (check-false (hash-ref withdrawn "stable-valid")))
  (define-values (cleared reset-ok) (replay_sample required_offer pending (pins #t #f #f 3)))
  (check-true (hash-ref reset-ok "allowed"))
  (check-false (hash-ref cleared "pending"))
  (define-values (_next fresh) (replay_sample required_offer cleared (pins #f #t #t 3)))
  (check-true (hash-ref fresh "allowed")))

;; Sample all physical outputs before each RTL edge, starting with an explicit reset sample.
(define (rtl-trace top samples)
  (define plan (prepare_transition_snapshot (formal_snapshot top) "clock"))
  (check-equal? (hash-ref (first samples) "reset") 1)
  (for/fold ([regs (for/hash ([reg (transition_registers plan)]) (values (hash-ref reg "name") 0))]
             [rows '()] #:result (reverse rows)) ([sample samples])
    (define e (step_transition plan regs sample))
    (check-true (transition_step_validity e))
    (values (transition_step_state e)
            (cons (for/hash ([(k v) (transition_step_outputs e)]) (values k (bitvector->natural v))) rows))))

(test-case "the transfer-only wrapper receives a stable word but remembers zero during the stall"
  (define trace '((1 0 0 0) (0 1 0 2) (0 1 0 2) (0 1 1 2)))
  (define samples (for/list ([s trace])
                    (match-define (list rst valid ready payload) s)
                    (hash "reset" rst "valid" valid "ready" ready "payload" payload)))
  (for ([top (list stall_wrapper transfer_only_wrapper)] [expected '(#t #f)])
    (define outputs (rtl-trace top samples))
    (check-equal? (hash-ref (last outputs) "remembered") (if expected 2 0))
    (define verdicts
      (for/fold ([env #f] [rec #f] [rows '()] #:result (reverse rows)) ([s samples] [o outputs])
        (define input (for/hash ([(k v) s]) (values k (if (equal? k "payload") v (= v 1)))))
        (define-values (env+ assumption) (replay_sample wrapper_offer env input))
        (define-values (rec+ receipt) (replay_sample wrapped rec (hash-set input "remembered" (hash-ref o "remembered"))))
        (check-true (hash-ref assumption "allowed"))
        (values env+ rec+ (cons (hash-ref receipt "allowed") rows))))
    (check-equal? verdicts (list #t #t #t expected))))

;; Run the public RSPECL monitors over the pinned observations of the complete hierarchy.
(define (replay top trace)
  (define samples (for/list ([s trace])
                    (match-define (list rst push ready data) s)
                    (hash "reset" rst "offer" push "consume" ready "data" data)))
  (for/fold ([old #f] [rec #f] [rows '()] #:result (reverse rows)) ([s trace] [outputs (rtl-trace top samples)])
    (match-define (list rst push ready data) s)
    (define (out name) (hash-ref outputs name))
    (define p (pins (= rst 1) (= (out "valid") 1) (= ready 1) (out "word")))
    (define-values (next verdict) (replay_sample required_offer old p))
    (define-values (next-rec receipt-verdict) (replay_sample receipt rec (hash-set p "remembered" (out "remembered"))))
    (values next next-rec
            (cons (list (hash-ref verdict "allowed") (hash-ref receipt-verdict "allowed") (out "word") (out "remembered")) rows))))

(test-case "changing live input during a stall breaks the offer before the recorder's handshake"
  (define trace '((1 0 0 0) (0 1 0 1) (0 0 0 2) (0 0 0 3) (0 0 1 3)))
  (define good (replay stall_parent trace))
  (define bad (replay changing_offer trace))
  (check-equal? (map first good) '(#t #t #t #t #t))
  (check-equal? (last good) '(#t #t 1 1))
  (check-equal? (map first bad) '(#t #t #t #f #f))
  (check-equal? (map third (drop bad 2)) '(2 3 1))
  (check-equal? (last bad) '(#f #f 1 2))
  (define cancelled (replay stall_parent '((1 0 0 0) (0 1 0 1) (0 0 0 1) (1 0 0 2) (0 1 0 3) (0 0 1 3))))
  (check-true (andmap (lambda (row) (and (first row) (second row))) cancelled))
  (check-equal? (third (last cancelled)) 3))
