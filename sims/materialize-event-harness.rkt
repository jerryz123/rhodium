#lang racket/base
;; Saves event metadata beside RTL emitted by the normal SoC harness selector.
;; SPDX-License-Identifier: Apache-2.0
(require racket/file racket/runtime-path)
(define-runtime-path emitter "emit-soc-harness.rhm")
(define args (current-command-line-arguments))
(unless (= (vector-length args) 4)
  (error 'event-harness "expected output header path, SoC shape, core, and explicit ISA"))
(define-values (manifest frequency)
  (parameterize ([current-command-line-arguments (vector "--trace" (vector-ref args 1) (vector-ref args 2) (vector-ref args 3))])
    (dynamic-require emitter #f)
    (values (dynamic-require emitter 'event_manifest_cpp)
            (dynamic-require emitter 'trace_clock_frequency_hz))))
(unless (and (exact-positive-integer? frequency) (< frequency (expt 2 64)))
  (error 'event-harness "clock frequency must fit a positive uint64"))
(display-to-file
 (string-append manifest "\nnamespace rheg_generated {\n"
                "inline constexpr std::uint64_t clock_frequency_hz = "
                (number->string frequency) "ULL;\n}\n")
 (vector-ref args 0) #:exists 'truncate/replace)
