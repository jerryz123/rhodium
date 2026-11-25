#lang rosette
#| Checks reset and step refinement across independent transaction monitors on actual parent RTL wiring. |#
;; SPDX-License-Identifier: Apache-2.0

(require racket/match (only-in racket/base [equal? same-schema?] [eq? same?])
         "spec-engine.rkt" (only-in "wiring.rhm" wrapper_wiring))
(provide (struct-out monitor-site) open-monitor-sites check-monitor-refinement! check-monitor-edge! monitor-allowed?)

;; A private proof strategy supplies these adapters from already checked evidence.
;; Neither a descriptor nor successful use of this low-level checker is reusable evidence.
(struct monitor-site (definition layout visible advance))

#| Open the parent only. An arbitrary physical output at each occurrence is one shared
   net for all its RTL sinks. Return per-site pin pairs, not any child implementation state. |#
(define (open-monitor-sites top layout bool sites check!)
  #| Reject a same-shaped substitute before interpreting the recorded port layout. |#
  (define (layout-at name definition)
    (define site (hash-ref sites name #f))
    (unless (and site (same? definition (monitor-site-definition site)))
      (error 'transaction-cut "transaction evidence does not match the RTL occurrence: ~a" name))
    (monitor-site-layout site))
  (define graph (wrapper_wiring top layout
                  (for/hash ([(k s) sites]) (values k (monitor-site-visible s))) layout-at bool))
  (define locals (hash-ref graph "children"))
  (define states (for/hash ([(k model) locals]) (values k (symbolic-spec-state model))))
  (define inputs (fresh-fields (hash-ref graph "inputs")))
  (define-values (outputs edges settled) (wired-edge-at graph locals states inputs))
  (define local-ok (for/fold ([ok settled]) ([e (in-hash-values edges)])
                     (&& ok (edge-legal e) (edge-defined e) (spec-state-legal? (edge-next e)))))
  (check! 'wrapper-rtl #t (list (cons 'defined local-ok)))
  #| Split qualified graph endpoints only after the layout reader has rejected name collisions. |#
  (define (side pins name)
    (for/hash ([(k v) pins]
               #:when (if name (string-prefix? k (string-append name "."))
                          (not (for/or ([s (hash-keys sites)]) (string-prefix? k (string-append s "."))))))
      (values (if name (string->immutable-string (substring k (add1 (string-length name)))) k) v)))
  (values (side inputs #f) (side outputs #f) edges
          (for/hash ([k (hash-keys sites)]) (values k (list (side outputs k) (side inputs k))))))

#| Legal, defined steps with an affirmative monitor verdict are the only public successors. |#
(define (monitor-allowed? e)
  (define obs (hash-ref (edge-outputs e) "allowed"))
  (&& (edge-legal e) (edge-defined e) (spec-state-legal? (edge-next e))
      (observation-care obs) (observation-value obs)))

#| Sequence padding is not observable even if different concrete or symbolic slots encode it. |#
(define (equivalent-value? x y)
  (if (sequence-value? x)
      (let ([n (bitvector->natural (sequence-value-count x))])
        (&& (= n (bitvector->natural (sequence-value-count y)))
            (for/fold ([ok #t]) ([a (sequence-value-slots x)] [b (sequence-value-slots y)] [i (in-naturals)])
              (&& ok (=> (< i n) (equal? a b))))))
      (equal? x y)))

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

#| Check the projection against the public monitor for every legal combination of child steps.
   Each advance allocates independent abstract state, even when two sites use the same evidence. |#
(define (check-monitor-refinement! check! top layout bool sites monitor sample relation checks)
  (define-values (inputs outputs locals pin-pairs) (open-monitor-sites top layout bool sites check!))
  #| Child histories must be disjoint; neither declaration order nor equal field names can alias them. |#
  (define (join a b)
    (for/fold ([h a]) ([(k v) b])
      (when (hash-has-key? h k) (error 'transaction-cut "duplicate history: ~a" k))
      (hash-set h k v)))
  (define-values (domain before ok after next-ok child-initial)
    (for/fold ([domain #t] [before (hash)] [ok #t] [after (hash)] [next-ok #t] [init (hash)])
              ([(name site) sites])
      (match-define (list ci co) (hash-ref pin-pairs name))
      (define-values (legal old pre-ok next post-ok start)
        ((monitor-site-advance site) ci co inputs outputs))
      (values (&& domain legal) (join before old) (&& ok pre-ok) (join after next)
              (&& next-ok post-ok) (join init start))))
  (define local (hash-ref locals "@local" #f))
  (define local-old (if local (edge-state local) (hash)))
  (define local-next (if local (edge-next local) (hash)))
  (define reset (hash-ref inputs "reset"))
  (define-values (batch sampler-ok) (sample inputs outputs))
  (define pre (hint-edge relation before local-old))
  (define post (hint-edge relation after local-next))
  (define side (hint-edge checks before local-old inputs outputs batch))
  (check-monitor-edge! check! monitor batch reset domain pre post
                       (hint-edge relation child-initial local-next)
                       (list (cons 'sampler sampler-ok)) #:pre-ok ok #:post-ok next-ok #:side side))

#| Discharge the same monitor induction for a checked RTL cut or a certified cycle-spec edge.
   Only the old relation restricts active steps. Reset ignores it, and sampler definedness,
   auxiliary invariants and the successor relation are conclusions. |#
(define (check-monitor-edge! check! monitor batch reset domain pre post initial-projection obligations
                             #:pre-ok [ok #t] #:post-ok [next-ok #t] #:side [side #f])
  (define verdicts (hash-ref monitor "outputs"))
  (unless (and (hash-has-key? verdicts "allowed")
               (for/and ([field (in-hash-values verdicts)]) (same-schema? (second (second field)) '("bool"))))
    (error 'refinement "expected Boolean monitor verdicts including allowed"))
  (define old (symbolic-spec-state monitor))
  (define step (spec-edge-at monitor old batch))
  (define base (&& reset domain (spec-state-legal? old)))
  (define active (&& (! reset) domain (spec-state-legal? old) (related? pre ok old)))
  (define goals
    (append (for/list ([k (sort (hash-keys (edge-outputs step)) string<?)] #:unless (same-schema? k "allowed"))
              (cons k (holds (hash-ref (edge-outputs step) k))))
            (if side (append (for/list ([(k v) (edge-outputs side)]) (cons k (holds v)))
                             (list (cons 'checks-defined (edge-defined side)))) '())
            obligations
            (list (cons 'allowed (monitor-allowed? step))
                  (cons 'successor (related? post next-ok (edge-next step))))))
  (define initial (spec-initial-state monitor))
  (check! 'reset base
    (append (list (cons 'initial (related? post next-ok initial))
                  (cons 'declared-initial (related? initial-projection #t initial))) goals))
  (check! 'step active goals)
  (when side (for ([(k v) (edge-events side)]) (check! k (&& active (holds v)) '())))
  (void))
