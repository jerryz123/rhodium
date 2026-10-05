# SPDX-License-Identifier: Apache-2.0
"""Independent integer expectations for the scalar bit-manipulation fixture."""


def output_widths(width):
    extended = min(width + 3, 64)
    part = min(width, 32)
    tail = min(width, 62)
    return (
        ("left_shift", width), ("right_shift", width), ("signed_shift", width),
        ("signed_left", width), ("signed_less", 1),
        ("full", width), ("msb", 1), ("lsb", 1),
        ("joined", 2 * part), ("joined_three", 2 + tail),
        ("cross_slice", 2), ("truncated", part),
        ("zero_extended", extended), ("sign_extended", extended),
        ("extended_right", extended), ("widened_sum", extended),
        ("widened_product", extended), ("widened_shift", extended),
        ("constant_left", width), ("constant_right", width), ("constant_signed", width),
    )


def signed(value, width):
    return value - (1 << width) if value >= (1 << (width - 1)) else value


def expected(width, a, b, amount):
    mask = (1 << width) - 1
    a, b = a & mask, b & mask
    sa, sb = signed(a, width), signed(b, width)
    base_width = min(width, 63)
    base_mask = (1 << base_width) - 1
    base_a, base_b = a & base_mask, b & base_mask
    base_signed = signed(base_a, base_width)
    extended_width = min(width + 3, 64)
    extended_mask = (1 << extended_width) - 1
    part_width = min(width, 32)
    part_mask = (1 << part_width) - 1
    tail_width = min(width, 62)
    joined = (a & part_mask) * (1 << part_width) + (b & part_mask)
    three = (1 << (tail_width + 1)) + (a & 1) * (1 << tail_width) + b % (1 << tail_width)
    arithmetic = (mask if sa < 0 else 0) if amount >= width else (sa >> amount) & mask
    extended_right = ((extended_mask if base_signed < 0 else 0) if amount >= extended_width
                      else (base_signed >> amount) & extended_mask)
    left = 0 if amount >= width else (a << amount) & mask
    return [
        left, 0 if amount >= width else a >> amount, arithmetic, left, int(sa < sb),
        a, a >> (width - 1), a & 1,
        joined, three, (joined >> (part_width - 1)) & 3, b & part_mask,
        base_a, base_signed & extended_mask, extended_right,
        (base_a + base_b) & base_mask, (base_a * base_b) & base_mask,
        0 if amount >= base_width else (base_a << amount) & base_mask,
        0, 0, mask if sa < 0 else 0,
    ]
