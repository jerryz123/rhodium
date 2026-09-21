#lang racket/base
;; Removes cached modules whose source or transitive project dependency changed.
;; SPDX-License-Identifier: Apache-2.0

(require compiler/compilation-path
         compiler/depend
         racket/file
         racket/path
         racket/set)

(define arguments (current-command-line-arguments))
(unless (>= (vector-length arguments) 2)
  (error 'invalidate-racket-build-cache
         "expected REPOSITORY SOURCE-MANIFEST [CHANGED-SOURCE ...]"))

(define repository (simplify-path (path->complete-path (vector-ref arguments 0))))
(define source-manifest (vector-ref arguments 1))
(define changed
  (for/set ([argument (in-vector arguments 2)])
    (simplify-path (path->complete-path argument repository))))

(define (compiled-dependency-path bytecode-path)
  (path-replace-extension bytecode-path #".dep"))

(parameterize ([current-directory repository])
  (define sources
    (for/list ([relative-source (in-list (file->lines source-manifest))])
      (simplify-path (path->complete-path relative-source repository))))
  (define bytecode-paths
    (for/hash ([source-path (in-list sources)])
      (values source-path (get-compilation-bytecode-file source-path))))
  (define dependencies
    (for/hash ([source-path (in-list sources)])
      (define bytecode-path (hash-ref bytecode-paths source-path))
      (values
       source-path
       (if (file-exists? bytecode-path)
           (with-handlers ([exn:fail? (lambda (_failure) #f)])
             (for/set ([dependency (in-list (module-recorded-dependencies source-path))])
               (simplify-path dependency)))
           (set)))))
  (define stale
    (let loop ([stale changed])
      (define expanded
        (for/fold ([expanded stale]) ([source-path (in-list sources)])
          (define source-dependencies (hash-ref dependencies source-path))
          (if (or (not source-dependencies)
                  (not (set-empty? (set-intersect source-dependencies stale))))
              (set-add expanded source-path)
              expanded)))
      (if (= (set-count expanded) (set-count stale))
          expanded
          (loop expanded))))
  (for ([source-path (in-set stale)])
    (define bytecode-path (hash-ref bytecode-paths source-path))
    (when (file-exists? bytecode-path)
      (delete-file bytecode-path)
      (define dependency-path (compiled-dependency-path bytecode-path))
      (when (file-exists? dependency-path)
        (delete-file dependency-path)))))
