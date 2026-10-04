#lang racket/base
#| Encodes sorted, string-keyed product metadata and computes its portable SHA-256 identity. |#
;; SPDX-License-Identifier: Apache-2.0
(require json racket/list racket/port racket/treelist file/sha1)
(provide metadata_json metadata_fingerprint)
#| Returns immutable JSON with deterministic ordering of string-keyed maps. |#
(define (metadata_json value)
  (string->immutable-string (encode value)))
#| Recursively encodes maps, Rhombus treelists, and lists using JSON scalar encoding. |#
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
#| Hashes the canonical UTF-8 JSON representation into an immutable hexadecimal SHA-256 identity. |#
(define (metadata_fingerprint value)
  (string->immutable-string (bytes->hex-string (sha256-bytes (open-input-string (metadata_json value))))))
