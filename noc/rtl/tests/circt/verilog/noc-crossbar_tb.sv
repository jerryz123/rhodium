// Exercises independent destination selection and contention in the linkless NoC crossbar.
// SPDX-License-Identifier: Apache-2.0
module noc_crossbar_tb;
    logic clock;
    logic reset;
    struct packed {logic valid; RoutedBeat bits;} ingress_0_in;
    struct packed {logic valid; RoutedBeat bits;} ingress_1_in;
    struct packed {logic valid; RoutedBeat bits;} ingress_2_in;
    struct packed {logic ready;} egress_0_in;
    struct packed {logic ready;} egress_1_in;
    struct packed {logic ready;} egress_2_in;
    struct packed {logic ready;} ingress_0_out;
    struct packed {logic ready;} ingress_1_out;
    struct packed {logic ready;} ingress_2_out;
    struct packed {logic valid; RoutedBeat bits;} egress_0_out;
    struct packed {logic valid; RoutedBeat bits;} egress_1_out;
    struct packed {logic valid; RoutedBeat bits;} egress_2_out;

    OneRouterCrossbarFixture dut (
        .clock(clock),
        .reset(reset),
        .ingress_0_in(ingress_0_in),
        .ingress_1_in(ingress_1_in),
        .ingress_2_in(ingress_2_in),
        .egress_0_in(egress_0_in),
        .egress_1_in(egress_1_in),
        .egress_2_in(egress_2_in),
        .ingress_0_out(ingress_0_out),
        .ingress_1_out(ingress_1_out),
        .ingress_2_out(ingress_2_out),
        .egress_0_out(egress_0_out),
        .egress_1_out(egress_1_out),
        .egress_2_out(egress_2_out)
    );

    always #5 clock = ~clock;

    task automatic tick;
        @(posedge clock);
        #1;
    endtask

    initial begin
        clock = 0;
        reset = 1;
        ingress_0_in = '0;
        ingress_1_in = '0;
        ingress_2_in = '0;
        egress_0_in.ready = 1;
        egress_1_in.ready = 1;
        egress_2_in.ready = 1;
        tick();
        tick();
        reset = 0;
        #1;

        assert (ingress_0_out.ready && ingress_1_out.ready && ingress_2_out.ready)
            else $fatal(1, "crossbar did not reset empty");

        // Source-major route keys independently select all three ejection terminals.
        ingress_0_in = '{valid: 1'b1,
                         bits: '{route_key: 4'd2, payload: 8'hA0}};
        ingress_1_in = '{valid: 1'b1,
                         bits: '{route_key: 4'd3, payload: 8'hB1}};
        ingress_2_in = '{valid: 1'b1,
                         bits: '{route_key: 4'd7, payload: 8'hC2}};
        tick();
        ingress_0_in.valid = 0;
        ingress_1_in.valid = 0;
        ingress_2_in.valid = 0;
        #1;
        assert (egress_0_out.valid && egress_0_out.bits.payload == 8'hB1)
            else $fatal(1, "terminal 0 selection failed");
        assert (egress_1_out.valid && egress_1_out.bits.payload == 8'hC2)
            else $fatal(1, "terminal 1 selection failed");
        assert (egress_2_out.valid && egress_2_out.bits.payload == 8'hA0)
            else $fatal(1, "terminal 2 selection failed");
        tick();

        // Two sources targeting terminal 0 serialize without loss.
        egress_0_in.ready = 0;
        ingress_0_in = '{valid: 1'b1,
                         bits: '{route_key: 4'd0, payload: 8'hA4}};
        ingress_1_in = '{valid: 1'b1,
                         bits: '{route_key: 4'd3, payload: 8'hB5}};
        tick();
        ingress_0_in.valid = 0;
        ingress_1_in.valid = 0;
        #1;
        assert (egress_0_out.valid && egress_0_out.bits.payload == 8'hA4)
            else $fatal(1, "fixed-priority contention winner was incorrect");
        egress_0_in.ready = 1;
        tick();
        assert (egress_0_out.valid && egress_0_out.bits.payload == 8'hB5)
            else $fatal(1, "contending request was lost");
        tick();
        assert (!egress_0_out.valid)
            else $fatal(1, "crossbar emitted a duplicate transfer");

        $display("NoC one-router crossbar simulation passed");
        $finish;
    end
endmodule
