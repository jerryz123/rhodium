// Verifies multi-router wormhole delivery, packet retention, sharing, and backpressure.
// SPDX-License-Identifier: Apache-2.0
module noc_wormhole_tb;
    logic clock;
    logic reset;
    struct packed {logic valid; VariableFlit bits;} injection_0_in;
    struct packed {logic valid; VariableFlit bits;} injection_1_in;
    struct packed {logic ready;} ejection_in;
    struct packed {logic ready;} injection_0_out;
    struct packed {logic ready;} injection_1_out;
    struct packed {logic valid; VariableFlit bits;} ejection_out;

    WormholeNetworkFixture dut (.*);
    always #5 clock = ~clock;
    task automatic tick; @(posedge clock); #1; endtask

    task automatic send_0(
        input logic head,
        input logic tail,
        input logic [7:0] payload
    );
        @(negedge clock);
        injection_0_in = '{valid: 1'b1,
                           bits: '{head: head,
                                   tail: tail,
                                   payload: '{route_key: head ? 1'b0 : 1'b1,
                                              payload: payload}}};
        do @(posedge clock); while (!injection_0_out.ready);
        #1 injection_0_in.valid = 0;
    endtask

    task automatic send_1(
        input logic head,
        input logic tail,
        input logic [7:0] payload
    );
        @(negedge clock);
        injection_1_in = '{valid: 1'b1,
                           bits: '{head: head,
                                   tail: tail,
                                   payload: '{route_key: head ? 1'b1 : 1'b0,
                                              payload: payload}}};
        do @(posedge clock); while (!injection_1_out.ready);
        #1 injection_1_in.valid = 0;
    endtask

    initial begin
        int received;
        int cycles;
        int active_packet;
        int active_index;
        logic [4:0] seen;
        logic [7:0] payload;

        clock = 0;
        reset = 1;
        injection_0_in = '0;
        injection_1_in = '0;
        ejection_in = '0;
        received = 0;
        cycles = 0;
        active_packet = -1;
        active_index = 0;
        seen = '0;
        tick();
        tick();
        reset = 0;

        fork
            begin
                send_0(1'b1, 1'b0, 8'hA0);
                send_0(1'b0, 1'b0, 8'hA1);
                send_0(1'b0, 1'b1, 8'hA2);
            end
            begin
                send_1(1'b1, 1'b0, 8'hB0);
                send_1(1'b0, 1'b1, 8'hB1);
            end
            begin
                while (received < 5 && cycles < 400) begin
                    @(negedge clock);
                    ejection_in.ready = ($urandom_range(0, 2) != 0);
                    @(posedge clock);
                    if (ejection_out.valid && ejection_in.ready) begin
                        payload = ejection_out.bits.payload.payload;
                        assert ((payload >= 8'hA0 && payload <= 8'hA2) ||
                                (payload >= 8'hB0 && payload <= 8'hB1))
                            else $fatal(1, "unknown wormhole payload %h", payload);
                        if (ejection_out.bits.head) begin
                            assert (active_packet == -1)
                                else $fatal(1, "packet heads interleaved at ejection");
                            active_packet = payload[4] ? 1 : 0;
                            active_index = 0;
                        end else begin
                            assert (active_packet != -1)
                                else $fatal(1, "body arrived without an active packet");
                        end
                        assert ((active_packet == 0 && payload == 8'hA0 + active_index[7:0]) ||
                                (active_packet == 1 && payload == 8'hB0 + active_index[7:0]))
                            else $fatal(1, "packet beat reordered or interleaved: %h", payload);
                        if (active_packet == 0) begin
                            assert (!seen[active_index])
                                else $fatal(1, "A packet beat duplicated");
                            seen[active_index] = 1;
                        end else begin
                            assert (!seen[3 + active_index])
                                else $fatal(1, "B packet beat duplicated");
                            seen[3 + active_index] = 1;
                        end
                        active_index++;
                        if (ejection_out.bits.tail) begin
                            assert ((active_packet == 0 && active_index == 3) ||
                                    (active_packet == 1 && active_index == 2))
                                else $fatal(1, "packet tail arrived at wrong length");
                            active_packet = -1;
                        end
                        received++;
                    end
                    cycles++;
                end
            end
        join

        assert (received == 5 && seen == 5'b11111 && active_packet == -1)
            else $fatal(1, "wormhole network conservation failed");
        assert (cycles < 400)
            else $fatal(1, "wormhole network made no forward progress");
        $display("NoC wormhole network simulation passed");
        $finish;
    end
endmodule
