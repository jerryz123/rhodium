// Simulates composed valid propagation through the specialized predicate filters.
// SPDX-License-Identifier: Apache-2.0
module predicate_filter_tb;
    typedef struct packed {
        logic valid;
        logic [7:0] bits;
    } valid_t;

    valid_t ingress;
    valid_t egress;

    SingleEvenFilter dut (
        .ingress_in(ingress),
        .egress_out(egress)
    );

    task automatic check_case(logic input_valid,
                              logic [7:0] input_bits,
                              logic expected_valid);
        ingress.valid = input_valid;
        ingress.bits = input_bits;
        #1;
        assert (egress.bits == input_bits)
            else $fatal(1, "filter changed payload bits");
        assert (egress.valid == expected_valid)
            else $fatal(1, "filter produced wrong valid result");
    endtask

    initial begin
        check_case(1'b0, 8'd3, 1'b0);
        check_case(1'b1, 8'd3, 1'b1);
        check_case(1'b1, 8'd8, 1'b0);
        check_case(1'b1, 8'd11, 1'b0);

        $display("predicate filter simulation passed");
        $finish;
    end
endmodule
