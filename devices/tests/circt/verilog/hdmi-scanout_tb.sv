// Checks CHI-to-TMDS video, SRAM row reuse, sync alignment, starvation, and recovery.
// SPDX-License-Identifier: Apache-2.0
module hdmi_scanout_tb;
  `include "devices/tests/circt/verilog/tmds-reference.svh"
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_t;
  typedef struct packed { ready_t requester; rsp_t response; } rsp_in_t;
  typedef struct packed { rsp_t requester; ready_t response; } rsp_out_t;
  typedef struct packed { ready_t request; dat_t response; } dat_in_t;
  typedef struct packed { dat_t request; ready_t response; } dat_out_t;
  typedef struct packed { ready_t req; rsp_in_t rsp; dat_in_t dat; } chi_in_t;
  typedef struct packed { req_t req; rsp_out_t rsp; dat_out_t dat; } chi_out_t;
  typedef struct packed { logic [6:0] node_id; logic [6:0] home_id; } identity_t;

  logic clock = 0, reset = 1;
  identity_t identity = '{node_id: 2, home_id: 5};
  HDMIFrameReaderCommand framebuffer;
  logic enable = 1, pixel_enable = 0, pixel_valid, underflow, fetch_failed;
  HDMIVideo video;
  logic symbol_valid;
  TMDSSymbols symbols;
  chi_in_t chi_in;
  chi_out_t chi_out;
  HDMIScanoutFixture dut (.*);
  always #5 clock = ~clock;

  CHIReqFlit pending[$];
  CHIReqFlit accepted;
  CHIReqFlit retired;
  int x = 0, y = 5, epoch = 0, cycles = 0;
  int requests = 0, responses = 0, acknowledgements = 0, checked_pixels = 0;
  int blank_cycles = 0;
  bit send_data, req_fire, dat_fire, ack_fire;
  logic [23:0] expected_rgb;
  int response_epoch, line_number;
  bit encoded_present;
  int blue_disparity = 0, green_disparity = 0, red_disparity = 0;
  TMDSSymbols expected_symbols = '{default: 10'b1101010100};
  int encoded_samples = 0;

  function automatic logic [23:0] color(input int frame, input int pixel);
    return {8'(frame + 32), 8'(pixel / 32 + 64), 8'(pixel % 32 + 128)};
  endfunction

  initial begin
    chi_in = '0;
    framebuffer = '0;
    framebuffer.pas = 3;
    repeat (3) @(posedge clock);
    @(negedge clock);
    reset = 0;
    while (epoch < 9) begin
      // Exercise both one sample per clock and gapped pixel enables.
      pixel_enable = cycles < 1200 || cycles % 3 == 0;
      // Re-enabling in the middle of frame 6 must wait for vertical blank.
      enable = epoch != 6 || (y >= 2 && y < 5);
      if (pixel_enable && x == 0 && y == 5) begin
        epoch++;
        framebuffer.base_address = 44'(epoch * 4096);
        blank_cycles = 0;
        enable = epoch != 6;
      end
      if (y >= 5) blank_cycles++;
      chi_in = '0;
      chi_in.req.ready = cycles % 7 != 0;
      chi_in.rsp.requester.ready = cycles % 5 != 0;
      send_data = pending.size() != 0;
      if (send_data) begin
        response_epoch = int'(pending[0].address / 4096);
        line_number = int'((pending[0].address % 4096) / 64);
        // Leave old responses outstanding across the restart into frame 3.
        if (response_epoch == 2 && line_number >= 4 && (epoch < 3 || blank_cycles < 15))
          send_data = 0;
      end
      if (send_data) begin
        chi_in.dat.response.valid = 1;
        chi_in.dat.response.bits.opcode = 4'h4;
        chi_in.dat.response.bits.src_id = 5;
        chi_in.dat.response.bits.tgt_id = 2;
        chi_in.dat.response.bits.home_nid_or_pbha_or_mismatched_mecid = 5;
        chi_in.dat.response.bits.txn_id = pending[0].txn_id;
        chi_in.dat.response.bits.dbid_or_mecid = {4'b0, pending[0].txn_id};
        chi_in.dat.response.bits.byte_enable = '1;
        chi_in.dat.response.bits.resp_err = response_epoch == 7 && line_number == 0 ? 2 : 0;
        chi_in.dat.response.bits.poison = response_epoch == 4 && line_number == 0 ? 1 : 0;
        for (int lane = 0; lane < 16; lane++)
          chi_in.dat.response.bits.data[lane*32 +: 32] = {8'ha5, color(response_epoch, line_number*16 + lane)};
      end
      #1;
      req_fire = chi_out.req.valid && chi_in.req.ready;
      dat_fire = chi_in.dat.response.valid && chi_out.dat.response.ready;
      ack_fire = chi_out.rsp.requester.valid && chi_in.rsp.requester.ready;
      accepted = chi_out.req.bits;
      encoded_present = pixel_valid;
      if (encoded_present) begin
        expected_symbols.blue = tmds_reference(video.rgb[7:0], {video.vsync, video.hsync}, video.data_enable, blue_disparity);
        expected_symbols.green = tmds_reference(video.rgb[15:8], 0, video.data_enable, green_disparity);
        expected_symbols.red = tmds_reference(video.rgb[23:16], 0, video.data_enable, red_disparity);
        encoded_samples++;
      end
      @(posedge clock);
      #1;
      assert(symbol_valid == encoded_present && symbols == expected_symbols)
        else $fatal(1, "scanout TMDS mapping or latency mismatch");
      if (dat_fire) begin
        retired = pending.pop_front();
        responses++;
      end
      if (req_fire) begin
        assert(accepted.opcode == 3 && !accepted.mem_attr.allocate &&
               accepted.src_id == 2 && accepted.tgt_id == 5 &&
               accepted.address[5:0] == 0 && accepted.address % 4096 < 640)
          else $fatal(1, "scanout request policy");
        pending.push_back(accepted);
        requests++;
      end
      if (ack_fire) acknowledgements++;
      assert(pixel_valid == pixel_enable) else $fatal(1, "pixel-enable latency");
      if (pixel_enable) begin
        assert(video.data_enable == (x < 32 && y < 5)) else $fatal(1, "DE at %0d,%0d", x, y);
        assert(video.hsync == (x >= 34 && x < 36)) else $fatal(1, "HS at %0d,%0d", x, y);
        assert(video.vsync == !(y == 6)) else $fatal(1, "VS at %0d,%0d", x, y);
        expected_rgb = 0;
        if (x < 32 && y < 5 && (epoch == 1 || epoch == 3 || epoch == 5 || epoch == 8 || (epoch == 2 && y < 2)))
          expected_rgb = color(epoch, y*32 + x);
        assert(video.rgb == expected_rgb)
          else $fatal(1, "RGB frame %0d at %0d,%0d got %h expected %h", epoch, x, y, video.rgb, expected_rgb);
        if (x < 32 && y < 5) checked_pixels++;
        if (epoch == 2 && y == 2 && x == 0)
          assert(underflow) else $fatal(1, "missing display underflow");
        if (epoch == 4 && y == 0 && x == 0)
          assert(fetch_failed) else $fatal(1, "missing fetch failure");
        if (epoch == 5 && y == 0 && x == 0)
          assert(underflow && fetch_failed) else $fatal(1, "status not sticky");
        if (epoch == 6)
          assert(!underflow && !fetch_failed) else $fatal(1, "disable did not clear status");
        if ((epoch == 7 || epoch == 8) && y == 0 && x == 0)
          assert(fetch_failed && !underflow) else $fatal(1, "response error status");
        if (x == 36) begin
          x = 0;
          y = y == 7 ? 0 : y + 1;
        end else x++;
      end
      cycles++;
      @(negedge clock);
    end
    assert(checked_pixels == 8*160) else $fatal(1, "incomplete frame coverage");
    assert(requests > 40 && responses > 40 && acknowledgements > 40)
      else $fatal(1, "insufficient CHI traffic");
    assert(encoded_samples >= 8*37*8) else $fatal(1, "incomplete TMDS frame coverage");
    $display("HDMI scanout RGB/TMDS, timing, row reuse, late completion, starvation, and recovery passed");
    $finish;
  end

  initial begin
    #100000;
    $fatal(1, "scanout timeout");
  end
endmodule
