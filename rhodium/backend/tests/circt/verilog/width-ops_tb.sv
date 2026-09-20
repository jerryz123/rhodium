// Simulates concatenation, extraction, zero extension, and truncation after CIRCT export.
// SPDX-License-Identifier: Apache-2.0
// Simulates the CIRCT-exported width-changing datapath module.
module width_ops_tb;
    logic [7:0] a;
    logic [3:0] b;
    logic [11:0] joined;
    logic [3:0] middle;
    logic [11:0] widened;
    logic [3:0] narrowed;

    WidthOps8 dut (
        .a(a),
        .b(b),
        .joined(joined),
        .middle(middle),
        .widened(widened),
        .narrowed(narrowed)
    );

    task automatic check_outputs(
        input logic [7:0] a_value,
        input logic [3:0] b_value,
        input logic [11:0] expected_joined,
        input logic [3:0] expected_middle,
        input logic [11:0] expected_widened,
        input logic [3:0] expected_narrowed
    );
        a = a_value;
        b = b_value;
        #1;
        assert (joined == expected_joined) else $fatal(1, "concat failed");
        assert (middle == expected_middle) else $fatal(1, "extract failed");
        assert (widened == expected_widened) else $fatal(1, "zext failed");
        assert (narrowed == expected_narrowed) else $fatal(1, "trunc failed");
    endtask

    initial begin
        check_outputs(8'hd6, 4'ha, 12'hd6a, 4'h5, 12'h0d6, 4'h6);
        check_outputs(8'h3c, 4'h5, 12'h3c5, 4'hf, 12'h03c, 4'hc);

        $display("width-changing operations simulation passed");
        $finish;
    end
endmodule
