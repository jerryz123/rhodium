// Connects the minimal Rhodium leaf to the pinned Double-Wide OpenFrame pad contract.
// SPDX-License-Identifier: Apache-2.0

`default_nettype none

module double_wide_openframe_project_wrapper (
`ifdef USE_POWER_PINS
    inout vddio, vssio, vccd, vssd, vdda, vssa,
    inout vdda1, vssa1, vccd1, vssd1,
    inout vdda2, vssa2, vccd2, vssd2,
`endif
    // Frame POR / reset (openframe-compatible)
    input  porb_h,
    input  por_l,
    input  porb_l,
    input  resetb_h,
    input  resetb_l,
    input  [62:0] gpio_in,
    input  [62:0] gpio_in_h,
    input  [62:0] gpio_loopback_one,
    input  [62:0] gpio_loopback_zero,
    output [62:0] gpio_out,
    output [62:0] gpio_oeb,
    output [62:0] gpio_inp_dis,
    output [62:0] gpio_ib_mode_sel,
    output [62:0] gpio_vtrip_sel,
    output [62:0] gpio_slow_sel,
    output [62:0] gpio_holdover,
    output [62:0] gpio_analog_en,
    output [62:0] gpio_analog_sel,
    output [62:0] gpio_analog_pol,
    output [62:0] gpio_dm0,
    output [62:0] gpio_dm1,
    output [62:0] gpio_dm2,
    inout  [62:0] analog_io,
    inout  [62:0] analog_noesd_io
);

    wire rhodium_out;

    RhodiumTop rhodium_top (.in_bit(gpio_in[0]), .out_bit(rhodium_out));

    // GPIO 0 is the input and GPIO 1 drives its inverse; every other pad is input-only.
    assign gpio_out = {61'b0, rhodium_out, 1'b0};
    assign gpio_oeb = {{61{1'b1}}, 1'b0, 1'b1};
    assign gpio_inp_dis = 63'b0;
    assign gpio_ib_mode_sel = 63'b0;
    assign gpio_vtrip_sel = 63'b0;
    assign gpio_slow_sel = 63'b0;
    assign gpio_holdover = 63'b0;
    assign gpio_analog_en = 63'b0;
    assign gpio_analog_sel = 63'b0;
    assign gpio_analog_pol = 63'b0;
    assign gpio_dm0 = {{61{1'b1}}, 1'b0, 1'b1};
    assign gpio_dm1 = {61'b0, 1'b1, 1'b0};
    assign gpio_dm2 = {61'b0, 1'b1, 1'b0};

endmodule

`default_nettype wire
