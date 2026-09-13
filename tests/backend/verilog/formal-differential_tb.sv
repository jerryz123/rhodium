// Exhaustively checks reduced-width formal semantics and replays solver counterexamples.
// SPDX-License-Identifier: Apache-2.0
module formal_differential_tb;
  logic [2:0] shift_value;
  logic [3:0] shift_amount;
  logic [1:0] high;
  logic [1:0] low;
  logic [1:0] element0;
  logic [1:0] element1;
  logic [1:0] element2;
  logic [3:0] pair_left;
  logic [3:0] pair_right;
  logic [2:0] decode_selector;
  logic [2:0] onehot_selector;
  logic [3:0] onehot_a;
  logic [3:0] onehot_b;
  logic [3:0] onehot_c;
  logic [2:0] shift_left;
  logic [2:0] shift_right;
  logic [2:0] shift_defect;
  logic [3:0] record_bits;
  logic [1:0] record_high;
  logic [5:0] vector_bits;
  logic [1:0] vector_one;
  logic [3:0] record_defect;
  logic [3:0] pair_sum;
  logic [3:0] pair_difference;
  logic [3:0] pair_defect;
  logic [2:0] decode_reference;
  logic [2:0] decode_candidate;
  logic [2:0] decode_defect;
  logic [3:0] onehot_reference;
  logic [3:0] onehot_candidate;
  logic [3:0] onehot_defect;

  integer model_shift_value = 1;
  integer model_shift_amount = 1;
  integer model_shift_reference = 2;
  integer model_shift_defect = 0;
  integer model_record_high = 1;
  integer model_record_low = 2;
  integer model_record_reference = 6;
  integer model_record_defect = 9;
  integer model_pair_left = 1;
  integer model_pair_right = 2;
  integer model_pair_reference = 15;
  integer model_pair_defect = 1;
  integer model_decode_selector = 0;
  integer model_decode_reference = 1;
  integer model_decode_defect = 3;
  integer model_onehot_selector = 1;
  integer model_onehot_a = 1;
  integer model_onehot_b = 2;
  integer model_onehot_c = 3;
  integer model_onehot_reference = 1;
  integer model_onehot_defect = 2;
  integer model_reach_selector = 1;
  integer model_reach_a = 7;
  integer model_reach_b = 0;
  integer model_reach_c = 0;
  integer model_reach_output = 7;
  integer model_property_selector = 1;
  integer model_property_a = 0;
  integer model_property_b = 0;
  integer model_property_c = 0;
  integer model_property_output = 0;
  integer expected_shift_left;
  integer expected_shift_right;
  integer expected_pair_sum;
  integer expected_pair_difference;
  integer expected_decode;
  integer expected_onehot;

  FormalDifferentialDut dut (
    .shift_value(shift_value),
    .shift_amount(shift_amount),
    .high(high),
    .low(low),
    .element0(element0),
    .element1(element1),
    .element2(element2),
    .pair_left(pair_left),
    .pair_right(pair_right),
    .decode_selector(decode_selector),
    .onehot_selector(onehot_selector),
    .onehot_a(onehot_a),
    .onehot_b(onehot_b),
    .onehot_c(onehot_c),
    .shift_left(shift_left),
    .shift_right(shift_right),
    .shift_defect(shift_defect),
    .record_bits(record_bits),
    .record_high(record_high),
    .vector_bits(vector_bits),
    .vector_one(vector_one),
    .record_defect(record_defect),
    .pair_sum(pair_sum),
    .pair_difference(pair_difference),
    .pair_defect(pair_defect),
    .decode_reference(decode_reference),
    .decode_candidate(decode_candidate),
    .decode_defect(decode_defect),
    .onehot_reference(onehot_reference),
    .onehot_candidate(onehot_candidate),
    .onehot_defect(onehot_defect)
  );

  initial begin
    void'($value$plusargs("SHIFT_VALUE=%d", model_shift_value));
    void'($value$plusargs("SHIFT_AMOUNT=%d", model_shift_amount));
    void'($value$plusargs("SHIFT_REFERENCE=%d", model_shift_reference));
    void'($value$plusargs("SHIFT_DEFECT=%d", model_shift_defect));
    void'($value$plusargs("RECORD_HIGH=%d", model_record_high));
    void'($value$plusargs("RECORD_LOW=%d", model_record_low));
    void'($value$plusargs("RECORD_REFERENCE=%d", model_record_reference));
    void'($value$plusargs("RECORD_DEFECT=%d", model_record_defect));
    void'($value$plusargs("PAIR_LEFT=%d", model_pair_left));
    void'($value$plusargs("PAIR_RIGHT=%d", model_pair_right));
    void'($value$plusargs("PAIR_REFERENCE=%d", model_pair_reference));
    void'($value$plusargs("PAIR_DEFECT=%d", model_pair_defect));
    void'($value$plusargs("DECODE_SELECTOR=%d", model_decode_selector));
    void'($value$plusargs("DECODE_REFERENCE=%d", model_decode_reference));
    void'($value$plusargs("DECODE_DEFECT=%d", model_decode_defect));
    void'($value$plusargs("ONEHOT_SELECTOR=%d", model_onehot_selector));
    void'($value$plusargs("ONEHOT_A=%d", model_onehot_a));
    void'($value$plusargs("ONEHOT_B=%d", model_onehot_b));
    void'($value$plusargs("ONEHOT_C=%d", model_onehot_c));
    void'($value$plusargs("ONEHOT_REFERENCE=%d", model_onehot_reference));
    void'($value$plusargs("ONEHOT_DEFECT=%d", model_onehot_defect));
    void'($value$plusargs("REACH_SELECTOR=%d", model_reach_selector));
    void'($value$plusargs("REACH_A=%d", model_reach_a));
    void'($value$plusargs("REACH_B=%d", model_reach_b));
    void'($value$plusargs("REACH_C=%d", model_reach_c));
    void'($value$plusargs("REACH_OUTPUT=%d", model_reach_output));
    void'($value$plusargs("PROPERTY_SELECTOR=%d", model_property_selector));
    void'($value$plusargs("PROPERTY_A=%d", model_property_a));
    void'($value$plusargs("PROPERTY_B=%d", model_property_b));
    void'($value$plusargs("PROPERTY_C=%d", model_property_c));
    void'($value$plusargs("PROPERTY_OUTPUT=%d", model_property_output));

    high = 0;
    low = 0;
    element0 = 0;
    element1 = 0;
    element2 = 0;
    pair_left = 0;
    pair_right = 0;
    decode_selector = 0;
    onehot_selector = 1;
    onehot_a = 0;
    onehot_b = 0;
    onehot_c = 0;
    for (integer value = 0; value < 8; value = value + 1) begin
      for (integer amount = 0; amount < 16; amount = amount + 1) begin
        shift_value = value[2:0];
        shift_amount = amount[3:0];
        expected_shift_left = (value << amount) & 7;
        expected_shift_right = (value >> amount) & 7;
        #1;
        assert (shift_left == expected_shift_left[2:0])
          else $fatal(1, "unequal-width left shift mismatch");
        assert (shift_right == expected_shift_right[2:0])
          else $fatal(1, "unequal-width right shift mismatch");
      end
    end

    for (integer selector = 0; selector < 8; selector = selector + 1) begin
      decode_selector = selector[2:0];
      case (selector & 3)
        0: expected_decode = 1;
        1: expected_decode = 2;
        default: expected_decode = 7;
      endcase
      #1;
      assert (decode_reference == expected_decode[2:0])
        else $fatal(1, "decode reference mismatch");
      assert (decode_candidate == expected_decode[2:0])
        else $fatal(1, "decode candidate mismatch");
    end

    for (integer selector_index = 0; selector_index < 3; selector_index = selector_index + 1) begin
      onehot_selector = 1 << selector_index;
      for (integer first = 0; first < 16; first = first + 1) begin
        for (integer second = 0; second < 16; second = second + 1) begin
          for (integer third = 0; third < 16; third = third + 1) begin
            onehot_a = first[3:0];
            onehot_b = second[3:0];
            onehot_c = third[3:0];
            case (selector_index)
              0: expected_onehot = first;
              1: expected_onehot = second;
              default: expected_onehot = third;
            endcase
            #1;
            assert (onehot_reference == expected_onehot[3:0])
              else $fatal(1, "one-hot reference mismatch");
            assert (onehot_candidate == expected_onehot[3:0])
              else $fatal(1, "one-hot candidate mismatch");
          end
        end
      end
    end

    shift_value = 0;
    shift_amount = 0;
    pair_left = 0;
    pair_right = 0;
    for (integer high_value = 0; high_value < 4; high_value = high_value + 1) begin
      for (integer low_value = 0; low_value < 4; low_value = low_value + 1) begin
        high = high_value[1:0];
        low = low_value[1:0];
        #1;
        assert (record_bits == {high_value[1:0], low_value[1:0]})
          else $fatal(1, "record packing mismatch");
        assert (record_high == high_value[1:0])
          else $fatal(1, "record projection mismatch");
      end
    end
    for (integer first = 0; first < 4; first = first + 1) begin
      for (integer second = 0; second < 4; second = second + 1) begin
        for (integer third = 0; third < 4; third = third + 1) begin
          element0 = first[1:0];
          element1 = second[1:0];
          element2 = third[1:0];
          #1;
          assert (vector_bits == {third[1:0], second[1:0], first[1:0]})
            else $fatal(1, "vector packing mismatch");
          assert (vector_one == second[1:0])
            else $fatal(1, "vector projection mismatch");
        end
      end
    end

    high = 0;
    low = 0;
    element0 = 0;
    element1 = 0;
    element2 = 0;
    for (integer left_value = 0; left_value < 16; left_value = left_value + 1) begin
      for (integer right_value = 0; right_value < 16; right_value = right_value + 1) begin
        pair_left = left_value[3:0];
        pair_right = right_value[3:0];
        expected_pair_sum = (left_value + right_value) & 15;
        expected_pair_difference = (left_value - right_value) & 15;
        #1;
        assert (pair_sum == expected_pair_sum[3:0])
          else $fatal(1, "hierarchical sum mismatch");
        assert (pair_difference == expected_pair_difference[3:0])
          else $fatal(1, "hierarchical difference mismatch");
      end
    end

    shift_value = model_shift_value[2:0];
    shift_amount = model_shift_amount[3:0];
    #1;
    assert (shift_left == model_shift_reference[2:0])
      else $fatal(1, "Rosette shift reference replay mismatch");
    assert (shift_defect == model_shift_defect[2:0])
      else $fatal(1, "Rosette shift defect replay mismatch");
    assert (shift_left != shift_defect)
      else $fatal(1, "Rosette shift counterexample did not reproduce");

    high = model_record_high[1:0];
    low = model_record_low[1:0];
    #1;
    assert (record_bits == model_record_reference[3:0])
      else $fatal(1, "Rosette record reference replay mismatch");
    assert (record_defect == model_record_defect[3:0])
      else $fatal(1, "Rosette record defect replay mismatch");
    assert (record_bits != record_defect)
      else $fatal(1, "Rosette record counterexample did not reproduce");

    pair_left = model_pair_left[3:0];
    pair_right = model_pair_right[3:0];
    #1;
    assert (pair_difference == model_pair_reference[3:0])
      else $fatal(1, "Rosette hierarchy reference replay mismatch");
    assert (pair_defect == model_pair_defect[3:0])
      else $fatal(1, "Rosette hierarchy defect replay mismatch");
    assert (pair_difference != pair_defect)
      else $fatal(1, "Rosette hierarchy counterexample did not reproduce");

    decode_selector = model_decode_selector[2:0];
    #1;
    assert (decode_reference == model_decode_reference[2:0])
      else $fatal(1, "Rosette decode reference replay mismatch");
    assert (decode_defect == model_decode_defect[2:0])
      else $fatal(1, "Rosette decode defect replay mismatch");
    assert (decode_reference != decode_defect)
      else $fatal(1, "Rosette decode counterexample did not reproduce");

    onehot_selector = model_onehot_selector[2:0];
    onehot_a = model_onehot_a[3:0];
    onehot_b = model_onehot_b[3:0];
    onehot_c = model_onehot_c[3:0];
    #1;
    assert (onehot_reference == model_onehot_reference[3:0])
      else $fatal(1, "Rosette one-hot reference replay mismatch");
    assert (onehot_defect == model_onehot_defect[3:0])
      else $fatal(1, "Rosette one-hot defect replay mismatch");
    assert (onehot_reference != onehot_defect)
      else $fatal(1, "Rosette one-hot counterexample did not reproduce");

    onehot_selector = model_reach_selector[2:0];
    onehot_a = model_reach_a[3:0];
    onehot_b = model_reach_b[3:0];
    onehot_c = model_reach_c[3:0];
    #1;
    assert (onehot_candidate == model_reach_output[3:0])
      else $fatal(1, "Rosette reachability witness output mismatch");
    assert (onehot_candidate == 4'd7)
      else $fatal(1, "Rosette reachability witness missed its target");

    onehot_selector = model_property_selector[2:0];
    onehot_a = model_property_a[3:0];
    onehot_b = model_property_b[3:0];
    onehot_c = model_property_c[3:0];
    #1;
    assert (onehot_candidate == model_property_output[3:0])
      else $fatal(1, "Rosette property counterexample output mismatch");
    assert (onehot_candidate != 4'd7)
      else $fatal(1, "Rosette property counterexample did not violate its target");

    $display("formal differential simulation passed");
    $finish;
  end
endmodule
