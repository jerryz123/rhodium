// Implements fixed-width wide arithmetic, bit placement, word marshalling, and masked memory merges for rsim models.
// SPDX-License-Identifier: Apache-2.0
#ifndef RHODIUM_RSIM_BITS_HPP
#define RHODIUM_RSIM_BITS_HPP
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace rhodium_rsim {
// Low word first; only inputs may contain padding until the evaluator normalizes them.
// No implicit scalar conversion: high bits must never disappear in a control expression.
template <std::size_t W> struct WideBits {
  static_assert(W > 64, "wide carriers require more than 64 bits");
  std::array<std::uint32_t, (W + 31) / 32> words{};
  void normalize() {
    if constexpr (W % 32 != 0) words.back() &= (UINT32_C(1) << (W % 32)) - 1;
  }
};

template <std::size_t W> WideBits<W> normalized(WideBits<W> value) {
  value.normalize();
  return value;
}
template <std::size_t W> WideBits<W> operator~(WideBits<W> value) {
  for (auto& word : value.words) word = ~word;
  return normalized(value);
}
template <std::size_t W> WideBits<W> operator&(WideBits<W> left, const WideBits<W>& right) {
  for (std::size_t i = 0; i < left.words.size(); ++i) left.words[i] &= right.words[i];
  return normalized(left);
}
template <std::size_t W> WideBits<W> operator|(WideBits<W> left, const WideBits<W>& right) {
  for (std::size_t i = 0; i < left.words.size(); ++i) left.words[i] |= right.words[i];
  return normalized(left);
}
template <std::size_t W> WideBits<W> operator^(WideBits<W> left, const WideBits<W>& right) {
  for (std::size_t i = 0; i < left.words.size(); ++i) left.words[i] ^= right.words[i];
  return normalized(left);
}
// Generated operands are normalized before comparisons, including external inputs.
template <std::size_t W> bool operator==(const WideBits<W>& left, const WideBits<W>& right) {
  return left.words == right.words;
}

// Propagate carry/borrow through unsigned limbs, then discard bits above W.
template <std::size_t W> WideBits<W> operator+(WideBits<W> left, const WideBits<W>& right) {
  std::uint64_t carry = 0;
  for (std::size_t i = 0; i < left.words.size(); ++i) {
    const auto sum = std::uint64_t(left.words[i]) + right.words[i] + carry;
    left.words[i] = static_cast<std::uint32_t>(sum);
    carry = sum >> 32;
  }
  return normalized(left);
}
template <std::size_t W> WideBits<W> operator-(WideBits<W> left, const WideBits<W>& right) {
  std::uint64_t borrow = 0;
  for (std::size_t i = 0; i < left.words.size(); ++i) {
    const auto minuend = std::uint64_t(left.words[i]);
    const auto subtrahend = std::uint64_t(right.words[i]) + borrow;
    left.words[i] = static_cast<std::uint32_t>(minuend - subtrahend);
    borrow = minuend < subtrahend;
  }
  return normalized(left);
}

// Schoolbook multiplication retains only destination limbs below W. A limb
// product plus an existing limb and carry is at most 2^64 - 1, so each unsigned
// accumulator fits uint64_t. Carry beyond the final stored limb is discarded.
template <std::size_t W> WideBits<W> operator*(const WideBits<W>& left, const WideBits<W>& right) {
  WideBits<W> result{};
  for (std::size_t i = 0; i < result.words.size(); ++i) {
    std::uint64_t carry = 0;
    for (std::size_t j = 0; j < result.words.size() - i; ++j) {
      const auto product = std::uint64_t(left.words[i]) * right.words[j] + result.words[i + j] + carry;
      result.words[i + j] = static_cast<std::uint32_t>(product);
      carry = product >> 32;
    }
  }
  return normalized(result);
}

// Compare normalized values from the most-significant limb, including every
// high bit even when the low 64 bits match. Storage remains unsigned throughout.
template <std::size_t W> bool operator<(const WideBits<W>& left, const WideBits<W>& right) {
  for (std::size_t i = left.words.size(); i > 0; --i) {
    if (left.words[i - 1] != right.words[i - 1]) return left.words[i - 1] < right.words[i - 1];
  }
  return false;
}
// Only the operation interprets the declared sign bit. Equal-sign two's-complement
// values retain unsigned ordering, without signed host conversion or overflow.
template <std::size_t W> bool signed_less(const WideBits<W>& left, const WideBits<W>& right) {
  const auto sign = UINT32_C(1) << ((W - 1) % 32);
  const bool left_negative = (left.words.back() & sign) != 0;
  const bool right_negative = (right.words.back() & sign) != 0;
  return left_negative != right_negative ? left_negative : left < right;
}

// Explicit template widths retain hardware sizing even for uint64_t carriers.
template <std::size_t W>
using Carrier = std::conditional_t<(W <= 64), std::uint64_t, WideBits<W>>;

template <std::size_t W> std::uint32_t word(const Carrier<W>& value, std::size_t index) {
  if (index >= (W + 31) / 32) return 0;
  if constexpr (W <= 64) return static_cast<std::uint32_t>(value >> (index * 32));
  else return value.words[index];
}
template <std::size_t W> void put_word(Carrier<W>& value, std::size_t index, std::uint32_t bits) {
  if constexpr (W <= 64) value |= std::uint64_t(bits) << (index * 32);
  else value.words[index] = bits;
}
template <std::size_t W> Carrier<W> normalize(Carrier<W> value) {
  if constexpr (W > 64) value.normalize();
  else if constexpr (W < 64) value &= (UINT64_C(1) << W) - 1;
  return value;
}

// Copy packed ABI words explicitly; no object layout or aliasing assumption
// crosses the boundary. Outgoing padding is zero, incoming padding is discarded.
template <std::size_t W>
std::array<std::uint32_t, (W + 31) / 32> to_words(const Carrier<W>& value) {
  std::array<std::uint32_t, (W + 31) / 32> result{};
  const auto clean = normalize<W>(value);
  for (std::size_t i = 0; i < result.size(); ++i) result[i] = word<W>(clean, i);
  return result;
}
template <std::size_t W> Carrier<W> from_words(const std::uint32_t* words) {
  Carrier<W> result{};
  for (std::size_t i = 0; i < (W + 31) / 32; ++i) put_word<W>(result, i, words[i]);
  return normalize<W>(result);
}

// Merge one typed leaf using mask granules in the enclosing canonical layout.
// Its offset can split a granule across fields or limbs. Build a mask per limb
// instead of emitting a growing expression of wide constants for every granule.
// Operands are normalized; verified RTL supplies positive, in-range granularity.
template <std::size_t W, std::size_t Mask>
Carrier<W> masked_merge(const Carrier<W>& old, const Carrier<W>& data,
                       const Carrier<Mask>& mask, std::size_t granularity,
                       std::size_t offset) {
  Carrier<W> result{};
  for (std::size_t i = 0; i < (W + 31) / 32; ++i) {
    std::uint32_t selected = 0;
    for (unsigned bit = 0; bit < 32 && i * 32 + bit < W; ++bit) {
      const auto granule = (offset + i * 32 + bit) / granularity;
      if (((word<Mask>(mask, granule / 32) >> (granule % 32)) & 1u) != 0) {
        selected |= UINT32_C(1) << bit;
      }
    }
    put_word<W>(result, i, (word<W>(old, i) & ~selected) | (word<W>(data, i) & selected));
  }
  return normalize<W>(result);
}

// Merge bits into a limb, discarding limbs outside the result.
template <std::size_t W> void merge_word(Carrier<W>& value, std::size_t index, std::uint32_t bits) {
  if (index < (W + 31) / 32) put_word<W>(value, index, word<W>(value, index) | bits);
}
// OR normalized source bits at a nonnegative offset, discarding overflow.
// Concatenation places whole fields; left shifts truncate at the result width.
// Splitting a limb avoids shifts by 32 or 64.
template <std::size_t To, std::size_t From>
void place_bits(Carrier<To>& result, const Carrier<From>& value, std::size_t low) {
  const auto first = low / 32;
  const auto shift = low % 32;
  for (std::size_t i = 0; i < (From + 31) / 32; ++i) {
    const auto bits = word<From>(value, i);
    merge_word<To>(result, first + i, bits << shift);
    if (shift != 0) merge_word<To>(result, first + i + 1, bits >> (32 - shift));
  }
}
// The first operand occupies the highest bits. Explicit template widths keep
// narrow carriers sized correctly and assemble many operands into one result.
template <std::size_t To, std::size_t... Widths>
Carrier<To> concat(const Carrier<Widths>&... values) {
  static_assert(To == (Widths + ...), "concatenation widths must sum to the result width");
  Carrier<To> result{};
  std::size_t remaining = To;
  ((remaining -= Widths, place_bits<To, Widths>(result, values, remaining)), ...);
  return normalize<To>(result);
}

// Slice or zero-extend normalized bits without a shift by a host word's width.
// The emitter supplies a valid offset from verified RTL; absent high words are zero.
template <std::size_t To, std::size_t From>
Carrier<To> slice(const Carrier<From>& value, std::size_t low = 0) {
  Carrier<To> result{};
  const auto first = low / 32;
  const auto shift = low % 32;
  for (std::size_t i = 0; i < (To + 31) / 32; ++i) {
    auto bits = word<From>(value, first + i) >> shift;
    if (shift != 0) bits |= word<From>(value, first + i + 1) << (32 - shift);
    put_word<To>(result, i, bits);
  }
  return normalize<To>(result);
}

// Set bits [first, W) without changing lower bits, then clear final-limb padding.
// Sign extension and arithmetic shifts supply a boundary within the result.
template <std::size_t W> Carrier<W> fill_high(Carrier<W> value, std::size_t first) {
  for (std::size_t i = first / 32; i < (W + 31) / 32; ++i) {
    const auto fill = UINT32_MAX << (i == first / 32 ? first % 32 : 0);
    merge_word<W>(value, i, fill);
  }
  return normalize<W>(value);
}

// Fill only bits above the source's declared sign bit, using unsigned words.
template <std::size_t To, std::size_t From>
Carrier<To> sign_extend(const Carrier<From>& value) {
  static_assert(To >= From, "sign extension cannot narrow");
  auto result = slice<To, From>(value);
  if ((word<From>(value, (From - 1) / 32) & (UINT32_C(1) << ((From - 1) % 32))) != 0) {
    return fill_high<To>(result, From);
  }
  return normalize<To>(result);
}

// Inspect the complete normalized count before narrowing to a host index. Any
// nonzero word above bit 63 already exceeds a 32- or 64-bit host's width bound.
// Saturating to the operand width also makes the following limb shifts bounded.
template <std::size_t Count> std::size_t shift_count(const Carrier<Count>& amount, std::size_t width) {
  for (std::size_t i = 2; i < (Count + 31) / 32; ++i) {
    if (word<Count>(amount, i) != 0) return width;
  }
  const auto low = std::uint64_t(word<Count>(amount, 0)) | (std::uint64_t(word<Count>(amount, 1)) << 32);
  return low < width ? static_cast<std::size_t>(low) : width;
}

// Shift normalized values using bounded limb indices and intra-limb shifts.
// Narrow data with a wide count uses these helpers too; ordinary narrow shifts
// retain the emitter's existing uint64_t fast path.
template <std::size_t W, std::size_t Count>
Carrier<W> shift_left(const Carrier<W>& value, const Carrier<Count>& amount) {
  const auto count = shift_count<Count>(amount, W);
  if (count == W) return {};
  Carrier<W> result{};
  place_bits<W, W>(result, value, count);
  return normalize<W>(result);
}
template <std::size_t W, std::size_t Count>
Carrier<W> shift_right(const Carrier<W>& value, const Carrier<Count>& amount) {
  const auto count = shift_count<Count>(amount, W);
  return count == W ? Carrier<W>{} : slice<W, W>(value, count);
}
// Arithmetic right shifts read the declared sign bit and fill above W-count.
// A saturated count gives zero or all ones; no signed C++ shift is used.
template <std::size_t W, std::size_t Count>
Carrier<W> shift_signed(const Carrier<W>& value, const Carrier<Count>& amount) {
  const auto count = shift_count<Count>(amount, W);
  const auto result = count == W ? Carrier<W>{} : slice<W, W>(value, count);
  const bool negative = (word<W>(value, (W - 1) / 32) & (UINT32_C(1) << ((W - 1) % 32))) != 0;
  return negative ? fill_high<W>(result, W - count) : result;
}
}
#endif
