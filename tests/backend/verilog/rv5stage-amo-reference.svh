// Supplies an independent scalar arithmetic oracle for cache-level AMO sweeps.
function automatic logic [63:0] amo_reference(input logic [63:0] old_value, operand,
                                             input int operation, input bit word_access);
  logic [63:0] left_value, right_value, result;
  left_value = word_access ? {32'b0, old_value[31:0]} : old_value;
  right_value = word_access ? {32'b0, operand[31:0]} : operand;
  case (operation)
    0: result = right_value;
    1: result = left_value + right_value;
    2: result = left_value ^ right_value;
    3: result = left_value & right_value;
    4: result = left_value | right_value;
    5: result = (word_access ? $signed(left_value[31:0]) < $signed(right_value[31:0]) : $signed(left_value) < $signed(right_value)) ? left_value : right_value;
    6: result = (word_access ? $signed(left_value[31:0]) > $signed(right_value[31:0]) : $signed(left_value) > $signed(right_value)) ? left_value : right_value;
    7: result = left_value < right_value ? left_value : right_value;
    8: result = left_value > right_value ? left_value : right_value;
    default: $fatal(1, "invalid AMO in test oracle");
  endcase
  return word_access ? {{32{result[31]}}, result[31:0]} : result;
endfunction

function automatic logic [63:0] amo_operand(input int sample);
  case (sample)
    0: return 64'h80000000_80000001;
    1: return 64'h7fffffff_7ffffffe;
    2: return 64'hffffffff_ffffffff;
    3: return 64'h00000000_00000001;
    default: $fatal(1, "invalid sample in AMO test");
  endcase
endfunction
