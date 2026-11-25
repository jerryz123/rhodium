#lang rosette
#| Proves RSPECL transaction monitors across one FIFO or forwarded sealed child using checked state relations. |#
;; SPDX-License-Identifier: Apache-2.0

(require racket/match (only-in racket/base [eq? same?] [equal? same-schema?])
         "rtl-boundary.rhm" "../rhodium/formal/refinement.rkt" "../rhodium/formal/spec-engine.rkt"
         (only-in "../rhodium/formal/spec.rhm" spec_snapshot)
         (only-in "../rhodium/formal/port-types.rhm" leaf_layout)
         (only-in "../rhodium/frontend/layers/bool.rhm" Bool))
(provide prove-transaction transaction-for? transaction-step transaction-cut sample-transaction)

;; Run-local evidence, constructed only at the end of the induction. Identity includes the
;; exact contract, not merely a family name or an equivalent-looking physical interface.
(struct seal (top layout view sampler monitor ins outs))

#| Match both the physical definition and the expected public specification. |#
(define (transaction-for? evidence top view)
  (and (seal? evidence) (same? top (seal-top evidence)) (same? view (seal-view evidence))
       (same-schema? (leaf_layout top (interface_prefixes top)) (seal-layout evidence))))

#| Require the same contract when using a seal, including for a theorem-only consumer. |#
(define (check-view evidence view)
  (unless (and (seal? evidence) (same? (seal-view evidence) view))
    (error 'transaction "expected certified evidence for this transaction view")))

#| Take a single dense sample from the actual directional pin drivers. |#
(define (sample snapshot ins outs inputs outputs)
  (define pins (for/fold ([pins (for/hash ([k ins]) (values k (hash-ref inputs k)))])
                         ([k outs]) (hash-set pins k (hash-ref outputs k))))
  (for/hash ([(k obs) (edge-outputs (spec-edge-at snapshot (hash) pins))])
    (values k (observation-value obs))))

#| Sample public pins using the same total projection certified by the issuer. |#
(define (sample-transaction evidence view inputs outputs)
  (check-view evidence view)
  (sample (seal-sampler evidence) (seal-ins evidence) (seal-outs evidence) inputs outputs))

#| Legal, defined steps with an affirmative monitor verdict are the only public successors. |#
(define (allowed? e)
  (define obs (hash-ref (edge-outputs e) "allowed"))
  (&& (edge-legal e) (edge-defined e) (spec-state-legal? (edge-next e))
      (observation-care obs) (observation-value obs)))

#| Execute the sealed monitor; the caller may use the successor only under the returned verdict. |#
(define (transaction-step evidence view old batch)
  (check-view evidence view)
  (define e (spec-edge-at (seal-monitor evidence) old batch))
  (values (allowed? e) (edge-next e)))

#| Bind the proved implementation at the actual parent occurrence before sampling its pins. |#
(define (transaction-cut top site evidence view)
  (check-view evidence view)
  (define layout (seal-layout evidence))
  (define-values (ins outs edges ci co)
    (symbolic-wrapper-cut top (leaf_layout top (interface_prefixes top)) Bool site
                          (seal-top evidence) layout (seal-outs evidence)))
  (values ins outs edges (sample-transaction evidence view ci co)))

#| Sequence padding is not observable even if different concrete or symbolic slots encode it. |#
(define (equivalent-value? x y)
  (if (sequence-value? x)
      (let ([n (bitvector->natural (sequence-value-count x))])
        (&& (= n (bitvector->natural (sequence-value-count y)))
            (for/fold ([ok #t]) ([a (sequence-value-slots x)] [b (sequence-value-slots y)] [i (in-naturals)])
              (&& ok (=> (< i n) (equal? a b))))))
      (equal? x y)))

#| Encode FIFO history without truncating an invalid count or silently treating a non-tag word as false. |#
(define (fifo-value q sort child)
  (match-define (list "seq" capacity element) sort)
  (define width (fifo-transactions-width child))
  (unless (and (= capacity (fifo-transactions-capacity child))
               (or (same-schema? element '("bool")) (same-schema? element (list "bits" width))))
    (error 'refinement "FIFO evidence does not match the declared sequence"))
  (define n (fifo-history-count q))
  (define words (fifo-history-slots q))
  (define tags? (same-schema? element '("bool")))
  (values (sequence-value (integer->bitvector n (bitvector (integer-length capacity)))
                          (if tags? (map (lambda (w) (bveq w (bv 1 width))) words) words))
          (&& (<= 0 n capacity)
              (for/fold ([ok #t]) ([w words] [i (in-naturals)])
                (&& ok (=> (&& tags? (< i n)) (|| (bveq w (bv 0 width)) (bveq w (bv 1 width)))))))))

#| Resolve the declared sources against real local registers and the independent abstract child history. |#
(define (hint-edge snapshot history regs [inputs #f] [outputs #f] [batch #f])
  (define args
    (for/hash ([key (hash-keys (hash-ref snapshot "inputs"))])
      (values key
        (if (hash-has-key? history key) (hash-ref history key)
            (match (regexp-match #px"^([^.]+)[.](.+)$" key)
              [(list _ prefix name)
               (define source (match prefix ["local" regs] ["input" inputs] ["output" outputs] ["batch" batch]))
               (unless (and source (hash-has-key? source name))
                 (error 'refinement "unknown or unavailable hint source: ~a" key))
               (hash-ref source name)])))))
  (spec-edge-at snapshot (hash) args))

#| A predicate is an obligation only if its value is defined and observed. |#
(define (holds obs) (&& (observation-care obs) (observation-value obs)))

#| Compare all declared monitor fields under their care and prove every auxiliary invariant. |#
(define (related? projection domain state)
  (&& domain (edge-legal projection) (edge-defined projection)
      (andmap holds (hash-values (edge-events projection)))
      (for/fold ([ok #t]) ([(key obs) (edge-outputs projection)])
        (&& ok (=> (observation-care obs) (equivalent-value? (observation-value obs) (hash-ref state key)))))))

#| Open a FIFO theorem at the designated occurrence; histories are independently quantified
   and the theorem, not a FIFO cycle implementation, constrains their relationship. |#
(define (fifo-boundary top layout site fifo relation child)
  (define cut (symbolic-fifo-cut top layout Bool site child))
  (define q (fresh-fifo-history child))
  (define next (fresh-fifo-history child))
  (define domain (&& (fifo-cut-domain cut) (fifo-allows? child q (fifo-cut-batch cut) next)))
  (define fifo-sort (hash-ref (hash-ref relation "inputs") fifo))
  (define-values (before ok) (fifo-value q fifo-sort child))
  (define-values (after next-ok) (fifo-value next fifo-sort child))
  (values (fifo-cut-inputs cut) (fifo-cut-outputs cut) (fifo-cut-locals cut) domain
          (hash fifo before) ok (hash fifo after) next-ok #f))

#| Keep public monitor state separate from both local registers and parent monitor state. |#
(define (history-fields state)
  (for/hash ([(k v) state]) (values (string-append "history." k) v)))

#| Check the whole forwarded child batch on arbitrary cut pins, before using child legality.
   Only the sealed monitor steps. There is no child RTL or predicted ready/valid timing here. |#
(define (sealed-boundary top site expected child)
  (define-values (inputs outputs locals inside) (transaction-cut top site child expected))
  (define outside (sample-transaction child expected inputs outputs))
  (check-refinement-phase! 'child-wiring #t (list (cons 'forwarded-batch (equal? inside outside))))
  (define model (seal-monitor child))
  (define old (symbolic-spec-state model))
  (define-values (domain next) (transaction-step child expected old inside))
  (values inputs outputs locals domain (history-fields old) #t (history-fields next) #t
          (history-fields (spec-initial-state model))))

#| Prove initialization and preservation with an arbitrary monitor state and every admitted child
   edge. Relation hints are conclusions on reset and next state, never environment premises. |#
(define (prove-transaction top view child #:child-site [implementation-site #f])
  (match-define (list site cut relation checks) (refinement_parts view))
  ;; A private binding may select a renamed occurrence; the cut still checks definition,
  ;; view, clock/reset and actual wiring before admitting any child guarantee.
  (define actual-site (or implementation-site site))
  (define-values (_ monitor-model ins outs) (transaction_parts view))
  (define monitor (spec_snapshot monitor-model))
  (define sampler (spec_snapshot (transaction_batch_model view)))
  (unless (zero? (hash-count (hash-ref monitor "assumptions"))) (error 'refinement "conditional monitor"))
  (check-batch-total! sampler)
  (define layout (leaf_layout top (interface_prefixes top)))
  (define-values (inputs outputs locals domain before ok after next-ok child-initial)
    (if (string? cut) (fifo-boundary top layout actual-site cut relation child) (sealed-boundary top actual-site cut child)))
  (define local (hash-ref locals "@local"))
  (define reset (hash-ref inputs "reset"))
  (define batch (sample sampler ins outs inputs outputs))
  (define pre (hint-edge relation before (edge-state local)))
  (define post (hint-edge relation after (edge-next local)))
  (define side (hint-edge checks before (edge-state local) inputs outputs batch))
  (define old (symbolic-spec-state monitor))
  (define step (spec-edge-at monitor old batch))
  (define base (&& reset domain (spec-state-legal? old)))
  (define active (&& (! reset) domain (spec-state-legal? old) (related? pre ok old)))
  (define goals
    (append (for/list ([k (sort (hash-keys (edge-outputs step)) string<?)] #:unless (same-schema? k "allowed"))
              (cons k (holds (hash-ref (edge-outputs step) k))))
            (for/list ([(k v) (edge-outputs side)]) (cons k (holds v)))
            (list (cons 'checks-defined (edge-defined side)) (cons 'allowed (allowed? step))
                  (cons 'successor (related? post next-ok (edge-next step))))))
  (define initial (spec-initial-state monitor))
  (check-refinement-phase! 'reset base
    (append (if child-initial
                (list (cons 'child-initial (related? (hint-edge relation child-initial (edge-next local)) #t initial)))
                '())
            (list (cons 'initial (related? post next-ok initial))) goals))
  (check-refinement-phase! 'step active goals)
  (for ([(k v) (edge-events side)]) (check-refinement-phase! k (&& active (holds v)) '()))
  (seal top layout view sampler monitor ins outs))
