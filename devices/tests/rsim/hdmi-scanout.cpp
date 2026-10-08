// Checks CHI-to-TMDS video, SRAM row reuse, sync alignment, starvation, and
// recovery.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "tmds-reference.hpp"
#include "wide.hpp"
#include <deque>
using Request = std::remove_cvref_t<decltype(chi_out.preq.pbits)>;
using Symbols = std::remove_cvref_t<decltype(symbols)>;
std::deque<Request> pending;
Request accepted{}, retired{};
int x = 0, y = 5, epoch = 0, cycles = 0;
int requests = 0, responses = 0, acknowledgements = 0, checked_pixels = 0,
    blank_cycles = 0;
bool send_data, req_fire, dat_fire, ack_fire, encoded_present;
std::uint32_t expected_rgb;
int response_epoch, line_number;
int blue_disparity = 0, green_disparity = 0, red_disparity = 0,
    encoded_samples = 0;
Symbols expected_symbols{.pblue = 0x354, .pgreen = 0x354, .pred = 0x354};

std::uint32_t color(int frame, int pixel) {
  std::uint32_t return_value{};

  return ((frame + 32) & 255) << 16 | ((pixel / 32 + 64) & 255) << 8 |
         ((pixel % 32 + 128) & 255);

  return return_value;
}

void run_case() {
  reset = 1;
  enable = 1;
  pixel_enable = 0;
  identity = {.pnode_uid = 2, .phome_uid = 5};
  chi_in = {};
  framebuffer = {};
  framebuffer.ppas = 3;
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    tick_model();
  eval();
  reset = 0;
  while (epoch < 9) {

    pixel_enable = cycles < 1200 || cycles % 3 == 0;

    enable = epoch != 6 || (y >= 2 && y < 5);
    if (pixel_enable && x == 0 && y == 5) {
      epoch++;
      framebuffer.pbase_uaddress = ((epoch * 4096) & low_mask(44));
      blank_cycles = 0;
      enable = epoch != 6;
    }
    if (y >= 5)
      blank_cycles++;
    chi_in = {};
    chi_in.preq.pready = cycles % 7 != 0;
    chi_in.prsp.prequester.pready = cycles % 5 != 0;
    send_data = pending.size() != 0;
    if (send_data) {
      response_epoch = int(pending[0].paddress / 4096);
      line_number = int((pending[0].paddress % 4096) / 64);

      if (response_epoch == 2 && line_number >= 4 &&
          (epoch < 3 || blank_cycles < 15))
        send_data = 0;
    }
    if (send_data) {
      chi_in.pdat.presponse.pvalid = 1;
      chi_in.pdat.presponse.pbits.popcode = UINT64_C(4);
      chi_in.pdat.presponse.pbits.psrc_uid = 5;
      chi_in.pdat.presponse.pbits.ptgt_uid = 2;
      chi_in.pdat.presponse.pbits.phome_unid_uor_upbha_uor_umismatched_umecid =
          5;
      chi_in.pdat.presponse.pbits.ptxn_uid = pending[0].ptxn_uid;
      chi_in.pdat.presponse.pbits.pdbid_uor_umecid = pending[0].ptxn_uid;
      chi_in.pdat.presponse.pbits.pbyte_uenable = UINT64_MAX;
      chi_in.pdat.presponse.pbits.presp_uerr =
          response_epoch == 7 && line_number == 0 ? 2 : 0;
      chi_in.pdat.presponse.pbits.ppoison =
          response_epoch == 4 && line_number == 0 ? 1 : 0;
      for (int lane = 0; lane < 16; lane++)
        chi_in.pdat.presponse.pbits.pdata.words[lane] =
            color(response_epoch, line_number * 16 + lane);
    }
    eval();
    req_fire = chi_out.preq.pvalid && chi_in.preq.pready;
    dat_fire = chi_in.pdat.presponse.pvalid && chi_out.pdat.presponse.pready;
    ack_fire = chi_out.prsp.prequester.pvalid && chi_in.prsp.prequester.pready;
    accepted = chi_out.preq.pbits;
    encoded_present = pixel_valid;
    if (encoded_present) {
      expected_symbols.pblue = tmds_reference(
          slice(video.prgb, 7, 0), ((video.pvsync << 1) | video.phsync),
          video.pdata_uenable, blue_disparity);
      expected_symbols.pgreen = tmds_reference(
          slice(video.prgb, 15, 8), 0, video.pdata_uenable, green_disparity);
      expected_symbols.pred = tmds_reference(
          slice(video.prgb, 23, 16), 0, video.pdata_uenable, red_disparity);
      encoded_samples++;
    }
    tick_model();
    CHECK(symbol_valid == encoded_present &&
          symbols.pblue == expected_symbols.pblue &&
          symbols.pgreen == expected_symbols.pgreen &&
          symbols.pred == expected_symbols.pred);
    if (dat_fire) {
      retired = pending.front();
      pending.pop_front();
      responses++;
    }
    if (req_fire) {
      CHECK(accepted.popcode == 3 && !accepted.pmem_uattr.pallocate &&
            accepted.psrc_uid == 2 && accepted.ptgt_uid == 5 &&
            slice(accepted.paddress, 5, 0) == 0 &&
            accepted.paddress % 4096 < 640);
      pending.push_back(accepted);
      requests++;
    }
    if (ack_fire)
      acknowledgements++;
    CHECK(pixel_valid == pixel_enable);
    if (pixel_enable) {
      CHECK(video.pdata_uenable == (x < 32 && y < 5));
      CHECK(video.phsync == (x >= 34 && x < 36));
      CHECK(video.pvsync == !(y == 6));
      expected_rgb = 0;
      if (x < 32 && y < 5 &&
          (epoch == 1 || epoch == 3 || epoch == 5 || epoch == 8 ||
           (epoch == 2 && y < 2)))
        expected_rgb = color(epoch, y * 32 + x);
      CHECK(video.prgb == expected_rgb);
      if (x < 32 && y < 5)
        checked_pixels++;
      if (epoch == 2 && y == 2 && x == 0)
        CHECK(underflow);
      if (epoch == 4 && y == 0 && x == 0)
        CHECK(fetch_failed);
      if (epoch == 5 && y == 0 && x == 0)
        CHECK(underflow && fetch_failed);
      if (epoch == 6)
        CHECK(!underflow && !fetch_failed);
      if ((epoch == 7 || epoch == 8) && y == 0 && x == 0)
        CHECK(fetch_failed && !underflow);
      if (x == 36) {
        x = 0;
        y = y == 7 ? 0 : y + 1;
      } else
        x++;
    }
    cycles++;
    eval();
  }
  CHECK(checked_pixels == 8 * 160);
  CHECK(requests > 40 && responses > 40 && acknowledgements > 40);
  CHECK(encoded_samples >= 8 * 37 * 8);
}

int main() { return run_test(run_case); }
