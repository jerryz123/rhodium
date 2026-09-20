#lang racket/base
;; Runs all formal tests in one importing thread so Rosette owns one initialized term cache.
;; SPDX-License-Identifier: Apache-2.0

(require "aggregate-test.rhm"
         "api-test.rhm"
         "assumption-test.rhm"
         "decode-test.rhm"
         "engine-test.rkt"
         "hierarchy-test.rhm"
         "onehot-assumption-test.rhm"
         "property-test.rhm"
         "reachability-test.rhm"
         "snapshot-test.rhm")
