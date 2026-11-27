#lang rosette
#| Certifies RSPECL transaction monitors from proved cycle specs or occurrence-indexed child contracts. |#
;; SPDX-License-Identifier: Apache-2.0

(require racket/match (only-in racket/base [eq? same?] [equal? same-schema?])
         "rtl-boundary.rhm" "../rhodium/formal/refinement.rkt" "../rhodium/formal/spec-engine.rkt"
         "../rhodium/formal/transaction-induction.rkt"
         (only-in "../rhodium/formal/fifo-view.rhm" fifo_behavior_view fifo_view_parts)
         (only-in "../rhodium/formal/spec.rhm" spec_snapshot input_premise_snapshot)
         (only-in "../rhodium/formal/port-types.rhm" leaf_layout)
         (only-in "../rhodium/frontend/layers/bool.rhm" Bool))
(provide prove-transaction derive-transaction prove-forwarded-transaction transaction-for? transaction-input-premises
         transaction-step transaction-cut sample-transaction)

;; Run-local evidence, constructed only at the end of the induction. Identity includes the
;; exact contract, not merely a family name or an equivalent-looking physical interface.
(struct seal (top layout view sampler monitor ins outs premises requirement))
(struct input-requirement (monitor sampler ins))

#| Event-only use cannot discharge a current-input predicate or a history requirement. |#
(define (unrestricted? s) (and (zero? (hash-count (seal-premises s))) (not (seal-requirement s))))

#| Match both the physical definition and the expected public specification. |#
(define (transaction-for? evidence top view)
  (and (seal? evidence) (unrestricted? evidence)
       (same? top (seal-top evidence)) (same? view (seal-view evidence))
       (same-schema? (leaf_layout top (interface_prefixes top)) (seal-layout evidence))))

#| Require the same contract when using a seal, including for a theorem-only consumer. |#
(define (check-view evidence view #:at-cut? [at-cut? #f])
  (unless (and (seal? evidence) (same? (seal-view evidence) view))
    (error 'transaction "expected certified evidence for this transaction view"))
  (unless (or at-cut? (unrestricted? evidence))
    (error 'transaction "conditional transaction evidence requires discharge on RTL input pins")))

#| Inspect the certified current-input predicates, without authorizing event-only use. |#
(define (transaction-input-premises evidence view)
  (check-view evidence view #:at-cut? #t)
  (seal-premises evidence))

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

#| Execute the sealed monitor; the caller may use the successor only under the returned verdict. |#
(define (transaction-step evidence view old batch)
  (check-view evidence view)
  (advance-monitor evidence old batch))

#| Internal use requires either an unconditional seal or an already discharged occurrence. |#
(define (advance-monitor evidence old batch)
  (define e (spec-edge-at (seal-monitor evidence) old batch))
  (values (monitor-allowed? e) (edge-next e)))

#| Bind the proved implementation at the actual parent occurrence before sampling its pins. |#
(define (transaction-cut top site evidence view)
  (check-view evidence view)
  (define layout (seal-layout evidence))
  (define-values (ins outs edges ci co)
    (symbolic-wrapper-cut top (leaf_layout top (interface_prefixes top)) Bool site
                          (seal-top evidence) layout (seal-outs evidence)))
  (values ins outs edges (sample-transaction evidence view ci co)))

#| Check the schema concretely and compare symbolic batch fields without equating sequence padding. |#
(define (batch-goals outside inside)
  (unless (same-schema? (sort (hash-keys outside) string<?) (sort (hash-keys inside) string<?))
    (error 'transaction "forwarded batch schema mismatch"))
  (for/list ([field (sort (hash-keys outside) string<?)])
    (cons field (equivalent-value? (hash-ref outside field) (hash-ref inside field)))))

#| Namespace the public state of each occurrence; a repeated definition does not share history. |#
(define (history-fields site state)
  (for/hash ([(k v) state]) (values (string-append "history." site "." k) v)))

#| Sample and advance the sealed monitor on its actual pins. For legacy forwarding wrappers
   verify the full re-export on arbitrary pins, before assuming any child verdict. |#
(define (sealed-site site expected child forward? [linked? #f])
  (check-view child expected #:at-cut? #t)
  (when (and (seal-requirement child) (not linked?))
    (error 'transaction "missing temporal supplier for ~a" site))
  #| Sample one independent edge; no cycle-level ready/valid or private RTL enters the cut. |#
  (define (advance ci co inputs outputs)
    (define inside (sample (seal-sampler child) (seal-ins child) (seal-outs child) ci co))
    (when forward?
      (define outside (sample (seal-sampler child) (seal-ins child) (seal-outs child) inputs outputs))
      (check-refinement-phase! 'child-wiring #t
                               (list (cons 'forwarded-batch (andmap cdr (batch-goals outside inside))))))
    (define model (seal-monitor child))
    (define old (symbolic-spec-state model))
    (define-values (domain next) (advance-monitor child old inside))
    (values domain (history-fields site old) #t (history-fields site next) #t
            (history-fields site (spec-initial-state model))))
  (if (zero? (hash-count (seal-premises child)))
      (monitor-site (seal-top child) (seal-layout child) (seal-outs child) advance)
      (guarded-site (seal-top child) (seal-layout child) (seal-outs child) advance (seal-premises child))))

#| Only these issuers supply the proof procedure. Always seal exactly the monitor and dense
   public sampler used in the successful proof, irrespective of where its evidence came from. |#
(define (certify-view top view prove! #:requirement [req #f])
  (define-values (_ model ins outs) (transaction_parts view))
  (define monitor (spec_snapshot model))
  (define sampler (spec_snapshot (transaction_batch_model view)))
  (unless (zero? (hash-count (hash-ref monitor "assumptions"))) (error 'refinement "conditional monitor"))
  (check-batch-total! sampler)
  (define layout (leaf_layout top (interface_prefixes top)))
  (define premises (prove! model monitor layout ins outs (lambda (i o) (values (sample sampler ins outs i o) #t))))
  (seal top layout view sampler monitor ins outs premises req))

#| Weaken cycle evidence without opening RTL; a general monitor seal retains all input premises.
   A FIFO event view infers its monitor's capacity from the designated cycle sequence. |#
(define derive-transaction
  (case-lambda
    [(cycle top spec)
     (define-values (view projection) (spec top #:cycle_derivation #t))
     (derive-view cycle top view projection)]
    [(cycle top view projection) (derive-view cycle top view projection)]))

#| Both authored and low-level projections enter the same induction before any evidence is issued. |#
(define (derive-view cycle top view projection)
  (cond
    [(fifo_behavior_view view)
     (define-values (expected _i _o _w _s) (fifo_view_parts view))
     (unless (same? top expected) (error 'cycle-view "expected the bound FIFO top"))
     (define relation
       (cond [(hash? projection)
              (unless (same-schema? (hash-keys projection) '("q")) (error 'cycle-view "expected a projection for q"))
              (hash-ref projection "q")]
             [else projection]))
     (derive-fifo-transactions cycle view relation)]
    [else
     (certify-view top view
       (lambda (model _monitor layout ins outs sample)
         (check-cycle-monitor! cycle top layout model projection ins outs sample #:retain-premises? #t)))]))

#| Admit only unconditional, stateless cycle evidence as a driver for each named RTL occurrence. |#
(define (combinational-sites drivers)
  (for/hash ([(site proof) (in-hash drivers)]) (values site (certified-combinational-site proof))))

#| Specialize the declaration on the actual top; consume only its checked current-input predicates. |#
(define (declared-premises top specification)
  (if specification (input_premise_snapshot (specification top)) (hash)))

#| Lift an identical public monitor across a wrapper. Discharge every input premise at the actual
   occurrence before using the child's theorem. Equal batches and identical initial/update rules
   imply identical histories by induction, without executing the child's cycle model. |#
(define (prove-forwarded-transaction top contract site child expected #:combinational [drivers (hash)]
                                     #:requires [required-view #f])
  ;; A bare view requests an unconditional proof. A bound declaration supplies exactly
  ;; its named input predicates; its cycle outputs, updates and relation are never used.
  (define-values (view premises)
    (if (procedure? contract)
        (values (contract top #:transaction_view #t) (declared-premises top contract))
        (values contract (hash))))
  (define req (and required-view (input-history-requirement required-view)))
  (define binding (sealed-site site expected child #f (and req #t)))
  (define child-req (seal-requirement child))
  (when (and req (not (and child-req (same-schema? (input-requirement-monitor req)
                                                   (input-requirement-monitor child-req)))))
    (error 'transaction "expected matching temporal input monitors for forwarding"))
  #| Identical input monitors start together. Equal batches on every open edge keep their
     histories equal, so the external assumption suffices without assuming a child verdict. |#
  (define (check-required-wiring external pins)
    (when req
      (check-refinement-phase! 'forward-requirement #t
        (batch-goals (sample-required req external) (sample-required child-req (first (hash-ref pins site)))))))
  (define comb (combinational-sites drivers))
  (certify-view top view
    (lambda (_model monitor layout _ins _outs sample-parent)
      (unless (same-schema? monitor (seal-monitor child)) (error 'transaction "expected identical public monitor"))
      (define-values (inputs outputs _edges pins allowed)
        (open-monitor-sites top layout Bool (hash site binding) check-refinement-phase!
                            #:combinational comb #:parent-premises premises #:check-links check-required-wiring))
      (match-define (list ci co) (hash-ref pins site))
      (define inside (sample (seal-sampler child) (seal-ins child) (seal-outs child) ci co))
      (define-values (outside ok) (sample-parent inputs outputs))
      (check-refinement-phase! 'forward allowed (cons (cons 'sampler ok) (batch-goals outside inside)))
      premises)
    #:requirement req))

#| Compile the total input sampler for the environment's independently initialized history. |#
(define (input-history-requirement view)
  (define-values (_ model ins outs) (transaction_parts view))
  (unless (null? outs) (error 'transaction "expected an input-only history requirement"))
  (define sampler (spec_snapshot (transaction_batch_model view)))
  (define monitor (spec_snapshot model))
  (unless (zero? (hash-count (hash-ref monitor "assumptions"))) (error 'transaction "conditional input monitor"))
  (check-batch-total! sampler)
  (input-requirement monitor sampler ins))

#| Read a requirement exclusively from pins driven into this occurrence. |#
(define (sample-required req inputs)
  (sample (input-requirement-sampler req) (input-requirement-ins req) '() inputs (hash)))

#| Step fresh environment state on actual consumer pins and supply its own declared reset state. |#
(define ((input-history-at req) inputs)
  (define monitor (input-requirement-monitor req))
  (define batch (sample-required req inputs))
  (values (spec-edge-at monitor (symbolic-spec-state monitor) batch) (spec-initial-state monitor)))

#| Match proof identities first. The returned checker proves equal batches on open pins,
   before either verdict is available. Identical monitors then have identical trace histories. |#
(define (temporal-link-checker links evidence)
  (for ([(sink source) links])
    (define s (hash-ref evidence sink #f))
    (define p (hash-ref evidence source #f))
    (unless (and (seal? s) (seal-requirement s) (seal? p) (unrestricted? p)
                 (same-schema? (seal-monitor p) (input-requirement-monitor (seal-requirement s))))
      (error 'transaction "expected unconditional matching temporal supplier for ~a" sink)))
  (lambda (_external pins)
    (for ([(sink source) links])
      (define r (seal-requirement (hash-ref evidence sink)))
      (define p (hash-ref evidence source))
      (match-define (list pi po) (hash-ref pins source))
      (define ci (first (hash-ref pins sink)))
      (check-refinement-phase! 'history-link #t
        (batch-goals (sample-required r ci)
                     (sample (seal-sampler p) (seal-ins p) (seal-outs p) pi po))))))

#| Bind existing transaction evidence to exact RTL sites, then dispatch only the public
   protocol adapter. All parent obligations run through the same monitor-cut induction. |#
(define (prove-transaction top view children #:child-site [implementation-site #f] #:combinational [drivers (hash)]
                           #:under [parent-spec #f] #:requires [required-view #f] #:suppliers [links (hash)])
  (cond
    [(fifo_behavior_view view)
     (when (or parent-spec required-view (positive? (hash-count links))) (error 'refinement "FIFO series requires an unconditional contract"))
     (unless (zero? (hash-count drivers)) (error 'refinement "FIFO series requires a direct handoff"))
     (define-values (expected _i _o _w _s) (fifo_view_parts view))
     (unless (and (same? top expected) (not implementation-site)) (error 'refinement "wrong FIFO parent or site"))
     (prove-fifo-series-refinement view children)]
    [else
     (define premises (declared-premises top parent-spec))
     (match-define (list sites cuts relation checks direct) (refinement_parts view top implementation-site))
     (define single? (= 1 (length sites)))
     (when (and implementation-site (not single?)) (error 'refinement "site override requires one child"))
     ;; The original single-seal calling convention asks for a forwarded batch proof.
     ;; An occurrence map always uses the authored parent sampler, even for one site.
     (define single-evidence? (and single? (not (hash? children))))
     (define evidence (if single-evidence? (hash (car sites) children) children))
     (unless (and (hash? evidence) (same-schema? (sort sites string<?) (sort (hash-keys evidence) string<?)))
       (error 'refinement "expected evidence for each declared occurrence"))
     (when (and required-view (or (pair? sites) (positive? (hash-count drivers)) parent-spec))
       (error 'transaction "expected an input-only history requirement on a leaf"))
     (define req (and required-view (input-history-requirement required-view)))
     (when (and implementation-site (positive? (hash-count links))) (error 'transaction "history links require named occurrences"))
     (define check-links (temporal-link-checker links evidence))
     (define bindings
       (for/hash ([site sites])
         (define cut (hash-ref cuts site))
         (define child (hash-ref evidence site))
         (values (or implementation-site site)
                 (if (string? cut) (fifo-monitor-site child cut (hash-ref (hash-ref relation "inputs") cut))
                     (sealed-site site cut child single-evidence? (hash-has-key? links site))))))
     (certify-view top view
       (lambda (_model monitor layout _ins _outs sample-parent)
         (check-monitor-refinement! check-refinement-phase! top layout Bool bindings monitor sample-parent relation checks
                                    #:combinational (combinational-sites drivers) #:parent-premises premises
                                    #:check-links check-links #:direct-wires direct
                                    #:input-history (and req (input-history-at req)))
         premises)
       #:requirement req)]))
