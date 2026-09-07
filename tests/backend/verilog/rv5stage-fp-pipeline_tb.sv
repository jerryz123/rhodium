// Verifies the standalone FP pipeline's LSU bridge, hazards, backpressure, and F/D/Zfh/Zfa execution paths.
module rv5stage_fp_pipeline_tb;
  typedef struct packed {
    logic valid;
    RV5StageFpIssue bits;
  } issue_t;
  typedef struct packed {
    logic valid;
    RV5StageFpLoadReservation bits;
  } load_reserve_t;
  typedef struct packed {
    logic valid;
    RV5StageFpLoadCompletion bits;
  } load_completion_t;
  typedef struct packed {
    logic valid;
    RV5StageFpStoreRequest bits;
  } store_request_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  issue_t issue_in;
  struct packed {logic ready;} completion_in;
  load_reserve_t load_reserve_in;
  load_completion_t load_completion_in;
  store_request_t store_request_in;
  struct packed {logic ready;} issue_out;
  struct packed {logic valid; RV5StageFpCompletion bits;} completion_out;
  struct packed {logic ready;} load_reserve_out;
  struct packed {logic valid; RV5StageFpStoreResponse bits;} store_response_out;
  struct packed {logic valid; logic [4:0] bits;} state_update_out;
  logic [31:0] busy;
  logic drained;

  RV5StageFpPipeline dut (.*);
  always #5 clock = ~clock;

  task automatic load_register(input logic [4:0] rd,
                               input logic [63:0] data,
                               input logic [1:0] precision);
    begin
      @(negedge clock);
      load_reserve_in.valid = 1'b1;
      load_reserve_in.bits.context_0 = {3'b0, rd};
      load_reserve_in.bits.precision = precision;
      load_reserve_in.bits.rd = rd;
      do @(posedge clock); while (!load_reserve_out.ready);
      @(negedge clock);
      load_reserve_in.valid = 1'b0;
      load_completion_in.valid = 1'b1;
      load_completion_in.bits.context_0 = {3'b0, rd};
      load_completion_in.bits.precision = precision;
      load_completion_in.bits.rd = rd;
      load_completion_in.bits.data = data;
      #1;
      assert (state_update_out.valid);
      assert (state_update_out.bits == 5'b0);
      @(posedge clock);
      @(negedge clock);
      load_completion_in.valid = 1'b0;
    end
  endtask

  task automatic consume_completion;
    begin
      completion_in.ready = 1'b1;
      @(posedge clock);
      @(negedge clock);
      completion_in.ready = 1'b0;
    end
  endtask

  initial begin
    issue_in = '0;
    completion_in = '0;
    load_reserve_in = '0;
    load_completion_in = '0;
    store_request_in = '0;
    repeat (2) @(posedge clock);
    #1;
    reset = 1'b0;

    load_register(5'd1, 64'h3ff0000000000000, 2'd2);
    load_register(5'd2, 64'h0000000000000001, 2'd2);
    assert (busy == 32'b0);

    // A reserved load destination blocks a dependent arithmetic request.
    @(negedge clock);
    load_reserve_in.valid = 1'b1;
    load_reserve_in.bits.context_0 = 8'h44;
    load_reserve_in.bits.precision = 2'd2;
    load_reserve_in.bits.rd = 5'd4;
    @(posedge clock);
    @(negedge clock);
    load_reserve_in.valid = 1'b0;
    issue_in.valid = 1'b1;
    issue_in.bits = '0;
    issue_in.bits.control.registers.uses_frs1 = 1'b1;
    issue_in.bits.control.registers.destination = 2'd2;
    issue_in.bits.control.execution.unit = 4'd2;
    issue_in.bits.control.execution.source_precision = 2'd2;
    issue_in.bits.control.execution.destination_precision = 2'd2;
    issue_in.bits.frs1 = 5'd4;
    issue_in.bits.frs2 = 5'd2;
    issue_in.bits.frd = 5'd5;
    #1;
    assert (!issue_out.ready);
    issue_in.valid = 1'b0;
    load_completion_in.valid = 1'b1;
    load_completion_in.bits.context_0 = 8'h44;
    load_completion_in.bits.precision = 2'd2;
    load_completion_in.bits.rd = 5'd4;
    load_completion_in.bits.data = 64'h4010000000000000;
    @(posedge clock);
    @(negedge clock);
    load_completion_in.valid = 1'b0;

    // Issue an inexact FADD.D and hold its completion under backpressure.
    issue_in.valid = 1'b1;
    issue_in.bits = '0;
    issue_in.bits.context_0 = 8'ha5;
    issue_in.bits.control.registers.uses_frs1 = 1'b1;
    issue_in.bits.control.registers.uses_frs2 = 1'b1;
    issue_in.bits.control.registers.destination = 2'd2;
    issue_in.bits.control.execution.unit = 4'd2;
    issue_in.bits.control.execution.source_precision = 2'd2;
    issue_in.bits.control.execution.destination_precision = 2'd2;
    issue_in.bits.control.execution.uses_rounding_mode = 1'b1;
    issue_in.bits.frs1 = 5'd1;
    issue_in.bits.frs2 = 5'd2;
    issue_in.bits.frd = 5'd3;
    issue_in.bits.rounding_mode = 3'd0;
    do @(posedge clock); while (!issue_out.ready);
    @(negedge clock);
    issue_in.valid = 1'b0;
    wait (completion_out.valid);
    #1;
    assert (completion_out.bits.context_0 == 8'ha5);
    assert (completion_out.bits.destination == 2'd2);
    assert (completion_out.bits.rd == 5'd3);
    assert (completion_out.bits.fp_value == 64'h3ff0000000000000);
    assert (completion_out.bits.exception_flags == 5'b00001);
    assert (completion_out.bits.exception_flags_valid);
    assert (busy[3]);
    repeat (2) @(posedge clock);
    #1;
    assert (completion_out.valid);
    assert (completion_out.bits.fp_value == 64'h3ff0000000000000);
    consume_completion();
    #1;
    assert (!busy[3]);

    // A later FP load dirties FP state but must not replay stale arithmetic flags.
    load_register(5'd2, 64'h4000000000000000, 2'd2);

    // The variable-latency divide lane uses the same buffered completion path.
    issue_in.valid = 1'b1;
    issue_in.bits = '0;
    issue_in.bits.context_0 = 8'hd1;
    issue_in.bits.control.registers.uses_frs1 = 1'b1;
    issue_in.bits.control.registers.uses_frs2 = 1'b1;
    issue_in.bits.control.registers.destination = 2'd2;
    issue_in.bits.control.execution.unit = 4'd4;
    issue_in.bits.control.execution.source_precision = 2'd2;
    issue_in.bits.control.execution.destination_precision = 2'd2;
    issue_in.bits.control.execution.uses_rounding_mode = 1'b1;
    issue_in.bits.control.execution.divide_operation = 1'b0;
    issue_in.bits.frs1 = 5'd4;
    issue_in.bits.frs2 = 5'd2;
    issue_in.bits.frd = 5'd6;
    issue_in.bits.rounding_mode = 3'd0;
    do @(posedge clock); while (!issue_out.ready);
    @(negedge clock);
    issue_in.valid = 1'b0;
    wait (completion_out.valid);
    #1;
    assert (completion_out.bits.context_0 == 8'hd1);
    assert (completion_out.bits.fp_value == 64'h4000000000000000);
    assert (completion_out.bits.exception_flags == 5'b0);
    assert (busy[6]);
    repeat (4) begin
      @(negedge clock);
      assert (completion_out.valid && completion_out.bits.context_0 == 8'hd1 &&
              completion_out.bits.fp_value == 64'h4000000000000000 &&
              completion_out.bits.exception_flags == 5'b0 && busy[6])
        else $fatal(1, "divide completion changed while stalled");
    end
    consume_completion();
    #1;
    assert (!busy[6]);

    // The LSU store bridge observes the value written by the FP completion.
    store_request_in.valid = 1'b1;
    store_request_in.bits.context_0 = 8'h3c;
    store_request_in.bits.precision = 2'd2;
    store_request_in.bits.rs = 5'd3;
    @(posedge clock);
    @(negedge clock);
    store_request_in.valid = 1'b0;
    #1;
    assert (store_response_out.valid);
    assert (store_response_out.bits.context_0 == 8'h3c);
    assert (store_response_out.bits.data == 64'h3ff0000000000000);
    assert (drained) else $fatal(1, "read-only operand probe blocked architectural drain");
    @(posedge clock);
    @(negedge clock);
    #1;
    assert (drained);

    // Integer-source operations select their FP lane by destination precision.
    issue_in.valid = 1'b1;
    issue_in.bits = '0;
    issue_in.bits.context_0 = 8'hc1;
    issue_in.bits.control.registers.uses_integer_rs1 = 1'b1;
    issue_in.bits.control.registers.destination = 2'd2;
    issue_in.bits.control.execution.unit = 4'd8;
    issue_in.bits.control.execution.destination_precision = 2'd2;
    issue_in.bits.control.execution.integer_width = 1'b0;
    issue_in.bits.integer_operand = 64'd2;
    issue_in.bits.frd = 5'd7;
    issue_in.bits.rounding_mode = 3'd0;
    do @(posedge clock); while (!issue_out.ready);
    @(negedge clock);
    issue_in.valid = 1'b0;
    wait (completion_out.valid);
    #1;
    assert (completion_out.bits.context_0 == 8'hc1);
    assert (completion_out.bits.fp_value == 64'h4000000000000000);
    assert (completion_out.bits.exception_flags == 5'b0);
    consume_completion();

    issue_in.valid = 1'b1;
    issue_in.bits = '0;
    issue_in.bits.context_0 = 8'hc2;
    issue_in.bits.control.registers.uses_integer_rs1 = 1'b1;
    issue_in.bits.control.registers.destination = 2'd2;
    issue_in.bits.control.execution.unit = 4'd9;
    issue_in.bits.control.execution.destination_precision = 2'd2;
    issue_in.bits.integer_operand = 64'h3ff0000000000000;
    issue_in.bits.frd = 5'd8;
    do @(posedge clock); while (!issue_out.ready);
    @(negedge clock);
    issue_in.valid = 1'b0;
    wait (completion_out.valid);
    #1;
    assert (completion_out.bits.context_0 == 8'hc2);
    assert (completion_out.bits.fp_value == 64'h3ff0000000000000);
    assert (completion_out.bits.exception_flags == 5'b0);
    consume_completion();

    // FP-to-integer conversion retains the selected source lane and its flags.
    load_register(5'd9, 64'hc1e65a0bc0000000, 2'd2);
    issue_in.valid = 1'b1;
    issue_in.bits = '0;
    issue_in.bits.context_0 = 8'hc3;
    issue_in.bits.control.registers.uses_frs1 = 1'b1;
    issue_in.bits.control.registers.destination = 2'd1;
    issue_in.bits.control.execution.unit = 4'd8;
    issue_in.bits.control.execution.source_precision = 2'd2;
    issue_in.bits.control.execution.integer_width = 1'b0;
    issue_in.bits.frs1 = 5'd9;
    issue_in.bits.frd = 5'd10;
    issue_in.bits.rounding_mode = 3'd1;
    do @(posedge clock); while (!issue_out.ready);
    @(negedge clock);
    issue_in.valid = 1'b0;
    wait (completion_out.valid);
    #1;
    assert (completion_out.bits.context_0 == 8'hc3);
    assert (completion_out.bits.integer_value == 64'hffffffff80000000);
    assert (completion_out.bits.exception_flags == 5'b10000);
    assert (completion_out.bits.exception_flags_valid);
    completion_in.ready = 1'b1;
    #1;
    assert (state_update_out.valid);
    assert (state_update_out.bits == 5'b10000);
    @(posedge clock);
    @(negedge clock);
    completion_in.ready = 1'b0;

    // FP-to-integer moves copy raw low bits rather than applying NaN unboxing.
    load_register(5'd10, 64'h7fffffff11111111, 2'd2);
    issue_in.valid = 1'b1;
    issue_in.bits = '0;
    issue_in.bits.context_0 = 8'hc4;
    issue_in.bits.control.registers.uses_frs1 = 1'b1;
    issue_in.bits.control.registers.destination = 2'd1;
    issue_in.bits.control.execution.unit = 4'd9;
    issue_in.bits.control.execution.source_precision = 2'd1;
    issue_in.bits.frs1 = 5'd10;
    issue_in.bits.frd = 5'd11;
    do @(posedge clock); while (!issue_out.ready);
    @(negedge clock);
    issue_in.valid = 1'b0;
    wait (completion_out.valid);
    #1;
    assert (completion_out.bits.context_0 == 8'hc4);
    assert (completion_out.bits.integer_value == 64'h0000000011111111);
    assert (!completion_out.bits.exception_flags_valid);
    consume_completion();

    // Zfh arithmetic consumes and produces NaN-boxed binary16 values.
    load_register(5'd12, 64'h0000000000003c00, 2'd0);
    issue_in.valid = 1'b1;
    issue_in.bits = '0;
    issue_in.bits.context_0 = 8'h16;
    issue_in.bits.control.registers.uses_frs1 = 1'b1;
    issue_in.bits.control.registers.uses_frs2 = 1'b1;
    issue_in.bits.control.registers.destination = 2'd2;
    issue_in.bits.control.execution.unit = 4'd2;
    issue_in.bits.control.execution.source_precision = 2'd0;
    issue_in.bits.control.execution.destination_precision = 2'd0;
    issue_in.bits.control.execution.uses_rounding_mode = 1'b1;
    issue_in.bits.frs1 = 5'd12;
    issue_in.bits.frs2 = 5'd12;
    issue_in.bits.frd = 5'd13;
    issue_in.bits.rounding_mode = 3'd0;
    do @(posedge clock); while (!issue_out.ready);
    @(negedge clock);
    issue_in.valid = 1'b0;
    wait (completion_out.valid);
    #1;
    assert (completion_out.bits.context_0 == 8'h16);
    assert (completion_out.bits.fp_value == 64'hffffffffffff4000);
    assert (completion_out.bits.exception_flags == 5'b0);
    consume_completion();

    // Zfhmin H-to-D conversion reuses the cross-format conversion path.
    issue_in.valid = 1'b1;
    issue_in.bits = '0;
    issue_in.bits.context_0 = 8'h17;
    issue_in.bits.control.registers.uses_frs1 = 1'b1;
    issue_in.bits.control.registers.destination = 2'd2;
    issue_in.bits.control.execution.unit = 4'd8;
    issue_in.bits.control.execution.source_precision = 2'd0;
    issue_in.bits.control.execution.destination_precision = 2'd2;
    issue_in.bits.control.execution.uses_rounding_mode = 1'b1;
    issue_in.bits.frs1 = 5'd12;
    issue_in.bits.frd = 5'd14;
    issue_in.bits.rounding_mode = 3'd0;
    do @(posedge clock); while (!issue_out.ready);
    @(negedge clock);
    issue_in.valid = 1'b0;
    wait (completion_out.valid);
    #1;
    assert (completion_out.bits.context_0 == 8'h17);
    assert (completion_out.bits.fp_value == 64'h3ff0000000000000);
    assert (completion_out.bits.exception_flags == 5'b0);
    consume_completion();

    // A half-precision store exposes only the architectural low 16 bits.
    store_request_in.valid = 1'b1;
    store_request_in.bits.context_0 = 8'h18;
    store_request_in.bits.precision = 2'd0;
    store_request_in.bits.rs = 5'd13;
    @(posedge clock);
    @(negedge clock);
    store_request_in.valid = 1'b0;
    #1;
    assert (store_response_out.valid);
    assert (store_response_out.bits.context_0 == 8'h18);
    assert (store_response_out.bits.data == 64'h0000000000004000);
    @(posedge clock);
    @(negedge clock);
    #1;
    assert (drained);

    // Zfa FLI uses the encoded rs1 field as a constant selector without a
    // register dependency. Selector 18 is 1.5 in every supported format.
    issue_in.valid = 1'b1;
    issue_in.bits = '0;
    issue_in.bits.context_0 = 8'hfa;
    issue_in.bits.control.registers.destination = 2'd2;
    issue_in.bits.control.execution.unit = 4'd11;
    issue_in.bits.control.execution.destination_precision = 2'd2;
    issue_in.bits.frs1 = 5'd18;
    issue_in.bits.frd = 5'd16;
    do @(posedge clock); while (!issue_out.ready);
    @(negedge clock);
    issue_in.valid = 1'b0;
    wait (completion_out.valid);
    #1;
    assert (completion_out.bits.fp_value == 64'h3ff8000000000000);
    assert (!completion_out.bits.exception_flags_valid);
    consume_completion();

    // FROUND suppresses inexact while FROUNDNX reports it for the same result.
    issue_in.valid = 1'b1;
    issue_in.bits = '0;
    issue_in.bits.context_0 = 8'hf0;
    issue_in.bits.control.registers.uses_frs1 = 1'b1;
    issue_in.bits.control.registers.destination = 2'd2;
    issue_in.bits.control.execution.unit = 4'd12;
    issue_in.bits.control.execution.source_precision = 2'd2;
    issue_in.bits.control.execution.destination_precision = 2'd2;
    issue_in.bits.control.execution.uses_rounding_mode = 1'b1;
    issue_in.bits.control.execution.round_raise_inexact = 1'b0;
    issue_in.bits.frs1 = 5'd16;
    issue_in.bits.frd = 5'd17;
    issue_in.bits.rounding_mode = 3'd0;
    do @(posedge clock); while (!issue_out.ready);
    @(negedge clock);
    issue_in.valid = 1'b0;
    wait (completion_out.valid);
    #1;
    assert (completion_out.bits.fp_value == 64'h4000000000000000);
    assert (completion_out.bits.exception_flags == 5'b0);
    assert (completion_out.bits.exception_flags_valid);
    consume_completion();

    issue_in.valid = 1'b1;
    issue_in.bits = '0;
    issue_in.bits.context_0 = 8'hf1;
    issue_in.bits.control.registers.uses_frs1 = 1'b1;
    issue_in.bits.control.registers.destination = 2'd2;
    issue_in.bits.control.execution.unit = 4'd12;
    issue_in.bits.control.execution.source_precision = 2'd2;
    issue_in.bits.control.execution.destination_precision = 2'd2;
    issue_in.bits.control.execution.uses_rounding_mode = 1'b1;
    issue_in.bits.control.execution.round_raise_inexact = 1'b1;
    issue_in.bits.frs1 = 5'd16;
    issue_in.bits.frd = 5'd17;
    issue_in.bits.rounding_mode = 3'd0;
    do @(posedge clock); while (!issue_out.ready);
    @(negedge clock);
    issue_in.valid = 1'b0;
    wait (completion_out.valid);
    #1;
    assert (completion_out.bits.fp_value == 64'h4000000000000000);
    assert (completion_out.bits.exception_flags == 5'b00001);
    consume_completion();

    // Zfa minimum propagates a quiet NaN and quiet comparisons do not raise
    // invalid for that operand.
    load_register(5'd18, 64'h7ff8000000000001, 2'd2);
    issue_in.valid = 1'b1;
    issue_in.bits = '0;
    issue_in.bits.context_0 = 8'hf2;
    issue_in.bits.control.registers.uses_frs1 = 1'b1;
    issue_in.bits.control.registers.uses_frs2 = 1'b1;
    issue_in.bits.control.registers.destination = 2'd2;
    issue_in.bits.control.execution.unit = 4'd6;
    issue_in.bits.control.execution.source_precision = 2'd2;
    issue_in.bits.control.execution.destination_precision = 2'd2;
    issue_in.bits.control.execution.minmax_operation = 2'd0;
    issue_in.bits.frs1 = 5'd18;
    issue_in.bits.frs2 = 5'd1;
    issue_in.bits.frd = 5'd19;
    do @(posedge clock); while (!issue_out.ready);
    @(negedge clock);
    issue_in.valid = 1'b0;
    wait (completion_out.valid);
    #1;
    assert (completion_out.bits.fp_value == 64'h7ff8000000000000);
    assert (completion_out.bits.exception_flags == 5'b0);
    consume_completion();

    issue_in.valid = 1'b1;
    issue_in.bits = '0;
    issue_in.bits.context_0 = 8'hf3;
    issue_in.bits.control.registers.uses_frs1 = 1'b1;
    issue_in.bits.control.registers.uses_frs2 = 1'b1;
    issue_in.bits.control.registers.destination = 2'd1;
    issue_in.bits.control.execution.unit = 4'd7;
    issue_in.bits.control.execution.source_precision = 2'd2;
    issue_in.bits.control.execution.comparison = 2'd1;
    issue_in.bits.control.execution.comparison_signaling = 1'b0;
    issue_in.bits.frs1 = 5'd18;
    issue_in.bits.frs2 = 5'd1;
    issue_in.bits.frd = 5'd20;
    do @(posedge clock); while (!issue_out.ready);
    @(negedge clock);
    issue_in.valid = 1'b0;
    wait (completion_out.valid);
    #1;
    assert (completion_out.bits.integer_value == 64'b0);
    assert (completion_out.bits.exception_flags == 5'b0);
    consume_completion();

    // FCVTMOD.W.D returns the low 32 bits while retaining FCVT.W.D flags.
    load_register(5'd21, 64'h41f0000000100000, 2'd2);
    issue_in.valid = 1'b1;
    issue_in.bits = '0;
    issue_in.bits.context_0 = 8'hf4;
    issue_in.bits.control.registers.uses_frs1 = 1'b1;
    issue_in.bits.control.registers.destination = 2'd1;
    issue_in.bits.control.execution.unit = 4'd13;
    issue_in.bits.control.execution.source_precision = 2'd2;
    issue_in.bits.control.execution.integer_width = 1'b0;
    issue_in.bits.frs1 = 5'd21;
    issue_in.bits.frd = 5'd22;
    do @(posedge clock); while (!issue_out.ready);
    @(negedge clock);
    issue_in.valid = 1'b0;
    wait (completion_out.valid);
    #1;
    assert (completion_out.bits.integer_value == 64'h1);
    assert (completion_out.bits.exception_flags == 5'b10000);
    consume_completion();

    $display("RV5Stage FP pipeline passed");
    $finish;
  end
endmodule
