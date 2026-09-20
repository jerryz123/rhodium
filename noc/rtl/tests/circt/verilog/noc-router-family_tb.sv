// Checks family-site routing, generic physical-link binding, and downstream backpressure.
// SPDX-License-Identifier: Apache-2.0
module noc_router_family_tb;
    logic clock;
    logic reset;
    struct packed {logic valid; RoutedBeat bits;} source_ingress_0_in;
    struct packed {logic valid; RoutedBeat bits;} source_ingress_1_in;
    struct packed {logic valid; RoutedBeat bits;} source_ingress_2_in;
    struct packed {logic ready;} source_egress_0_in;
    struct packed {logic ready;} source_egress_1_in;
    struct packed {logic valid; RoutedBeat bits;} hub_ingress_0_in;
    struct packed {logic valid; RoutedBeat bits;} hub_ingress_1_in;
    struct packed {logic valid; RoutedBeat bits;} hub_ingress_2_in;
    struct packed {logic ready;} hub_egress_0_in;
    struct packed {logic ready;} hub_egress_1_in;
    struct packed {logic ready;} source_ingress_0_out;
    struct packed {logic ready;} source_ingress_1_out;
    struct packed {logic ready;} source_ingress_2_out;
    struct packed {logic valid; RoutedBeat bits;} source_egress_0_out;
    struct packed {logic valid; RoutedBeat bits;} source_egress_1_out;
    struct packed {logic ready;} hub_ingress_0_out;
    struct packed {logic ready;} hub_ingress_1_out;
    struct packed {logic ready;} hub_ingress_2_out;
    struct packed {logic valid; RoutedBeat bits;} hub_egress_0_out;
    struct packed {logic valid; RoutedBeat bits;} hub_egress_1_out;

    RouterFamilyFixture dut (
        .clock(clock),
        .reset(reset),
        .source_ingress_0_in(source_ingress_0_in),
        .source_ingress_1_in(source_ingress_1_in),
        .source_ingress_2_in(source_ingress_2_in),
        .source_egress_0_in(source_egress_0_in),
        .source_egress_1_in(source_egress_1_in),
        .hub_ingress_0_in(hub_ingress_0_in),
        .hub_ingress_1_in(hub_ingress_1_in),
        .hub_ingress_2_in(hub_ingress_2_in),
        .hub_egress_0_in(hub_egress_0_in),
        .hub_egress_1_in(hub_egress_1_in),
        .source_ingress_0_out(source_ingress_0_out),
        .source_ingress_1_out(source_ingress_1_out),
        .source_ingress_2_out(source_ingress_2_out),
        .source_egress_0_out(source_egress_0_out),
        .source_egress_1_out(source_egress_1_out),
        .hub_ingress_0_out(hub_ingress_0_out),
        .hub_ingress_1_out(hub_ingress_1_out),
        .hub_ingress_2_out(hub_ingress_2_out),
        .hub_egress_0_out(hub_egress_0_out),
        .hub_egress_1_out(hub_egress_1_out)
    );

    always #5 clock = ~clock;

    task automatic tick;
        @(posedge clock);
        #1;
    endtask

    task automatic send_source(input logic [1:0] route_key, input logic [7:0] payload);
        @(negedge clock);
        source_ingress_0_in = '{valid: 1'b1, bits: '{route_key: route_key, payload: payload}};
        do @(posedge clock); while (!source_ingress_0_out.ready);
        #1;
        source_ingress_0_in.valid = 0;
    endtask

    task automatic send_hub_local(input logic [7:0] payload);
        @(negedge clock);
        hub_ingress_0_in = '{valid: 1'b1, bits: '{route_key: 2'd2, payload: payload}};
        do @(posedge clock); while (!hub_ingress_0_out.ready);
        #1;
        hub_ingress_0_in.valid = 0;
    endtask

    task automatic send_hub_transit(input logic [7:0] payload);
        @(negedge clock);
        hub_ingress_1_in = '{valid: 1'b1, bits: '{route_key: 2'd0, payload: payload}};
        do @(posedge clock); while (!hub_ingress_1_out.ready);
        #1;
        hub_ingress_1_in.valid = 0;
    endtask

    initial begin
        clock = 0;
        reset = 1;
        source_ingress_0_in = '0;
        source_ingress_1_in = '0;
        source_ingress_2_in = '0;
        source_egress_0_in = '{ready: 1'b1};
        source_egress_1_in = '{ready: 1'b1};
        hub_ingress_0_in = '0;
        hub_ingress_1_in = '0;
        hub_ingress_2_in = '0;
        hub_egress_0_in = '{ready: 1'b1};
        hub_egress_1_in = '{ready: 1'b1};
        tick();
        tick();
        reset = 0;

        send_source(2'd0, 8'hA0);
        wait (source_egress_0_out.valid);
        assert (source_egress_0_out.bits.payload == 8'hA0 && !source_egress_1_out.valid)
            else $fatal(1, "source site did not select its physical-link target");
        tick();

        send_hub_local(8'hB0);
        wait (hub_egress_1_out.valid);
        assert (hub_egress_1_out.bits.payload == 8'hB0 && !hub_egress_0_out.valid)
            else $fatal(1, "hub site did not select its padded ejection target");
        tick();

        send_hub_transit(8'hC0);
        wait (hub_egress_0_out.valid);
        assert (hub_egress_0_out.bits.payload == 8'hC0 && !hub_egress_1_out.valid)
            else $fatal(1, "hub site did not select its physical-link target");
        tick();

        @(negedge clock);
        source_egress_0_in.ready = 0;
        send_source(2'd0, 8'hD0);
        repeat (4) begin
            tick();
            assert (!source_egress_1_out.valid)
                else $fatal(1, "closed local target emitted traffic");
        end
        @(negedge clock);
        source_egress_0_in.ready = 1;
        #1;
        wait (source_egress_0_out.valid);
        assert (source_egress_0_out.bits.payload == 8'hD0)
            else $fatal(1, "physical-link binding lost a backpressured packet");
        tick();

        $display("NoC router-family simulation passed");
        $finish;
    end
endmodule
