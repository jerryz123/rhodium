#lang racket/base
;; Exposes the nested #lang rhodium/base reader through Racket's conventional lang/reader path.
;; SPDX-License-Identifier: Apache-2.0

(require (submod "../main.rkt" reader))

(provide (all-from-out (submod "../main.rkt" reader)))
