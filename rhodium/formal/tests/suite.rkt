#lang racket/base
;; Runs all formal tests in one importing thread so Rosette owns one initialized term cache.
;; SPDX-License-Identifier: Apache-2.0

(require "aggregate-test.rhm"
         (submod "arbiter-refinement.rkt" test)
         "selector-refinement.rkt"
         "arbiter-queue-order.rkt"
         "buffered-arbiter-queue-order.rkt"
         "gated-arbiter-queue-order.rkt"
         "two-tag-rob-proof.rkt"
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
         "shift-queue-pair.rkt"
         "spec-symbolic-test.rhm"
         "transition-test.rhm")
