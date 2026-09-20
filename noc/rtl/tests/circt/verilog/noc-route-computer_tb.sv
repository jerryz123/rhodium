// Exhaustively checks every local router lookup in the validated two-hop fixture.
// SPDX-License-Identifier: Apache-2.0
module noc_route_computer_tb;
    logic source_route_key;
    logic source_origin_key;
    logic [1:0] source_target_mask;
    logic [1:0] source_fallback_mask;
    logic source_valid;

    logic middle_route_key;
    logic middle_origin_key;
    logic [1:0] middle_target_mask;
    logic [1:0] middle_fallback_mask;
    logic middle_valid;

    logic destination_route_key;
    logic destination_origin_key;
    logic destination_target_mask;
    logic destination_fallback_mask;
    logic destination_valid;

    RouteComputerFixture dut (
        .source_route_key(source_route_key),
        .source_origin_key(source_origin_key),
        .source_target_mask(source_target_mask),
        .source_fallback_mask(source_fallback_mask),
        .source_valid(source_valid),
        .middle_route_key(middle_route_key),
        .middle_origin_key(middle_origin_key),
        .middle_target_mask(middle_target_mask),
        .middle_fallback_mask(middle_fallback_mask),
        .middle_valid(middle_valid),
        .destination_route_key(destination_route_key),
        .destination_origin_key(destination_origin_key),
        .destination_target_mask(destination_target_mask),
        .destination_fallback_mask(destination_fallback_mask),
        .destination_valid(destination_valid)
    );

    initial begin
        for (int key = 0; key < 4; key++) begin
            logic [2:0] expected_source;
            logic [2:0] expected_middle;
            logic [1:0] expected_destination;

            source_route_key = key[1];
            source_origin_key = key[0];
            middle_route_key = key[1];
            middle_origin_key = key[0];
            destination_route_key = key[1];
            destination_origin_key = key[0];

            case (key)
                0: begin
                    expected_source = 3'b111;
                    expected_middle = 3'b011;
                    expected_destination = 2'b11;
                end
                1: begin
                    expected_source = 3'b000;
                    expected_middle = 3'b101;
                    expected_destination = 2'b11;
                end
                default: begin
                    expected_source = 3'b000;
                    expected_middle = 3'b000;
                    expected_destination = 2'b00;
                end
            endcase

            #1;
            assert ({source_target_mask, source_valid} == expected_source &&
                    source_fallback_mask == source_target_mask)
                else $fatal(1, "source route lookup failed for encoded key %0d", key);
            assert ({middle_target_mask, middle_valid} == expected_middle &&
                    middle_fallback_mask == middle_target_mask)
                else $fatal(1, "middle route lookup failed for encoded key %0d", key);
            assert ({destination_target_mask, destination_valid} == expected_destination &&
                    destination_fallback_mask == destination_target_mask)
                else $fatal(1, "destination route lookup failed for encoded key %0d", key);
        end

        $display("NoC router-local route-computer simulation passed");
        $finish;
    end
endmodule
