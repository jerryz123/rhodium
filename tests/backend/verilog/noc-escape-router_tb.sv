// Exercises immediate adaptive routing, persistent escape fallback, and fair contention.
// SPDX-License-Identifier: Apache-2.0
module noc_escape_router_tb;
    logic clock;
    logic reset;
    struct packed {logic valid; RoutedBeat bits;} ingress_0_in;
    struct packed {logic valid; RoutedBeat bits;} ingress_1_in;
    struct packed {logic ready;} egress_0_in;
    struct packed {logic ready;} egress_1_in;
    struct packed {logic ready;} ingress_0_out;
    struct packed {logic ready;} ingress_1_out;
    struct packed {logic valid; RoutedBeat bits;} egress_0_out;
    struct packed {logic valid; RoutedBeat bits;} egress_1_out;

    EscapeRouterFixture dut (.*);
    always #5 clock = ~clock;
    task automatic tick; @(posedge clock); #1; endtask

    task automatic offer_0(input logic [7:0] payload);
        @(negedge clock);
        ingress_0_in = '{valid: 1'b1,
                         bits: '{route_key: 1'b0, payload: payload}};
        do @(posedge clock); while (!ingress_0_out.ready);
        #1 ingress_0_in.valid = 0;
    endtask

    initial begin
        clock = 0;
        reset = 1;
        ingress_0_in = '0;
        ingress_1_in = '0;
        egress_0_in = '0;
        egress_1_in = '0;
        tick();
        tick();
        reset = 0;

        // A ready adaptive target transfers immediately instead of consuming
        // the escape VC.
        egress_0_in.ready = 1;
        offer_0(8'hA0);
        #1;
        assert (egress_0_out.valid && egress_0_out.bits.payload == 8'hA0 &&
                !egress_1_out.valid)
            else $fatal(1, "ready adaptive target was not selected");
        tick();
        egress_0_in.ready = 0;

        // With no adaptive transfer available, the escape output remains
        // asserted throughout backpressure and priority does not advance.
        offer_0(8'hB0);
        repeat (3) begin
            #1;
            assert (!egress_0_out.valid && egress_1_out.valid &&
                    egress_1_out.bits.payload == 8'hB0)
                else $fatal(1, "escape fallback was not persistent");
            tick();
        end
        egress_1_in.ready = 1;
        tick();
        egress_1_in.ready = 0;

        // Reset the fairness state, then hold two escape requests. A stalled
        // grant stays on input zero; after acceptance, input one wins next.
        reset = 1;
        tick();
        reset = 0;
        ingress_0_in = '{valid: 1'b1,
                         bits: '{route_key: 1'b0, payload: 8'hC0}};
        ingress_1_in = '{valid: 1'b1,
                         bits: '{route_key: 1'b0, payload: 8'hD0}};
        tick();
        ingress_0_in.valid = 0;
        ingress_1_in.valid = 0;
        repeat (2) begin
            assert (egress_1_out.valid && egress_1_out.bits.payload == 8'hC0)
                else $fatal(1, "stalled contention changed winner");
            tick();
        end
        egress_1_in.ready = 1;
        tick();
        assert (egress_1_out.valid && egress_1_out.bits.payload == 8'hD0)
            else $fatal(1, "accepted escape grant did not rotate fairly");
        tick();
        assert (!egress_1_out.valid)
            else $fatal(1, "escape transfer was duplicated");

        $display("NoC escape router simulation passed");
        $finish;
    end
endmodule
