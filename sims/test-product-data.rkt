#lang racket/base
;; Reads the sole CI product inventory without importing hardware configuration.
;; SPDX-License-Identifier: Apache-2.0
(require racket/file racket/runtime-path racket/string racket/treelist)
(provide test_product_names)
(define-runtime-path inventory "test-products.txt")
(define test_product_names
  (for/treelist ([line (in-list (file->lines inventory))]
                 #:unless (or (string-prefix? line "#") (string=? line "")))
    (string->immutable-string line)))
