// Models DVI 1.0 Figure 3-5 using integer state and transmitted-symbol population counts.
// SPDX-License-Identifier: Apache-2.0
function automatic logic [9:0] tmds_reference(
    input logic [7:0] value, input logic [1:0] control_bits,
    input bit active, inout integer running);
  logic [8:0] q;
  logic [9:0] result;
  int ones;
  bit xnor_step;
  if (!active) begin
    running = 0;
    case (control_bits)
      0: result = 10'b1101010100;
      1: result = 10'b0010101011;
      2: result = 10'b0101010100;
      3: result = 10'b1010101011;
    endcase
  end else begin
    xnor_step = $countones(value) > 4 || ($countones(value) == 4 && !value[0]);
    q[0] = value[0];
    for (int i = 1; i < 8; i++)
      q[i] = xnor_step ? ~(q[i-1] ^ value[i]) : q[i-1] ^ value[i];
    q[8] = !xnor_step;
    ones = $countones(q[7:0]);
    if (running == 0 || ones == 4)
      result = {!q[8], q[8], q[8] ? q[7:0] : ~q[7:0]};
    else if ((running > 0 && ones > 4) || (running < 0 && ones < 4))
      result = {1'b1, q[8], ~q[7:0]};
    else
      result = {1'b0, q[8], q[7:0]};
    // Count the actual transmitted bits instead of duplicating RTL delta arithmetic.
    running += 2 * $countones(result) - 10;
  end
  return result;
endfunction
