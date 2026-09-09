// Exhaustively checks runtime alignment, explicit bounds, and mismatched operand widths.
module runtime_alignment_tb;
  logic [7:0] value;
  logic [5:0] exponent;
  wire full, bounded, five_bit, single_bit, narrow_exponent, unit;
  RuntimeAlignmentFixture dut(.*);

  function automatic bit aligned(input int address, input int power, input int bound);
    if (power > bound) return 0;
    return (address % (1 << power)) == 0;
  endfunction

  initial begin
    for (int address = 0; address < 256; address++) begin
      for (int power = 0; power < 64; power++) begin
        value = 8'(address);
        exponent = 6'(power);
        #1;
        assert (full === aligned(address, power, 8)) else $fatal(1, "full alignment: %0d/%0d", address, power);
        assert (bounded === aligned(address, power, 6)) else $fatal(1, "bounded alignment: %0d/%0d", address, power);
        assert (five_bit === aligned(address & 31, power, 5)) else $fatal(1, "five-bit alignment");
        assert (single_bit === aligned(address & 1, power, 1)) else $fatal(1, "single-bit alignment");
        assert (narrow_exponent === aligned(address, power & 1, 8)) else $fatal(1, "narrow exponent alignment");
        assert (unit === (power == 0)) else $fatal(1, "unit alignment bound");
      end
    end
    $display("runtime alignment passed: 16384 inputs across six width/bound variants");
    $finish;
  end
endmodule
