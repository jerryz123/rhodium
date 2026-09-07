// Exercises invalid boot-address requests and write-data identity/mask checks.
module boot_address_invalid_case #(parameter int MODE = 0);
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_forward_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_forward_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_forward_t;
  typedef struct packed {
    struct packed { ready_t response; } rsp;
    req_forward_t req;
    struct packed { dat_forward_t request; ready_t response; } dat;
  } sn_in_t;
  typedef struct packed {
    struct packed { rsp_forward_t response; } rsp;
    ready_t req;
    struct packed { ready_t request; dat_forward_t response; } dat;
  } sn_out_t;
  typedef struct packed { logic [6:0] node_id; logic [43:0] base_address; } identity_t;

  localparam logic [6:0] READ_NO_SNP = 7'h04;
  localparam logic [6:0] WRITE_NO_SNP_FULL = 7'h1d;
  localparam logic [6:0] WRITE_NO_SNP_PTL = 7'h1c;
  localparam logic [4:0] COMP = 5'h04;
  localparam logic [4:0] DBID_RESP = 5'h06;
  localparam logic [3:0] NON_COPY_BACK_WRITE_DATA = 4'h3;
  localparam logic [3:0] COMP_DATA = 4'h4;
  localparam logic [6:0] REQUESTER_ID = 7'h03;
  localparam logic [6:0] BOOT_ID = 7'h0d;
  localparam logic [43:0] BOOT_BASE = 44'h00001000;
  logic clock = 1'b0;
  logic reset = 1'b1;
  identity_t identity;
  sn_in_t port_in;
  sn_out_t port_out;

  CHIBootAddressRegister dut (.*);
  always #5 clock = ~clock;


  initial begin
    identity = '{node_id: BOOT_ID, base_address: BOOT_BASE};
    port_in = '0;
    repeat (2) @(negedge clock);
    reset = 0;
    @(negedge clock);
    port_in.req.valid = 1;
    port_in.req.bits.opcode = MODE < 3 ? READ_NO_SNP : WRITE_NO_SNP_FULL;
    port_in.req.bits.src_id = REQUESTER_ID;
    port_in.req.bits.tgt_id = BOOT_ID;
    port_in.req.bits.address = BOOT_BASE + (MODE == 0 ? 8 : MODE == 1 ? 2 : 0);
    port_in.req.bits.size_or_num_req = MODE == 2 ? 1 : 2;
    if (MODE >= 3) begin
      do @(posedge clock); while (!port_out.req.ready);
      @(negedge clock);
      port_in.req.valid = 0;
      port_in.rsp.response.ready = 1;
      do @(posedge clock); while (!port_out.rsp.response.valid);
      @(negedge clock);
      port_in.rsp.response.ready = 0;
      port_in.dat.request.valid = 1;
      port_in.dat.request.bits.opcode = NON_COPY_BACK_WRITE_DATA;
      port_in.dat.request.bits.src_id = MODE == 3 ? REQUESTER_ID + 1 : REQUESTER_ID;
      port_in.dat.request.bits.tgt_id = BOOT_ID;
      port_in.dat.request.bits.byte_enable = MODE == 4 ? 16'h001f : 16'h000f;
    end
    @(posedge clock);
    #1;
    $fatal(1, "invalid boot-address access did not assert");
  end
  initial begin
    #10000;
    $fatal(1, "invalid boot-address test timed out");
  end
endmodule

module boot_address_hole_tb;
  boot_address_invalid_case #(.MODE(0)) test_case();
endmodule
module boot_address_alignment_tb;
  boot_address_invalid_case #(.MODE(1)) test_case();
endmodule
module boot_address_size_tb;
  boot_address_invalid_case #(.MODE(2)) test_case();
endmodule
module boot_address_source_tb;
  boot_address_invalid_case #(.MODE(3)) test_case();
endmodule
module boot_address_mask_tb;
  boot_address_invalid_case #(.MODE(4)) test_case();
endmodule
