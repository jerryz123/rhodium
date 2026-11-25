#lang rosette
#| Certifies RSPECL transaction monitors from proved cycle specs or occurrence-indexed child contracts. |#
;; SPDX-License-Identifier: Apache-2.0

(require racket/match (only-in racket/base [eq? same?] [equal? same-schema?])
         "rtl-boundary.rhm" "../rhodium/formal/refinement.rkt" "../rhodium/formal/spec-engine.rkt"
         "../rhodium/formal/transaction-induction.rkt"
         (only-in "../rhodium/formal/fifo-view.rhm" fifo_behavior_view fifo_view_parts)
         (only-in "../rhodium/formal/spec.rhm" spec_snapshot)
         (only-in "../rhodium/formal/port-types.rhm" leaf_layout)
         (only-in "../rhodium/frontend/layers/bool.rhm" Bool))
(provide prove-transaction derive-transaction transaction-for? transaction-step transaction-cut sample-transaction)

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

#| Execute the sealed monitor; the caller may use the successor only under the returned verdict. |#
(define (transaction-step evidence view old batch)
  (check-view evidence view)
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

#| Namespace the public state of each occurrence; a repeated definition does not share history. |#
(define (history-fields site state)
  (for/hash ([(k v) state]) (values (string-append "history." site "." k) v)))

#| Sample and advance the sealed monitor on its actual pins. For legacy forwarding wrappers
   verify the full re-export on arbitrary pins, before assuming any child verdict. |#
(define (sealed-site site expected child forward?)
  (check-view child expected)
  #| Sample one independent edge; no cycle-level ready/valid or private RTL enters the cut. |#
  (define (advance ci co inputs outputs)
    (define inside (sample-transaction child expected ci co))
    (when forward?
      (define outside (sample-transaction child expected inputs outputs))
      (check-refinement-phase! 'child-wiring #t (list (cons 'forwarded-batch (equal? inside outside)))))
    (define model (seal-monitor child))
    (define old (symbolic-spec-state model))
    (define-values (domain next) (transaction-step child expected old inside))
    (values domain (history-fields site old) #t (history-fields site next) #t
            (history-fields site (spec-initial-state model))))
  (monitor-site (seal-top child) (seal-layout child) (seal-outs child) advance))

#| Only these issuers supply the proof procedure. Always seal exactly the monitor and dense
   public sampler used in the successful proof, irrespective of where its evidence came from. |#
(define (certify-view top view prove!)
  (define-values (_ model ins outs) (transaction_parts view))
  (define monitor (spec_snapshot model))
  (define sampler (spec_snapshot (transaction_batch_model view)))
  (unless (zero? (hash-count (hash-ref monitor "assumptions"))) (error 'refinement "conditional monitor"))
  (check-batch-total! sampler)
  (define layout (leaf_layout top (interface_prefixes top)))
  (prove! model monitor layout ins outs (lambda (i o) (values (sample sampler ins outs i o) #t)))
  (seal top layout view sampler monitor ins outs))

#| Weaken unconditional cycle evidence to a public transaction view without opening RTL.
   A FIFO event view infers its monitor's capacity from the designated cycle sequence. |#
(define (derive-transaction cycle top view projection)
  (cond
    [(fifo_behavior_view view)
     (define-values (expected _i _o _w _s) (fifo_view_parts view))
     (unless (and (same? top expected) (hash? projection) (same-schema? (hash-keys projection) '("q")))
       (error 'cycle-view "expected the bound FIFO top and a projection for q"))
     (derive-fifo-transactions cycle view (hash-ref projection "q"))]
    [else
     (certify-view top view
       (lambda (model _monitor layout ins outs sample)
         (check-cycle-monitor! cycle top layout model projection ins outs sample)))]))

#| Bind existing transaction evidence to exact RTL sites, then dispatch only the public
   protocol adapter. All parent obligations run through the same monitor-cut induction. |#
(define (prove-transaction top view children #:child-site [implementation-site #f])
  (cond
    [(fifo_behavior_view view)
     (define-values (expected _i _o _w _s) (fifo_view_parts view))
     (unless (and (same? top expected) (not implementation-site)) (error 'refinement "wrong FIFO parent or site"))
     (prove-fifo-series-refinement view children)]
    [else
     (match-define (list sites cuts relation checks) (refinement_parts view))
     (define single? (= 1 (length sites)))
     (when (and implementation-site (not single?)) (error 'refinement "site override requires one child"))
     ;; The original single-seal calling convention asks for a forwarded batch proof.
     ;; An occurrence map always uses the authored parent sampler, even for one site.
     (define single-evidence? (and single? (not (hash? children))))
     (define evidence (if single-evidence? (hash (car sites) children) children))
     (unless (and (hash? evidence) (same-schema? (sort sites string<?) (sort (hash-keys evidence) string<?)))
       (error 'refinement "expected evidence for each declared occurrence"))
     (define bindings
       (for/hash ([site sites])
         (define cut (hash-ref cuts site))
         (define child (hash-ref evidence site))
         (values (or implementation-site site)
                 (if (string? cut) (fifo-monitor-site child cut (hash-ref (hash-ref relation "inputs") cut))
                     (sealed-site site cut child single-evidence?)))))
     (certify-view top view
       (lambda (_model monitor layout _ins _outs sample)
         (check-monitor-refinement! check-refinement-phase! top layout Bool bindings monitor sample relation checks)))]))
