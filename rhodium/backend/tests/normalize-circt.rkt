#lang racket/base
;; Canonicalizes only backend-generated SSA names for portable-path CIRCT comparisons.
;; SPDX-License-Identifier: Apache-2.0

(provide normalize_circt)

(define (normalize_circt text)
  (define names (make-hash))
  (string->immutable-string
   (regexp-replace* #px"%__rhodium_[A-Za-z0-9_]+" text
                   (lambda (name)
                     (hash-ref! names name
                                (lambda () (string-append "%__rhodium_canonical_" (number->string (hash-count names)))))))))
