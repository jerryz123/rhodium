#lang racket/base
;; Invalidates changed project bytecode with one metadata pass and reverse-dependency traversal.
;; SPDX-License-Identifier: Apache-2.0

(require compiler/compilation-path
         racket/file
         racket/path
         racket/set
         setup/collects)

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
  (define started (current-inexact-milliseconds))
  (define sources
    (for/list ([relative-source (in-list (file->lines source-manifest))])
      (simplify-path (path->complete-path relative-source repository))))
  (define bytecode-paths
    (for/hash ([source-path (in-list sources)])
      (values source-path (get-compilation-bytecode-file source-path))))
  (define resolved-paths (make-hash))
  (define (dependency-path dependency)
    (cond
      [(and (pair? dependency) (memq (car dependency) '(ext indirect)))
       (dependency-path (cdr dependency))]
      [else
       (hash-ref! resolved-paths dependency
                  (lambda ()
                    (define path (collects-relative->path dependency))
                    (simplify-path (path->complete-path (if (bytes? path) (bytes->path path) path)))))]))
  (define reverse-dependencies (make-hash))
  (define unreadable (mutable-set))
  (define visited (mutable-set))
  (define cached 0)
  (define metadata-read 0)
  (let scan ([pending sources])
    (unless (null? pending)
      (define source-path (car pending))
      (define next (cdr pending))
      (unless (set-member? visited source-path)
        (set-add! visited source-path)
        (define project? (hash-has-key? bytecode-paths source-path))
        (define bytecode-path (if project? (hash-ref bytecode-paths source-path)
                                 (get-compilation-bytecode-file source-path)))
        (when (file-exists? bytecode-path)
          (when project? (set! cached (add1 cached)))
          (with-handlers ([exn:fail? (lambda (_failure)
                                      (when project? (set-add! unreadable source-path)))])
            (set! metadata-read (add1 metadata-read))
            (define record (call-with-input-file* (compiled-dependency-path bytecode-path) read))
            (unless (and (list? record) (>= (length record) 3)
                         (string? (car record)) (pair? (caddr record)))
              (error 'invalidate-racket-build-cache "invalid dependency metadata for ~a" source-path))
            ;; Scan each reachable metadata file once, including external
            ;; bridges, without a separate transitive walk for every source.
            (define dependencies (map dependency-path (cdddr record)))
            (for ([dependency (in-list dependencies)])
              (hash-update! reverse-dependencies dependency
                            (lambda (importers) (cons source-path importers)) null)
              (unless (set-member? visited dependency)
                (set! next (cons dependency next)))))))
      (scan next)))
  (define roots (set-union changed (list->set (set->list unreadable))))
  (define stale
    (let loop ([pending (set->list roots)] [stale roots])
      (cond
        [(null? pending) stale]
        [else
         (define-values (next expanded)
           (for/fold ([next (cdr pending)] [stale stale])
                     ([importer (in-list (hash-ref reverse-dependencies (car pending) null))])
             (if (set-member? stale importer)
                 (values next stale)
                 (values (cons importer next) (set-add stale importer)))))
         (loop next expanded)])))
  (define invalidated 0)
  (for ([source-path (in-set stale)] #:when (hash-has-key? bytecode-paths source-path))
    (define bytecode-path (hash-ref bytecode-paths source-path))
    (when (file-exists? bytecode-path)
      (set! invalidated (add1 invalidated))
      (delete-file bytecode-path)
      (define dependency-path (compiled-dependency-path bytecode-path))
      (when (file-exists? dependency-path)
        (delete-file dependency-path))))
  (eprintf "rhodium-cache: changed=~a cached=~a metadata-read=~a unreadable=~a invalidated=~a retained=~a elapsed-ms=~a\n"
           (set-count changed) cached metadata-read (set-count unreadable)
           invalidated (- cached invalidated) (inexact->exact (round (- (current-inexact-milliseconds) started)))))
