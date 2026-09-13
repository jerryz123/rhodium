#lang racket/base
;; Loads every invalid NoC-language fixture in one process and checks its diagnostic.
;; SPDX-License-Identifier: Apache-2.0

(require racket/runtime-path
         racket/string)

(define-runtime-path invalid-directory "invalid")

(define cases
  '(("duplicate-node.rhm.invalid" "duplicate topology node repeated")
    ("duplicate-injection.rhm.invalid" "duplicate topology injection terminal port")
    ("duplicate-link.rhm.invalid" "duplicate topology link forward")
    ("unknown-node.rhm.invalid" "unknown topology node missing")
    ("unknown-terminal-router.rhm.invalid" "unknown topology node missing")
    ("unknown-group.rhm.invalid" "unknown topology VC group missing")
    ("malformed-clause.rhm.invalid" "expected vc_group, node, injection, ejection, directed, or bidirectional topology declaration")
    ("empty-vcs.rhm.invalid" "directed topology link requires at least one VC group")
    ("duplicate-link-group.rhm.invalid" "duplicate VC group escape on topology link broken")
    ("topology-no-node.rhm.invalid" "topology requires at least one node")
    ("routing-duplicate-rule.rhm.invalid" "duplicate routing rule repeated")
    ("routing-empty-rule.rhm.invalid" "routing rule empty must contain at least one constraint")
    ("routing-empty.rhm.invalid" "routing requires at least one rule")
    ("routing-bad-origin.rhm.invalid" "routing origin must be injection or forwarding")
    ("routing-unknown-clause.rhm.invalid" "expected use, origin, routes, current_vcs, candidate_vcs, or candidate_links routing constraint")
    ("routing-empty-selection.rhm.invalid" "candidate-VC-group selection must be nonempty")
    ("routing-duplicate-group.rhm.invalid" "duplicate candidate-VC-group selection escape")))

(define (check-invalid source-file expected)
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
           source-file expected message))
  (unless (string-contains? message (string-append source-file ":"))
    (error 'run-negative
           "~a diagnostic did not retain its source location:\n~a"
           source-file message)))

(for ([case (in-list cases)])
  (apply check-invalid case))
