#lang racket/base
;; Materializes designs, goldens, and optional compiler manifest headers for external tests.

(require racket/file
         racket/match)

(match (vector->list (current-command-line-arguments))
  [(list* "materialize" output-directory specifications)
   (define emit-circt
     (dynamic-require "rhodium/backend/circt.rhm" 'emit_circt))
   (make-directory* output-directory)
   (let loop ([remaining specifications])
     (match remaining
       ['() (void)]
       [(list* "example" fixture example-path design-export rest)
        (define design
          (dynamic-require example-path (string->symbol design-export)))
        (call-with-output-file
         (build-path output-directory (string-append fixture ".mlir"))
         #:exists 'truncate/replace
         (lambda (out) (display (emit-circt design) out)))
        (loop rest)]
       [(list* "golden" fixture example-path design-export reference-export rest)
        (define design
          (dynamic-require example-path (string->symbol design-export)))
        (define reference
          (dynamic-require example-path (string->symbol reference-export)))
        (call-with-output-file
         (build-path output-directory (string-append fixture ".mlir"))
         #:exists 'truncate/replace
         (lambda (out) (display (emit-circt design) out)))
        (call-with-output-file
         (build-path output-directory (string-append fixture ".expected.sv"))
         #:exists 'truncate/replace
         (lambda (out) (display reference out)))
        (loop rest)]
       [(list* "emitter" fixture emitter-path rest)
        (call-with-output-file
         (build-path output-directory (string-append fixture ".mlir"))
         #:exists 'truncate/replace
         (lambda (out)
           (parameterize ([current-output-port out])
             (dynamic-require emitter-path #f))))
        (define event-manifest
          (dynamic-require emitter-path 'event_manifest_cpp (lambda () #f)))
        (when event-manifest
          (display-to-file event-manifest
                           (build-path output-directory (string-append fixture "_manifest.h"))
                           #:exists 'truncate/replace))
        (loop rest)]
       [_
        (raise-user-error
         'load-example
         "each fixture must be tagged as example, golden, or emitter")]))]
  [_
   (raise-user-error
    'load-example
    "expected: materialize OUTPUT-DIRECTORY (example FIXTURE EXAMPLE DESIGN-EXPORT | golden FIXTURE EXAMPLE DESIGN-EXPORT REFERENCE-EXPORT | emitter FIXTURE EMITTER) ...")])
