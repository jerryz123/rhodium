// Checks complete native DAT/RSP transformations across widths/options, ordering, and stalls.
module chi_fragmenter_metadata_case #(
  parameter int DATA_WIDTH = 128,
  parameter type InputT = logic,
  parameter type OutputT = logic
)(
  input logic clock, reset,
  output InputT upstream_in,
  input OutputT upstream_out,
  output OutputT downstream_in,
  input InputT downstream_out,
  output logic done = 0
);
  localparam int DATA_BYTES = DATA_WIDTH / 8;
  type(upstream_in.dat.request.bits) packets [0:3], expected_data;
  type(downstream_in.rsp.bits) response, expected_response;
  type(upstream_in.req.bits) request, expected_request;
  logic [11:0] parent_dbid, child_dbid;
  int children, first_id, packet_id;

  task automatic tick;
    @(posedge clock); #1;
  endtask

  initial begin
    upstream_in = '0;
    downstream_in = '0;
    wait (!reset);
    tick();
    for (int sample = 0; sample < 16; sample++) begin
      // Alternate a full line and a narrow upper-line write. At 512 bits the
      // full line is one packet but still exercises the same DAT/RSP updates.
      request = '0;
      request.opcode = 7'h1c;
      request.src_id = 7'd5;
      request.tgt_id = 7'd9;
      request.txn_id = 12'(sample);
      request.address = 44'h80000000 + 44'(sample * 64 + (sample % 2 == 0 ? 0 : 48));
      request.size_or_num_req = sample % 2 == 0 ? 6'd6 : 6'd2;
      children = sample % 2 == 0 ? 64 / DATA_BYTES : 1;
      first_id = (int'(request.address[5:0]) / DATA_BYTES) * (DATA_BYTES / 16);
      parent_dbid = 12'h800 + 12'(sample * 4);
      upstream_in.req.bits = request;
      upstream_in.req.valid = 1;
      #1; while (!upstream_out.req.ready) tick();
      tick(); upstream_in.req.valid = 0;

      for (int child = 0; child < children; child++) begin
        child_dbid = parent_dbid + 12'(child);
        expected_request = request;
        expected_request.address = request.address + 44'(child * DATA_BYTES);
        if (children > 1) expected_request.size_or_num_req = 6'($clog2(DATA_BYTES));
        while (!downstream_out.req.valid) tick();
        repeat (3) begin
          assert(downstream_out.req.valid && downstream_out.req.bits === expected_request)
            else $fatal(1, "fragment REQ changed or was incorrectly positioned");
          tick();
        end
        downstream_in.req.ready = 1; tick(); downstream_in.req.ready = 0;

        // Nonzero/random unused fields exercise transparency, not additional
        // CHI transaction-profile support. The engine's association fields are valid.
        for (int b = 0; b < $bits(response); b++) response[b] = sample == 0 ? 1'b1 : 1'($urandom());
        response.opcode = 5'd6;
        response.dbid_or_group_id = child_dbid;
        downstream_in.rsp.bits = response;
        downstream_in.rsp.valid = 1;
        #1;
        if (child == 0) begin
          repeat (3) begin
            assert(upstream_out.rsp.valid && upstream_out.rsp.bits === response && !downstream_out.rsp.ready)
              else $fatal(1, "parent DBID response changed under backpressure");
            tick();
          end
          upstream_in.rsp.ready = 1;
        end else
          assert(!upstream_out.rsp.valid) else $fatal(1, "child DBID leaked upstream");
        #1; while (!downstream_out.rsp.ready) tick();
        tick(); downstream_in.rsp.valid = 0; upstream_in.rsp.ready = 0;

        if (child == 0) begin
          // Supply packets in reverse order, with distinct payload and optional metadata.
          for (int packet = children - 1; packet >= 0; packet--) begin
            packet_id = first_id + packet * (DATA_BYTES / 16);
            for (int b = 0; b < $bits(expected_data); b++) expected_data[b] = sample == 0 ? 1'b1 : 1'($urandom());
            expected_data.opcode = 4'd3;
            expected_data.txn_id = parent_dbid;
            expected_data.data_id = 2'(packet_id);
            expected_data.replicate = 1;
            expected_data.num_dat = 3;
            packets[packet_id] = expected_data;
            upstream_in.dat.request.bits = expected_data;
            upstream_in.dat.request.valid = 1;
            #1; while (!upstream_out.dat.request.ready) tick();
            tick(); upstream_in.dat.request.valid = 0;
          end
        end

        packet_id = first_id + child * (DATA_BYTES / 16);
        expected_data = packets[packet_id];
        expected_data.replicate = 0;
        expected_data.num_dat = 0;
        expected_data.txn_id = child_dbid;
        while (!downstream_out.dat.request.valid) tick();
        repeat (3) begin
          assert(downstream_out.dat.request.valid && downstream_out.dat.request.bits === expected_data)
            else $fatal(1, "fragment DAT metadata/payload mismatch (width=%0d sample=%0d child=%0d)", DATA_WIDTH, sample, child);
          tick();
        end
        downstream_in.dat.request.ready = 1; tick(); downstream_in.dat.request.ready = 0;

        for (int b = 0; b < $bits(response); b++) response[b] = sample == 0 ? 1'b1 : 1'($urandom());
        response.opcode = 5'd4;
        // Distinguish this field even on the one-packet 512-bit path.
        response.dbid_or_group_id = child_dbid ^ 12'h400;
        expected_response = response;
        expected_response.dbid_or_group_id = parent_dbid;
        downstream_in.rsp.bits = response;
        downstream_in.rsp.valid = 1;
        #1;
        if (child == children - 1) begin
          repeat (3) begin
            assert(upstream_out.rsp.valid && upstream_out.rsp.bits === expected_response && !downstream_out.rsp.ready)
              else $fatal(1, "completion metadata changed or parent DBID was not restored");
            tick();
          end
          upstream_in.rsp.ready = 1;
        end else
          assert(!upstream_out.rsp.valid) else $fatal(1, "intermediate completion leaked upstream");
        #1; while (!downstream_out.rsp.ready) tick();
        tick(); downstream_in.rsp.valid = 0; upstream_in.rsp.ready = 0;
      end
      assert(upstream_out.req.ready) else $fatal(1, "fragmenter did not retire the write");
    end
    done = 1;
  end
endmodule

module chi_fragmenter_metadata_tb;
  logic clock = 0, reset = 1;
  logic [5:0] done;
  always #5 clock = ~clock;

`define FRAGMENTER_CASE(NAME, INDEX, WIDTH) \
  type(dut.upstream_``NAME``_in) upstream_``NAME``_in; \
  type(dut.upstream_``NAME``_out) upstream_``NAME``_out; \
  type(dut.downstream_``NAME``_in) downstream_``NAME``_in; \
  type(dut.downstream_``NAME``_out) downstream_``NAME``_out; \
  chi_fragmenter_metadata_case #(.DATA_WIDTH(WIDTH), .InputT(type(upstream_``NAME``_in)), .OutputT(type(upstream_``NAME``_out))) check_``NAME ( \
    .clock(clock), .reset(reset), .done(done[INDEX]), \
    .upstream_in(upstream_``NAME``_in), .upstream_out(upstream_``NAME``_out), \
    .downstream_in(downstream_``NAME``_in), .downstream_out(downstream_``NAME``_out));

  `FRAGMENTER_CASE(w128, 0, 128)
  `FRAGMENTER_CASE(o128, 1, 128)
  `FRAGMENTER_CASE(w256, 2, 256)
  `FRAGMENTER_CASE(o256, 3, 256)
  `FRAGMENTER_CASE(w512, 4, 512)
  `FRAGMENTER_CASE(o512, 5, 512)
`undef FRAGMENTER_CASE

  CHIFragmenterMetadataFixture dut(.*);

  initial begin
    repeat (3) @(posedge clock);
    #1; reset = 0;
    wait (&done);
    $display("CHI fragmenter whole-packet metadata checks passed (six width/option variants, 96 writes)");
    $finish;
  end
  initial begin
    repeat (10000) @(posedge clock);
    $fatal(1, "fragmenter metadata timeout");
  end
endmodule
