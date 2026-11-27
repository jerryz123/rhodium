#lang rosette
#| Proves specialized RSPECL leaves against RTL and composes their seals at direct child occurrences. |#
;; SPDX-License-Identifier: Apache-2.0

(require racket/match "proof.rkt" "rtl-boundary.rhm"
         (only-in "../rhodium/formal/refinement.rkt" prove-leaf)
         (only-in "../rhodium/formal/transaction-induction.rkt" projection-related?)
         (only-in "../rhodium/formal/spec.rhm" spec_snapshot)
         "../rhodium/formal/spec-engine.rkt")
(provide prove-design (rename-out [prove-design prove_design]))

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

#| Check physical observations, using a state relation only for clocked RTL, then derive the monitor. |#
(define (prove-rtl-leaf top root cycle view projection hints)
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
  (define certificate (prove-leaf top cycle interface_prefixes observe #:relation (and relation related)))
  (derive-transaction certificate top view projection))

#| Resolve declared specs on actual RTL definitions. Share successful proofs by identity
   only within this call; the induction still allocates independent histories per occurrence. |#
(define (prove-design top specification)
  (define proved (make-hasheq))
  #| Dispatch on checked proof data, never the circuit name, depth, or endpoint protocol. |#
  (define (visit definition spec)
    (hash-ref! (hash-ref! proved definition make-hasheq) spec
      (lambda ()
        (match (spec definition #:proof_plan #t)
          [(list "leaf" root cycle view projection hints)
           (prove-rtl-leaf definition root cycle view projection hints)]
          [(list "forward" view site child-spec)
           (define child (child_definition definition site))
           (prove-forwarded-transaction definition view site (visit child child-spec)
                                        (child-spec child #:transaction_view #t))]
          [(list "parent" view children)
           (prove-transaction definition view
             (for/hash ([(site binding) children])
               (match-define (list child child-spec) binding)
               (values site (visit child child-spec))))]))))
  (visit top specification))
