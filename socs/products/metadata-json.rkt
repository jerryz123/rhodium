#lang racket/base
;; Encodes sorted, string-keyed product metadata and computes its portable SHA-256 identity.
;; SPDX-License-Identifier: Apache-2.0
(require json racket/list racket/port racket/treelist file/sha1)
(provide metadata_json metadata_fingerprint)
(define (metadata_json value)
  (string->immutable-string (encode value)))
(define (encode value)
  (cond
    [(hash? value)
     (string-append "{"
                    (apply string-append
                           (add-between
                            (for/list ([key (in-list (sort (hash-keys value) string<?))])
                              (string-append (jsexpr->string key) ":" (metadata_json (hash-ref value key))))
                            ","))
                    "}")]
    [(treelist? value) (metadata_json (treelist->list value))]
    [(list? value)
     (string-append "[" (apply string-append (add-between (map metadata_json value) ",")) "]")]
    [else (jsexpr->string value)]))
(define (metadata_fingerprint value)
  (string->immutable-string (bytes->hex-string (sha256-bytes (open-input-string (metadata_json value))))))
