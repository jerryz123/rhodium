// Verifies end-to-end delivery, destination selection, ordering, and conservation under backpressure.
// SPDX-License-Identifier: Apache-2.0
module noc_network_tb;
    logic clock;
    logic reset;
    struct packed {logic valid; RoutedBeat bits;} injection_in;
    struct packed {logic ready;} injection_out;
    struct packed {logic ready;} ejection_0_in;
    struct packed {logic ready;} ejection_1_in;
    struct packed {logic valid; RoutedBeat bits;} ejection_0_out;
    struct packed {logic valid; RoutedBeat bits;} ejection_1_out;

    HierarchicalNoCFixture dut (
        .clock(clock),
        .reset(reset),
        .injection_in(injection_in),
        .ejection_0_in(ejection_0_in),
        .ejection_1_in(ejection_1_in),
        .injection_out(injection_out),
        .ejection_0_out(ejection_0_out),
        .ejection_1_out(ejection_1_out)
    );

    always #5 clock = ~clock;

    task automatic tick;
        @(posedge clock);
        #1;
    endtask

    initial begin
        int sent;
        int received;
        int cycles;
        int near_expected;
        int far_expected;
        logic [7:0] seen;
        logic [7:0] payload;

        clock = 0;
        reset = 1;
        injection_in = '0;
        ejection_0_in = '0;
        ejection_1_in = '0;
        tick();
        tick();
        reset = 0;
        #1;

        assert (injection_out.ready)
            else $fatal(1, "network injection did not reset ready");

        sent = 0;
        received = 0;
        cycles = 0;
        near_expected = 0;
        far_expected = 1;
        seen = '0;
        while (received < 8 && cycles < 300) begin
            @(negedge clock);
            injection_in.valid = sent < 8;
            injection_in.bits.route_key = sent[0];
            injection_in.bits.payload = sent[7:0];
            ejection_0_in.ready = ($urandom_range(0, 1) == 1);
            ejection_1_in.ready = ($urandom_range(0, 1) == 1);

            @(posedge clock);
            if (ejection_0_out.valid && ejection_0_in.ready) begin
                payload = ejection_0_out.bits.payload;
                assert (!payload[0])
                    else $fatal(1, "far-route payload reached near ejection: %h",
                                payload);
                assert (payload == near_expected[7:0])
                    else $fatal(1, "near-route ordering failure: got=%h expected=%h",
                                payload, near_expected[7:0]);
                assert (!seen[payload[2:0]])
                    else $fatal(1, "network duplicated payload %h", payload);
                seen[payload[2:0]] = 1;
                near_expected += 2;
                received++;
            end
            if (ejection_1_out.valid && ejection_1_in.ready) begin
                payload = ejection_1_out.bits.payload;
                assert (payload[0])
                    else $fatal(1, "near-route payload reached far ejection: %h",
                                payload);
                assert (payload == far_expected[7:0])
                    else $fatal(1, "far-route ordering failure: got=%h expected=%h",
                                payload, far_expected[7:0]);
                assert (!seen[payload[2:0]])
                    else $fatal(1, "network duplicated payload %h", payload);
                seen[payload[2:0]] = 1;
                far_expected += 2;
                received++;
            end
            if (injection_in.valid && injection_out.ready)
                sent++;
            cycles++;
        end
        #1;
        injection_in.valid = 0;
        ejection_0_in.ready = 0;
        ejection_1_in.ready = 0;

        assert (sent == 8)
            else $fatal(1, "not every packet entered the network: sent=%0d", sent);
        assert (received == 8 && seen == 8'hFF)
            else $fatal(1, "network conservation failed: received=%0d seen=%h",
                        received, seen);
        assert (near_expected == 8 && far_expected == 9)
            else $fatal(1, "destination delivery counts were incorrect");

        $display("NoC simple-router network simulation passed");
        $finish;
    end
endmodule
