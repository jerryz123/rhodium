// Checks parameterized RV5Stage fetch-word compaction, append, and clear priority.
module rv5stage_fetch_word_buffer_tb;
  logic clock = 1'b0, reset = 1'b1;
  logic fill_valid = 1'b0, clear = 1'b0, release_valid = 1'b0;
  logic [63:0] fill_base = '0;
  logic [31:0] fill_word = '0;
  logic [1:0] release_count = '0;
  logic [1:0] count;
  logic head_valid, following_valid;
  logic [63:0] head_base, following_base;
  logic [31:0] head_word, following_word;
  RV5StageFetchWordBufferFixture dut (.*);

  always #5 clock = ~clock;

  task automatic step(input bit next_fill_valid,
                      input logic [63:0] next_fill_base,
                      input logic [31:0] next_fill_word,
                      input bit next_release_valid,
                      input logic [1:0] next_release_count,
                      input bit next_clear);
    @(negedge clock);
    fill_valid = next_fill_valid;
    fill_base = next_fill_base;
    fill_word = next_fill_word;
    release_valid = next_release_valid;
    release_count = next_release_count;
    clear = next_clear;
    @(posedge clock);
    #1;
  endtask

  task automatic expect_window(input logic [1:0] expected_count,
                               input bit expected_head_valid,
                               input logic [63:0] expected_head_base,
                               input logic [31:0] expected_head_word,
                               input bit expected_following_valid,
                               input logic [63:0] expected_following_base,
                               input logic [31:0] expected_following_word);
    assert (count == expected_count &&
            head_valid == expected_head_valid &&
            (!head_valid || (head_base == expected_head_base && head_word == expected_head_word)) &&
            following_valid == expected_following_valid &&
            (!following_valid || (following_base == expected_following_base && following_word == expected_following_word)))
      else $fatal(1, "fetch-word window mismatch count=%0d head=%b/%h/%h following=%b/%h/%h",
                  count, head_valid, head_base, head_word,
                  following_valid, following_base, following_word);
  endtask

  initial begin
    repeat (2) @(posedge clock);
    reset = 1'b0;

    step(0, '0, '0, 0, '0, 0);
    expect_window(0, 0, '0, '0, 0, '0, '0);
    step(1, 64'h1000, 32'haaaa_0001, 0, '0, 0);
    expect_window(1, 1, 64'h1000, 32'haaaa_0001, 0, '0, '0);
    step(1, 64'h1004, 32'hbbbb_0002, 0, '0, 0);
    expect_window(2, 1, 64'h1000, 32'haaaa_0001, 1, 64'h1004, 32'hbbbb_0002);

    // Releasing A and completing C on the same edge preserves compact order.
    step(1, 64'h1008, 32'hcccc_0003, 1, 2'd1, 0);
    expect_window(2, 1, 64'h1004, 32'hbbbb_0002, 1, 64'h1008, 32'hcccc_0003);
    step(0, '0, '0, 1, 2'd2, 0);
    expect_window(0, 0, '0, '0, 0, '0, '0);

    // A capacity-three instance can retain exactly three words.
    step(1, 64'h2000, 32'hdddd_0004, 0, '0, 0);
    step(1, 64'h2004, 32'heeee_0005, 0, '0, 0);
    step(1, 64'h2008, 32'hffff_0006, 0, '0, 0);
    expect_window(3, 1, 64'h2000, 32'hdddd_0004, 1, 64'h2004, 32'heeee_0005);

    // A frontend recovery discards simultaneous completion and consumption.
    step(1, 64'h3000, 32'h1111_0007, 1, 2'd1, 1);
    expect_window(0, 0, '0, '0, 0, '0, '0);
    $display("RV5Stage parameterized fetch-word buffer passed");
    $finish;
  end

  initial begin
    #1000;
    $fatal(1, "fetch-word buffer timeout");
  end
endmodule
