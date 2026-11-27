#lang rosette
#| Proves specialized RSPECL leaves against RTL and composes their seals at direct child occurrences. |#
;; SPDX-License-Identifier: Apache-2.0

(require racket/match "proof.rkt" "rtl-boundary.rhm"
         (only-in "../rhodium/formal/refinement.rkt" prove-leaf)
         (only-in "../rhodium/formal/transaction-induction.rkt" projection-related?)
         (only-in "../rhodium/formal/spec.rhm" spec_snapshot)
         "../rhodium/formal/spec-engine.rkt")
(provide prove-design (rename-out [prove-design prove_design]) child_definition)

#| Logical Booleans may arrive as one physical bit; never truncate a wider pin or register. |#
(define (typed sort v)
  (match sort
    [(list "bool") (cond [(boolean? v) v] [((bitvector 1) v) (bveq v (bv 1 1))]
                         [else (error 'prove-design "expected Boolean pin or register")])]
    [(list "bits" w) (unless ((bitvector w) v) (error 'prove-design "expected ~a physical bits" w)) v]))

#| Evaluate stateless hint IR on exactly its declared hardware sources. |#
(define (sample model lookup)
  (spec-edge-at model (hash)
    (for/hash ([(k sort) (hash-ref model "inputs")])
      (values k (typed sort (lookup k))))))

#| Certify the cycle observations against physical RTL independently of any chosen transaction monitor. |#
(define (prove-rtl-cycle top root cycle view hints)
  (match-define (list relation physical) hints)
  (define model (spec_snapshot cycle))
  (define-values (sampler _monitor _ins _outs) (transaction_parts view))
  (define events (spec_snapshot sampler))
  #| Register names refer to this elaboration, not a generated instance path in a test. |#
  (define (related registers abstract)
    (projection-related?
     (sample relation (lambda (k) (hash-ref registers (string-append root "." k)))) #t abstract))
  #| Compute care and event payloads independently of expected values, on the pre-edge RTL sample. |#
  (define (observe ports inputs)
    #| Both maps describe the same edge; pin names are disjoint across directions. |#
    (define (pin k) (hash-ref inputs k (lambda () (hash-ref ports k))))
    (define actual (sample physical pin))
    ;; An explicit physical event map is complete. Otherwise reuse the contract's pin sampler.
    (define transfers (if (zero? (hash-count (edge-events actual))) (sample events pin) actual))
    (define outs
      (for/hash ([(k field) (hash-ref model "outputs")])
        (define mask (hash-ref (edge-outputs actual) k))
        (values k (observation (observation-value mask) (typed (second (second field)) (hash-ref ports k))))))
    (values outs (for/hash ([k (hash-keys (hash-ref model "events"))])
                   (values k (hash-ref (edge-events transfers) k)))
            (&& (edge-defined actual) (edge-defined transfers))))
  (prove-leaf top cycle interface_prefixes observe #:relation (and relation related)))

(define automatic-children (gensym 'automatic-children))

#| Prove the top using either an exact map of immediate child seals or recursive certification.
   Each cut still checks its RTL occurrences and allocates independent histories. |#
(define (prove-design top specification #:children [supplied automatic-children])
  (unless (or (eq? supplied automatic-children) (hash? supplied))
    (raise-argument-error 'prove-design "occurrence-to-contract hash for #:children" supplied))
  (define proved (make-hasheq))
  #| Dispatch on checked proof data, never the circuit name, depth, or endpoint protocol. |#
  (define (visit definition spec [children automatic-children])
    #| In supplied mode even an empty map is authoritative: no missing site is proved on demand. |#
    (define (check-sites sites)
      (unless (or (eq? children automatic-children)
                  (and (= (length sites) (hash-count children))
                       (andmap (lambda (site) (hash-has-key? children site)) sites)))
        (error 'prove-design "#:children expected occurrences ~a; received ~a" sites (hash-keys children))))
    #| Issuers below validate the seal's view, physical specialization and premises in both modes. |#
    (define (child-seal site child child-spec)
      (if (eq? children automatic-children) (visit child child-spec) (hash-ref children site)))
    (hash-ref! (hash-ref! proved definition make-hasheq) spec
      (lambda ()
        (match (spec definition #:proof_plan #t)
          [(list "leaf" root cycle view projection hints)
           (check-sites '())
           (derive-transaction (prove-rtl-cycle definition root cycle view hints) definition view projection)]
          [(list "adjacent" view cycle-spec)
           (check-sites '())
           (match (cycle-spec definition #:proof_plan #t)
             [(list "leaf" root cycle base _ hints)
              (derive-transaction (prove-rtl-cycle definition root cycle base hints) definition view 'adjacent-samples)]
             [_ (error 'prove-design "adjacent samples require a proved RTL cycle leaf")])]
          [(list "forward" contract site child-spec requirement)
           (check-sites (list site))
           (define child (child_definition definition site))
           (prove-forwarded-transaction definition contract site (child-seal site child child-spec)
                                        (child-spec child #:transaction_view #t) #:requires requirement)]
          [(list "parent" view bindings requirement suppliers domain)
           (check-sites (hash-keys bindings))
           (prove-transaction definition view
             (for/hash ([(site binding) bindings])
               (match-define (list child child-spec) binding)
               (values site (child-seal site child child-spec))) #:requires requirement #:suppliers suppliers #:under domain)]))))
  (visit top specification supplied))
