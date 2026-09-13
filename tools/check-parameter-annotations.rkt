#lang racket/base
;; Checks Rhombus source declarations for explicitly annotated parameters.
;; SPDX-License-Identifier: Apache-2.0

(require racket/cmdline
         racket/file
         racket/list
         racket/path
         racket/string
         shrubbery/parse)

(provide check-source
         check-file
         collect-source-files
         (struct-out violation))

(struct violation (path line column kind declaration parameter issue) #:transparent)
(struct declaration (kind name parameters anonymous?) #:transparent)

(define annotation-operators '(:: :~))

(define (syntax-children stx)
  (or (syntax->list stx) '()))

(define (syntax-atom stx)
  (define value (syntax-e stx))
  (and (or (symbol? value) (keyword? value)) value))

(define (form-tag stx)
  (define children (syntax-children stx))
  (and (pair? children) (syntax-atom (car children))))

(define (operator? stx operator)
  (equal? (syntax->datum stx) `(op ,operator)))

(define (form-index items tag)
  (for/first ([item (in-list items)]
              [index (in-naturals)]
              #:when (eq? (form-tag item) tag))
    index))

(define (direct-symbols items)
  (filter symbol? (filter values (map syntax-atom items))))

(define (find-declaration-kind head inherited-kind)
  (or inherited-kind
      (for/first ([kind (in-list '(class constructor method fun))]
                  #:when (member kind (direct-symbols head)))
        kind)))

(define (find-declaration-name kind head inherited-kind)
  (cond
    [(eq? kind 'constructor) "constructor"]
    [else
     (define symbols (direct-symbols head))
     (define candidates
       (if inherited-kind
           symbols
           (let ([tail (member kind symbols)])
             (if tail (cdr tail) '()))))
     (and (pair? candidates) (symbol->string (last candidates)))]))

(define (parse-declaration group [inherited-kind #f])
  (and (eq? (form-tag group) 'group)
       (let* ([items (cdr (syntax-children group))]
              [parens-index (form-index items 'parens)])
         (and parens-index
              (let* ([head (take items parens-index)]
                     [kind (find-declaration-kind head inherited-kind)]
                     [name (and kind
                                (find-declaration-name kind head inherited-kind))])
                (and kind
                     (declaration
                      kind
                      (or name
                          (if (eq? kind 'fun)
                              "<anonymous>"
                              (symbol->string kind)))
                      (cdr (syntax-children (list-ref items parens-index)))
                      (and (eq? kind 'fun) (not name)))))))))

(define (ellipsis-parameter? parameter)
  (equal? (syntax->datum parameter) '(group (op ...))))

(define (keyword-binding-group parameter)
  (define items (cdr (syntax-children parameter)))
  (and (pair? items)
       (keyword? (syntax-atom (car items)))
       (for*/first ([item (in-list items)]
                    #:when (eq? (form-tag item) 'block)
                    [candidate (in-list (cdr (syntax-children item)))]
                    #:when (eq? (form-tag candidate) 'group))
         candidate)))

(define (binding-has-annotation? parameter)
  (define binding (or (keyword-binding-group parameter) parameter))
  (for/or ([item (in-list (cdr (syntax-children binding)))]
           #:break (operator? item '=))
    (for/or ([operator (in-list annotation-operators)])
      (operator? item operator))))

(define (binding-has-any-annotation? parameter)
  (define binding (or (keyword-binding-group parameter) parameter))
  (define annotation-started? #f)
  (for/or ([item (in-list (cdr (syntax-children binding)))]
           #:break (operator? item '=))
    (cond
      [(for/or ([operator (in-list annotation-operators)])
         (operator? item operator))
       (set! annotation-started? #t)
       #false]
      [else
       (and annotation-started?
            (eq? (syntax-atom item) 'Any))])))

(define (parameter-location parameter)
  (define binding (or (keyword-binding-group parameter) parameter))
  (or (for/first ([item (in-list (cdr (syntax-children binding)))]
                  #:when (syntax-atom item))
        item)
      (for/first ([item (in-list (cdr (syntax-children parameter)))]
                  #:when (syntax-atom item))
        item)
      parameter))

(define (parameter-name parameter)
  (define value (syntax-atom (parameter-location parameter)))
  (cond
    [(symbol? value) (symbol->string value)]
    [(keyword? value) (keyword->string value)]
    [else "<pattern>"]))

(define (group-family-kind group)
  (and (eq? (form-tag group) 'group)
       (let* ([items (cdr (syntax-children group))]
              [parens-index (form-index items 'parens)]
              [head (if parens-index (take items parens-index) items)])
         (for/first ([kind (in-list '(method fun))]
                     #:when (member kind (direct-symbols head)))
           kind))))

(define (check-parsed parsed
                      path
                      #:include-anonymous? [include-anonymous? #t]
                      #:reject-any? [reject-any? #f])
  (define found '())

  (define (record-declaration! parsed-declaration implicit-receiver?)
    (when (and parsed-declaration
               (or include-anonymous?
                   (not (declaration-anonymous? parsed-declaration))))
      (for ([parameter (in-list (declaration-parameters parsed-declaration))]
            [parameter-index (in-naturals)]
            #:unless (and implicit-receiver?
                          (eq? (declaration-kind parsed-declaration) 'method)
                          (= parameter-index 0))
            #:unless (ellipsis-parameter? parameter))
        (define issue
          (cond
            [(not (binding-has-annotation? parameter)) 'missing]
            [(and reject-any? (binding-has-any-annotation? parameter)) 'any]
            [else #false]))
        (when issue
          (define location (parameter-location parameter))
          (set! found
                (cons
                 (violation
                  path
                  (or (syntax-line location) 1)
                  (or (syntax-column location) 0)
                  (declaration-kind parsed-declaration)
                  (declaration-name parsed-declaration)
                  (parameter-name parameter)
                  issue)
                 found))))))

  (define (walk-alternatives alternatives family-kind implicit-method-receiver?)
    (for ([alternative (in-list (cdr (syntax-children alternatives)))])
      (for ([group (in-list (cdr (syntax-children alternative)))])
        (walk group family-kind implicit-method-receiver?))))

  (define (walk stx [inherited-kind #f] [implicit-method-receiver? #false])
    (define tag (form-tag stx))
    (unless (eq? tag 'quotes)
      (when (eq? tag 'group)
        (record-declaration! (parse-declaration stx inherited-kind)
                             implicit-method-receiver?))
      (define family-kind
        (and (eq? tag 'group) (group-family-kind stx)))
      (define child-implicit-method-receiver?
        (or implicit-method-receiver?
            (and (eq? tag 'group)
                 (for/or ([family (in-list '(hardware_enum bundle))])
                   (member family
                           (direct-symbols (cdr (syntax-children stx))))))))
      (for ([child (in-list (syntax-children stx))])
        (if (and family-kind (eq? (form-tag child) 'alts))
            (walk-alternatives child family-kind child-implicit-method-receiver?)
            (walk child #false child-implicit-method-receiver?)))))

  (walk parsed)
  (reverse found))

(define (check-source source [path "<source>"]
                      #:include-anonymous? [include-anonymous? #t]
                      #:reject-any? [reject-any? #f])
  (define input (open-input-string source))
  (port-count-lines! input)
  (check-parsed
   (parse-all input #:source path)
   path
   #:include-anonymous? include-anonymous?
   #:reject-any? reject-any?))

(define (check-file path
                    #:include-anonymous? [include-anonymous? #t]
                    #:reject-any? [reject-any? #f])
  (call-with-input-file path
    (lambda (input)
      (port-count-lines! input)
      (define language-line (read-line input 'any))
      (unless (and (string? language-line)
                   (string-prefix? language-line "#lang "))
        (error 'check-parameter-annotations
               "expected a #lang line in ~a"
               path))
      (check-parsed
       (parse-all input #:source path)
       path
       #:include-anonymous? include-anonymous?
       #:reject-any? reject-any?))))

(define (rhombus-source-path? path)
  (member (path-get-extension path) '(#".rhm" #".rhdl")))

(define (tests-path? path)
  (for/or ([component (in-list (explode-path (simplify-path path)))])
    (and (path? component)
         (string=? (path->string component) "tests"))))

(define (descend-directory? path)
  (not (member (path->string (file-name-from-path path))
               '(".git" ".tools" "compiled" "tests"))))

(define (collect-source-files paths)
  (sort
   (remove-duplicates
    (append*
     (for/list ([supplied (in-list paths)])
       (define path (simplify-path supplied))
       (cond
         [(file-exists? path)
          (if (and (rhombus-source-path? path)
                   (not (tests-path? path)))
              (list path)
              '())]
         [(directory-exists? path)
          (for/list ([candidate (in-directory path descend-directory?)]
                     #:when (and (file-exists? candidate)
                                 (rhombus-source-path? candidate)
                                 (not (tests-path? candidate))))
            candidate)]
         [else
          (error 'check-parameter-annotations
                 "source path does not exist: ~a"
                 supplied)])))
    equal?)
   path<?))

(define (scope-lines->paths lines)
  (for/list ([line (in-list lines)]
             #:do [(define trimmed (string-trim line))]
             #:unless (or (string=? trimmed "")
                          (string-prefix? trimmed "#")))
    trimmed))

(define (scope-file-paths path)
  (scope-lines->paths (file->lines path)))

(define (display-violation problem)
  (eprintf "~a:~a:~a: ~a ~a parameter `~a` ~a\n"
           (violation-path problem)
           (violation-line problem)
           (add1 (violation-column problem))
           (violation-kind problem)
           (violation-declaration problem)
           (violation-parameter problem)
           (if (eq? (violation-issue problem) 'any)
               "uses the forbidden broad Any annotation"
               "lacks a :: or :~ annotation")))

(module+ main
  (define include-anonymous? #t)
  (define reject-any? #f)
  (define supplied-paths '())
  (define scope-paths '())
  (command-line
   #:program "check-parameter-annotations.rkt"
   #:once-each
   [("--named-only")
    "Exclude anonymous function expressions from the policy"
    (set! include-anonymous? #f)]
   [("--reject-any")
    "Reject parameter annotations whose top-level expression contains Any"
    (set! reject-any? #t)]
   [("--files-from") path
    "Read additional source paths from a line-oriented scope file"
    (set! scope-paths (append scope-paths (scope-file-paths path)))]
   #:args paths
   (set! supplied-paths paths))
  (define source-paths (append scope-paths supplied-paths))
  (when (null? source-paths)
    (raise-user-error 'check-parameter-annotations
                      "provide at least one .rhm/.rhdl file or directory"))
  (define problems
    (append*
     (for/list ([path (in-list (collect-source-files source-paths))])
       (check-file path
                   #:include-anonymous? include-anonymous?
                   #:reject-any? reject-any?))))
  (for-each display-violation problems)
  (unless (null? problems)
    (eprintf "check-parameter-annotations: found ~a parameter annotation violation~a\n"
             (length problems)
             (if (= (length problems) 1) "" "s"))
    (exit 1)))

(module+ test
  (require rackunit)

  (define annotated-source
    #<<SOURCE
fun identity(value :: Any): value
fun apply(~value: value :: Any = #false): value
fun trusted(value :~ Any): value
fun repeat(value :: Any, ...): value
class Box(value :: Any):
  method replace(next :: Any): Box(next)
  constructor(initial :: Any): Box(initial)
SOURCE
    )
  (check-equal? (check-source annotated-source) '())

  (define unannotated-source
    #<<SOURCE
fun identity(value): value
class Box(value :: Any, label):
  method replace(next): Box(next, "")
  constructor(initial): Box(initial, "")
SOURCE
    )
  (define unannotated (check-source unannotated-source))
  (check-equal? (map violation-kind unannotated)
                '(fun class method constructor))
  (check-equal? (map violation-parameter unannotated)
                '("value" "label" "next" "initial"))

  (define keyword-source
    "fun keyword(~value: value): value\n")
  (check-equal?
   (map violation-parameter (check-source keyword-source))
   '("value"))

  (define anonymous-source
    "def callback = fun (value): value\n")
  (check-equal? (length (check-source anonymous-source)) 1)
  (check-equal?
   (check-source anonymous-source #:include-anonymous? #f)
   '())

  (define hardware-enum-source
    #<<SOURCE
hardware_enum State:
  Idle = 0
  method active(state) :: Bool: state === State.Idle
  method select(state, choice): choice
SOURCE
    )
  (check-equal?
   (map violation-parameter (check-source hardware-enum-source))
   '("choice"))

  (define bundle-method-source
    #<<SOURCE
bundle Flag():
  set: Bool
  method active(flag): flag.set
  method combine(flag, other): flag.set & other.set
SOURCE
    )
  (check-equal?
   (map violation-parameter (check-source bundle-method-source))
   '("other"))

  (define multi-case-source
    #<<SOURCE
fun choose
| choose(value): value
| choose(left :: Any, right): right
SOURCE
    )
  (check-equal? (map violation-parameter (check-source multi-case-source))
                '("value" "right"))

  (define quoted-source
    #<<SOURCE
macro 'make':
  'fun generated(value): value'
SOURCE
    )
  (check-equal? (check-source quoted-source) '())

  (define default-expression-source
    "fun defaulted(value = (other :: Any)): value\n")
  (check-equal?
   (map violation-parameter (check-source default-expression-source))
   '("value"))

  (define diagnostic-output (open-output-string))
  (parameterize ([current-error-port diagnostic-output])
    (display-violation (first unannotated)))
  (check-true
   (string-contains? (get-output-string diagnostic-output)
                     "lacks a :: or :~ annotation"))

  (check-equal?
   (scope-lines->paths
    '("# Strict source scope" "" "  rhodium/std  " "rhodium/core/ops.rhm"))
   '("rhodium/std" "rhodium/core/ops.rhm"))

  (check-true (tests-path? (build-path "noc" "tests" "model-test.rhm")))
  (check-true (tests-path? (build-path "cores" "ricket" "tests" "core-test.rhm")))
  (check-false (tests-path? (build-path "rhodium" "core" "types.rhm")))

  (define broad-annotation-source
    #<<SOURCE
fun broad(value :: Any): value
fun broad_union(value :: String || Any): value
SOURCE
    )
  (define broad-annotations
    (check-source broad-annotation-source #:reject-any? #t))
  (check-equal? (map violation-parameter broad-annotations)
                '("value" "value"))
  (check-equal? (map violation-issue broad-annotations)
                '(any any)))
