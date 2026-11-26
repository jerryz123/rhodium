#lang rosette
;; Proves an observer's retirement-order theorem from an exported FIFO transaction contract.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "composition-checks.rkt" "fifo-observer-input.rkt")
(provide check-retirement-order check-ready-not-entailed)

;; The observer counts accepted and retired words since reset. For arbitrary ordinal k,
;; it remembers the payload on the kth acceptance, independently of the FIFO history.
(define (check-retirement-order contract)
  (define old (fresh-fifo-history contract))
  (define next (fresh-fifo-history contract))
  (define width (bitvector (fifo-transactions-width contract)))
  (define-symbolic* reset push pop boolean?)
  (define-symbolic* in out remembered width)
  (define-symbolic* accepted retired k integer?)
  (define batch (fifo-events reset push in pop out))
  (define allowed (fifo-allows? contract old batch next))
  (define (invariant q a r word)
    (&& (<= 0 r a) (= (fifo-history-count q) (- a r))
        (for/fold ([ok #t]) ([slot (fifo-history-slots q)] [i (in-naturals)])
          (&& ok (=> (&& (= k (+ r i)) (< k a)) (bveq slot word))))))
  (define base (&& (>= k 0) allowed reset))
  (define step (&& (>= k 0) allowed (! reset) (invariant old accepted retired remembered)))
  (define kth-in (&& push (= accepted k)))
  (define kth-out (&& pop (= retired k)))
  (require-feasible (&& base (> (fifo-history-count old) 0)))
  (discharge 'new-epoch base (&& (! push) (! pop) (invariant next 0 0 remembered)))
  (require-feasible (&& step kth-in pop))
  (require-feasible (&& step kth-out push))
  (discharge 'observer-preserved step
             (invariant next (+ accepted (if push 1 0)) (+ retired (if pop 1 0))
                        (if kth-in in remembered)))
  (discharge 'kth-retirement step (=> kth-out (&& (< k accepted) (bveq out remembered))))
  (discharge 'pending-bound step (<= 0 (fifo-history-count next) (fifo-transactions-capacity contract)))
  ;; Return the independent coverage query: can k wait *behind* the head on an idle edge?
  ;; It is not a premise or a conclusion of the ordering theorem.
  (&& step (< retired k accepted) (! push) (! pop)))

(define (check-ready-not-entailed contract)
  (define (fresh) (fresh-fifo-history contract))
  (define old (fresh))
  (define empty (fresh))
  (define already-pending? (> (fifo-transactions-capacity contract) 1))
  (define before (if already-pending? (fresh) empty))
  (define next (fresh))
  (define x (bv 1 2))
  (define y (bv 2 2))
  ;; The environment offers y. Its acceptance would equal ready on this edge;
  ;; ready is a candidate pin interpretation, not something the contract drives.
  (define-symbolic* ready boolean?)
  (define prefix
    (&& (fifo-allows? contract old (fifo-events #t #f x #f x) empty)
        (if already-pending? (fifo-allows? contract empty (fifo-events #f #t x #f x) before) #t)))
  (define allowed (&& prefix (fifo-allows? contract before (fifo-events #f ready y #f x) next)))
  (define space (< (fifo-history-count before) (fifo-transactions-capacity contract)))
  (define witness (solve (assert (&& allowed (! (=> space ready))))))
  (check-true (sat? witness))
  (check-false (evaluate ready witness))
  (check-equal? (evaluate (fifo-history-count next) witness) (if already-pending? 1 0))
  ;; The event view permits either choice for this offer.
  (require-feasible (&& allowed space ready)))

(module+ test
  (require (only-in "shift-queue-pair.rkt" verified-queue-pair-transactions-at))
  (for ([depth '(1 2)])
    (test-case (format "event-only retirement order for child depth ~a" depth)
      (define contract (verified-queue-pair-transactions-at depth))
      (check-equal? (fifo-transactions-capacity contract) (* 2 depth))
      (require-feasible (check-retirement-order contract))))
  (test-case "ready whenever space remains is not entailed by the event contract"
    (check-ready-not-entailed (verified-queue-pair-transactions-at 1))))
