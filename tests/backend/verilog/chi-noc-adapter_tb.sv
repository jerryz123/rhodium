// Checks complete CHI adapter payloads, route keys, backpressure, and expected assertion failures.
module chi_noc_adapter_test #(parameter int FAILURE = 0);
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic route_key; CHIReqFlit payload; } req_beat_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_input_t;
  typedef struct packed { logic valid; req_beat_t bits; } req_routed_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_output_t;
  typedef struct packed { logic route_key; CHIRspFlit payload; } rsp_beat_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_input_t;
  typedef struct packed { logic valid; rsp_beat_t bits; } rsp_routed_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_output_t;
  typedef struct packed { logic route_key; CHISnpFlit payload; } snp_beat_t;
  typedef struct packed { logic valid; CHISnoopDispatch bits; } snp_input_t;
  typedef struct packed { logic valid; snp_beat_t bits; } snp_routed_t;
  typedef struct packed { logic valid; CHISnpFlit bits; } snp_output_t;
  typedef struct packed { logic route_key; CHIDatFlit payload; } dat_beat_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_input_t;
  typedef struct packed { logic valid; dat_beat_t bits; } dat_routed_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_output_t;
  logic clock = 0;
  logic reset = 1;
  logic [1:0] injection_site = 2;
  logic [1:0] ejection_site = 2;
  req_input_t req_in_0_in;
  ready_t req_in_0_out;
  ready_t req_routed_0_in;
  req_routed_t req_routed_0_out;
  req_routed_t req_eject_0_in;
  ready_t req_eject_0_out;
  ready_t req_out_0_in;
  req_output_t req_out_0_out;
  req_input_t req_in_1_in;
  ready_t req_in_1_out;
  ready_t req_routed_1_in;
  req_routed_t req_routed_1_out;
  req_routed_t req_eject_1_in;
  ready_t req_eject_1_out;
  ready_t req_out_1_in;
  req_output_t req_out_1_out;
  rsp_input_t rsp_in_0_in;
  ready_t rsp_in_0_out;
  ready_t rsp_routed_0_in;
  rsp_routed_t rsp_routed_0_out;
  rsp_routed_t rsp_eject_0_in;
  ready_t rsp_eject_0_out;
  ready_t rsp_out_0_in;
  rsp_output_t rsp_out_0_out;
  rsp_input_t rsp_in_1_in;
  ready_t rsp_in_1_out;
  ready_t rsp_routed_1_in;
  rsp_routed_t rsp_routed_1_out;
  rsp_routed_t rsp_eject_1_in;
  ready_t rsp_eject_1_out;
  ready_t rsp_out_1_in;
  rsp_output_t rsp_out_1_out;
  snp_input_t snp_in_0_in;
  ready_t snp_in_0_out;
  ready_t snp_routed_0_in;
  snp_routed_t snp_routed_0_out;
  snp_routed_t snp_eject_0_in;
  ready_t snp_eject_0_out;
  ready_t snp_out_0_in;
  snp_output_t snp_out_0_out;
  snp_input_t snp_in_1_in;
  ready_t snp_in_1_out;
  ready_t snp_routed_1_in;
  snp_routed_t snp_routed_1_out;
  snp_routed_t snp_eject_1_in;
  ready_t snp_eject_1_out;
  ready_t snp_out_1_in;
  snp_output_t snp_out_1_out;
  dat_input_t dat_in_0_in;
  ready_t dat_in_0_out;
  ready_t dat_routed_0_in;
  dat_routed_t dat_routed_0_out;
  dat_routed_t dat_eject_0_in;
  ready_t dat_eject_0_out;
  ready_t dat_out_0_in;
  dat_output_t dat_out_0_out;
  dat_input_t dat_in_1_in;
  ready_t dat_in_1_out;
  ready_t dat_routed_1_in;
  dat_routed_t dat_routed_1_out;
  dat_routed_t dat_eject_1_in;
  ready_t dat_eject_1_out;
  ready_t dat_out_1_in;
  dat_output_t dat_out_1_out;

  CHINoCAdapterFixture dut (.*);
  always #5 clock = ~clock;
  initial begin
    #2000;
    $fatal(1, "CHI adapter test timed out");
  end

  task automatic tick;
    @(posedge clock);
    #1;
  endtask

  task automatic clear;
    req_in_0_in = '0;
    req_routed_0_in = '0;
    req_eject_0_in = '0;
    req_out_0_in = '0;
    req_in_1_in = '0;
    req_routed_1_in = '0;
    req_eject_1_in = '0;
    req_out_1_in = '0;
    rsp_in_0_in = '0;
    rsp_routed_0_in = '0;
    rsp_eject_0_in = '0;
    rsp_out_0_in = '0;
    rsp_in_1_in = '0;
    rsp_routed_1_in = '0;
    rsp_eject_1_in = '0;
    rsp_out_1_in = '0;
    snp_in_0_in = '0;
    snp_routed_0_in = '0;
    snp_eject_0_in = '0;
    snp_out_0_in = '0;
    snp_in_1_in = '0;
    snp_routed_1_in = '0;
    snp_eject_1_in = '0;
    snp_out_1_in = '0;
    dat_in_0_in = '0;
    dat_routed_0_in = '0;
    dat_eject_0_in = '0;
    dat_out_0_in = '0;
    dat_in_1_in = '0;
    dat_routed_1_in = '0;
    dat_eject_1_in = '0;
    dat_out_1_in = '0;
  endtask

  task automatic drive(input int seed, input logic [6:0] target);
    req_in_0_in = seed[0] ? '1 : '0;
    req_in_0_in.valid = 1;
    req_in_0_in.bits.tgt_id = target;
    req_in_0_in.bits.txn_id = 12'(seed + 0);
    req_eject_0_in = seed[0] ? '0 : '1;
    req_eject_0_in.valid = 1;
    req_eject_0_in.bits.route_key = 1;
    req_eject_0_in.bits.payload.tgt_id = 7'd5;
    req_eject_0_in.bits.payload.txn_id = 12'(seed + 0);
    req_routed_0_in.ready = seed[0] ^ 1'(0);
    req_out_0_in.ready = seed[0] ^ 1'(0);
    req_in_1_in = seed[0] ? '1 : '0;
    req_in_1_in.valid = 1;
    req_in_1_in.bits.tgt_id = target;
    req_in_1_in.bits.txn_id = 12'(seed + 17);
    req_eject_1_in = seed[0] ? '0 : '1;
    req_eject_1_in.valid = 1;
    req_eject_1_in.bits.route_key = 1;
    req_eject_1_in.bits.payload.tgt_id = 7'd5;
    req_eject_1_in.bits.payload.txn_id = 12'(seed + 31);
    req_routed_1_in.ready = seed[0] ^ 1'(1);
    req_out_1_in.ready = seed[0] ^ 1'(1);
    rsp_in_0_in = seed[0] ? '1 : '0;
    rsp_in_0_in.valid = 1;
    rsp_in_0_in.bits.tgt_id = target;
    rsp_in_0_in.bits.txn_id = 12'(seed + 0);
    rsp_eject_0_in = seed[0] ? '0 : '1;
    rsp_eject_0_in.valid = 1;
    rsp_eject_0_in.bits.route_key = 1;
    rsp_eject_0_in.bits.payload.tgt_id = 7'd5;
    rsp_eject_0_in.bits.payload.txn_id = 12'(seed + 0);
    rsp_routed_0_in.ready = seed[0] ^ 1'(0);
    rsp_out_0_in.ready = seed[0] ^ 1'(0);
    rsp_in_1_in = seed[0] ? '1 : '0;
    rsp_in_1_in.valid = 1;
    rsp_in_1_in.bits.tgt_id = target;
    rsp_in_1_in.bits.txn_id = 12'(seed + 17);
    rsp_eject_1_in = seed[0] ? '0 : '1;
    rsp_eject_1_in.valid = 1;
    rsp_eject_1_in.bits.route_key = 1;
    rsp_eject_1_in.bits.payload.tgt_id = 7'd5;
    rsp_eject_1_in.bits.payload.txn_id = 12'(seed + 31);
    rsp_routed_1_in.ready = seed[0] ^ 1'(1);
    rsp_out_1_in.ready = seed[0] ^ 1'(1);
    snp_in_0_in = seed[0] ? '1 : '0;
    snp_in_0_in.valid = 1;
    snp_in_0_in.bits.target_id = target;
    snp_in_0_in.bits.flit.txn_id = 12'(seed + 0);
    snp_eject_0_in = seed[0] ? '0 : '1;
    snp_eject_0_in.valid = 1;
    snp_eject_0_in.bits.route_key = 1;
    snp_eject_0_in.bits.payload.src_id = 7'd11;
    snp_eject_0_in.bits.payload.txn_id = 12'(seed + 0);
    snp_routed_0_in.ready = seed[0] ^ 1'(0);
    snp_out_0_in.ready = seed[0] ^ 1'(0);
    snp_in_1_in = seed[0] ? '1 : '0;
    snp_in_1_in.valid = 1;
    snp_in_1_in.bits.target_id = target;
    snp_in_1_in.bits.flit.txn_id = 12'(seed + 17);
    snp_eject_1_in = seed[0] ? '0 : '1;
    snp_eject_1_in.valid = 1;
    snp_eject_1_in.bits.route_key = 1;
    snp_eject_1_in.bits.payload.src_id = 7'd11;
    snp_eject_1_in.bits.payload.txn_id = 12'(seed + 31);
    snp_routed_1_in.ready = seed[0] ^ 1'(1);
    snp_out_1_in.ready = seed[0] ^ 1'(1);
    dat_in_0_in = seed[0] ? '1 : '0;
    dat_in_0_in.valid = 1;
    dat_in_0_in.bits.tgt_id = target;
    dat_in_0_in.bits.txn_id = 12'(seed + 0);
    dat_eject_0_in = seed[0] ? '0 : '1;
    dat_eject_0_in.valid = 1;
    dat_eject_0_in.bits.route_key = 1;
    dat_eject_0_in.bits.payload.tgt_id = 7'd5;
    dat_eject_0_in.bits.payload.txn_id = 12'(seed + 0);
    dat_routed_0_in.ready = seed[0] ^ 1'(0);
    dat_out_0_in.ready = seed[0] ^ 1'(0);
    dat_in_1_in = seed[0] ? '1 : '0;
    dat_in_1_in.valid = 1;
    dat_in_1_in.bits.tgt_id = target;
    dat_in_1_in.bits.txn_id = 12'(seed + 17);
    dat_eject_1_in = seed[0] ? '0 : '1;
    dat_eject_1_in.valid = 1;
    dat_eject_1_in.bits.route_key = 1;
    dat_eject_1_in.bits.payload.tgt_id = 7'd5;
    dat_eject_1_in.bits.payload.txn_id = 12'(seed + 31);
    dat_routed_1_in.ready = seed[0] ^ 1'(1);
    dat_out_1_in.ready = seed[0] ^ 1'(1);
  endtask

  task automatic check_paths(input logic expected_route);
    assert (req_routed_0_out.valid && req_routed_0_out.bits.route_key == expected_route &&
            req_routed_0_out.bits.payload === req_in_0_in.bits &&
            req_in_0_out.ready == req_routed_0_in.ready)
      else $fatal(1, "req 0 injection: valid=%b route=%b expected=%b payload_equal=%b ready=%b expected_ready=%b",
                  req_routed_0_out.valid, req_routed_0_out.bits.route_key, expected_route,
                  req_routed_0_out.bits.payload === req_in_0_in.bits, req_in_0_out.ready, req_routed_0_in.ready);
    assert (req_out_0_out.valid && req_out_0_out.bits === req_eject_0_in.bits.payload &&
            req_eject_0_out.ready == req_out_0_in.ready)
      else $fatal(1, "req 0 ejection changed payload or backpressure");
    assert (req_routed_1_out.valid && req_routed_1_out.bits.route_key == expected_route &&
            req_routed_1_out.bits.payload === req_in_1_in.bits &&
            req_in_1_out.ready == req_routed_1_in.ready)
      else $fatal(1, "req 1 injection changed payload, route, or backpressure");
    assert (req_out_1_out.valid && req_out_1_out.bits === req_eject_1_in.bits.payload &&
            req_eject_1_out.ready == req_out_1_in.ready)
      else $fatal(1, "req 1 ejection changed payload or backpressure");
    assert (rsp_routed_0_out.valid && rsp_routed_0_out.bits.route_key == expected_route &&
            rsp_routed_0_out.bits.payload === rsp_in_0_in.bits &&
            rsp_in_0_out.ready == rsp_routed_0_in.ready)
      else $fatal(1, "rsp 0 injection changed payload, route, or backpressure");
    assert (rsp_out_0_out.valid && rsp_out_0_out.bits === rsp_eject_0_in.bits.payload &&
            rsp_eject_0_out.ready == rsp_out_0_in.ready)
      else $fatal(1, "rsp 0 ejection changed payload or backpressure");
    assert (rsp_routed_1_out.valid && rsp_routed_1_out.bits.route_key == expected_route &&
            rsp_routed_1_out.bits.payload === rsp_in_1_in.bits &&
            rsp_in_1_out.ready == rsp_routed_1_in.ready)
      else $fatal(1, "rsp 1 injection changed payload, route, or backpressure");
    assert (rsp_out_1_out.valid && rsp_out_1_out.bits === rsp_eject_1_in.bits.payload &&
            rsp_eject_1_out.ready == rsp_out_1_in.ready)
      else $fatal(1, "rsp 1 ejection changed payload or backpressure");
    assert (snp_routed_0_out.valid && snp_routed_0_out.bits.route_key == expected_route &&
            snp_routed_0_out.bits.payload === snp_in_0_in.bits.flit &&
            snp_in_0_out.ready == snp_routed_0_in.ready)
      else $fatal(1, "snp 0 injection changed payload, route, or backpressure");
    assert (snp_out_0_out.valid && snp_out_0_out.bits === snp_eject_0_in.bits.payload &&
            snp_eject_0_out.ready == snp_out_0_in.ready)
      else $fatal(1, "snp 0 ejection changed payload or backpressure");
    assert (snp_routed_1_out.valid && snp_routed_1_out.bits.route_key == expected_route &&
            snp_routed_1_out.bits.payload === snp_in_1_in.bits.flit &&
            snp_in_1_out.ready == snp_routed_1_in.ready)
      else $fatal(1, "snp 1 injection changed payload, route, or backpressure");
    assert (snp_out_1_out.valid && snp_out_1_out.bits === snp_eject_1_in.bits.payload &&
            snp_eject_1_out.ready == snp_out_1_in.ready)
      else $fatal(1, "snp 1 ejection changed payload or backpressure");
    assert (dat_routed_0_out.valid && dat_routed_0_out.bits.route_key == expected_route &&
            dat_routed_0_out.bits.payload === dat_in_0_in.bits &&
            dat_in_0_out.ready == dat_routed_0_in.ready)
      else $fatal(1, "dat 0 injection changed payload, route, or backpressure");
    assert (dat_out_0_out.valid && dat_out_0_out.bits === dat_eject_0_in.bits.payload &&
            dat_eject_0_out.ready == dat_out_0_in.ready)
      else $fatal(1, "dat 0 ejection changed payload or backpressure");
    assert (dat_routed_1_out.valid && dat_routed_1_out.bits.route_key == expected_route &&
            dat_routed_1_out.bits.payload === dat_in_1_in.bits &&
            dat_in_1_out.ready == dat_routed_1_in.ready)
      else $fatal(1, "dat 1 injection changed payload, route, or backpressure");
    assert (dat_out_1_out.valid && dat_out_1_out.bits === dat_eject_1_in.bits.payload &&
            dat_eject_1_out.ready == dat_out_1_in.ready)
      else $fatal(1, "dat 1 ejection changed payload or backpressure");
  endtask

  initial begin
    clear();
    tick();
    tick();
    @(negedge clock);
    reset = 0;
    if (FAILURE != 0) begin
      case (FAILURE)
        1: begin req_in_0_in.valid = 1; req_in_0_in.bits.tgt_id = 7'd99; end
        2: begin rsp_eject_0_in.valid = 1; rsp_eject_0_in.bits.payload.tgt_id = 7'd6; end
        3: begin injection_site = 0; dat_in_1_in.valid = 1; dat_in_1_in.bits.tgt_id = 7'd5; end
        4: begin req_eject_1_in.valid = 1; req_eject_1_in.bits.payload.tgt_id = 7'd6; end
        5: begin ejection_site = 0; snp_eject_1_in.valid = 1; end
      endcase
      repeat (3) tick();
      $fatal(1, "expected CHI adapter assertion did not fire");
    end else begin
      for (int seed = 0; seed < 4; seed++) begin
        @(negedge clock);
        drive(seed, seed[1] ? 7'd6 : 7'd5);
        #1;
        // Terminal "alternate" sorts before "target": NodeID 6 gets key 0.
        check_paths(!seed[1]);
        tick();
        check_paths(!seed[1]);
      end
      @(negedge clock);
      clear();
      // Unused family rows are harmless while no flit is offered.
      injection_site = 0;
      ejection_site = 0;
      repeat (2) tick();
      $display("CHI all-channel adapter simulation passed");
      $finish;
    end
  end
endmodule

module chi_noc_adapter_tb;
  chi_noc_adapter_test test();
endmodule

module chi_noc_adapter_route_tb;
  chi_noc_adapter_test #(.FAILURE(1)) test();
endmodule

module chi_noc_adapter_target_tb;
  chi_noc_adapter_test #(.FAILURE(2)) test();
endmodule

module chi_noc_adapter_family_route_tb;
  chi_noc_adapter_test #(.FAILURE(3)) test();
endmodule

module chi_noc_adapter_family_target_tb;
  chi_noc_adapter_test #(.FAILURE(4)) test();
endmodule

module chi_noc_adapter_family_snp_site_tb;
  chi_noc_adapter_test #(.FAILURE(5)) test();
endmodule
