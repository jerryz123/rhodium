// Checks edge timing, enables, independent occurrences, and both scoped reset forms.
// SPDX-License-Identifier: Apache-2.0
module retained_state_tb;
  logic clock = 0, reset = 0;
  logic [7:0] a = 0, b = 0;
  logic enable_a = 0, enable_b = 0, reset_a = 0, clear_b = 0;
  wire [7:0] left, right, delayed, bypass;
  wire [7:0] eager_left, eager_right, eager_delayed, eager_bypass;
  logic [7:0] expected_left = 0, expected_right = 0, expected_delayed = 0;
  ComparisonTop dut(.*);

  task automatic check_outputs;
    if ({left, right, delayed, bypass} !== {expected_left, expected_right, expected_delayed, a})
      $fatal(1, "cycle model mismatch: left=%0d right=%0d delayed=%0d bypass=%0d", left, right, delayed, bypass);
    if ({left, right, delayed, bypass} !== {eager_left, eager_right, eager_delayed, eager_bypass})
      $fatal(1, "eager/retained mismatch");
  endtask

  task automatic tick;
    #2;
    expected_delayed = reset ? 0 : expected_left;
    if (reset || reset_a) expected_left = 0;
    else if (enable_a) expected_left = a;
    if (reset || clear_b) expected_right = 0;
    else if (enable_b) expected_right = b;
    clock = 1;
    #2;
    check_outputs();
    clock = 0;
    #2;
  endtask

  initial begin
    reset = 1;
    tick();
    reset = 0;
    a = 11; b = 83; enable_a = 1; enable_b = 1;
    #1; check_outputs();
    tick();
    a = 22; b = 94; enable_b = 0;
    #1; check_outputs();
    tick();
    // A scoped reset is synchronous and affects only the chosen occurrence.
    reset_a = 1;
    #1; check_outputs();
    tick();
    reset_a = 0; enable_a = 0; clear_b = 1;
    tick();
    clear_b = 0;
    for (int i = 0; i < 40; i++) begin
      a = 8'(i * 7 + 3); b = 8'(i * 13 + 9);
      enable_a = (i % 3 != 0); enable_b = (i % 4 != 0);
      reset_a = (i == 12); clear_b = (i == 19); reset = (i == 27);
      #1; check_outputs();
      tick();
    end
    $display("retained state: cycle model and eager equivalence passed");
    $finish;
  end
endmodule
