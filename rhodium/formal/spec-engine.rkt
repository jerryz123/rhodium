#lang rosette
#| Interprets checked spec snapshots as symbolic one-edge functions with explicit domain obligations. |#
;; SPDX-License-Identifier: Apache-2.0

(require racket/match)
(provide symbolic-spec-edge symbolic-spec-state spec-state-legal? spec-edge-at spec-offers-at wired-edge-at
         spec-input-assumptions discharge-wired-inputs!
         (struct-out edge) (struct-out sequence-value) (struct-out observation))

;; Only the prefix of slots below count denotes a value; unused slots are unconstrained.
(struct sequence-value (count slots) #:transparent)
(struct observation (care value) #:transparent)
(struct result (value defined) #:transparent)
(struct edge (state inputs legal outputs events next defined) #:transparent)

#| Construct a fresh symbolic scalar, or a fixed bank of cells and a finite occupation count. |#
(define (fresh type)
  (match type
    [(list "bool") (define-symbolic* b boolean?) b]
    [(list "bits" width) (define-symbolic* x (bitvector width)) x]
    [(list "seq" cap element)
     (sequence-value (fresh (list "bits" (integer-length cap)))
                     (for/list ([i (in-range cap)]) (fresh element)))]))

#| Convert a host natural to the width of a bounded sequence's count. |#
(define (count-bv n q) (bv n (type-of (sequence-value-count q))))

#| Reject only invalid count encodings, never constrain inactive payload slots. |#
(define (legal-value? v)
  (or (not (sequence-value? v))
      (bvule (sequence-value-count v) (count-bv (length (sequence-value-slots v)) v))))

#| Test only the representation of an old or next state, without assuming anything about wired inputs. |#
(define (spec-state-legal? state) (andmap legal-value? (hash-values state)))

#| Encode a checked literal; zero padding has no observable meaning. |#
(define (literal type data)
  (match type
    [(list "bool") data]
    [(list "bits" width) (bv data width)]
    [(list "seq" cap element)
     (sequence-value (bv (length data) (integer-length cap))
                     (for/list ([i (in-range cap)])
                       (literal element (if (< i (length data)) (list-ref data i)
                                            (if (equal? element '("bool")) #f 0)))))]))

#| Evaluate a node to a total solver term plus the condition under which that term denotes a value. |#
(define (interpret expr state inputs)
  (match-define (list* op type data children) expr)
  (define args (for/list ([child children]) (interpret child state inputs)))
  (define vals (map result-value args))
  (define all-defined (andmap result-defined args))
  (match (cons op vals)
    [(list "state") (result (hash-ref state data) #t)]
    [(list "input") (result (hash-ref inputs data) #t)]
    [(list "literal") (result (literal type data) #t)]
    [(list "bits-equal" p q) (result (bveq p q) all-defined)]
    [(list "not" p) (result (! p) all-defined)]
    [(list "and" p q)
     (result (&& p q) (&& (result-defined (first args)) (=> p (result-defined (second args)))))]
    [(list "if" p t f)
     (result (if p t f) (&& (result-defined (first args))
                            (if p (result-defined (second args)) (result-defined (third args)))))]
    [(list* (or "empty" "full" "front" "tail" "append") q rest)
     (match-define (sequence-value n slots) q)
     (define empty (bveq n (count-bv 0 q)))
     (define full (bveq n (count-bv (length slots) q)))
     (match op
       ["empty" (result empty all-defined)]
       ["full" (result full all-defined)]
       ["front" (result (car slots) (&& all-defined (! empty)))]
       ["tail" (result (sequence-value (bvsub n (count-bv 1 q)) (append (cdr slots) (list (last slots))))
                       (&& all-defined (! empty)))]
       ["append" (result (sequence-value (bvadd n (count-bv 1 q))
                                         (for/list ([old slots] [i (in-naturals)])
                                           (if (bveq n (count-bv i q)) (car rest) old)))
                         (&& all-defined (! full)))])]))

#| Allocate independent terms for every field of a specialized schema. |#
(define (fresh-fields schema)
  (for/hash ([(name type) schema]) (values name (fresh type))))

#| Allocate a distinct abstract state for one occurrence, not an implicit initial state. |#
(define (symbolic-spec-state snapshot) (fresh-fields (hash-ref snapshot "state")))

#| Check solver sorts and shapes without assuming any domain predicate on symbolic counts. |#
(define (fits? type v)
  (match type
    [(list "bool") (boolean? v)]
    [(list "bits" width) (equal? (type-of v) (bitvector width))]
    [(list "seq" cap elem)
     (and (sequence-value? v) (fits? (list "bits" (integer-length cap)) (sequence-value-count v))
          (= cap (length (sequence-value-slots v)))
          (andmap (lambda (slot) (fits? elem slot)) (sequence-value-slots v)))]))

#| Reject missing, extra, or mistyped pins; only offer evaluation permits partial input bindings. |#
(define (check-fields schema bindings [partial? #f])
  (unless (and (or partial? (= (hash-count schema) (hash-count bindings)))
               (for/and ([(k v) bindings]) (and (hash-has-key? schema k) (fits? (hash-ref schema k) v))))
    (error 'spec-binding "incomplete or mistyped spec binding")))

#| Compute care, value and definedness from the same pre-edge environment. |#
(define (view fields state inputs)
  (for/fold ([obs (hash)] [ok #t]) ([(name pair) fields])
    (define care (interpret (first pair) state inputs))
    (define payload (interpret (second pair) state inputs))
    (values (hash-set obs name (observation (result-value care) (result-value payload)))
            (&& ok (result-defined care) (=> (result-value care) (result-defined payload))))))

#| Resolve cycle offers before wiring the full sample. An output referencing an unavailable
   input fails closed rather than imposing a same-edge fixed-point assumption. |#
(define (spec-offers-at snapshot state available)
  (check-fields (hash-ref snapshot "state") state)
  (check-fields (hash-ref snapshot "inputs") available #t)
  (view (hash-ref snapshot "outputs") state available))

#| Settle one scheduled output with its care and payload dependencies. An uncared
   wire is a fresh physical value, shared by all consumers of this evaluation. |#
(define (spec-pin-at snapshot port state available)
  (check-fields (hash-ref snapshot "state") state)
  (check-fields (hash-ref snapshot "inputs") available #t)
  (define field (hash-ref (hash-ref snapshot "outputs") port))
  (define-values (obs ok) (view (hash port field) state available))
  (define offer (hash-ref obs port))
  (values (if (observation-care offer) (observation-value offer) (fresh (second (second field)))) ok))

#| Step explicitly wired old state and inputs, including previously produced successor terms. |#
(define (spec-edge-at snapshot state inputs)
  (check-fields (hash-ref snapshot "state") state)
  (check-fields (hash-ref snapshot "inputs") inputs)
  #| Every observer and update reads the same pre-edge bindings. |#
  (define (run expr) (interpret expr state inputs))
  (define-values (outputs out-ok) (view (hash-ref snapshot "outputs") state inputs))
  (define-values (events event-ok) (view (hash-ref snapshot "events") state inputs))
  (define-values (next next-ok)
    (for/fold ([after state] [ok #t]) ([(name expr) (hash-ref snapshot "updates")])
      (define rhs (run expr))
      (values (hash-set after name (result-value rhs)) (&& ok (result-defined rhs)))))
  (edge state inputs (andmap legal-value? (append (hash-values state) (hash-values inputs)))
        outputs events next (&& out-ok event-ok next-ok)))

#| Build a one-edge formula on independent old state and inputs, without asserting any obligation. |#
(define (symbolic-spec-edge snapshot)
  (spec-edge-at snapshot (symbolic-spec-state snapshot) (fresh-fields (hash-ref snapshot "inputs"))))

#| Evaluate named scalar input predicates without asserting them in the ambient VC. |#
(define (spec-input-assumptions model inputs)
  (for/hash ([(name expr) (hash-ref model "assumptions")])
    (define pred (interpret expr (hash) inputs))
    (values name (&& (result-defined pred) (result-value pred)))))

#| Read only requested physical nets, sharing each offer (including uncared values) across fanout.
   The proof reader checks the source site's contract before consulting its output semantics. |#
(define (make-wire-reader graph children states inputs before-offer)
  (check-fields (hash-ref graph "inputs") inputs)
  (define expected (hash-ref graph "children"))
  (unless (and (= (hash-count children) (hash-count expected))
               (for/and ([(site model) children]) (equal? model (hash-ref expected site #f)))
               (equal? (sort (hash-keys children) string<?) (sort (hash-keys states) string<?)))
    (error 'wired-edge-at "child models or occurrence states do not match graph"))
  (define drivers (hash-ref graph "wires"))
  #| Qualify names with immutable strings for the cross-language snapshot maps. |#
  (define (pin-name site port) (string->immutable-string (string-append site "." port)))
  #| Read just the inputs on which the scheduled output or complete edge depends. |#
  (define (bind site ports)
    (for/hash ([p ports]) (values p (wire (pin-name site p)))))
  (define offers (for/hash ([row (hash-ref graph "order")])
                   (values (pin-name (first row) (second row)) row)))
  (define nets (hash-copy inputs))
  (define defined #t)
  #| Read the physical driver of this sink. |#
  (define (wire sink) (offer (hash-ref drivers sink)))
  #| Evaluate a source once, following the checked care and value dependencies of its offer. |#
  (define (offer source)
    (hash-ref! nets source
               (lambda ()
                 (match-define (list site port deps) (hash-ref offers source))
                 (before-offer site)
                 (define-values (v safe)
                   (spec-pin-at (hash-ref children site) port (hash-ref states site) (bind site deps)))
                 (set! defined (&& defined safe))
                 v)))
  #| A complete edge includes the definedness of unused output offers as well. |#
  (define (settle-all!)
    (for ([row (hash-ref graph "order")]) (offer (pin-name (first row) (second row)))))
  (values wire bind (lambda () defined) settle-all!))

#| Recover all current input reads of a checked predicate, including reads inside conditional branches. |#
(define (predicate-ports expr)
  (match-define (list* op _ datum children) expr)
  (if (equal? op "input") (list datum) (append-map predicate-ports children)))

#| Discharge each occurrence on the same unrestricted sample. Demand upstream proofs first;
   a site becomes usable only after the checker returns successfully for all its predicates. |#
(define (discharge-wired-inputs! graph children check!)
  (define inputs (fresh-fields (hash-ref graph "inputs")))
  (define states (for/hash ([(site child) children]) (values site (symbolic-spec-state child))))
  (define legal (andmap spec-state-legal? (hash-values states)))
  (define status (make-hash))
  #| Visiting an active occurrence means its own guarantee is needed before its premises hold. |#
  (define (admit! site)
    (case (hash-ref status site #f)
      [(done) (void)]
      [(visiting) (error 'child-input "circular same-edge input contract at ~a" site)]
      [else
       (define model (hash-ref children site))
       (define predicates (hash-ref model "assumptions"))
       (unless (zero? (hash-count predicates))
         (hash-set! status site 'visiting)
         (define ports (remove-duplicates (append-map predicate-ports (hash-values predicates))))
         (define goals (spec-input-assumptions model (bind site ports)))
         (check! inputs legal
                 (cons (cons 'upstream-defined (defined?))
                       (for/list ([name (sort (hash-keys goals) string<?)])
                         (cons (format "~a/~a" site name) (hash-ref goals name))))))
       (hash-set! status site 'done)]))
  (define-values (_ bind defined? _all) (make-wire-reader graph children states inputs admit!))
  (for-each admit! (sort (hash-keys children) string<?)))

#| Execute the graph on matching snapshots. This low-level step is not a proof of its input contracts. |#
(define (wired-edge-at graph children states inputs)
  (define-values (wire bind defined? settle-all!) (make-wire-reader graph children states inputs void))
  (settle-all!)
  (values (for/hash ([p (hash-ref graph "outputs")]) (values p (wire p)))
          (for/hash ([(site child) children])
            (values site (spec-edge-at child (hash-ref states site) (bind site (hash-keys (hash-ref child "inputs"))))))
          (defined?)))
