// Verifies serial transfers through the production UART model's slave PTY.
// SPDX-License-Identifier: Apache-2.0
module uart_dpi_tb;
  import "DPI-C" function int uart_test_connect(input int model_id);
  import "DPI-C" function int uart_test_write(input int model_id,
                                               input int value);
  import "DPI-C" function int uart_test_read(input int model_id);
  import "DPI-C" function int uart_pty_framing_errors(
      input int model_id);

  localparam int MODEL_ID = 7;
  localparam int CLOCKS_PER_BIT = 16;

  logic clock = 1'b0;
  logic reset = 1'b1;
  logic uart_tx = 1'b1;
  logic uart_rx;
  int received;

  UartDPI dut (.*);
  always #5 clock = ~clock;

  task automatic cycle;
    begin
      @(posedge clock);
      #1;
    end
  endtask

  task automatic expect_uart_rx_byte(input logic [7:0] expected);
    integer index;
    begin
      while (uart_rx !== 1'b0)
        cycle();
      repeat (CLOCKS_PER_BIT / 2) cycle();
      assert (uart_rx == 1'b0)
        else $fatal(1, "UART DPI start bit was not low");
      for (index = 0; index < 8; index = index + 1) begin
        repeat (CLOCKS_PER_BIT) cycle();
        assert (uart_rx == expected[index])
          else $fatal(1, "UART DPI receive bit %0d was incorrect", index);
      end
      repeat (CLOCKS_PER_BIT) cycle();
      assert (uart_rx == 1'b1)
        else $fatal(1, "UART DPI stop bit was not high");
      repeat (CLOCKS_PER_BIT / 2) cycle();
    end
  endtask

  task automatic send_uart_tx_byte(input logic [7:0] value,
                                   input logic valid_stop);
    integer index;
    begin
      uart_tx = 1'b0;
      repeat (CLOCKS_PER_BIT) cycle();
      for (index = 0; index < 8; index = index + 1) begin
        uart_tx = value[index];
        repeat (CLOCKS_PER_BIT) cycle();
      end
      uart_tx = valid_stop;
      repeat (CLOCKS_PER_BIT) cycle();
      uart_tx = 1'b1;
      repeat (8) cycle();
    end
  endtask

  initial begin
    repeat (4) cycle();
    assert (uart_test_connect(MODEL_ID) == 0)
      else $fatal(1, "UART DPI test could not open the slave PTY");
    assert (uart_test_write(MODEL_ID, 32'ha5) == 0)
      else $fatal(1, "UART DPI test could not write the slave PTY");
    repeat (2) cycle();
    assert (uart_rx == 1'b1)
      else $fatal(1, "UART DPI drove serial data during reset");
    reset = 1'b0;

    expect_uart_rx_byte(8'ha5);
    send_uart_tx_byte(8'ha6, 1'b1);
    received = uart_test_read(MODEL_ID);
    assert (received == 32'ha6)
      else $fatal(1,
                  "UART DPI slave PTY received 0x%02x instead of 0xa6",
                  received);

    send_uart_tx_byte(8'h55, 1'b0);
    received = uart_test_read(MODEL_ID);
    assert (received == 32'h55)
      else $fatal(1,
                  "UART DPI slave PTY received 0x%02x instead of 0x55",
                  received);
    assert (uart_pty_framing_errors(MODEL_ID) == 1)
      else $fatal(1, "UART DPI did not count the framing error");
    assert (uart_test_read(MODEL_ID) == -1)
      else $fatal(1, "UART DPI produced an unexpected extra PTY byte");

    $display("UART DPI PTY and serial behavior passed");
    $finish;
  end
endmodule
