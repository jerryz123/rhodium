#lang rosette
#| Interprets packed RTL for combinational equivalence and explicit single-clock register steps. |#
;; SPDX-License-Identifier: Apache-2.0

(require racket/list
         racket/match
         racket/set)

(provide check_equivalent_snapshots
         interpret_snapshot
         combinational_sample
         prepare_transition_snapshot
         transition_registers
         symbolic_transition_state
         symbolic_transition_inputs
         step_transition
         transition_step_outputs
         transition_step_state
         transition_step_obligations
         transition_step_validity
         preflight_snapshot
         engine_result_status
         engine_result_message
         engine_result_inputs
         engine_result_differences
         engine_result_diagnostic
         formal_engine_result)

(struct formal-engine-result (status message inputs differences diagnostic) #:transparent)
(struct exn:fail:formal:unsupported exn:fail (diagnostic) #:transparent)
(struct validity-obligation (condition module operation path) #:transparent)
(struct transition-step (outputs state obligations) #:transparent)
;; Exposes precise predicates so the typed Rhombus facade does not treat
;; opaque engine results as unrestricted foreign values.
(define formal_engine_result formal-engine-result?)

(define supported-opcodes
  (set "rtl.input_port"
       "rtl.output_port"
       "rtl.wire"
       "rtl.drive"
       "rtl.instance"
       "rtl.constant"
       "rtl.not"
       "rtl.and"
       "rtl.or"
       "rtl.any"
       "rtl.all"
       "rtl.xor"
       "rtl.add"
       "rtl.mul"
       "rtl.urem"
       "rtl.sub"
       "rtl.shl"
       "rtl.shru"
       "rtl.shrs"
       "rtl.eq"
       "rtl.ult"
       "rtl.slt"
       "rtl.mux_lookup"
       "rtl.decode"
       "rtl.onehot_mux"
       "rtl.cast"
       "rtl.concat"
       "rtl.extract"
       "rtl.zext"
       "rtl.sext"
       "rtl.trunc"
       "rtl.record_create"
       "rtl.record_get"
       "rtl.vector_create"
       "rtl.vector_get"))

#| Return the outcome tag used by the Rhombus facade. |#
(define (engine_result_status result)
  (formal-engine-result-status result))

#| Expose the optional explanation for a non-success result. |#
(define (engine_result_message result)
  (formal-engine-result-message result))

#| Return the complete input assignments for concrete replay. |#
(define (engine_result_inputs result)
  (formal-engine-result-inputs result))

#| Return paired output values that disagree on replay. |#
(define (engine_result_differences result)
  (formal-engine-result-differences result))

#| Expose the failing IR location when available. |#
(define (engine_result_diagnostic result)
  (formal-engine-result-diagnostic result))

#| Read a mandatory snapshot field or report the malformed record. |#
(define (required hash key [context "snapshot"])
  (hash-ref hash key
            (lambda ()
              (error 'rhodium-formal "~a is missing required field ~s" context key))))

#| Resolve a module-local numeric identity in its owning table. |#
(define (find-by-id values id context)
  (or (findf (lambda (value) (equal? (required value "id" context) id)) values)
      (error 'rhodium-formal "~a references missing id ~a" context id)))

#| Resolve a reachable module definition within a design snapshot. |#
(define (find-module snapshot id)
  (find-by-id (required snapshot "modules" "formal snapshot") id "module"))

#| Recover source identity and occurrence path, including after register lowering. |#
(define (diagnostic module operation message [path #f])
  (hash "module" (or (and operation (hash-ref operation "source_module" #f))
                       (and module (required module "name" "module")))
        "module_path" (or (and operation (hash-ref operation "source_path" #f))
                          path (and module (required module "name" "module")))
        "opcode" (and operation (required operation "opcode" "operation"))
        "operation_id" (and operation (hash-ref operation "source_operation_id"
                                                 (lambda () (required operation "id" "operation"))))
        "location" (and operation (required operation "location" "operation"))
        "origin" (and operation (required operation "origin" "operation"))
        "message" message))

#| Stop translation with a structured diagnostic rather than assigning arbitrary semantics. |#
(define (unsupported module operation message [path #f])
  (raise
   (exn:fail:formal:unsupported
    message
    (current-continuation-marks)
    (diagnostic module operation message path))))

#| Require a determinate positive packed width before constructing a bitvector. |#
(define (positive-width type module [operation #f])
  (define kind (required type "kind" "type"))
  (define width (required type "width" "type"))
  (unless (and (member kind '("flat" "record" "vector"))
               (exact-positive-integer? width))
    (unsupported module operation
                 (format "formal engine does not support hardware type ~a"
                         (required type "text" "type"))))
  width)

#| Check that the type and its nested elements have supported packed representations. |#
(define (validate-type type module [operation #f])
  (positive-width type module operation)
  (match (required type "kind" "type")
    ["record"
     (for ([field (in-list (required type "fields" "record type"))])
       (required field "name" "record field")
       (validate-type (required field "type" "record field") module operation))]
    ["vector"
     (unless (exact-positive-integer? (required type "length" "vector type"))
       (error 'rhodium-formal "vector snapshot has invalid length"))
     (validate-type (required type "element_type" "vector type") module operation)]
    [_ (void)]))

#| Check opcode-specific arity, attributes, and domains required for translation. |#
(define (validate-operation module operation module-ids)
  (define opcode (required operation "opcode" "operation"))
  (unless (set-member? supported-opcodes opcode)
    (unsupported module operation
                 (format "formal milestone 1 does not support operation ~a" opcode)))
  (define operands (required operation "operands" "operation"))
  (define results (required operation "results" "operation"))
  (define places (required operation "places" "operation"))
  (define attributes (required operation "attributes" "operation"))
  #| Reject an operation with a malformed operand, result, or place count. |#
  (define (arity expected-operands expected-results expected-places)
    (unless (and (or (not expected-operands) (= (length operands) expected-operands))
                 (or (not expected-results) (= (length results) expected-results))
                 (or (not expected-places) (= (length places) expected-places)))
      (error 'rhodium-formal "operation ~a has malformed snapshot arity" opcode)))
  (match opcode
    [(or "rtl.input_port") (arity 0 1 0)]
    [(or "rtl.output_port") (arity 0 0 1)]
    [(or "rtl.wire") (arity 0 1 1)]
    [(or "rtl.drive") (arity 1 0 1)]
    [(or "rtl.constant")
     (arity 0 1 0)
     (required attributes "value" "constant attributes")]
    [(or "rtl.not" "rtl.cast") (arity 1 1 0)]
    ["rtl.extract"
     (arity 1 1 0)
     (required attributes "high" "extract attributes")
     (required attributes "low" "extract attributes")]
    [(or "rtl.zext" "rtl.sext" "rtl.trunc")
     (arity 1 1 0)
     (required attributes "target_width" "extension or truncation attributes")]
    ["rtl.record_get"
     (arity 1 1 0)
     (required attributes "field" "record projection attributes")]
    ["rtl.vector_get"
     (arity 1 1 0)
     (required attributes "index" "vector projection attributes")]
    [(or "rtl.and" "rtl.or" "rtl.xor" "rtl.add" "rtl.mul" "rtl.sub" "rtl.urem"
         "rtl.shl" "rtl.shru" "rtl.shrs" "rtl.eq" "rtl.ult" "rtl.slt")
     (arity 2 1 0)]
    [(or "rtl.mux_lookup")
     (arity #f 1 0)
     (define keys (required attributes "keys" "mux attributes"))
     (unless (and (>= (length operands) 3)
                  (= (length keys) (- (length operands) 2)))
       (error 'rhodium-formal "mux snapshot has inconsistent keys and operands"))]
    ["rtl.onehot_mux"
     (arity #f 1 0)
     (unless (>= (length operands) 2)
       (error 'rhodium-formal "one-hot mux snapshot requires a selector and choices"))
     (define selector-width
       (value-width (module-value module (first operands))))
     (unless (= (sub1 (length operands)) selector-width)
       (error 'rhodium-formal "one-hot mux snapshot choice count does not match selector width"))
     (define result-width
       (value-width (module-value module (first results))))
     (for ([choice-id (in-list (rest operands))])
       (unless (= (value-width (module-value module choice-id)) result-width)
         (error 'rhodium-formal "one-hot mux snapshot choice width does not match result")))]
    ["rtl.decode"
     (arity 1 1 0)
     (define cases (required attributes "cases" "decode attributes"))
     (define default-value
       (required attributes "default_value" "decode attributes"))
     (define default-care
       (required attributes "default_care" "decode attributes"))
     (unless (list? cases)
       (error 'rhodium-formal "decode cases must be a list"))
     (define input-width
       (value-width (module-value module (first operands))))
     (define output-width
       (value-width (module-value module (first results))))
     #| Check that both packed masks fit and the value sets only cared bits. |#
     (define (pattern-valid? value care width)
       (define limit (arithmetic-shift 1 width))
       (and (exact-nonnegative-integer? value)
            (exact-nonnegative-integer? care)
            (< value limit)
            (< care limit)
            (= value (bitwise-and value care))))
     (for ([row (in-list cases)] [index (in-naturals)])
       (unless (and (list? row) (= (length row) 4))
         (error 'rhodium-formal "decode case ~a is malformed" index))
       (unless (pattern-valid? (first row) (second row) input-width)
         (error 'rhodium-formal "decode case ~a input is malformed" index))
       (unless (pattern-valid? (third row) (fourth row) output-width)
         (error 'rhodium-formal "decode case ~a output is malformed" index)))
     (unless (pattern-valid? default-value default-care output-width)
       (error 'rhodium-formal "decode default is malformed"))
     #| Detect two decode rows that can match the same input. |#
     (define (patterns-overlap? left right)
       (zero?
        (bitwise-and (bitwise-xor (first left) (first right))
                     (bitwise-and (second left) (second right)))))
     (for* ([left-index (in-range (length cases))]
            [right-index (in-range (add1 left-index) (length cases))])
       (when (patterns-overlap? (list-ref cases left-index)
                                (list-ref cases right-index))
         (error 'rhodium-formal "decode cases ~a and ~a overlap"
                left-index right-index)))
     (define full-output-care (sub1 (arithmetic-shift 1 output-width)))
     (unless (and (= default-care full-output-care)
                  (for/and ([row (in-list cases)])
                    (= (fourth row) full-output-care)))
       (unsupported module operation
                    "formal milestone 1 supports rtl.decode only when every output bit is cared"))]
    [(or "rtl.any" "rtl.all")
     (arity #f 1 0)
     (unless (and (pair? operands)
                  (= 1 (value-width (module-value module (first results)))))
       (error 'rhodium-formal "reduction requires operands and a one-bit result"))]
    [(or "rtl.concat")
     (arity #f 1 0)
     (unless (>= (length operands) 2)
       (error 'rhodium-formal "concat snapshot has fewer than two operands"))]
    ["rtl.record_create"
     (arity #f 1 0)
     (unless (positive? (length operands))
       (error 'rhodium-formal "record construction snapshot has no operands"))
     (unless (= (length (required attributes "fields" "record construction attributes"))
                (length operands))
       (error 'rhodium-formal "record snapshot has inconsistent fields and operands"))]
    ["rtl.vector_create"
     (arity #f 1 0)
     (unless (positive? (length operands))
       (error 'rhodium-formal "vector construction snapshot has no operands"))]
    ["rtl.instance"
     (arity 0 #f #f)
     (define child-id (required attributes "child_module" "instance attributes"))
     (required attributes "name" "instance attributes")
     (unless (set-member? module-ids child-id)
       (error 'rhodium-formal "instance references missing child module ~a" child-id))]
    [_ (void)]))

#| Preflight all ports, values, places, and operations of one module. |#
(define (validate-module snapshot module)
  (for ([port (in-list (append (required module "inputs" "module")
                               (required module "outputs" "module")))])
    (validate-type (required port "type" "port") module))
  (for ([value (in-list (required module "values" "module"))])
    (validate-type (required value "type" "value") module))
  (for ([place (in-list (required module "places" "module"))])
    (validate-type (required place "type" "place") module)
    (unless (exact-nonnegative-integer? (required place "driver" "place"))
      (error 'rhodium-formal "place ~a has no final driver"
             (required place "name" "place"))))
  (define module-ids
    (for/set ([candidate (in-list (required snapshot "modules" "formal snapshot"))])
      (required candidate "id" "module")))
  (for ([operation (in-list (required module "operations" "module"))])
    (validate-operation module operation module-ids)))

#| Validates the top module and every referenced module before symbolic execution. |#
(define (validate-snapshot snapshot)
  (define modules (required snapshot "modules" "formal snapshot"))
  (unless (pair? modules)
    (error 'rhodium-formal "formal snapshot has no modules"))
  (find-module snapshot (required snapshot "top" "formal snapshot"))
  (for ([module (in-list modules)])
    (validate-module snapshot module))
  #t)

#| Report unsupported semantics before starting a solver query. |#
(define (preflight_snapshot snapshot)
  (with-handlers ([exn:fail:formal:unsupported?
                   (lambda (exception)
                     (formal-engine-result
                      "unsupported"
                      (exn-message exception)
                      '()
                      '()
                      (exn:fail:formal:unsupported-diagnostic exception)))])
    (validate-snapshot snapshot)
    #f))

#| Read the type's canonical bitvector width. |#
(define (type-width type)
  (required type "width" "type"))

#| Get the packed width from a value descriptor. |#
(define (value-width value)
  (type-width (required value "type" "value")))

#| Resolve a value ID in this module's value table. |#
(define (module-value module id)
  (find-by-id (required module "values" "module") id "value"))

#| Resolve a place ID in this module's place table. |#
(define (module-place module id)
  (find-by-id (required module "places" "module") id "place"))

#| Read an opcode-specific attribute from its snapshot record. |#
(define (operation-attribute operation name)
  (required (required operation "attributes" "operation") name "operation attributes"))

#| Encode a logical result as a one-bit hardware value. |#
(define (bv-boolean condition)
  (if condition (bv 1 1) (bv 0 1)))

#| Concatenate packed fields in their supplied significance order. |#
(define (pack-concat values)
  (if (= (length values) 1)
      (car values)
      (apply concat values)))

#| Interpret unequal operand widths and oversized shift amounts at the hardware width. |#
(define (normalize-shift opcode value amount value-width amount-width)
  (define shift
    (match opcode
      ["rtl.shl" bvshl]
      ["rtl.shru" bvlshr]
      ["rtl.shrs" bvashr]))
  (cond
    [(= value-width amount-width) (shift value amount)]
    [(< amount-width value-width)
     (shift value (zero-extend amount (bitvector value-width)))]
    [else
     (define widened
       (if (equal? opcode "rtl.shrs")
           (sign-extend value (bitvector amount-width))
           (zero-extend value (bitvector amount-width))))
     (extract (sub1 value-width) 0 (shift widened amount))]))

#| Find the packed high and low bit positions for a named record field. |#
(define (record-field-range type field-name)
  (define total (type-width type))
  (let loop ([fields (required type "fields" "record type")]
             [consumed 0])
    (unless (pair? fields)
      (error 'rhodium-formal "record snapshot has no field named ~a" field-name))
    (define field (car fields))
    (define width (type-width (required field "type" "record field")))
    (if (equal? (required field "name" "record field") field-name)
        (values (- total consumed 1) (- total consumed width))
        (loop (cdr fields) (+ consumed width)))))

#| Construct the selector validity predicate without enumerating choices. |#
(define (exactly-one-bv value width)
  (define zero (bv 0 width))
  (&& (! (bveq value zero))
      (bveq (bvand value (bvsub value (bv 1 width))) zero)))

#| Evaluate one occurrence with local value caching and optional domain collection. |#
(define (interpret-module snapshot module-id input-values path [obligations #f])
  (define module (find-module snapshot module-id))
  (define value-snapshots (required module "values" "module"))
  (define place-snapshots (required module "places" "module"))
  (define operations (required module "operations" "module"))
  (define environment (make-hash))
  (define memo (make-hash))
  (define active (mutable-set))
  (define instance-cache (make-hash))
  (define result-operations (make-hash))
  (for ([operation (in-list operations)])
    (for ([result-id (in-list (required operation "results" "operation"))])
      (hash-set! result-operations result-id operation)))
  (for ([port (in-list (required module "inputs" "module"))])
    (define name (required port "name" "input port"))
    (define value-id (required port "value" "input port"))
    (unless (hash-has-key? input-values name)
      (error 'rhodium-formal "module ~a is missing input ~a" path name))
    (hash-set! environment value-id (hash-ref input-values name)))

  #| Resolve a value via its defining operation or memoized input. |#
  (define (eval-value id)
    (cond
      [(hash-has-key? environment id) (hash-ref environment id)]
      [(hash-has-key? memo id) (hash-ref memo id)]
      [(set-member? active id)
       (error 'rhodium-formal "malformed snapshot contains recursive value dependency ~a" id)]
      [else
       (set-add! active id)
       (define operation
         (hash-ref result-operations id
                   (lambda ()
                     (error 'rhodium-formal "value ~a has no defining operation" id))))
       (define result (eval-operation operation id))
       (set-remove! active id)
       (hash-set! memo id result)
       result]))

  #| Evaluate operands in their declared order. |#
  (define (operand-values operation)
    (map eval-value (required operation "operands" "operation")))

  #| Evaluate a child occurrence once and cache all its outputs. |#
  (define (ensure-instance operation)
    (define operation-id (required operation "id" "instance"))
    (unless (hash-has-key? instance-cache operation-id)
      (define child-id (operation-attribute operation "child_module"))
      (define child (find-module snapshot child-id))
      (define child-inputs (required child "inputs" "child module"))
      (define instance-places (required operation "places" "instance"))
      (unless (= (length child-inputs) (length instance-places))
        (error 'rhodium-formal "instance input count does not match child module"))
      (define child-values
        (for/hash ([port (in-list child-inputs)]
                   [place-id (in-list instance-places)])
          (define place (module-place module place-id))
          (values (required port "name" "child input")
                  (eval-value (required place "driver" "instance input place")))))
      (define instance-name (operation-attribute operation "name"))
      (define child-outputs
        (interpret-module snapshot child-id child-values
                          (string-append path "." instance-name)
                          obligations))
      (define result-ids (required operation "results" "instance"))
      (define output-ports (required child "outputs" "child module"))
      (unless (= (length result-ids) (length output-ports))
        (error 'rhodium-formal "instance output count does not match child module"))
      (hash-set!
       instance-cache
       operation-id
       (for/hash ([id (in-list result-ids)]
                  [port (in-list output-ports)])
         (values id (hash-ref child-outputs (required port "name" "child output"))))))
    (hash-ref instance-cache operation-id))

  #| Select the child output associated with this instance result. |#
  (define (eval-instance operation result-id)
    (define outputs (ensure-instance operation))
    (hash-ref outputs result-id))

  #| Interpret a supported opcode with packed Rosette bitvectors. |#
  (define (eval-operation operation result-id)
    (define opcode (required operation "opcode" "operation"))
    (define operands (operand-values operation))
    (define result-value (module-value module result-id))
    (define result-width (value-width result-value))
    (match opcode
      ["rtl.input_port"
       (error 'rhodium-formal "input value ~a was not bound" result-id)]
      ["rtl.wire"
       (define place-id (car (required operation "places" "wire")))
       (eval-value (required (module-place module place-id) "driver" "wire place"))]
      ["rtl.instance" (eval-instance operation result-id)]
      ["rtl.constant" (bv (operation-attribute operation "value") result-width)]
      ["rtl.not" (bvnot (car operands))]
      ["rtl.and" (bvand (first operands) (second operands))]
      ["rtl.or" (bvor (first operands) (second operands))]
      ;; Explicit symbolic folds retain every operand; host short-circuit loops
      ;; must not treat a symbolic Boolean term as a concrete truth value.
      ["rtl.any"
       (bv-boolean (for/fold ([result #f]) ([operand (in-list operands)])
                     (|| result (! (bveq operand (bv 0 (bitvector-size (type-of operand))))))))]
      ["rtl.all"
       (bv-boolean (for/fold ([result #t]) ([operand (in-list operands)])
                     (&& result (bveq operand (bv -1 (bitvector-size (type-of operand)))))))]
      ["rtl.xor" (bvxor (first operands) (second operands))]
      ["rtl.add" (bvadd (first operands) (second operands))]
      ["rtl.mul" (bvmul (first operands) (second operands))]
      ["rtl.urem"
       (define divisor (second operands))
       (define validity (! (bveq divisor (bv 0 result-width))))
       (when obligations
         (set-box! obligations
                   (cons (validity-obligation validity module operation path)
                         (unbox obligations))))
       (when (concrete? divisor)
         (unless validity
           (error 'rhodium-formal "concrete unsigned remainder divisor is zero")))
       (bvurem (first operands) divisor)]
      ["rtl.sub" (bvsub (first operands) (second operands))]
      [(or "rtl.shl" "rtl.shru" "rtl.shrs")
       (define operand-ids (required operation "operands" "shift"))
       (normalize-shift opcode
                        (first operands)
                        (second operands)
                        (value-width (module-value module (first operand-ids)))
                        (value-width (module-value module (second operand-ids))))]
      ["rtl.eq" (bv-boolean (bveq (first operands) (second operands)))]
      ["rtl.ult" (bv-boolean (bvult (first operands) (second operands)))]
      ["rtl.slt" (bv-boolean (bvslt (first operands) (second operands)))]
      ["rtl.mux_lookup"
       (define selector (first operands))
       (define default (second operands))
       (define choices (drop operands 2))
       (define keys (operation-attribute operation "keys"))
       (define selector-width
         (value-width
          (module-value module (first (required operation "operands" "mux")))))
       (for/fold ([selected default])
                 ([key (in-list keys)] [choice (in-list choices)])
         (if (bveq selector (bv key selector-width)) choice selected))]
      ["rtl.decode"
       (define selector (first operands))
       (define selector-id (first (required operation "operands" "decode")))
       (define selector-width (value-width (module-value module selector-id)))
       (define default (bv (operation-attribute operation "default_value") result-width))
       (for/fold ([selected default])
                 ([row (in-list (operation-attribute operation "cases"))])
         (define input-value (first row))
         (define input-care (second row))
         (define output-value (third row))
         (if (bveq (bvand selector (bv input-care selector-width))
                   (bv input-value selector-width))
             (bv output-value result-width)
             selected))]
      ["rtl.onehot_mux"
       (define selector (first operands))
       (define choices (rest operands))
       (define selector-id (first (required operation "operands" "one-hot mux")))
       (define selector-width (value-width (module-value module selector-id)))
       (define validity (exactly-one-bv selector selector-width))
       (when obligations
         (set-box! obligations
                   (cons (validity-obligation validity module operation path)
                         (unbox obligations))))
       (when (concrete? selector)
         (unless validity
           (error 'rhodium-formal
                  "concrete one-hot mux selector is not exactly one-hot")))
       (for/fold ([selected (bv 0 result-width)])
                 ([choice (in-list choices)] [index (in-naturals)])
         (bvor selected
               (if (bveq (extract index index selector) (bv 1 1))
                   choice
                   (bv 0 result-width))))]
      ["rtl.cast" (first operands)]
      ["rtl.concat" (pack-concat operands)]
      ["rtl.extract"
       (extract (operation-attribute operation "high")
                (operation-attribute operation "low")
                (first operands))]
      ["rtl.zext" (zero-extend (first operands) (bitvector result-width))]
      ["rtl.sext" (sign-extend (first operands) (bitvector result-width))]
      ["rtl.trunc" (extract (sub1 result-width) 0 (first operands))]
      ["rtl.record_create" (pack-concat operands)]
      ["rtl.record_get"
       (define operand-id (first (required operation "operands" "record get")))
       (define record-type (required (module-value module operand-id) "type" "record value"))
       (define-values (high low)
         (record-field-range record-type (operation-attribute operation "field")))
       (extract high low (first operands))]
      ["rtl.vector_create" (pack-concat (reverse operands))]
      ["rtl.vector_get"
       (define operand-id (first (required operation "operands" "vector get")))
       (define vector-type (required (module-value module operand-id) "type" "vector value"))
       (define element-width
         (type-width (required vector-type "element_type" "vector type")))
       (define low (* (operation-attribute operation "index") element-width))
       (extract (+ low element-width -1) low (first operands))]
      [_ (error 'rhodium-formal "preflight missed unsupported opcode ~a" opcode)]))

  (when obligations
    (for ([operation (in-list operations)])
      (match (required operation "opcode" "operation")
        [(or "rtl.onehot_mux" "rtl.urem")
         (eval-value (first (required operation "results" "partial operation")))]
        ["rtl.instance" (ensure-instance operation)]
        [_ (void)])))

  (for/hash ([port (in-list (required module "outputs" "module"))])
    (values (required port "name" "output port")
            (eval-value (required port "driver" "output port")))))

#| Evaluate the top occurrence on the supplied bitvector inputs. |#
(define (interpret-top-bv snapshot inputs [obligations #f])
  (define top-id (required snapshot "top" "formal snapshot"))
  (define top (find-module snapshot top-id))
  (interpret-module snapshot top-id inputs (required top "name" "top module") obligations))

#| Return the top definition and its input and output port lists. |#
(define (interfaces snapshot)
  (define top (find-module snapshot (required snapshot "top" "formal snapshot")))
  (values top
          (required top "inputs" "top module")
          (required top "outputs" "top module")))

#| Index port descriptors by their public names. |#
(define (port-map ports)
  (for/hash ([port (in-list ports)])
    (values (required port "name" "port") port)))

#| Check packed input constraints before passing them to the solver. |#
(define (validate-assumptions assumptions input-ports)
  (unless (list? assumptions)
    (error 'rhodium-formal "formal assumptions must be a list"))
  (define inputs (port-map input-ports))
  (for ([assumption (in-list assumptions)] [index (in-naturals)])
    (unless (hash? assumption)
      (error 'rhodium-formal "formal assumption ~a is malformed" index))
    (define kind (required assumption "kind" "formal assumption"))
    (unless (member kind '("input_pattern" "onehot"))
      (error 'rhodium-formal "formal assumption ~a has an unknown kind" index))
    (define name (required assumption "port" "formal input assumption"))
    (unless (and (string? name) (hash-has-key? inputs name))
      (error 'rhodium-formal "formal assumption ~a references unknown input ~a" index name))
    (when (equal? kind "input_pattern")
      (define value (required assumption "value" "formal input assumption"))
      (define care (required assumption "care" "formal input assumption"))
      (define width
        (type-width (required (hash-ref inputs name) "type" "input port")))
      (define limit (arithmetic-shift 1 width))
      (unless (and (exact-nonnegative-integer? value)
                   (exact-nonnegative-integer? care)
                   (< value limit)
                   (< care limit)
                   (= value (bitwise-and value care)))
        (error 'rhodium-formal "formal assumption ~a has an invalid packed pattern" index))))
  #t)

#| Match top input and output signatures before sharing symbolic inputs. |#
(define (check-interface left-snapshot right-snapshot)
  (define-values (left-top left-inputs left-outputs) (interfaces left-snapshot))
  (define-values (_right-top right-inputs right-outputs) (interfaces right-snapshot))
  #| Compare both port sets in one direction of the hardware interface. |#
  (define (check-kind kind left-ports right-ports)
    (define left-map (port-map left-ports))
    (define right-map (port-map right-ports))
    (unless (equal? (sort (hash-keys left-map) string<?)
                    (sort (hash-keys right-map) string<?))
      (unsupported left-top #f
                   (format "formal subjects have different ~a port names" kind)))
    (for ([name (in-list (hash-keys left-map))])
      (define left-width (type-width (required (hash-ref left-map name) "type" "port")))
      (define right-width (type-width (required (hash-ref right-map name) "type" "port")))
      (unless (= left-width right-width)
        (unsupported left-top #f
                     (format "formal subjects have different widths for ~a port ~a" kind name)))))
  (check-kind "input" left-inputs right-inputs)
  (check-kind "output" left-outputs right-outputs)
  (values left-top left-inputs left-outputs right-inputs right-outputs))

#| Complete a solver value and decode it as a packed natural for replay. |#
(define (model-natural value solution)
  (define evaluated (evaluate value solution))
  (if (concrete? evaluated)
      (bitvector->natural evaluated)
      0))

#| Ask Rosette for a model satisfying the supplied formula. |#
(define (default-solve query)
  (solve (assert query)))

#| Allocate an independent bitvector of the declared width for each top input. |#
(define (make-symbolic-inputs input-ports)
  (for/hash ([port (in-list input-ports)])
    (define name (required port "name" "input port"))
    (define width (type-width (required port "type" "input port")))
    (values name (constant (gensym (string-append "rhodium_" name "_"))
                           (bitvector width)))))

#| Conjoin all explicit input constraints over the shared symbolic sample. |#
(define (make-assumption-formula assumptions input-map symbolic-inputs)
  (for/fold ([result #t]) ([entry (in-list assumptions)])
    (define name (required entry "port" "formal input assumption"))
    (define width
      (type-width (required (hash-ref input-map name) "type" "input port")))
    (define input (hash-ref symbolic-inputs name))
    (define term
      (match (required entry "kind" "formal assumption")
        ["input_pattern"
         (define value (required entry "value" "formal input assumption"))
         (define care (required entry "care" "formal input assumption"))
         (bveq (bvand input (bv care width)) (bv value width))]
        ["onehot" (exactly-one-bv input width)]))
    (&& result term)))

#| Materialize a complete ordered assignment for a solver counterexample. |#
(define (model-assignments input-ports symbolic-inputs solution)
  (for/list ([port (in-list input-ports)])
    (define name (required port "name" "input port"))
    (hash "port" name
          "type" (required port "type" "input port")
          "value" (model-natural (hash-ref symbolic-inputs name) solution))))

#| Rebuild typed bitvectors from the packed replay assignment. |#
(define (assignments->inputs assignments input-map)
  (for/hash ([entry (in-list assignments)])
    (define name (required entry "port" "formal assignment"))
    (define port (hash-ref input-map name))
    (values name
            (bv (required entry "value" "formal assignment")
                (type-width (required port "type" "input port"))))))

#| Normalize missing differences to an empty list in non-witness results. |#
(define (make-equivalence-result status message inputs differences diagnostic)
  (formal-engine-result status message inputs (or differences '()) diagnostic))

#| Establish feasibility and partial-operation domains before solving the mismatch. |#
(define (solve-under-contract top assumptions assumption obligations solve-query
                              goal unsat-status sat-result make-result)
  #| Attach solver uncertainty to the relevant obligation or top module. |#
  (define (unknown-result message [obligation #f])
    (make-result
     "unknown" message '() #f
     (if obligation
         (diagnostic (validity-obligation-module obligation)
                     (validity-obligation-operation obligation)
                     message
                     (validity-obligation-path obligation))
         (diagnostic top #f message))))
  #| Classify the mismatch query and replay any satisfying assignment. |#
  (define (solve-goal)
    (define solution (solve-query (&& assumption goal)))
    (cond
      [(unsat? solution) (make-result unsat-status #f '() #f #f)]
      [(sat? solution) (sat-result solution)]
      [else (unknown-result "Rosette solver returned an unknown result")]))
  #| Prove each partial-operation precondition under the input assumptions. |#
  (define (check-validity remaining)
    (cond
      [(null? remaining) (solve-goal)]
      [else
       (define obligation (first remaining))
       (define proof
         (solve-query (&& assumption (! (validity-obligation-condition obligation)))))
       (cond
         [(unsat? proof) (check-validity (rest remaining))]
         [(sat? proof)
          (define message
            (if (equal? (required (validity-obligation-operation obligation) "opcode") "rtl.urem")
                "formal assumptions do not prove rtl.urem divisor is nonzero"
                "formal assumptions do not prove rtl.onehot_mux selector is exactly one-hot"))
          (make-result
           "unsupported" message '() #f
           (diagnostic (validity-obligation-module obligation)
                       (validity-obligation-operation obligation)
                       message
                       (validity-obligation-path obligation)))]
         [else
          (unknown-result
           "Rosette solver returned an unknown result while proving operation validity"
           obligation)])]))
  (if (null? assumptions)
      (check-validity obligations)
      (let ([feasibility (solve-query assumption)])
        (cond
          [(unsat? feasibility)
           (define message "formal assumptions are unsatisfiable")
           (make-result "vacuous" message '() #f (diagnostic top #f message))]
          [(sat? feasibility) (check-validity obligations)]
          [else
           (unknown-result
            "Rosette solver returned an unknown result while checking formal assumptions")]))))

#| Interpret both designs on shared inputs and solve for a differing output. |#
(define (run-equivalence-query left-snapshot right-snapshot assumptions solve-query)
  (validate-snapshot left-snapshot)
  (validate-snapshot right-snapshot)
  (define-values (left-top left-inputs left-outputs _right-inputs _right-outputs)
    (check-interface left-snapshot right-snapshot))
  (validate-assumptions assumptions left-inputs)
  (define symbolic-inputs (make-symbolic-inputs left-inputs))
  (define obligations (box '()))
  (define left-values (interpret-top-bv left-snapshot symbolic-inputs obligations))
  (define right-values (interpret-top-bv right-snapshot symbolic-inputs obligations))
  (define ordered-obligations (reverse (unbox obligations)))
  (define mismatches
    (for/list ([port (in-list left-outputs)])
      (define name (required port "name" "output port"))
      (! (bveq (hash-ref left-values name) (hash-ref right-values name)))))
  (define mismatch
    (for/fold ([result #f]) ([term (in-list mismatches)]) (|| result term)))
  (define input-map (port-map left-inputs))
  (define assumption (make-assumption-formula assumptions input-map symbolic-inputs))
  #| Replay the same complete assignment on both sides and record differences. |#
  (define (counterexample-result solution)
    (define assignments (model-assignments left-inputs symbolic-inputs solution))
    (define concrete-inputs (assignments->inputs assignments input-map))
    (define concrete-left-values (interpret-top-bv left-snapshot concrete-inputs))
    (define concrete-right-values (interpret-top-bv right-snapshot concrete-inputs))
    (define differences
      (for/list ([port (in-list left-outputs)]
                 #:when
                 (let* ([name (required port "name" "output port")]
                        [left (bitvector->natural (hash-ref concrete-left-values name))]
                        [right (bitvector->natural (hash-ref concrete-right-values name))])
                   (not (= left right))))
        (define name (required port "name" "output port"))
        (hash "port" name
              "type" (required port "type" "output port")
              "left" (bitvector->natural (hash-ref concrete-left-values name))
              "right" (bitvector->natural (hash-ref concrete-right-values name)))))
    (formal-engine-result "counterexample" #f assignments differences #f))
  (solve-under-contract left-top assumptions assumption ordered-obligations solve-query
                        mismatch "equivalent" counterexample-result make-equivalence-result))

#| Isolate solver state per equivalence query and report unsupported IR separately. |#
(define (check_equivalent_snapshots left-snapshot right-snapshot [assumptions '()]
                                    #:solve [solve-query default-solve])
  (with-handlers ([exn:fail:formal:unsupported?
                   (lambda (exception)
                     (formal-engine-result
                      "unsupported"
                      (exn-message exception)
                      '()
                      '()
                      (exn:fail:formal:unsupported-diagnostic exception)))])
    ;; Keep fail-closed preflight outside Rosette's verification-condition
    ;; exception wrapper so unsupported IR remains a typed formal result.
    (validate-snapshot left-snapshot)
    (validate-snapshot right-snapshot)
    (define-values (_left-top left-inputs _left-outputs
                              _right-inputs _right-outputs)
      (check-interface left-snapshot right-snapshot))
    (validate-assumptions assumptions left-inputs)
    (define isolated
      (with-terms '()
        (with-vc vc-true
          (run-equivalence-query left-snapshot right-snapshot assumptions solve-query))))
    (cond
      [(normal? isolated) (result-value isolated)]
      [else (raise (result-value isolated))])))

#| Evaluate a concrete combinational assignment and return packed natural outputs. |#
(define (interpret_snapshot snapshot concrete-inputs)
  (validate-snapshot snapshot)
  (define-values (_top inputs _outputs) (interfaces snapshot))
  (define bitvector-inputs
    (for/hash ([port (in-list inputs)])
      (define name (required port "name" "input port"))
      (define width (type-width (required port "type" "input port")))
      (define value
        (hash-ref concrete-inputs name
                  (lambda () (error 'rhodium-formal "missing concrete input ~a" name))))
      (values name (if (bitvector? value) value (bv value width)))))
  (for/hash ([(name value) (in-hash (interpret-top-bv snapshot bitvector-inputs))])
    (values name (bitvector->natural value))))

;; Each occurrence owns distinct state, even when several instances share a module.
(struct occurrence (module path bindings children) #:transparent)

#| Flatten occurrences into one combinational current-state to next-state relation. |#
(define (lower-register-transition snapshot clock supported? reject)
  (define modules (hash-ref snapshot "modules"))
  #| Find the definition for a child module or the designated top. |#
  (define (module-by-id id)
    (or (findf (lambda (m) (= (hash-ref m "id") id)) modules)
        (error 'register-transition "missing module ~a" id)))
  (define top (module-by-id (hash-ref snapshot "top")))
  #| Resolve an IR identifier in the supplied occurrence-local table. |#
  (define (lookup rows id)
    (or (findf (lambda (row) (= (hash-ref row "id") id)) rows)
        (error 'register-transition "missing IR id ~a" id)))
  #| Read a required opcode attribute from a lowering source. |#
  (define (attr op name) (hash-ref (hash-ref op "attributes") name))
  #| Find a source value in the module that owns this occurrence. |#
  (define (value occ id) (lookup (hash-ref (occurrence-module occ) "values") id))
  #| Follow a source place to the value that drives it. |#
  (define (driver occ id)
    (hash-ref (lookup (hash-ref (occurrence-module occ) "places") id) "driver"))
  #| Report the original occurrence when transition lowering cannot preserve semantics. |#
  (define (fail occ op message)
    (reject (occurrence-module occ) op message (occurrence-path occ)))
  #| Treat sampled reset as data but reject clocks in data positions. |#
  (define (packed-type occ type [op #f])
    (match (hash-ref type "kind")
      ["clock" (fail occ op "a clock may only drive the selected register clock through structural aliases")]
      ["reset"
       (unless (= 1 (hash-ref type "width")) (error 'register-transition "malformed Reset width"))
       (hash-set type "kind" "flat")]
      [_ type]))
  (define occurrences '())
  #| Bind and inventory each instance separately while rejecting recursive containment. |#
  (define (visit module path bindings ancestors)
    (define id (hash-ref module "id"))
    (when (member id ancestors) (error 'register-transition "recursive module hierarchy at ~a" path))
    (define occ (occurrence module path bindings (make-hash)))
    (set! occurrences (cons occ occurrences))
    (for ([op (in-list (hash-ref module "operations"))])
      (define opcode (hash-ref op "opcode"))
      (unless (or (supported? opcode) (member opcode '("rtl.register" "rtl.register_reset" "verif.assert")))
        (fail occ op (format "register transitions do not support operation ~a" opcode)))
      (when (equal? opcode "rtl.instance")
        (define child (module-by-id (attr op "child_module")))
        (define ports (hash-ref child "inputs"))
        (define places (hash-ref op "places"))
        (unless (= (length ports) (length places)) (error 'register-transition "instance input arity mismatch"))
        (define bindings
          (for/hash ([port (in-list ports)] [place (in-list places)])
            (values (hash-ref port "value") (cons occ (driver occ place)))))
        (hash-set! (occurrence-children occ) (hash-ref op "id")
                   (visit child (string-append path "." (attr op "name")) bindings (cons id ancestors)))))
    occ)
  (define root (visit top (hash-ref top "name") (hash) '()))
  (set! occurrences (reverse occurrences))
  (define clocks (filter (lambda (port) (equal? "clock" (hash-ref (hash-ref port "type") "kind")))
                         (hash-ref top "inputs")))
  (unless (and (string? clock) (= (length clocks) 1) (equal? clock (hash-ref (car clocks) "name")))
    (fail root #f "select the single top-level Clock input explicitly"))
  (define next-id 0)
  #| Allocate an unused identity in the synthetic combinational module. |#
  (define (fresh) (set! next-id (add1 next-id)) next-id)
  (define flat-values '())
  (define flat-ops '())
  (define flat-inputs '())
  (define flat-outputs '())
  (define flat-places '())
  (define memo (make-hash))
  (define active (mutable-set))
  #| Qualify a source value ID with its instance path before caching. |#
  (define (key occ id) (cons (occurrence-path occ) id))
  #| Attach original module, operation, and location to a synthetic opcode. |#
  (define (source-op occ op id opcode operands results places attributes)
    (hash "id" id "opcode" opcode "operands" operands "results" results "places" places
          "attributes" attributes "location" (if op (hash-ref op "location") "<transition>")
          "origin" (if op (hash-ref op "origin") "register transition")
          "source_module" (hash-ref (occurrence-module occ) "name")
          "source_path" (occurrence-path occ) "source_operation_id" (and op (hash-ref op "id"))))
  #| Create a synthetic input port and return its value ID. |#
  (define (add-input occ name type)
    (define id (fresh))
    (define op-id (fresh))
    (set! flat-values (cons (hash "id" id "name" name "type" type "defining_operation" op-id) flat-values))
    (set! flat-ops (cons (source-op occ #f op-id "rtl.input_port" '() (list id) '() (hash)) flat-ops))
    (set! flat-inputs (cons (hash "name" name "direction" "input" "type" type "value" id) flat-inputs))
    id)
  #| Expose a computed value as a driven synthetic output port. |#
  (define (add-output occ op name type id)
    (define place-id (fresh))
    (define op-id (fresh))
    (set! flat-places (cons (hash "id" place-id "name" name "type" type
                                  "owner_operation" op-id "driver" id) flat-places))
    (set! flat-ops (cons (source-op occ op op-id "rtl.output_port" '() '() (list place-id) (hash)) flat-ops))
    (set! flat-outputs (cons (hash "name" name "direction" "output" "type" type
                                   "place" place-id "driver" id) flat-outputs)))
  (hash-set! memo (key root (hash-ref (car clocks) "value")) 'selected-clock)
  (define inputs
    (for/list ([port (in-list (hash-ref top "inputs"))] #:unless (equal? port (car clocks)))
      (define name (format "i~a" (length flat-inputs)))
      (define type (packed-type root (hash-ref port "type")))
      (hash-set! memo (key root (hash-ref port "value")) (add-input root name type))
      (hash "name" (hash-ref port "name") "input" name "type" type)))
  (define sites
    (for*/list ([occ (in-list occurrences)]
                [op (in-list (hash-ref (occurrence-module occ) "operations"))]
                #:when (member (hash-ref op "opcode") '("rtl.register" "rtl.register_reset")))
      (define reset? (equal? (hash-ref op "opcode") "rtl.register_reset"))
      (unless (and (= (length (hash-ref op "operands")) (if reset? 3 1))
                   (= (length (hash-ref op "results")) 1) (= (length (hash-ref op "places")) 1))
        (error 'register-transition "malformed register arity"))
      (cons occ op)))
  (define registers
    (for/list ([site (in-list sites)] [index (in-naturals)])
      (define occ (car site))
      (define op (cdr site))
      (define current (car (hash-ref op "results")))
      (define type (packed-type occ (hash-ref (value occ current) "type") op))
      ;; Rhombus maps use stable-key equality: externally supplied immutable
      ;; state keys must match these identifiers, not mutable Racket strings.
      (define name (string->immutable-string (string-append (occurrence-path occ) "." (attr op "name"))))
      (define input (format "s~a" index))
      (hash-set! memo (key occ current) (add-input occ input type))
      (hash "name" name "input" input "next" (format "n~a" index) "type" type
            "module_path" (occurrence-path occ) "operation_id" (hash-ref op "id")
            "location" (hash-ref op "location") "origin" (hash-ref op "origin"))))
  (unless (= (length registers) (set-count (list->set (map (lambda (r) (hash-ref r "name")) registers))))
    (error 'register-transition "duplicate register occurrence identity"))
  ;; Resolve one value at a time. Eagerly evaluating all child inputs would create
  ;; false combinational recursion for legal feedback through a child's register.
  #| Follow only the needed instance bindings so register feedback breaks recursion. |#
  (define (resolve occ id)
    (define k (key occ id))
    (cond
      [(hash-has-key? memo k) (hash-ref memo k)]
      [(set-member? active k) (error 'register-transition "combinational cycle at ~a" k)]
      [else
       (set-add! active k)
       (define original (value occ id))
       (define op (lookup (hash-ref (occurrence-module occ) "operations") (hash-ref original "defining_operation")))
       (define result
         (match (hash-ref op "opcode")
           ["rtl.input_port"
            (define binding (hash-ref (occurrence-bindings occ) id))
            (resolve (car binding) (cdr binding))]
           ["rtl.wire" (resolve occ (driver occ (car (hash-ref op "places"))))]
           ["rtl.instance"
            (define child (hash-ref (occurrence-children occ) (hash-ref op "id")))
            (define index (index-of (hash-ref op "results") id))
            (define port (list-ref (hash-ref (occurrence-module child) "outputs") index))
            (resolve child (hash-ref port "driver"))]
           [_
            (define type (packed-type occ (hash-ref original "type") op))
            (define operands (map (lambda (v) (resolve occ v)) (hash-ref op "operands")))
            (when (member 'selected-clock operands) (fail occ op "clock used as data or derived logic"))
            (define new-id (fresh))
            (define op-id (fresh))
            (unless (= 1 (length (hash-ref op "results"))) (error 'register-transition "expected a single data result"))
            (set! flat-values (cons (hash "id" new-id "name" (hash-ref original "name") "type" type
                                          "defining_operation" op-id) flat-values))
            (set! flat-ops (cons (source-op occ op op-id (hash-ref op "opcode") operands (list new-id) '()
                                            (hash-ref op "attributes")) flat-ops))
            new-id]))
       (set-remove! active k)
       (hash-set! memo k result)
       result]))
  ;; Preserve unused operations and outputless occurrences for preflight/domain checks.
  (for* ([occ (in-list occurrences)] [v (in-list (hash-ref (occurrence-module occ) "values"))])
    (resolve occ (hash-ref v "id")))
  ;; Keep zero-result assertions even in outputless children. Their sampled
  ;; operands are private ports, never exposed as design outputs or assumptions.
  (define assertions
    (for*/list ([occ (in-list occurrences)] [op (in-list (hash-ref (occurrence-module occ) "operations"))]
                #:when (equal? (hash-ref op "opcode") "verif.assert"))
      (match-define (list condition clk reset guard) (hash-ref op "operands"))
      (unless (eq? 'selected-clock (resolve occ clk))
        (fail occ op "assertion clock does not alias the selected top clock"))
      (define taps
        (for/list ([id (in-list (list condition reset guard))])
          (define tap (format "a~a" (fresh)))
          (add-output occ op tap (packed-type occ (hash-ref (value occ id) "type") op) (resolve occ id))
          tap))
      (hash "taps" taps "label" (attr op "label")
            "diagnostic" (diagnostic (occurrence-module occ) op "active clocked assertion must hold"
                                     (occurrence-path occ)))))
  (for ([site (in-list sites)] [reg (in-list registers)])
    (define occ (car site))
    (define op (cdr site))
    (define operands (hash-ref op "operands"))
    (unless (eq? 'selected-clock (resolve occ (car operands)))
      (fail occ op "register clock does not alias the selected top clock"))
    (define next (resolve occ (driver occ (car (hash-ref op "places")))))
    (define type (hash-ref reg "type"))
    (define next-value
      (if (= 3 (length operands))
          (let ([id (fresh)] [op-id (fresh)])
            (set! flat-values (cons (hash "id" id "name" (hash-ref reg "next") "type" type
                                          "defining_operation" op-id) flat-values))
            (set! flat-ops (cons (source-op occ op op-id "rtl.mux_lookup"
                                            (list (resolve occ (second operands)) next (resolve occ (third operands)))
                                            (list id) '() (hash "keys" '(1))) flat-ops))
            id)
          next))
    (add-output occ op (hash-ref reg "next") type next-value))
  (define outputs
    (for/list ([port (in-list (hash-ref top "outputs"))] [index (in-naturals)])
      (define name (format "o~a" index))
      (define type (packed-type root (hash-ref port "type")))
      (add-output root #f name type (resolve root (hash-ref port "driver")))
      (hash "name" (hash-ref port "name") "output" name "type" type)))
  (define flat
    (hash "id" 0 "name" (hash-ref top "name") "values" (reverse flat-values)
          "places" (reverse flat-places) "operations" (reverse flat-ops)
          "inputs" (reverse flat-inputs) "outputs" (reverse flat-outputs)))
  (hash "snapshot" (hash "top" 0 "modules" (list flat))
        "clock" clock "inputs" inputs "registers" registers "outputs" outputs "assertions" assertions))

;; Internal transition primitives, not bounded proof queries. Lower once and
;; retain explicit state between calls. Normal combinational APIs keep their
;; existing preflight and do not silently acquire sequential semantics.
#| Lower and validate a reusable step plan for one selected clock. |#
(define (prepare_transition_snapshot snapshot clock)
  (define plan
    (lower-register-transition snapshot clock
                               (lambda (opcode) (set-member? supported-opcodes opcode))
                               unsupported))
  (validate-snapshot (hash-ref plan "snapshot"))
  plan)

#| Expose independently named state slots for each register occurrence. |#
(define (transition_registers plan) (hash-ref plan "registers"))

#| Allocate fresh bitvectors using the widths of the named descriptors. |#
(define (symbolic-transition-bindings descriptors)
  (for/hash ([entry (in-list descriptors)])
    (define-symbolic* value (bitvector (type-width (hash-ref entry "type"))))
    (values (hash-ref entry "name") value)))

;; Every register, resettable or not, starts independently unconstrained. Reset
;; values enter only through an actual transition with sampled reset asserted.
#| Allocate unconstrained initial state, including for resettable registers. |#
(define (symbolic_transition_state plan)
  (symbolic-transition-bindings (transition_registers plan)))

#| Allocate a fresh non-clock input sample for one edge. |#
(define (symbolic_transition_inputs plan)
  (symbolic-transition-bindings (hash-ref plan "inputs")))

#| Check exact keys and widths before binding a concrete or symbolic sample. |#
(define (packed-bindings descriptors assignments kind [key "input"] [who 'step_transition])
  (unless (and (hash? assignments)
               (equal? (list->set (hash-keys assignments))
                       (list->set (map (lambda (d) (hash-ref d "name")) descriptors))))
    (error who "~a must assign exactly the declared names" kind))
  (for/hash ([entry (in-list descriptors)])
    (define name (hash-ref entry "name"))
    (define width (type-width (hash-ref entry "type")))
    (define raw (hash-ref assignments name))
    (define value
      (cond
        [(and (concrete? raw) (exact-integer? raw))
         (unless (<= 0 raw (sub1 (arithmetic-shift 1 width)))
           (error who "~a ~a does not fit its packed width" kind name))
         (bv raw width)]
        [(equal? (type-of raw) (bitvector width)) raw]
        [else (error who "~a ~a requires an exact-width bitvector or packed natural" kind name)]))
    (values (hash-ref entry key) value)))

#| Preserve source identity for every partial operation, including unused ones in child modules. |#
(define (domain-obligations collected)
  (for/list ([obligation (in-list (reverse (unbox collected)))])
    (hash "kind" "domain" "condition" (validity-obligation-condition obligation)
          "diagnostic" (diagnostic (validity-obligation-module obligation)
                                   (validity-obligation-operation obligation)
                                   "partial operation requires a valid domain on this sampled edge"
                                   (validity-obligation-path obligation)))))

#| Sample a clockless RTL hierarchy with exact pins and return outputs plus domain obligations.
   Combinational preflight rejects state and effects; no dummy clock or reset is introduced. |#
(define (combinational_sample snapshot inputs)
  (validate-snapshot snapshot)
  (define-values (_top ports _outputs) (interfaces snapshot))
  (define pins (packed-bindings ports inputs "inputs" "name" 'combinational_sample))
  (define collected (box '()))
  (define outputs (interpret-top-bv snapshot pins collected))
  (transition-step outputs (hash) (domain-obligations collected)))

#| Observe pre-edge outputs and compute all next-state values simultaneously. |#
(define (step_transition plan state inputs)
  (define state-inputs (packed-bindings (transition_registers plan) state "state"))
  (define data-inputs (packed-bindings (hash-ref plan "inputs") inputs "inputs"))
  (define all-inputs
    (for/fold ([result state-inputs]) ([(key value) (in-hash data-inputs)])
      (hash-set result key value)))
  (define obligations (box '()))
  (define observed (interpret-top-bv (hash-ref plan "snapshot") all-inputs obligations))
  #| Translate synthetic port names back to externally identified results. |#
  (define (project descriptors field)
    (for/hash ([entry (in-list descriptors)])
      (values (hash-ref entry "name") (hash-ref observed (hash-ref entry field)))))
  (transition-step
   (project (hash-ref plan "outputs") "output")
   (project (transition_registers plan) "next")
   (append
    (domain-obligations obligations)
    (for/list ([site (in-list (hash-ref plan "assertions"))])
      (match-define (list condition reset guard)
        (map (lambda (tap) (bveq (hash-ref observed tap) (bv 1 1))) (hash-ref site "taps")))
      (hash "kind" "assertion" "label" (hash-ref site "label") "diagnostic" (hash-ref site "diagnostic")
            "condition" (=> (&& guard (! reset)) condition))))))

(define transition_step_outputs transition-step-outputs)
(define transition_step_state transition-step-state)
(define transition_step_obligations transition-step-obligations)
#| Conjoin domain and active assertion obligations without assuming or proving them. |#
(define (transition_step_validity step)
  (for/fold ([valid #t]) ([obligation (in-list (transition-step-obligations step))])
    (&& valid (hash-ref obligation "condition"))))
