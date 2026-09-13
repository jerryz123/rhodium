// Wraps the generated Rhodium inverter with explicit supplies for focused LVS testing.
// SPDX-License-Identifier: Apache-2.0

`default_nettype none

module rhodium_lvs_smoke (
`ifdef USE_POWER_PINS
    inout vccd1,
    inout vssd1,
`endif
    input  in_bit,
    output out_bit
);

    RhodiumTop rhodium_top (.in_bit(in_bit), .out_bit(out_bit));

endmodule

`default_nettype wire
