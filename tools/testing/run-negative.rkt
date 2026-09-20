#lang racket/base
;; Loads invalid-language fixtures from a suite manifest and checks their diagnostics.
;; SPDX-License-Identifier: Apache-2.0

(require racket/cmdline
         racket/match
         racket/string)

(define-values (invalid-directory manifest-file)
  (command-line
   #:args (invalid-directory manifest-file)
   (values invalid-directory manifest-file)))

(define cases
  (call-with-input-file manifest-file read))

(define (check-invalid case)
  (match-define (list source-file expected) case)
  (define path (build-path invalid-directory source-file))
  (define failure
    (with-handlers ([exn:fail? values])
      (dynamic-require path #f)
      #f))
  (unless failure
    (error 'run-negative "~a unexpectedly succeeded" source-file))
  (define message (exn-message failure))
  (unless (string-contains? message expected)
    (error 'run-negative
           "~a did not contain expected diagnostic ~s:\n~a"
           source-file expected message)))

(for-each check-invalid cases)
