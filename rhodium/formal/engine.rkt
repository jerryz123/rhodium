#lang rosette
;; Interprets immutable verified Rhodium snapshots for combinational equivalence, reachability, and property queries.

(require racket/list
         racket/match
         racket/set)

(provide check_equivalent_snapshots
         check_property_snapshot
         check_reachable_snapshot
         interpret_snapshot
         preflight_snapshot
         engine_result_status
         engine_result_message
         engine_result_inputs
         engine_result_differences
         engine_result_diagnostic
         reachability_result_status
         reachability_result_message
         reachability_result_inputs
         reachability_result_output
         reachability_result_diagnostic
         property_result_status
         property_result_message
         property_result_inputs
         property_result_output
         property_result_diagnostic
         formal_engine_result
         formal_reachability_result
         formal_property_result)

(struct formal-engine-result (status message inputs differences diagnostic) #:transparent)
(struct formal-reachability-result (status message inputs output diagnostic) #:transparent)
(struct formal-property-result (status message inputs output diagnostic) #:transparent)
(struct exn:fail:formal:unsupported exn:fail (diagnostic) #:transparent)
(struct validity-obligation (condition module operation path) #:transparent)

;; Exposes precise predicates so the typed Rhombus facade does not treat
;; opaque engine results as unrestricted foreign values.
(define formal_engine_result formal-engine-result?)
(define formal_reachability_result formal-reachability-result?)
(define formal_property_result formal-property-result?)

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
       "rtl.xor"
       "rtl.add"
       "rtl.mul"
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
       "rtl.vector_get"
       "rtl.vector_write_set"))

(define (engine_result_status result)
  (formal-engine-result-status result))

(define (engine_result_message result)
  (formal-engine-result-message result))

(define (engine_result_inputs result)
  (formal-engine-result-inputs result))

(define (engine_result_differences result)
  (formal-engine-result-differences result))

(define (engine_result_diagnostic result)
  (formal-engine-result-diagnostic result))

(define (reachability_result_status result)
  (formal-reachability-result-status result))

(define (reachability_result_message result)
  (formal-reachability-result-message result))

(define (reachability_result_inputs result)
  (formal-reachability-result-inputs result))

(define (reachability_result_output result)
  (formal-reachability-result-output result))

(define (reachability_result_diagnostic result)
  (formal-reachability-result-diagnostic result))

(define (property_result_status result)
  (formal-property-result-status result))

(define (property_result_message result)
  (formal-property-result-message result))

(define (property_result_inputs result)
  (formal-property-result-inputs result))

(define (property_result_output result)
  (formal-property-result-output result))

(define (property_result_diagnostic result)
  (formal-property-result-diagnostic result))

(define (required hash key [context "snapshot"])
  (hash-ref hash key
            (lambda ()
              (error 'rhodium-formal "~a is missing required field ~s" context key))))

(define (find-by-id values id context)
  (or (findf (lambda (value) (equal? (required value "id" context) id)) values)
      (error 'rhodium-formal "~a references missing id ~a" context id)))

(define (find-module snapshot id)
  (find-by-id (required snapshot "modules" "formal snapshot") id "module"))

(define (diagnostic module operation message [path #f])
  (hash "module" (and module (required module "name" "module"))
        "module_path" (or path (and module (required module "name" "module")))
        "opcode" (and operation (required operation "opcode" "operation"))
        "operation_id" (and operation (required operation "id" "operation"))
        "location" (and operation (required operation "location" "operation"))
        "origin" (and operation (required operation "origin" "operation"))
        "message" message))

(define (unsupported module operation message [path #f])
  (raise
   (exn:fail:formal:unsupported
    message
    (current-continuation-marks)
    (diagnostic module operation message path))))

(define (positive-width type module [operation #f])
  (define kind (required type "kind" "type"))
  (define width (required type "width" "type"))
  (unless (and (member kind '("flat" "record" "vector"))
               (exact-positive-integer? width))
    (unsupported module operation
                 (format "formal engine does not support hardware type ~a"
                         (required type "text" "type"))))
  width)

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

(define (validate-operation module operation module-ids)
  (define opcode (required operation "opcode" "operation"))
  (unless (set-member? supported-opcodes opcode)
    (unsupported module operation
                 (format "formal milestone 1 does not support operation ~a" opcode)))
  (define operands (required operation "operands" "operation"))
  (define results (required operation "results" "operation"))
  (define places (required operation "places" "operation"))
  (define attributes (required operation "attributes" "operation"))
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
    ["rtl.vector_write_set" (arity 4 1 0)]
    [(or "rtl.and" "rtl.or" "rtl.xor" "rtl.add" "rtl.mul" "rtl.sub"
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

(define (validate-snapshot snapshot)
  (unless (= (required snapshot "version" "formal snapshot") 1)
    (error 'rhodium-formal "unsupported formal snapshot version"))
  (define modules (required snapshot "modules" "formal snapshot"))
  (unless (pair? modules)
    (error 'rhodium-formal "formal snapshot has no modules"))
  (find-module snapshot (required snapshot "top" "formal snapshot"))
  (for ([module (in-list modules)])
    (validate-module snapshot module))
  #t)

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

(define (type-width type)
  (required type "width" "type"))

(define (value-width value)
  (type-width (required value "type" "value")))

(define (module-value module id)
  (find-by-id (required module "values" "module") id "value"))

(define (module-place module id)
  (find-by-id (required module "places" "module") id "place"))

(define (operation-attribute operation name)
  (required (required operation "attributes" "operation") name "operation attributes"))

(define (bv-boolean condition)
  (if condition (bv 1 1) (bv 0 1)))

(define (pack-concat values)
  (if (= (length values) 1)
      (car values)
      (apply concat values)))

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

(define (exactly-one-bv value width)
  (define zero (bv 0 width))
  (&& (! (bveq value zero))
      (bveq (bvand value (bvsub value (bv 1 width))) zero)))

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

  (define (operand-values operation)
    (map eval-value (required operation "operands" "operation")))

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

  (define (eval-instance operation result-id)
    (define outputs (ensure-instance operation))
    (hash-ref outputs result-id))

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
      ["rtl.xor" (bvxor (first operands) (second operands))]
      ["rtl.add" (bvadd (first operands) (second operands))]
      ["rtl.mul" (bvmul (first operands) (second operands))]
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
      ["rtl.vector_write_set"
       (define operand-ids (required operation "operands" "vector write set"))
       (define vector-type (required (module-value module (first operand-ids)) "type" "vector"))
       (define index-type (required (module-value module (third operand-ids)) "type" "indices"))
       (define count (required vector-type "length" "vector type"))
       (define ports (required index-type "length" "index vector type"))
       (define element-width (type-width (required vector-type "element_type" "vector type")))
       (define index-width (type-width (required index-type "element_type" "index vector type")))
       (define (lane value index width)
         (extract (sub1 (* (add1 index) width)) (* index width) value))
       (define enables
         (for/list ([p (in-range ports)]) (bveq (lane (second operands) p 1) (bv 1 1))))
       (define indices
         (for/list ([p (in-range ports)]) (lane (third operands) p index-width)))
       (define validity
         (&& (for/and ([enabled (in-list enables)] [index (in-list indices)])
               (|| (! enabled)
                   (if (= count (expt 2 index-width)) #t (bvult index (bv count index-width)))))
             (for*/and ([p (in-range ports)] [q (in-range (add1 p) ports)])
               (|| (! (list-ref enables p)) (! (list-ref enables q))
                   (! (bveq (list-ref indices p) (list-ref indices q)))))))
       (when obligations
         (set-box! obligations (cons (validity-obligation validity module operation path)
                                     (unbox obligations))))
       (when (and (not obligations) (concrete? validity) (! validity))
         (error 'rhodium-formal "concrete vector write set has an invalid enabled index or collision"))
       (pack-concat
        (reverse
         (for/list ([i (in-range count)])
           (for/fold ([selected (lane (first operands) i element-width)])
                     ([p (in-range ports)])
             (if (&& (list-ref enables p) (bveq (list-ref indices p) (bv i index-width)))
                 (lane (fourth operands) p element-width)
                 selected)))))]
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
        [(or "rtl.onehot_mux" "rtl.vector_write_set")
         (eval-value (first (required operation "results" "one-hot mux")))]
        ["rtl.instance" (ensure-instance operation)]
        [_ (void)])))

  (for/hash ([port (in-list (required module "outputs" "module"))])
    (values (required port "name" "output port")
            (eval-value (required port "driver" "output port")))))

(define (interpret-top-bv snapshot inputs [obligations #f])
  (define top-id (required snapshot "top" "formal snapshot"))
  (define top (find-module snapshot top-id))
  (interpret-module snapshot top-id inputs (required top "name" "top module") obligations))

(define (interfaces snapshot)
  (define top (find-module snapshot (required snapshot "top" "formal snapshot")))
  (values top
          (required top "inputs" "top module")
          (required top "outputs" "top module")))

(define (port-map ports)
  (for/hash ([port (in-list ports)])
    (values (required port "name" "port") port)))

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

(define (check-interface left-snapshot right-snapshot)
  (define-values (left-top left-inputs left-outputs) (interfaces left-snapshot))
  (define-values (_right-top right-inputs right-outputs) (interfaces right-snapshot))
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

(define (model-natural value solution)
  (define evaluated (evaluate value solution))
  (if (concrete? evaluated)
      (bitvector->natural evaluated)
      0))

(define (default-solve query)
  (solve (assert query)))

(define (make-symbolic-inputs input-ports)
  (for/hash ([port (in-list input-ports)])
    (define name (required port "name" "input port"))
    (define width (type-width (required port "type" "input port")))
    (values name (constant (gensym (string-append "rhodium_" name "_"))
                           (bitvector width)))))

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

(define (model-assignments input-ports symbolic-inputs solution)
  (for/list ([port (in-list input-ports)])
    (define name (required port "name" "input port"))
    (hash "port" name
          "type" (required port "type" "input port")
          "value" (model-natural (hash-ref symbolic-inputs name) solution))))

(define (assignments->inputs assignments input-map)
  (for/hash ([entry (in-list assignments)])
    (define name (required entry "port" "formal assignment"))
    (define port (hash-ref input-map name))
    (values name
            (bv (required entry "value" "formal assignment")
                (type-width (required port "type" "input port"))))))

(define (make-equivalence-result status message inputs differences diagnostic)
  (formal-engine-result status message inputs (or differences '()) diagnostic))

(define (validate-output-pattern target output-ports)
  (unless (hash? target)
    (error 'rhodium-formal "formal output pattern is malformed"))
  (define name (required target "port" "formal output pattern"))
  (define outputs (port-map output-ports))
  (unless (and (string? name) (hash-has-key? outputs name))
    (error 'rhodium-formal "formal output pattern references unknown output ~a" name))
  (define value (required target "value" "formal output pattern"))
  (define care (required target "care" "formal output pattern"))
  (define width
    (type-width (required (hash-ref outputs name) "type" "output port")))
  (define limit (arithmetic-shift 1 width))
  (unless (and (exact-nonnegative-integer? value)
               (exact-nonnegative-integer? care)
               (< value limit)
               (< care limit)
               (= value (bitwise-and value care)))
    (error 'rhodium-formal "formal output pattern has an invalid packed pattern"))
  #t)

(define (solve-under-contract top assumptions assumption obligations solve-query
                              goal unsat-status sat-result make-result)
  (define (unknown-result message [obligation #f])
    (make-result
     "unknown" message '() #f
     (if obligation
         (diagnostic (validity-obligation-module obligation)
                     (validity-obligation-operation obligation)
                     message
                     (validity-obligation-path obligation))
         (diagnostic top #f message))))
  (define (solve-goal)
    (define solution (solve-query (&& assumption goal)))
    (cond
      [(unsat? solution) (make-result unsat-status #f '() #f #f)]
      [(sat? solution) (sat-result solution)]
      [else (unknown-result "Rosette solver returned an unknown result")]))
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
            (if (equal? (required (validity-obligation-operation obligation) "opcode" "operation")
                        "rtl.vector_write_set")
                "formal assumptions do not prove enabled vector writes have in-range distinct indices"
                "formal assumptions do not prove rtl.onehot_mux selector is exactly one-hot"))
          (make-result
           "unsupported" message '() #f
           (diagnostic (validity-obligation-module obligation)
                       (validity-obligation-operation obligation)
                       message
                       (validity-obligation-path obligation)))]
         [else
          (unknown-result
           (if (equal? (required (validity-obligation-operation obligation) "opcode" "operation")
                       "rtl.vector_write_set")
               "Rosette solver returned an unknown result while proving vector write validity"
               "Rosette solver returned an unknown result while proving one-hot validity")
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

(define (run-output-query snapshot target assumptions solve-query mode)
  (validate-snapshot snapshot)
  (define-values (top inputs outputs) (interfaces snapshot))
  (validate-assumptions assumptions inputs)
  (validate-output-pattern target outputs)
  (define symbolic-inputs (make-symbolic-inputs inputs))
  (define obligations (box '()))
  (define output-values (interpret-top-bv snapshot symbolic-inputs obligations))
  (define ordered-obligations (reverse (unbox obligations)))
  (define input-map (port-map inputs))
  (define output-map (port-map outputs))
  (define assumption (make-assumption-formula assumptions input-map symbolic-inputs))
  (define name (required target "port" "formal output pattern"))
  (define value (required target "value" "formal output pattern"))
  (define care (required target "care" "formal output pattern"))
  (define width
    (type-width (required (hash-ref output-map name) "type" "output port")))
  (define matches
    (bveq (bvand (hash-ref output-values name) (bv care width)) (bv value width)))
  (define property? (equal? mode 'property))
  (define goal (if property? (! matches) matches))
  (define sat-status (if property? "counterexample" "reachable"))
  (define unsat-status (if property? "proved" "unreachable"))
  (define make-result
    (if property? formal-property-result formal-reachability-result))
  (define (witness-result solution)
    (define assignments (model-assignments inputs symbolic-inputs solution))
    (define concrete-inputs (assignments->inputs assignments input-map))
    (define concrete-outputs (interpret-top-bv snapshot concrete-inputs))
    (define concrete-value (bitvector->natural (hash-ref concrete-outputs name)))
    (define concrete-matches? (= (bitwise-and concrete-value care) value))
    (unless (if property? (not concrete-matches?) concrete-matches?)
      (error 'rhodium-formal "solver output witness failed concrete replay"))
    (make-result
     sat-status #f assignments
     (hash "port" name
           "type" (required (hash-ref output-map name) "type" "output port")
           "value" concrete-value)
     #f))
  (solve-under-contract top assumptions assumption ordered-obligations solve-query
                        goal unsat-status witness-result make-result))

(define (run-reachability-query snapshot target assumptions solve-query)
  (run-output-query snapshot target assumptions solve-query 'reachability))

(define (run-property-query snapshot target assumptions solve-query)
  (run-output-query snapshot target assumptions solve-query 'property))

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

(define (check_reachable_snapshot snapshot target [assumptions '()]
                                  #:solve [solve-query default-solve])
  (with-handlers ([exn:fail:formal:unsupported?
                   (lambda (exception)
                     (formal-reachability-result
                      "unsupported"
                      (exn-message exception)
                      '()
                      #f
                      (exn:fail:formal:unsupported-diagnostic exception)))])
    (validate-snapshot snapshot)
    (define-values (_top inputs outputs) (interfaces snapshot))
    (validate-assumptions assumptions inputs)
    (validate-output-pattern target outputs)
    (define isolated
      (with-terms '()
        (with-vc vc-true
          (run-reachability-query snapshot target assumptions solve-query))))
    (cond
      [(normal? isolated) (result-value isolated)]
      [else (raise (result-value isolated))])))

(define (check_property_snapshot snapshot target [assumptions '()]
                                 #:solve [solve-query default-solve])
  (with-handlers ([exn:fail:formal:unsupported?
                   (lambda (exception)
                     (formal-property-result
                      "unsupported"
                      (exn-message exception)
                      '()
                      #f
                      (exn:fail:formal:unsupported-diagnostic exception)))])
    (validate-snapshot snapshot)
    (define-values (_top inputs outputs) (interfaces snapshot))
    (validate-assumptions assumptions inputs)
    (validate-output-pattern target outputs)
    (define isolated
      (with-terms '()
        (with-vc vc-true
          (run-property-query snapshot target assumptions solve-query))))
    (cond
      [(normal? isolated) (result-value isolated)]
      [else (raise (result-value isolated))])))

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
