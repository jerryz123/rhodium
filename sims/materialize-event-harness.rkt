#lang racket/base
;; Emits instrumented RTL input and matching runtime metadata from one elaboration.
;; SPDX-License-Identifier: Apache-2.0
(require racket/file racket/runtime-path)
(define-runtime-path emitter "emit-event-harness.rhm")
(define args (current-command-line-arguments))
(unless (= (vector-length args) 1)
  (error 'event-harness "expected output header path"))
(dynamic-require emitter #f)
(define manifest (dynamic-require emitter 'event_manifest_cpp))
(define frequency (dynamic-require emitter 'trace_clock_frequency_hz))
(unless (and (exact-positive-integer? frequency) (< frequency (expt 2 64)))
  (error 'event-harness "clock frequency must fit a positive uint64"))
(display-to-file
 (string-append manifest "\nnamespace rheg_generated {\n"
                "inline constexpr std::uint64_t clock_frequency_hz = "
                (number->string frequency) "ULL;\n}\n")
 (vector-ref args 0) #:exists 'truncate/replace)
