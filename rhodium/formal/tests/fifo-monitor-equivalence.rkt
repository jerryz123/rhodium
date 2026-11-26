#lang rosette
;; SPDX-License-Identifier: Apache-2.0

(require rackunit "../spec-engine.rkt" "composition-checks.rkt"
         (only-in "../transaction-induction.rkt" equivalent-value?)
         (only-in "../spec.rhm" spec_snapshot)
         (only-in "../../../rspecl/rtl-boundary.rhm" transaction_parts transaction_batch_model)
         (prefix-in old: "../fifo-monitor.rhm") "fifo-monitor-fixture.rspl")

(for ([capacity '(2 4 6)])
  (test-case (format "RSPECL FIFO vs reference, capacity ~a, two-bit words" capacity)
    (define view (fifo_view capacity))
    (define-values (_ model _i _o) (transaction_parts view))
    (define candidate (spec_snapshot model))
    (define reference (spec_snapshot (old:fifo_monitor capacity 2)))
    (define (q s) (hash-ref s "q"))
    (for ([schema '("inputs" "state")])
      (check-equal? (hash-ref candidate schema) (hash-ref reference schema)))
    (check-true (equivalent-value? (q (spec-initial-state reference)) (q (spec-initial-state candidate))))

    (define a (symbolic-spec-edge reference))
    (define batch (edge-inputs a))
    ;; Compare identical abstract contents, allowing completely independent inactive slots.
    (define b (spec-edge-at candidate (symbolic-spec-state candidate) batch))
    (define domain (&& (edge-legal a) (edge-legal b)
                       (equivalent-value? (q (edge-state a)) (q (edge-state b)))))
    (define allowed-a (hash-ref (edge-outputs a) "allowed"))
    (define allowed-b (hash-ref (edge-outputs b) "allowed"))
    (define accept (observation-value allowed-a))
    (require-feasible domain)
    (discharge 'defined domain (&& (edge-defined a) (edge-defined b)
                                   (spec-state-legal? (edge-next a)) (spec-state-legal? (edge-next b))))
    (discharge 'legality domain (&& (observation-care allowed-a) (observation-care allowed-b)
                                    (equal? accept (observation-value allowed-b))))
    (discharge 'accepted-contents (&& domain accept) (equivalent-value? (q (edge-next a)) (q (edge-next b))))
    (discharge 'rejected-hold (&& domain (! accept))
               (&& (equivalent-value? (q (edge-next a)) (q (edge-state a)))
                   (equivalent-value? (q (edge-next b)) (q (edge-state b)))))

    ;; Also check the actual event bindings. In the proof above even inactive payloads were free.
    (define sampled (spec-edge-at (spec_snapshot (transaction_batch_model view)) (hash) batch))
    (define (field k) (observation-value (hash-ref (edge-outputs sampled) k)))
    (discharge 'batch-binding #t
               (&& (edge-defined sampled) (andmap observation-care (hash-values (edge-outputs sampled)))
                   (equal? (field "reset") (hash-ref batch "reset"))
                   (equal? (field "push") (hash-ref batch "push"))
                   (equal? (field "pop") (hash-ref batch "pop"))
                   (=> (field "push") (bveq (field "in") (hash-ref batch "in")))
                   (=> (field "pop") (bveq (field "out") (hash-ref batch "out")))))
    (define n (bitvector->natural (sequence-value-count (q (edge-state a)))))
    (require-feasible (&& domain accept (= n capacity) (hash-ref batch "push") (hash-ref batch "pop")))
    (require-feasible (&& domain (! accept) (> n 0) (hash-ref batch "reset") (hash-ref batch "push")))))
