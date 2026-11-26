#lang rosette
#| Checks reset and step refinement across independent transaction monitors on actual parent RTL wiring. |#
;; SPDX-License-Identifier: Apache-2.0

(require racket/match (only-in racket/base [equal? same-schema?] [eq? same?])
         "spec-engine.rkt" (only-in "wiring.rhm" wrapper_wiring leaf_protocol))
(provide (struct-out monitor-site) (struct-out guarded-site) (struct-out combinational-site)
         open-monitor-sites check-monitor-refinement! check-monitor-edge! monitor-allowed? equivalent-value?
         (rename-out [related? projection-related?]))

;; A private proof strategy supplies these adapters from already checked evidence.
;; Neither a descriptor nor successful use of this low-level checker is reusable evidence.
(struct monitor-site (definition layout visible advance))
(struct guarded-site monitor-site (premises))
(struct combinational-site (definition layout model))

#| Open the parent only. An arbitrary physical output at each occurrence is one shared
   net for all its RTL sinks. Return per-site pin pairs, not any child implementation state. |#
(define (open-monitor-sites top layout bool sites check! #:combinational [comb (hash)] #:parent-premises [premises (hash)]
                            #:check-links [check-links void] #:direct-wires [direct #f])
  #| Reject a same-shaped substitute before interpreting the recorded port layout. |#
  (define (layout-at name definition)
    (define actor (hash-ref comb name #f))
    (define site (hash-ref sites name #f))
    (unless (cond [actor (same? definition (combinational-site-definition actor))]
                  [site (same? definition (monitor-site-definition site))]
                  [else #f])
      (error 'transaction-cut "transaction evidence does not match the RTL occurrence: ~a" name))
    (if actor (combinational-site-layout actor) (monitor-site-layout site)))
  (define graph (wrapper_wiring top layout
                  (for/hash ([(k s) sites]) (values k (monitor-site-visible s))) layout-at bool
                  (for/hash ([(k actor) comb]) (values k (combinational-site-model actor)))
                  (for/hash ([(k s) sites] #:when (same-schema? (leaf_protocol (monitor-site-definition s)) "combinational"))
                    (values k #t)) direct))
  (define locals (hash-ref graph "children"))
  (define states (for/hash ([(k model) locals]) (values k (symbolic-spec-state model))))
  (define inputs (fresh-fields (hash-ref graph "inputs")))
  (define-values (outputs edges settled) (wired-edge-at graph locals states inputs))
  (define local-ok (for/fold ([ok settled]) ([e (in-hash-values edges)])
                     (&& ok (edge-legal e) (edge-defined e) (spec-state-legal? (edge-next e)))))
  #| Split qualified graph endpoints only after the layout reader has rejected name collisions. |#
  (define (side pins name)
    (for/hash ([(k v) pins]
               #:when (if name (string-prefix? k (string-append name "."))
                          (not (for/or ([s (hash-keys sites)]) (string-prefix? k (string-append s "."))))))
      (values (if name (string->immutable-string (substring k (add1 (string-length name)))) k) v)))
  (define external (side inputs #f))
  (define allowed (andmap values (hash-values (spec-input-assumptions (hash "assumptions" premises) external))))
  (unless (zero? (hash-count premises))
    (define reset (hash-ref external "reset"))
    (check! 'parent-reset (&& allowed reset) '())
    (check! 'parent-active (&& allowed (! reset)) '()))
  (check! 'wrapper-rtl allowed (list (cons 'defined local-ok)))
  (define pins (for/hash ([k (hash-keys sites)]) (values k (list (side outputs k) (side inputs k)))))
  (check-links external pins)
  (admit-monitor-inputs! sites pins external (side outputs #f) allowed check!)
  (values external (side outputs #f) edges pins allowed))

#| Each open child output is a fresh physical net. Follow the actual output symbols in a
   wired premise (including through local logic); only an admitted source can constrain it. |#
(define (admit-monitor-inputs! sites pins inputs outputs allowed check!)
  (define owners (make-hasheq))
  (for ([(k pair) pins])
    (for ([net (in-hash-values (second pair))]) (hash-set! owners net k)))
  (define status (make-hash))
  (define verdicts (make-hash))
  #| Use only the source's public step, with arbitrary old history and the same physical pins. |#
  (define (source-verdict k)
    (admit! k)
    (hash-ref! verdicts k
      (lambda ()
        (match-define (list ci co) (hash-ref pins k))
        (define-values (legal _old _pre _next _post _init)
          ((monitor-site-advance (hash-ref sites k)) ci co inputs outputs))
        legal)))
  #| Check a site's premises before releasing its verdict; never assume an SCC jointly. |#
  (define (admit! k)
    (case (hash-ref status k #f)
      [(done) (void)]
      [(visiting) (error 'child-input "circular same-edge transaction premise at ~a" k)]
      [else
       (hash-set! status k 'visiting)
       (define site (hash-ref sites k))
       (when (guarded-site? site)
         (define ci (first (hash-ref pins k)))
         (define goals (spec-input-assumptions (hash "assumptions" (guarded-site-premises site)) ci))
         (for ([name (sort (hash-keys goals) string<?)])
           (define goal (hash-ref goals name))
           (define sources (remove-duplicates
                            (for/list ([net (symbolics goal)] #:when (hash-has-key? owners net)) (hash-ref owners net))))
           (define prior (map source-verdict sources))
           (define witness (for*/fold ([w inputs]) ([s sources] [(pin v) (second (hash-ref pins s))])
                             (hash-set w (format "~a.~a" s pin) v)))
           (check! 'child-input (&& allowed (andmap values prior)) (list (cons (format "~a/~a" k name) goal))
                   #:inputs (for/fold ([w witness]) ([(pin v) ci]) (hash-set w (format "~a.~a" k pin) v)))))
       (hash-set! status k 'done)]))
  (for-each admit! (sort (hash-keys sites) string<?)))

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

#| Bind hints to RTL, child history, or the leaf's private input-monitor state, never a child's requirement. |#
(define (hint-edge snapshot history regs [inputs #f] [outputs #f] [batch #f] #:required [env #f])
  (define args
    (for/hash ([key (hash-keys (hash-ref snapshot "inputs"))])
      (values key
        (if (hash-has-key? history key) (hash-ref history key)
            (match (regexp-match #px"^([^.]+)[.](.+)$" key)
              [(list _ prefix name)
               (define source (match prefix ["local" regs] ["required" env] ["input" inputs] ["output" outputs] ["batch" batch]))
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
(define (check-monitor-refinement! check! top layout bool sites monitor sample relation checks #:combinational [comb (hash)]
                                   #:parent-premises [premises (hash)] #:check-links [check-links void]
                                   #:input-history [input-history #f] #:direct-wires [direct #f])
  (define-values (inputs outputs locals pin-pairs allowed)
    (open-monitor-sites top layout bool sites check! #:combinational comb #:parent-premises premises
                        #:check-links check-links #:direct-wires direct))
  #| Child histories must be disjoint; neither declaration order nor equal field names can alias them. |#
  (define (join a b)
    (for/fold ([h a]) ([(k v) b])
      (when (hash-has-key? h k) (error 'transaction-cut "duplicate history: ~a" k))
      (hash-set h k v)))
  (define-values (domain before ok after next-ok child-initial)
    (for/fold ([domain allowed] [before (hash)] [ok #t] [after (hash)] [next-ok #t] [init (hash)])
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
  (define-values (env env-initial) (if input-history (input-history inputs) (values #f #f)))
  (define env-old (and env (edge-state env)))
  (define pre (hint-edge relation before local-old #:required env-old))
  (define post (hint-edge relation after local-next #:required (and env (edge-next env))))
  (define side (hint-edge checks before local-old inputs outputs batch #:required env-old))
  (check-monitor-edge! check! monitor batch reset domain pre post
                       (hint-edge relation child-initial local-next #:required env-initial)
                       (list (cons 'sampler sampler-ok)) #:pre-ok ok #:post-ok next-ok #:side side
                       #:require-edge env #:require-initial env-initial))

#| Start reset from arbitrary legal, independent histories. Only active edges may assume the
   old relation and input verdict; the post-edge relation is a conclusion in both phases. |#
(define (check-monitor-edge! check! monitor batch reset domain pre post initial-projection obligations
                             #:pre-ok [ok #t] #:post-ok [next-ok #t] #:side [side #f]
                             #:require-edge [env #f] #:require-initial [env-initial #f])
  (define verdicts (hash-ref monitor "outputs"))
  (unless (and (hash-has-key? verdicts "allowed")
               (for/and ([field (in-hash-values verdicts)]) (same-schema? (second (second field)) '("bool"))))
    (error 'refinement "expected Boolean monitor verdicts including allowed"))
  (define old (symbolic-spec-state monitor))
  (define step (spec-edge-at monitor old batch))
  (define admitted (if env (monitor-allowed? env) #t))
  (define legal (&& domain (spec-state-legal? old) (if env (spec-state-legal? (edge-state env)) #t)))
  (define base (&& reset legal))
  (define active (&& (! reset) legal admitted (related? pre ok old)))
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
    (append (if env
                (list (cons 'reset-admitted admitted)
                      (cons 'history-reset (for/fold ([ok #t]) ([(k v) env-initial])
                                             (&& ok (equivalent-value? v (hash-ref (edge-next env) k)))))) '())
            (list (cons 'initial (related? post next-ok initial))
                  (cons 'declared-initial (related? initial-projection #t initial))) goals))
  (check! 'step active goals)
  (when side (for ([(k v) (edge-events side)]) (check! k (&& active (holds v)) '())))
  (void))
