#lang rosette
#| Interprets packed RTL for combinational equivalence and explicit single-clock register steps. |#
;; SPDX-License-Identifier: Apache-2.0

(require racket/list
         racket/match
         racket/set
         (rename-in "preflight.rhm" [find_by_id find-by-id] [find_module find-module]
                    [port_map port-map] [validate_assumptions validate-assumptions])
         (prefix-in plan: "transition-plan.rhm"))

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

#| Preserve the public unsupported result while concrete validation stays solver-free. |#
(define (reject-diagnostic d)
  (raise (exn:fail:formal:unsupported (hash-ref d "message") (current-continuation-marks) d)))

#| Validate all concrete operations before interpreting any symbolic value. |#
(define (validate-snapshot snapshot) (validate_snapshot snapshot reject-diagnostic))

#| Report unsupported semantics before starting a solver query. |#
(define (preflight_snapshot snapshot)
  (define d (snapshot_diagnostic snapshot))
  (and d (formal-engine-result "unsupported" (hash-ref d "message") '() '() d)))

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

#| Route mismatched concrete interfaces through the public diagnostic boundary. |#
(define (check-interface left right) (check_interface left right reject-diagnostic))

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
(define (run-equivalence-query left-snapshot right-snapshot assumptions solve-query left-top left-inputs left-outputs)
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
    (define-values (left-top left-inputs left-outputs
                              _right-inputs _right-outputs)
      (check-interface left-snapshot right-snapshot))
    (validate-assumptions assumptions left-inputs)
    (define isolated
      (with-terms '()
        (with-vc vc-true
          (run-equivalence-query left-snapshot right-snapshot assumptions solve-query left-top left-inputs left-outputs))))
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

#| Adapt concrete single-clock lowering diagnostics to the engine's result protocol. |#
(define (prepare_transition_snapshot snapshot clock)
  (plan:prepare_transition_snapshot snapshot clock reject-diagnostic))

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
