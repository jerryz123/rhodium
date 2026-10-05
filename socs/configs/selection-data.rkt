#lang racket/base
#| Loads the dependency-light public selector table for typed Rhombus consumers. |#
;; SPDX-License-Identifier: Apache-2.0
(require racket/file racket/runtime-path racket/string racket/treelist)
(provide selection_rows)
(define-runtime-path table "selections.tsv")
(define selection_rows
  (for/treelist ([line (in-list (file->lines table))]
                 #:unless (or (string-prefix? line "#") (string=? line "")))
    (list->treelist (map string->immutable-string (string-split line)))))
