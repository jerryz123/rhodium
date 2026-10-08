// Independent AMO arithmetic and corner-case operands for both cache widths.
// SPDX-License-Identifier: Apache-2.0
std::uint64_t amo_reference(std::uint64_t old_value, std::uint64_t operand,
                            int operation, std::uint8_t word_access) {
  std::uint64_t left_value, right_value, result;
  left_value = word_access ? (field(UINT64_C(0), 32, 32) |
                              field(bit_slice(old_value, 0, 32), 32, 0))
                           : old_value;
  right_value = word_access ? (field(UINT64_C(0), 32, 32) |
                               field(bit_slice(operand, 0, 32), 32, 0))
                            : operand;
  switch (operation) {
  case 0: {
    result = right_value;
  } break;
  case 1: {
    result = left_value + right_value;
  } break;
  case 2: {
    result = left_value ^ right_value;
  } break;
  case 3: {
    result = left_value & right_value;
  } break;
  case 4: {
    result = left_value | right_value;
  } break;
  case 5: {
    result =
        (word_access
             ? std::int64_t(sign_extend(bit_slice(left_value, 0, 32), 32)) <
                   std::int64_t(sign_extend(bit_slice(right_value, 0, 32), 32))
             : std::int64_t(sign_extend(left_value, 64)) <
                   std::int64_t(sign_extend(right_value, 64)))
            ? left_value
            : right_value;
  } break;
  case 6: {
    result =
        (word_access
             ? std::int64_t(sign_extend(bit_slice(left_value, 0, 32), 32)) >
                   std::int64_t(sign_extend(bit_slice(right_value, 0, 32), 32))
             : std::int64_t(sign_extend(left_value, 64)) >
                   std::int64_t(sign_extend(right_value, 64)))
            ? left_value
            : right_value;
  } break;
  case 7: {
    result = left_value < right_value ? left_value : right_value;
  } break;
  case 8: {
    result = left_value > right_value ? left_value : right_value;
  } break;
  default: {
    fail(1, "invalid AMO in test oracle");
  } break;
  }
  return word_access ? sign_extend(result, 32) : result;
}

std::uint64_t amo_operand(int sample) {
  switch (sample) {
  case 0: {
    return UINT64_C(0x8000000080000001);
  } break;
  case 1: {
    return UINT64_C(0x7fffffff7ffffffe);
  } break;
  case 2: {
    return UINT64_C(0xffffffffffffffff);
  } break;
  case 3: {
    return UINT64_C(1);
  } break;
  default: {
    fail(1, "invalid sample in AMO test");
  } break;
  }
}
