#lang racket/base
#| Checks Rhombus source declarations for explicitly annotated parameters. |#
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

#| Returns a syntax list's children, or an empty list for an atom. |#
(define (syntax-children stx)
  (or (syntax->list stx) '()))

#| Extracts symbols and keywords used in declaration heads and bindings. |#
(define (syntax-atom stx)
  (define value (syntax-e stx))
  (and (or (symbol? value) (keyword? value)) value))

#| Reads the leading tag of a parsed shrubbery form. |#
(define (form-tag stx)
  (define children (syntax-children stx))
  (and (pair? children) (syntax-atom (car children))))

#| Recognizes one exact shrubbery operator token. |#
(define (operator? stx operator)
  (equal? (syntax->datum stx) `(op ,operator)))

#| Finds the first child with the requested shrubbery tag. |#
(define (form-index items tag)
  (for/first ([item (in-list items)]
              [index (in-naturals)]
              #:when (eq? (form-tag item) tag))
    index))

#| Collects immediate symbols without entering nested syntax. |#
(define (direct-symbols items)
  (filter symbol? (filter values (map syntax-atom items))))

#| Selects an inherited declaration kind or one authored in the head. |#
(define (find-declaration-kind head inherited-kind)
  (or inherited-kind
      (for/first ([kind (in-list '(class constructor method fun))]
                  #:when (member kind (direct-symbols head)))
        kind)))

#| Extracts the declared name, preserving anonymous functions. |#
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

#| Decodes a declaration's head and parameter groups without expanding it. |#
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

#| Recognizes the repetition marker that introduces no parameter binding. |#
(define (ellipsis-parameter? parameter)
  (equal? (syntax->datum parameter) '(group (op ...))))

#| Finds a keyword parameter's nested binding group. |#
(define (keyword-binding-group parameter)
  (define items (cdr (syntax-children parameter)))
  (and (pair? items)
       (keyword? (syntax-atom (car items)))
       (for*/first ([item (in-list items)]
                    #:when (eq? (form-tag item) 'block)
                    [candidate (in-list (cdr (syntax-children item)))]
                    #:when (eq? (form-tag candidate) 'group))
         candidate)))

#| Checks the binding before its default expression for an annotation. |#
(define (binding-has-annotation? parameter)
  (define binding (or (keyword-binding-group parameter) parameter))
  (for/or ([item (in-list (cdr (syntax-children binding)))]
           #:break (operator? item '=))
    (for/or ([operator (in-list annotation-operators)])
      (operator? item operator))))

#| Detects a broad Any annotation before a parameter's default expression. |#
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

#| Selects the binding token used for parameter diagnostics. |#
(define (parameter-location parameter)
  (define binding (or (keyword-binding-group parameter) parameter))
  (or (for/first ([item (in-list (cdr (syntax-children binding)))]
                  #:when (syntax-atom item))
        item)
      (for/first ([item (in-list (cdr (syntax-children parameter)))]
                  #:when (syntax-atom item))
        item)
      parameter))

#| Formats a parameter's binding token for diagnostics. |#
(define (parameter-name parameter)
  (define value (syntax-atom (parameter-location parameter)))
  (cond
    [(symbol? value) (symbol->string value)]
    [(keyword? value) (keyword->string value)]
    [else "<pattern>"]))

#| Identifies multi-case function and method declaration families. |#
(define (group-family-kind group)
  (and (eq? (form-tag group) 'group)
       (let* ([items (cdr (syntax-children group))]
              [parens-index (form-index items 'parens)]
              [head (if parens-index (take items parens-index) items)])
         (for/first ([kind (in-list '(method fun))]
                     #:when (member kind (direct-symbols head)))
           kind))))

#| Collects annotation violations while respecting anonymous and receiver policy. |#
(define (check-parsed parsed
                      path
                      #:include-anonymous? [include-anonymous? #t]
                      #:reject-any? [reject-any? #f])
  (define found '())

  #| Records missing or forbidden annotations, excluding implicit receivers. |#
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

  #| Checks every case with its enclosing declaration-family context. |#
  (define (walk-alternatives alternatives family-kind implicit-method-receiver?)
    (for ([alternative (in-list (cdr (syntax-children alternatives)))])
      (for ([group (in-list (cdr (syntax-children alternative)))])
        (walk group family-kind implicit-method-receiver?))))

  #| Traverses authored declarations without checking quoted generated syntax. |#
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

#| Parses a source string and checks its parameter annotations. |#
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

#| Checks one language source file after validating its required language line. |#
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

#| Recognizes the source extensions subject to annotation policy. |#
(define (rhombus-source-path? path)
  (member (path-get-extension path) '(#".rhm" #".rhdl")))

#| Identifies paths exempted by a tests directory component. |#
(define (tests-path? path)
  (for/or ([component (in-list (explode-path (simplify-path path)))])
    (and (path? component)
         (string=? (path->string component) "tests"))))

#| Prunes tool, bytecode, Git, and test directories during source discovery. |#
(define (descend-directory? path)
  (not (member (path->string (file-name-from-path path))
               '(".git" ".tools" "compiled" "tests"))))

#| Resolves supplied paths into a sorted, unique production-source inventory. |#
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

#| Drops comments and blank lines from a source-scope manifest. |#
(define (scope-lines->paths lines)
  (for/list ([line (in-list lines)]
             #:do [(define trimmed (string-trim line))]
             #:unless (or (string=? trimmed "")
                          (string-prefix? trimmed "#")))
    trimmed))

#| Reads the production-source paths declared by a scope file. |#
(define (scope-file-paths path)
  (scope-lines->paths (file->lines path)))

#| Prints one located annotation diagnostic to the error stream. |#
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
