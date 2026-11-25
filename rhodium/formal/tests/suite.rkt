#lang racket/base
;; Runs all formal tests in one importing thread so Rosette owns one initialized term cache.
;; SPDX-License-Identifier: Apache-2.0

(require "aggregate-test.rhm"
         (submod "arbiter-refinement.rkt" test)
         "selector-refinement.rkt"
         "arbiter-queue-order.rkt"
         (submod "arbiter-queue-transactions.rkt" test)
         "buffered-arbiter-queue-order.rkt"
         (submod "buffered-arbiter-queue-transactions.rkt" test)
         "gated-arbiter-queue-order.rkt"
         (submod "gated-arbiter-queue-transactions.rkt" test)
         "two-tag-rob-proof.rkt"
         (submod "two-tag-rob-transactions.rkt" test)
         (submod "rob-commit-log.rkt" test)
         (submod "logged-two-tag-rob.rkt" test)
         (submod "logged-refinement.rkt" test)
         (submod "two-tag-rob-monitor.rkt" test)
         (submod "two-tag-rob-oracle.rkt" test)
         "api-test.rhm"
         "assumption-test.rhm"
         "decode-test.rhm"
         "engine-test.rkt"
         "hierarchy-test.rhm"
         "onehot-assumption-test.rhm"
         "remainder-test.rhm"
         "snapshot-test.rhm"
         (submod "shift-queue-replay.rkt" test)
         (submod "shift-queue-induction.rkt" test)
         (submod "queue-pair-transaction-order.rkt" test)
         (submod "shift-queue-transactions.rkt" test)
         (submod "queue-pair-grouped.rkt" test)
         (submod "queue-pair-series.rkt" test)
         "spec-symbolic-test.rhm"
         "transition-test.rhm")
