# Defines independent integer oracles for scalar and packed aggregate operations.
# SPDX-License-Identifier: Apache-2.0
import random

OUTPUTS = ("sum", "difference", "both", "either", "parity", "inverted_sum",
           "equal", "less", "signed_less", "signed_sum", "signed_difference",
           "wrapped_zero", "selected", "ones")
HIERARCHY_OUTPUTS = tuple(f"{instance}_{port}" for instance in ("first", "second", "combined", "portable", "again")
                          for port in ("sum", "difference"))
FLAGS = {"equal", "less", "signed_less", "wrapped_zero"}


def cases(width):
    mask = (1 << width) - 1
    if width <= 5:
        pairs = [(a, b) for a in range(mask + 1) for b in range(mask + 1)]
    else:
        boundaries = (0, 1, 2, (1 << 64) - 1, 1 << 64, (1 << 64) + 1, mask - 1, mask)
        pairs = [(a, b) for a in boundaries for b in boundaries]
        rng = random.Random(20260929)
        pairs += [(rng.getrandbits(width), rng.getrandbits(width)) for _ in range(256)]
    for a, b in pairs:
        for selector in range(4):
            yield a, b, selector, (a ^ b ^ (mask // 3)) & mask


def expected(width, a, b, selector, fallback):
    modulus = 1 << width
    mask = modulus - 1
    signed_a = a if a < modulus // 2 else a - modulus
    signed_b = b if b < modulus // 2 else b - modulus
    total, difference = (a + b) & mask, (a - b) & mask
    return (total, difference, a & b, a | b, a ^ b, mask ^ total,
            int(a == b), int(a < b), int(signed_a < signed_b),
            (signed_a + signed_b) % modulus, (signed_a - signed_b) % modulus,
            int(total == 0), {1: total, 2: difference}.get(selector, fallback), mask)


def hierarchy_expected(width, a, b, c):
    mask = (1 << width) - 1
    first = ((a + b) & mask, (a - b) & mask)
    second = ((b + c) & mask, (b - c) & mask)
    combined = ((first[0] + second[1]) & mask, (first[0] - second[1]) & mask)
    portable = ((combined[0] + first[1]) & mask, (combined[0] - first[1]) & mask)
    again = ((second[0] + combined[1]) & mask, (second[0] - combined[1]) & mask)
    return first + second + combined + portable + again


ARITHMETIC_OUTPUTS = ("product", "signed_product", "widened_product", "joined", "full",
                      "msb", "lsb", "cross_slice", "truncated", "zero_extended", "sign_extended",
                      "widened_shift", "extended_right", "payload") + tuple(
    f"{operation}_{count}" for count in ("narrow", "same", "wide")
    for operation in ("left", "right", "arithmetic", "signed_left"))


def arithmetic_sizes(width):
    return [width, width, width + 3, 2 * width + 1, width, 1, 1, width + 1,
            width, width + 3, width + 3, width + 3, width + 3, 3 * width + 3] + [width] * 12


def arithmetic_cases(width):
    """Exhaust independent primitive operand spaces without a redundant product
    of multiplication operands and unrelated shift counts.
    """
    mask = (1 << width) - 1
    if width <= 5:
        for a in range(mask + 1):
            for b in range(mask + 1):
                yield a, b, a & 1, b, a ^ b
        for a in range(mask + 1):
            for count in range(1 << (width + 3)):
                yield a, mask ^ a, count & 1, count & mask, count
    else:
        edges = (0, 1, 2, (1 << (width - 1)) - 1, 1 << (width - 1),
                 (1 << (width - 1)) + 1, mask - 1, mask)
        counts = (0, 1, width - 2, width - 1, width, width + 1, 127,
                  1 << 32, 1 << (width - 1), mask, 1 << width, (1 << (width + 3)) - 1)
        for a in edges:
            for b in edges:
                yield a, b, a & 1, b, a ^ b
            for count in counts:
                yield a, mask ^ a, count & 1, count & mask, count
        rng = random.Random(20260929 + width)
        for _ in range(256):
            yield (rng.getrandbits(width), rng.getrandbits(width), rng.getrandbits(1),
                   rng.randrange(width + 2), rng.choice(counts))


def arithmetic_expected(width, a, b, narrow, same, wide):
    mask = (1 << width) - 1
    extended_mask = (1 << (width + 3)) - 1
    signed_a = a if a < (1 << (width - 1)) else a - (1 << width)
    signed_b = b if b < (1 << (width - 1)) else b - (1 << width)

    def left(count):
        # Guard before shifting so huge hardware counts never allocate huge
        # Python integers. The oracle states fixed-width overshift semantics.
        return 0 if count >= width else (a << count) & mask

    def arithmetic(count, result_width):
        if count >= result_width:
            return (1 << result_width) - 1 if signed_a < 0 else 0
        return (signed_a >> count) & ((1 << result_width) - 1)

    product = (a * b) & mask
    joined = (a << (width + 1)) | (b << 1) | 1
    extended = signed_a & extended_mask
    payload = (product << (2 * width + 3)) | (left(wide) << (width + 3)) | extended
    result = [product, (signed_a * signed_b) & mask, product, joined, a,
              a >> (width - 1), a & 1, (joined >> 1) & ((1 << (width + 1)) - 1),
              joined & mask, a, extended, left(wide), arithmetic(wide, width + 3), payload]
    for count in (narrow, same, wide):
        result += [left(count), 0 if count >= width else a >> count,
                   arithmetic(count, width), left(count)]
    return tuple(result)


AGGREGATE_OUTPUTS = ("built", "first", "second", "chosen", "selected_items",
                     "reinterpreted", "picked", "single", "roundtrip", "projected")


def aggregate_sizes(width):
    packet = 2 * width + 18
    return [packet, packet, packet, packet, 2 * (width + 7), packet,
            width, width, packet, packet]


def aggregate_cases(width):
    packed_width = 2 * width + 18
    mask = (1 << packed_width) - 1
    rng = random.Random(20260929 + width)
    packets = [0, mask, mask // 3, (mask // 3) ^ mask]
    packets += [1 << bit for bit in range(packed_width)]
    packets += [rng.getrandbits(packed_width) for _ in range(64)]
    for packet in packets:
        a, b = rng.getrandbits(width), rng.getrandbits(width)
        for selector in range(4):
            yield a, b, selector, packet


def aggregate_expected(width, a, b, selector, packet):
    # Spell out the ABI independently: tag/payload/flags in each item, item zero
    # below item one, and header/items/trailer in the outer record.
    item_width = width + 7
    item0 = (1 << (width + 6)) | (a << 6) | (6 << 3) | 1
    item1 = (b << 6) | (3 << 3) | 2
    built_items = (item1 << item_width) | item0
    built = (5 << (2 * item_width + 1)) | (built_items << 1) | 1

    def transform(value):
        item_mask = (1 << item_width) - 1
        low = (value >> 1) & item_mask
        high = (value >> (item_width + 1)) & item_mask
        header = ((value >> (2 * item_width + 1)) & 7) ^ 7
        return (header << (2 * item_width + 1)) | (low << (item_width + 1)) | (high << 1) | (value & 1)

    half = width + 9
    reinterpreted = ((packet & ((1 << half) - 1)) << half) | (packet >> half)
    first = transform(built)
    chosen = {1: built, 2: first}.get(selector, packet)
    selected_items = ((packet >> 1) & ((1 << (2 * item_width)) - 1)) if selector == 2 else built_items
    return (built, first, transform(packet), chosen, selected_items,
            reinterpreted, a, a, packet, packet)
