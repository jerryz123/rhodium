#lang racket/base
;; Exhaustively checks the Rosette engine's packed combinational semantics and query results.
;; SPDX-License-Identifier: Apache-2.0

(require rackunit
         racket/list
         "../../rhodium/formal/engine.rkt")

(define (flat-type width [text #f])
  (hash "kind" "flat"
        "text" (or text (format "bits<~a>" width))
        "width" width))

(define (value-snapshot id name type operation-id)
  (hash "id" id
        "name" name
        "type" type
        "defining_operation" operation-id))

(define (port-snapshot name direction type #:value [value #f]
                       #:place [place #f] #:driver [driver #f])
  (hash "name" name
        "direction" direction
        "type" type
        "value" value
        "place" place
        "driver" driver))

(define (place-snapshot id name type owner driver)
  (hash "id" id
        "name" name
        "type" type
        "owner_operation" owner
        "driver" driver))

(define (operation-snapshot id opcode operands results places [attributes (hash)])
  (hash "id" id
        "opcode" opcode
        "operands" operands
        "results" results
        "places" places
        "attributes" attributes
        "location" (format "engine-test:~a" id)
        "origin" (format "engine-test(~a)" opcode)))

(define (module-snapshot id name inputs outputs values places operations)
  (hash "id" id
        "name" name
        "inputs" inputs
        "outputs" outputs
        "values" values
        "places" places
        "operations" operations))

(define (single-module-snapshot module)
  (hash "version" 1 "top" (hash-ref module "id") "modules" (list module)))

(define (operation-design opcode input-types result-type [attributes (hash)])
  (define input-values
    (for/list ([type (in-list input-types)] [index (in-naturals)])
      (value-snapshot (+ 10 index) (format "x~a" index) type (+ 100 index))))
  (define input-ports
    (for/list ([type (in-list input-types)] [index (in-naturals)])
      (port-snapshot (format "x~a" index) "input" type #:value (+ 10 index))))
  (define input-operations
    (for/list ([index (in-range (length input-types))])
      (operation-snapshot (+ 100 index) "rtl.input_port" '() (list (+ 10 index)) '())))
  (define result-id 1000)
  (define result-operation-id 1100)
  (define output-place-id 1200)
  (define output-operation-id 1300)
  (define drive-operation-id 1400)
  (define result-value (value-snapshot result-id "y" result-type result-operation-id))
  (define output-place
    (place-snapshot output-place-id "y" result-type output-operation-id result-id))
  (define module
    (module-snapshot
     1
     "Operation"
     input-ports
     (list (port-snapshot "y" "output" result-type
                          #:place output-place-id #:driver result-id))
     (append input-values (list result-value))
     (list output-place)
     (append input-operations
             (list (operation-snapshot result-operation-id opcode
                                       (map (lambda (value) (hash-ref value "id")) input-values)
                                       (list result-id) '() attributes)
                   (operation-snapshot output-operation-id "rtl.output_port"
                                       '() '() (list output-place-id))
                   (operation-snapshot drive-operation-id "rtl.drive"
                                       (list result-id) '() (list output-place-id))))))
  (single-module-snapshot module))

(define (run-operation opcode widths inputs
                       #:result-width [result-width (car widths)]
                       #:attributes [attributes (hash)])
  (define snapshot
    (operation-design opcode (map flat-type widths) (flat-type result-width) attributes))
  (hash-ref
   (interpret_snapshot
    snapshot
    (for/hash ([value (in-list inputs)] [index (in-naturals)])
      (values (format "x~a" index) value)))
   "y"))

(define (mask width)
  (sub1 (arithmetic-shift 1 width)))

(define (truncate value width)
  (bitwise-and value (mask width)))

(define (signed-value value width)
  (if (bitwise-bit-set? value (sub1 width))
      (- value (arithmetic-shift 1 width))
      value))

(define same-width-binary-oracles
  (list (cons "rtl.and" bitwise-and)
        (cons "rtl.or" bitwise-ior)
        (cons "rtl.xor" bitwise-xor)
        (cons "rtl.add" +)
        (cons "rtl.mul" *)
        (cons "rtl.sub" -)))

(for* ([width (in-range 1 5)]
       [operation+oracle (in-list same-width-binary-oracles)]
       [left (in-range (arithmetic-shift 1 width))]
       [right (in-range (arithmetic-shift 1 width))])
  (define opcode (car operation+oracle))
  (define oracle (cdr operation+oracle))
  (check-equal? (run-operation opcode (list width width) (list left right))
                (truncate (oracle left right) width)
                (format "~a width ~a inputs ~a ~a" opcode width left right)))

(for* ([width (in-range 1 5)]
       [value (in-range (arithmetic-shift 1 width))])
  (check-equal? (run-operation "rtl.not" (list width) (list value))
                (truncate (bitwise-not value) width)))

(define comparison-oracles
  (list (cons "rtl.eq" =)
        (cons "rtl.ult" <)
        (cons "rtl.slt"
              (lambda (left right width)
                (< (signed-value left width) (signed-value right width))))))

(for* ([width (in-range 1 5)]
       [operation+oracle (in-list comparison-oracles)]
       [left (in-range (arithmetic-shift 1 width))]
       [right (in-range (arithmetic-shift 1 width))])
  (define opcode (car operation+oracle))
  (define oracle (cdr operation+oracle))
  (define expected
    (if (equal? opcode "rtl.slt")
        (oracle left right width)
        (oracle left right)))
  (check-equal? (run-operation opcode (list width width) (list left right)
                               #:result-width 1)
                (if expected 1 0)))

(for* ([value-width (in-range 1 5)]
       [amount-width (in-range 1 5)]
       [value (in-range (arithmetic-shift 1 value-width))]
       [amount (in-range (arithmetic-shift 1 amount-width))])
  (check-equal? (run-operation "rtl.shl" (list value-width amount-width)
                               (list value amount))
                (truncate (arithmetic-shift value amount) value-width))
  (check-equal? (run-operation "rtl.shru" (list value-width amount-width)
                               (list value amount))
                (arithmetic-shift value (- amount)))
  (check-equal? (run-operation "rtl.shrs" (list value-width amount-width)
                               (list value amount))
                (truncate (arithmetic-shift (signed-value value value-width) (- amount))
                          value-width)))

(for* ([left-width (in-range 1 4)]
       [right-width (in-range 1 4)]
       [left (in-range (arithmetic-shift 1 left-width))]
       [right (in-range (arithmetic-shift 1 right-width))])
  (check-equal? (run-operation "rtl.concat" (list left-width right-width)
                               (list left right)
                               #:result-width (+ left-width right-width))
                (bitwise-ior (arithmetic-shift left right-width) right)))

(for* ([source-width (in-range 2 5)]
       [low (in-range source-width)]
       [high (in-range low source-width)]
       [value (in-range (arithmetic-shift 1 source-width))])
  (define result-width (add1 (- high low)))
  (check-equal? (run-operation "rtl.extract" (list source-width) (list value)
                               #:result-width result-width
                               #:attributes (hash "high" high "low" low))
                (truncate (arithmetic-shift value (- low)) result-width)))

(for* ([source-width (in-range 1 4)]
       [target-width (in-range (add1 source-width) 5)]
       [value (in-range (arithmetic-shift 1 source-width))])
  (check-equal? (run-operation "rtl.zext" (list source-width) (list value)
                               #:result-width target-width
                               #:attributes (hash "target_width" target-width))
                value)
  (check-equal? (run-operation "rtl.sext" (list source-width) (list value)
                               #:result-width target-width
                               #:attributes (hash "target_width" target-width))
                (truncate (signed-value value source-width) target-width)))

(for* ([source-width (in-range 2 5)]
       [target-width (in-range 1 source-width)]
       [value (in-range (arithmetic-shift 1 source-width))])
  (check-equal? (run-operation "rtl.trunc" (list source-width) (list value)
                               #:result-width target-width
                               #:attributes (hash "target_width" target-width))
                (truncate value target-width)))

(for* ([width (in-range 1 5)]
       [value (in-range (arithmetic-shift 1 width))])
  (check-equal? (run-operation "rtl.cast" (list width) (list value)) value))

(for* ([width (in-range 1 5)]
       [value (in-range (arithmetic-shift 1 width))])
  (check-equal? (run-operation "rtl.constant" '() '()
                               #:result-width width
                               #:attributes (hash "value" value))
                value))

(define mux-snapshot
  (operation-design "rtl.mux_lookup"
                    (list (flat-type 2) (flat-type 3) (flat-type 3) (flat-type 3))
                    (flat-type 3)
                    (hash "keys" (list 0 2))))
(for* ([selector (in-range 4)] [default (in-range 8)]
       [zero-choice (in-range 8)] [two-choice (in-range 8)])
  (check-equal?
   (hash-ref (interpret_snapshot mux-snapshot
                                 (hash "x0" selector "x1" default
                                       "x2" zero-choice "x3" two-choice))
             "y")
   (cond [(= selector 0) zero-choice]
         [(= selector 2) two-choice]
         [else default])))

(define decode-cases
  (list (list #b000 #b011 #b001 #b111)
        (list #b001 #b011 #b010 #b111)))
(define (decode-attributes cases [default-value #b111] [default-care #b111])
  (hash "cases" cases
        "default_value" default-value
        "default_care" default-care))
(define decode-snapshot
  (operation-design "rtl.decode" (list (flat-type 3)) (flat-type 3)
                    (decode-attributes decode-cases)))
(for ([selector (in-range 8)])
  (check-equal?
   (hash-ref (interpret_snapshot decode-snapshot (hash "x0" selector)) "y")
   (case (bitwise-and selector #b011)
     [(0) #b001]
     [(1) #b010]
     [else #b111])
   (format "decode selector ~a" selector)))

(define record-type
  (hash "kind" "record" "text" "record<a: bits<2>, b: bits<3>>" "width" 5
        "fields" (list (hash "name" "a" "type" (flat-type 2))
                       (hash "name" "b" "type" (flat-type 3)))))
(define record-create
  (operation-design "rtl.record_create" (list (flat-type 2) (flat-type 3))
                    record-type (hash "fields" (list "a" "b"))))
(for* ([a (in-range 4)] [b (in-range 8)])
  (check-equal? (hash-ref (interpret_snapshot record-create (hash "x0" a "x1" b)) "y")
                (+ (arithmetic-shift a 3) b)))
(define record-high
  (operation-design "rtl.record_get" (list record-type) (flat-type 2)
                    (hash "field" "a")))
(define record-low
  (operation-design "rtl.record_get" (list record-type) (flat-type 3)
                    (hash "field" "b")))
(for ([packed (in-range 32)])
  (check-equal? (hash-ref (interpret_snapshot record-high (hash "x0" packed)) "y")
                (arithmetic-shift packed -3))
  (check-equal? (hash-ref (interpret_snapshot record-low (hash "x0" packed)) "y")
                (truncate packed 3)))

(define vector-type
  (hash "kind" "vector" "text" "vec<3, bits<2>>" "width" 6 "length" 3
        "element_type" (flat-type 2)))
(define vector-create
  (operation-design "rtl.vector_create"
                    (list (flat-type 2) (flat-type 2) (flat-type 2)) vector-type))
(for* ([x0 (in-range 4)] [x1 (in-range 4)] [x2 (in-range 4)])
  (check-equal? (hash-ref (interpret_snapshot vector-create
                                               (hash "x0" x0 "x1" x1 "x2" x2))
                          "y")
                (+ x0 (arithmetic-shift x1 2) (arithmetic-shift x2 4))))
(for ([index (in-range 3)])
  (define vector-element
    (operation-design "rtl.vector_get" (list vector-type) (flat-type 2)
                      (hash "index" index)))
  (for ([packed (in-range 64)])
    (check-equal? (hash-ref (interpret_snapshot vector-element (hash "x0" packed)) "y")
                  (truncate (arithmetic-shift packed (* -2 index)) 2))))

(define aggregate-decode
  (operation-design
   "rtl.decode"
   (list record-type)
   vector-type
   (decode-attributes
    (list (list #b00000 #b11000 #b000110 #b111111)
          (list #b01000 #b11000 #b111000 #b111111))
    #b111111
    #b111111)))
(for ([packed (in-range 32)])
  (check-equal?
   (hash-ref (interpret_snapshot aggregate-decode (hash "x0" packed)) "y")
   (case (bitwise-and packed #b11000)
     [(0) #b000110]
     [(8) #b111000]
     [else #b111111])
   (format "aggregate decode input ~a" packed)))

(define wire-type (flat-type 3))
(define wire-design
  (single-module-snapshot
   (module-snapshot
    1 "ForwardWire"
    (list (port-snapshot "x" "input" wire-type #:value 10))
    (list (port-snapshot "y" "output" wire-type #:place 1201 #:driver 1000))
    (list (value-snapshot 10 "x" wire-type 100)
          (value-snapshot 1000 "forward" wire-type 1100))
    (list (place-snapshot 1200 "forward" wire-type 1100 10)
          (place-snapshot 1201 "y" wire-type 1300 1000))
    (list (operation-snapshot 100 "rtl.input_port" '() '(10) '())
          (operation-snapshot 1100 "rtl.wire" '() '(1000) '(1200))
          (operation-snapshot 1300 "rtl.output_port" '() '() '(1201))
          (operation-snapshot 1400 "rtl.drive" '(1000) '() '(1201))))))
(for ([value (in-range 8)])
  (check-equal? (hash-ref (interpret_snapshot wire-design (hash "x" value)) "y") value))

(define add-design
  (operation-design "rtl.add" (list (flat-type 4) (flat-type 4)) (flat-type 4)))
(define reversed-add-design
  (operation-design "rtl.add" (list (flat-type 4) (flat-type 4)) (flat-type 4)))
(define sub-design
  (operation-design "rtl.sub" (list (flat-type 4) (flat-type 4)) (flat-type 4)))
(define (input-assumption port value care)
  (hash "kind" "input_pattern" "port" port "value" value "care" care))
(define (onehot-assumption port)
  (hash "kind" "onehot" "port" port))
(define (output-target port value care)
  (hash "port" port "value" value "care" care))
(check-equal? (engine_result_status
               (check_equivalent_snapshots add-design reversed-add-design))
              "equivalent")
(define counterexample (check_equivalent_snapshots add-design sub-design))
(check-equal? (engine_result_status counterexample) "counterexample")
(define replay-inputs
  (for/hash ([entry (in-list (engine_result_inputs counterexample))])
    (values (hash-ref entry "port") (hash-ref entry "value"))))
(check-not-equal? (hash-ref (interpret_snapshot add-design replay-inputs) "y")
                  (hash-ref (interpret_snapshot sub-design replay-inputs) "y"))
(define unknown-result
  (check_equivalent_snapshots add-design add-design
                              #:solve (lambda (_mismatch) 'forced-unknown)))
(check-equal? (engine_result_status unknown-result) "unknown")
(check-true (regexp-match? #rx"unknown" (engine_result_message unknown-result)))

(define zero-b-assumptions (list (input-assumption "x1" 0 #b1111)))
(define low-zero-b-assumptions (list (input-assumption "x1" 0 #b0111)))
(check-equal?
 (engine_result_status
  (check_equivalent_snapshots add-design sub-design zero-b-assumptions))
 "equivalent")
(check-equal?
 (engine_result_status
  (check_equivalent_snapshots add-design sub-design low-zero-b-assumptions))
 "equivalent")
(define assumed-counterexample
  (check_equivalent_snapshots
   add-design sub-design (list (input-assumption "x1" 1 #b1111))))
(check-equal? (engine_result_status assumed-counterexample) "counterexample")
(check-equal?
 (hash-ref (findf (lambda (entry) (equal? (hash-ref entry "port") "x1"))
                  (engine_result_inputs assumed-counterexample))
           "value")
 1)
(define vacuous-result
  (check_equivalent_snapshots
   add-design
   sub-design
   (list (input-assumption "x1" 0 #b1111)
         (input-assumption "x1" 1 #b1111))))
(check-equal? (engine_result_status vacuous-result) "vacuous")
(check-true (regexp-match? #rx"unsatisfiable" (engine_result_message vacuous-result)))
(check-equal? (hash-ref (engine_result_diagnostic vacuous-result) "module") "Operation")
(define assumption-solver-calls 0)
(define unknown-assumption-result
  (check_equivalent_snapshots
   add-design
   sub-design
   zero-b-assumptions
   #:solve (lambda (_query)
             (set! assumption-solver-calls (add1 assumption-solver-calls))
             'forced-unknown)))
(check-equal? (engine_result_status unknown-assumption-result) "unknown")
(check-true (regexp-match? #rx"checking formal assumptions"
                           (engine_result_message unknown-assumption-result)))
(check-equal? assumption-solver-calls 1)

(define reachable-add
  (check_reachable_snapshot add-design (output-target "y" 5 #b1111)))
(check-equal? (reachability_result_status reachable-add) "reachable")
(check-equal? (hash-ref (reachability_result_output reachable-add) "port") "y")
(check-equal? (hash-ref (reachability_result_output reachable-add) "value") 5)
(define reachable-add-inputs
  (for/hash ([entry (in-list (reachability_result_inputs reachable-add))])
    (values (hash-ref entry "port") (hash-ref entry "value"))))
(check-equal? (hash-ref (interpret_snapshot add-design reachable-add-inputs) "y") 5)

(define partial-reachable-add
  (check_reachable_snapshot add-design (output-target "y" #b1000 #b1100)))
(check-equal? (reachability_result_status partial-reachable-add) "reachable")
(check-equal?
 (bitwise-and (hash-ref (reachability_result_output partial-reachable-add) "value")
              #b1100)
 #b1000)

(define assumed-reachable-add
  (check_reachable_snapshot
   add-design
   (output-target "y" 5 #b1111)
   (list (input-assumption "x0" 1 #b1111))))
(check-equal? (reachability_result_status assumed-reachable-add) "reachable")
(check-equal?
 (hash-ref (findf (lambda (entry) (equal? (hash-ref entry "port") "x0"))
                  (reachability_result_inputs assumed-reachable-add))
           "value")
 1)

(define constant-seven-design
  (operation-design "rtl.constant" '() (flat-type 4) (hash "value" 7)))
(define unreachable-constant
  (check_reachable_snapshot constant-seven-design (output-target "y" 8 #b1111)))
(check-equal? (reachability_result_status unreachable-constant) "unreachable")
(check-equal? (reachability_result_inputs unreachable-constant) '())
(check-false (reachability_result_output unreachable-constant))

(define proved-constant
  (check_property_snapshot constant-seven-design (output-target "y" 7 #b1111)))
(check-equal? (property_result_status proved-constant) "proved")
(check-equal? (property_result_inputs proved-constant) '())
(check-false (property_result_output proved-constant))
(define constant-property-counterexample
  (check_property_snapshot constant-seven-design (output-target "y" 8 #b1111)))
(check-equal? (property_result_status constant-property-counterexample)
              "counterexample")
(check-equal? (hash-ref (property_result_output constant-property-counterexample) "value")
              7)

(define assumed-proved-add
  (check_property_snapshot
   add-design
   (output-target "y" 5 #b1111)
   (list (input-assumption "x0" 1 #b1111)
         (input-assumption "x1" 4 #b1111))))
(check-equal? (property_result_status assumed-proved-add) "proved")
(define add-property-counterexample
  (check_property_snapshot add-design (output-target "y" 0 #b1000)))
(check-equal? (property_result_status add-property-counterexample)
              "counterexample")
(define add-property-inputs
  (for/hash ([entry (in-list (property_result_inputs add-property-counterexample))])
    (values (hash-ref entry "port") (hash-ref entry "value"))))
(define add-property-output
  (hash-ref (interpret_snapshot add-design add-property-inputs) "y"))
(check-equal? add-property-output
              (hash-ref (property_result_output add-property-counterexample) "value"))
(check-not-equal? (bitwise-and add-property-output #b1000) 0)

(define vacuous-reachability
  (check_reachable_snapshot
   add-design
   (output-target "y" 5 #b1111)
   (list (input-assumption "x0" 0 #b1111)
         (input-assumption "x0" 1 #b1111))))
(check-equal? (reachability_result_status vacuous-reachability) "vacuous")
(check-true (regexp-match? #rx"unsatisfiable"
                           (reachability_result_message vacuous-reachability)))
(define vacuous-property
  (check_property_snapshot
   add-design
   (output-target "y" 5 #b1111)
   (list (input-assumption "x0" 0 #b1111)
         (input-assumption "x0" 1 #b1111))))
(check-equal? (property_result_status vacuous-property) "vacuous")
(check-true (regexp-match? #rx"unsatisfiable"
                           (property_result_message vacuous-property)))

(define reachability-solver-calls 0)
(define unknown-reachability
  (check_reachable_snapshot
   add-design
   (output-target "y" 5 #b1111)
   #:solve (lambda (_query)
             (set! reachability-solver-calls (add1 reachability-solver-calls))
             'forced-unknown)))
(check-equal? (reachability_result_status unknown-reachability) "unknown")
(check-equal? reachability-solver-calls 1)
(define property-solver-calls 0)
(define unknown-property
  (check_property_snapshot
   add-design
   (output-target "y" 5 #b1111)
   #:solve (lambda (_query)
             (set! property-solver-calls (add1 property-solver-calls))
             'forced-unknown)))
(check-equal? (property_result_status unknown-property) "unknown")
(check-equal? property-solver-calls 1)

(for ([target+message
       (in-list
        (list
         (cons "not-a-hash" #rx"output pattern is malformed")
         (cons (hash "port" "y" "value" 0) #rx"missing required field")
         (cons (output-target "missing" 0 1) #rx"unknown output")
         (cons (output-target "y" 16 15) #rx"invalid packed pattern")
         (cons (output-target "y" 8 7) #rx"invalid packed pattern")))])
  (define malformed-reachability-solver-called? #f)
  (check-exn
   (cdr target+message)
   (lambda ()
     (check_reachable_snapshot
      add-design
      (car target+message)
      #:solve (lambda (_query)
                (set! malformed-reachability-solver-called? #t)
                'forced-unknown))))
  (check-false malformed-reachability-solver-called?))
(define malformed-property-solver-called? #f)
(check-exn
 #rx"unknown output"
 (lambda ()
   (check_property_snapshot
    add-design
    (output-target "missing" 0 1)
    #:solve (lambda (_query)
              (set! malformed-property-solver-called? #t)
              'forced-unknown))))
(check-false malformed-property-solver-called?)

(for ([assumptions+message
       (in-list
        (list
         (cons "not-a-list" #rx"assumptions must be a list")
         (cons (list "not-a-hash") #rx"assumption 0 is malformed")
         (cons (list (hash "kind" "unknown")) #rx"unknown kind")
         (cons (list (input-assumption "missing" 0 1)) #rx"unknown input")
         (cons (list (input-assumption "x1" 16 15)) #rx"invalid packed pattern")
         (cons (list (input-assumption "x1" 8 7)) #rx"invalid packed pattern")))])
  (define malformed-solver-called? #f)
  (check-exn
   (cdr assumptions+message)
   (lambda ()
     (check_equivalent_snapshots
      add-design sub-design (car assumptions+message)
      #:solve (lambda (_query)
                (set! malformed-solver-called? #t)
                'forced-unknown))))
  (check-false malformed-solver-called?))

(define onehot-snapshot
  (operation-design "rtl.onehot_mux"
                    (list (flat-type 3) (flat-type 4) (flat-type 4) (flat-type 4))
                    (flat-type 4)))
(for* ([selector+index (in-list '((1 . 1) (2 . 2) (4 . 3)))]
       [a (in-range 16)] [b (in-range 16)] [c (in-range 16)])
  (define selector (car selector+index))
  (define choices (list #f a b c))
  (check-equal?
   (hash-ref (interpret_snapshot onehot-snapshot
                                 (hash "x0" selector "x1" a "x2" b "x3" c))
             "y")
   (list-ref choices (cdr selector+index))))
(for ([selector (in-list '(0 3 5 6 7))])
  (check-exn
   #rx"not exactly one-hot"
   (lambda ()
     (interpret_snapshot onehot-snapshot
                         (hash "x0" selector "x1" 1 "x2" 2 "x3" 3)))))

(define (rewrite-onehot snapshot transform)
  (hash-set
   snapshot
   "modules"
   (for/list ([module (in-list (hash-ref snapshot "modules"))])
     (hash-set
      module
      "operations"
      (for/list ([operation (in-list (hash-ref module "operations"))])
        (if (equal? (hash-ref operation "opcode") "rtl.onehot_mux")
            (transform operation)
            operation))))))
(define swapped-onehot
  (rewrite-onehot onehot-snapshot
                  (lambda (operation)
                    (hash-set operation "operands" '(10 12 11 13)))))
(define onehot-query (list (onehot-assumption "x0")))
(check-equal?
 (engine_result_status
  (check_equivalent_snapshots onehot-snapshot onehot-snapshot onehot-query))
 "equivalent")
(check-equal?
 (engine_result_status
  (check_equivalent_snapshots
   onehot-snapshot onehot-snapshot (list (input-assumption "x0" 1 7))))
 "equivalent")
(define onehot-counterexample
  (check_equivalent_snapshots onehot-snapshot swapped-onehot onehot-query))
(check-equal? (engine_result_status onehot-counterexample) "counterexample")
(define onehot-counterexample-selector
  (hash-ref (findf (lambda (entry) (equal? (hash-ref entry "port") "x0"))
                   (engine_result_inputs onehot-counterexample))
            "value"))
(check-not-false (member onehot-counterexample-selector '(1 2 4)))
(for ([assumptions (in-list (list '()
                                  (list (input-assumption "x0" 1 1))))])
  (define rejected
    (check_equivalent_snapshots onehot-snapshot onehot-snapshot assumptions))
  (check-equal? (engine_result_status rejected) "unsupported")
  (check-equal? (hash-ref (engine_result_diagnostic rejected) "opcode")
                "rtl.onehot_mux")
  (check-true (regexp-match? #rx"do not prove" (engine_result_message rejected))))
(define vacuous-onehot
  (check_equivalent_snapshots
   onehot-snapshot onehot-snapshot
   (list (onehot-assumption "x0") (input-assumption "x0" 0 7))))
(check-equal? (engine_result_status vacuous-onehot) "vacuous")
(define onehot-validity-solver-calls 0)
(define unknown-onehot-validity
  (check_equivalent_snapshots
   onehot-snapshot onehot-snapshot
   #:solve (lambda (_query)
             (set! onehot-validity-solver-calls (add1 onehot-validity-solver-calls))
             'forced-unknown)))
(check-equal? (engine_result_status unknown-onehot-validity) "unknown")
(check-true (regexp-match? #rx"one-hot validity"
                           (engine_result_message unknown-onehot-validity)))
(check-equal? onehot-validity-solver-calls 1)

(define reachable-onehot
  (check_reachable_snapshot onehot-snapshot
                            (output-target "y" 7 #b1111)
                            onehot-query))
(check-equal? (reachability_result_status reachable-onehot) "reachable")
(check-not-false
 (member
  (hash-ref (findf (lambda (entry) (equal? (hash-ref entry "port") "x0"))
                   (reachability_result_inputs reachable-onehot))
            "value")
  '(1 2 4)))
(define unsupported-onehot-reachability
  (check_reachable_snapshot onehot-snapshot (output-target "y" 7 #b1111)))
(check-equal? (reachability_result_status unsupported-onehot-reachability)
              "unsupported")
(check-equal? (hash-ref (reachability_result_diagnostic unsupported-onehot-reachability)
                        "opcode")
              "rtl.onehot_mux")
(define proved-onehot-contract
  (check_property_snapshot onehot-snapshot
                           (output-target "y" 0 0)
                           onehot-query))
(check-equal? (property_result_status proved-onehot-contract) "proved")
(define onehot-property-counterexample
  (check_property_snapshot onehot-snapshot
                           (output-target "y" 7 #b1111)
                           onehot-query))
(check-equal? (property_result_status onehot-property-counterexample)
              "counterexample")
(check-not-equal? (hash-ref (property_result_output onehot-property-counterexample) "value")
                  7)
(define unsupported-onehot-property
  (check_property_snapshot onehot-snapshot (output-target "y" 0 0)))
(check-equal? (property_result_status unsupported-onehot-property) "unsupported")
(check-equal? (hash-ref (property_result_diagnostic unsupported-onehot-property)
                        "opcode")
              "rtl.onehot_mux")

(check-exn
 #rx"choice count"
 (lambda ()
   (preflight_snapshot
    (rewrite-onehot onehot-snapshot
                    (lambda (operation)
                      (hash-set operation "operands" '(10 11 12)))))))
(check-exn
 #rx"choice width"
 (lambda ()
   (preflight_snapshot
    (operation-design "rtl.onehot_mux"
                      (list (flat-type 3) (flat-type 4) (flat-type 2) (flat-type 4))
                      (flat-type 4)))))

(define reordered-decode
  (operation-design "rtl.decode" (list (flat-type 3)) (flat-type 3)
                    (decode-attributes (reverse decode-cases))))
(define defective-decode
  (operation-design
   "rtl.decode"
   (list (flat-type 3))
   (flat-type 3)
   (decode-attributes
    (list (list #b000 #b011 #b011 #b111)
          (second decode-cases)))))
(check-equal? (engine_result_status
               (check_equivalent_snapshots decode-snapshot reordered-decode))
              "equivalent")
(define decode-counterexample
  (check_equivalent_snapshots decode-snapshot defective-decode))
(check-equal? (engine_result_status decode-counterexample) "counterexample")
(define decode-replay-inputs
  (for/hash ([entry (in-list (engine_result_inputs decode-counterexample))])
    (values (hash-ref entry "port") (hash-ref entry "value"))))
(check-not-equal? (hash-ref (interpret_snapshot decode-snapshot decode-replay-inputs) "y")
                  (hash-ref (interpret_snapshot defective-decode decode-replay-inputs) "y"))

(for ([opcode (in-list '("rtl.dont_care"
                          "rtl.register" "rtl.memory" "rtl.memory_write"
                          "verif.assert" "sim.dpi_call" "unknown.operation"))])
  (define rejected
    (preflight_snapshot
     (operation-design opcode (list (flat-type 1)) (flat-type 1))))
  (check-equal? (engine_result_status rejected) "unsupported")
  (check-equal? (hash-ref (engine_result_diagnostic rejected) "opcode") opcode)
  (check-true (regexp-match? #rx"does not support" (engine_result_message rejected))))

(define solver-called? #f)
(define unsupported-query-result
  (check_equivalent_snapshots
   (operation-design "rtl.dont_care" (list (flat-type 1)) (flat-type 1))
   (operation-design "rtl.dont_care" (list (flat-type 1)) (flat-type 1))
   #:solve (lambda (_mismatch) (set! solver-called? #t) 'forced-unknown)))
(check-equal? (engine_result_status unsupported-query-result) "unsupported")
(check-false solver-called?)

(define partial-case-decode
  (operation-design
   "rtl.decode" (list (flat-type 1)) (flat-type 1)
   (decode-attributes (list (list 0 1 0 0)) 0 1)))
(define partial-default-decode
  (operation-design
   "rtl.decode" (list (flat-type 1)) (flat-type 1)
   (decode-attributes (list (list 0 1 0 1)) 0 0)))
(for ([snapshot (in-list (list partial-case-decode partial-default-decode))])
  (define rejected (preflight_snapshot snapshot))
  (check-equal? (engine_result_status rejected) "unsupported")
  (check-equal? (hash-ref (engine_result_diagnostic rejected) "opcode") "rtl.decode")
  (check-true (regexp-match? #rx"every output bit is cared"
                             (engine_result_message rejected))))
(define decode-solver-called? #f)
(define unsupported-decode-query-result
  (check_equivalent_snapshots
   partial-case-decode partial-case-decode
   #:solve (lambda (_mismatch) (set! decode-solver-called? #t) 'forced-unknown)))
(check-equal? (engine_result_status unsupported-decode-query-result) "unsupported")
(check-false decode-solver-called?)

(for ([attributes+message
       (in-list
        (list
         (cons (decode-attributes "not-a-list") #rx"cases must be a list")
         (cons (decode-attributes (list (list 0 1 0))) #rx"case 0 is malformed")
         (cons (decode-attributes (list (list 2 1 0 1))) #rx"input is malformed")
         (cons (decode-attributes (list (list 0 1 2 1))) #rx"output is malformed")
         (cons (decode-attributes (list (list 0 0 0 1) (list 0 1 1 1)) 0 1)
               #rx"cases 0 and 1 overlap")
         (cons (decode-attributes '() 2 1) #rx"default is malformed")))])
  (check-exn
   (cdr attributes+message)
   (lambda ()
     (preflight_snapshot
      (operation-design "rtl.decode" (list (flat-type 1)) (flat-type 1)
                        (car attributes+message))))))

(check-exn #rx"missing required field"
           (lambda ()
             (preflight_snapshot
              (operation-design "rtl.extract" (list (flat-type 2)) (flat-type 1)))))
