// Verifies two-edge stable-level synchronization and deliberately resetless stages.
// SPDX-License-Identifier: Apache-2.0
module clocking_sync_level_tb;
    logic clock = 1'b0;
    logic reset = 1'b0;
    logic asynchronous_level = 1'b0;
    logic synchronized_level;

    SyncLevelTop dut (
        .clock(clock),
        .reset(reset),
        .asynchronous_level(asynchronous_level),
        .synchronized_level(synchronized_level)
    );

    always #5 clock = ~clock;

    initial begin
        repeat (2) @(posedge clock);
        #1;
        assert (synchronized_level == 1'b0)
            else $fatal(1, "initial stable low level did not synchronize");

        @(negedge clock);
        asynchronous_level = 1'b1;
        @(posedge clock);
        #1;
        assert (synchronized_level == 1'b0)
            else $fatal(1, "level leaked through before two destination edges");

        @(negedge clock);
        reset = 1'b1;
        @(posedge clock);
        #1;
        assert (synchronized_level == 1'b1)
            else $fatal(1, "stable high level did not synchronize after two edges");

        repeat (2) @(posedge clock);
        #1;
        assert (synchronized_level == 1'b1)
            else $fatal(1, "ambient reset changed resetless synchronizer state");

        @(negedge clock);
        asynchronous_level = 1'b0;
        @(posedge clock);
        #1;
        assert (synchronized_level == 1'b1)
            else $fatal(1, "falling level leaked through before two destination edges");
        @(posedge clock);
        #1;
        assert (synchronized_level == 1'b0)
            else $fatal(1, "stable low level did not synchronize while reset was active");

        @(negedge clock);
        reset = 1'b0;
        asynchronous_level = 1'b1;
        @(posedge clock);
        #1;
        assert (synchronized_level == 1'b0)
            else $fatal(1, "second rising level leaked through early");
        @(posedge clock);
        #1;
        assert (synchronized_level == 1'b1)
            else $fatal(1, "second stable high level did not synchronize");

        $display("clocking sync-level simulation passed");
        $finish;
    end
endmodule
