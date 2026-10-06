# SPDX-License-Identifier: Apache-2.0
"""Check wide operations and state against Python integers and optional direct SV."""
import argparse
import os
from pathlib import Path
import random
import shlex
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
WIDTHS = (64, 65, 127, 128, 129, 512)


def output_widths(width):
    return [(name, width) for name in ('both', 'either', 'parity', 'inverted')] + [
        ('equal', 1), ('selected', width), ('low', 63), ('high', 64),
        ('cross', 65 if width > 128 else 33), ('zero', width), ('sign', width),
        ('small_sign', width), ('grown', width + 33), ('grown_sign', width + 33),
        ('shrunk', width), ('constant', width),
        ('sum', width), ('difference', width), ('product', width),
        ('unsigned_less', 1), ('signed_less', 1), ('sum_extended', width + 33),
        ('difference_extended', width + 33), ('product_extended', width + 33),
        ('left', width), ('right', width), ('signed_right', width),
        ('left_low_count', width), ('right_low_count', width), ('signed_low_count', width),
        ('narrow_left', 64), ('narrow_right', 64), ('narrow_signed', 64),
        ('small_left', 5), ('small_right', 5), ('small_signed', 5),
        ('joined', 2 * width + 69), ('joined_narrow', 74), ('joined_a', width), ('joined_low', 64),
        ('lookup_narrow', 5), ('lookup_wide', width), ('packet_tag', 5), ('packet_data', 64),
        ('decoded', width), ('decode_reordered', width), ('decode_small', 5),
        ('decode_from_small', width), ('decode_empty', width), ('decode_catchall', width),
        ('onehot_wide', width), ('onehot_narrow', 8), ('guarded_onehot', width)] + [
        (name, width) for name in ('first_q', 'first_delayed', 'second_q', 'second_delayed')]


def words(value, width):
    return [(value >> offset) & 0xffffffff for offset in range(0, width, 32)]


def signed(value, width):
    return value - (1 << width) if value & (1 << (width - 1)) else value


def shifted(value, amount, width):
    mask = (1 << width) - 1
    # Bound Python's allocation for a huge left shift; signed right shift of
    # that count has the same sign fill as a shift by the declared width.
    count = min(amount, width)
    return [(value << count) & mask, value >> count, (signed(value, width) >> count) & mask]


def selection_results(width, a, b, amount, small, narrow, select):
    mask, sign = (1 << width) - 1, 1 << (width - 1)
    seed = sign + 0x123456789abcdef
    code = a & (sign | (1 << 32) | 1)
    decoded = {0: (mask, mask), sign: (seed, mask ^ sign),
               1 << 32: (sign + 5, mask ^ (1 << 32))}.get(code, (7, mask ^ 1))
    small_decode = {0: (3, 31), 1 << 64: (9, 29), 1 << 128: (21, 31)}.get(
        amount & ((1 << 128) | (1 << 64) | 1), (7, 15))
    packet = {1: (17, a & ((1 << 64) - 1)),
              (1 << 64) + 1: (29, b & ((1 << 64) - 1))}.get(amount, (3, narrow))
    # Invalid one-hot encodings are partial RTL values, not promised zeros or
    # priority choices. Check only valid selections and their guarded context.
    choice = amount.bit_length() if amount and amount & (amount - 1) == 0 else None
    onehot = sign + choice if choice is not None else None
    return [
        {0: 7, 1: 9, sign + 1: 11, mask: 13}.get(a, small),
        {0: a, 1: b, (1 << 64) + 1: a ^ b, (1 << 128) + 1: a ^ mask}.get(amount, seed),
        *packet, decoded, decoded, small_decode,
        {0: (mask, mask), 1: (seed, mask ^ 1), 2: (0, mask)}.get(small & 3, (9, mask)),
        (seed, mask ^ sign), mask, onehot, choice, onehot if select == 1 else 0]


def expected_words(results, width):
    flat = []
    assert len(results) == len(output_widths(width))
    for value, (_, bits) in zip(results, output_widths(width)):
        if value is None:
            flat.extend([None] * ((bits + 31) // 32))
        elif isinstance(value, tuple):
            data, care = value
            # Cared RTL bits are portable; zero output padding is also required.
            care |= ((1 << (((bits + 31) // 32) * 32)) - 1) ^ ((1 << bits) - 1)
            flat.extend(zip(words(data, bits), words(care, bits)))
        else:
            flat.extend(words(value, bits))
    return flat


def stimuli(width):
    rng = random.Random(731 + width)
    mask = (1 << width) - 1
    seed = (1 << (width - 1)) + 0x123456789abcdef
    sign = 1 << (width - 1)
    alternating = int('aa' * ((width + 7) // 8), 16) & mask
    boundaries = [i for i in (31, 32, 63, 64, 95, 96, 127, 128, 255, 256, 511) if i < width]
    patterns = sorted({0, mask, 1, sign, sign - 1, sign + 1, alternating, mask ^ alternating,
                       *(1 << i for i in boundaries), *((1 << i) - 1 for i in boundaries)})
    pairs = [(a, b) for a in patterns for b in patterns]
    # Equal low bits must not hide ordering differences in any higher limb.
    for bit in boundaries:
        pairs += [(1 | (1 << bit), 1), (1, 1 | (1 << bit))]
    pairs += [(rng.getrandbits(width), rng.getrandbits(width)) for _ in range(256)]
    counts = sorted({0, 1, 2, 4, 5, 6, 31, 32, 33, 63, 64, 65,
                     width - 1, width, width + 1, 1 << 32, 1 << 63,
                     (1 << 64) - 1, 1 << 64, (1 << 64) + 1, 1 << 96,
                     1 << 128, (1 << 128) + 1})
    samples = [(a, b, counts[i % len(counts)]) for i, (a, b) in enumerate(pairs)]
    samples += [(a, mask ^ a, count) for a in (0, 1, mask, sign, sign - 1, alternating) for count in counts]
    # Every one-hot choice, including bits 64/96/128; invalid encodings remain
    # live computations but are excluded from portable result expectations.
    for bit in range(129):
        samples += [(rng.getrandbits(width), rng.getrandbits(width), 1 << bit),
                    (rng.getrandbits(width), rng.getrandbits(width), (1 << bit) | (1 << ((bit + 1) % 129)))]
    # Exhaust cared decoder bits while independently varying uncared limbs.
    input_care = sign | (1 << 32) | 1
    for pattern in range(8):
        cared = ((pattern & 1) * sign) | (((pattern >> 1) & 1) << 32) | (pattern >> 2)
        for _ in range(8):
            samples.append(((rng.getrandbits(width) & ~input_care) | cared,
                            rng.getrandbits(width), rng.getrandbits(129)))
    state = [0, 0, 0, 0]
    # Observe combinational constants before the first edge. Startup register
    # values are unspecified in portable RTL, so omit only those expectations.
    lines = [' '.join('0' for _ in range(11 + 2 * ((width + 31) // 32)))]
    initial = [0, 0, 0, mask, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, seed, *([0] * 24)]
    expected = [expected_words(initial + selection_results(width, 0, 0, 0, 0, 0, 0) + [None] * 4, width)]
    for index, (a, b, amount) in enumerate(samples):
        tick = int(index % 5 != 2)
        reset = int(index == 0 or index % 41 == 0)
        enable = int(index % 4 != 0)
        select = index % 4
        narrow = [0, (1 << 64) - 1, 1 << 63, rng.getrandbits(64)][index % 4]
        small = index % 32
        chosen = [a, b, a ^ b, (~a) & mask][select]
        # Deliberately dirty external padding. It must not affect equality,
        # widening, next state, or the padding in any emitted output word.
        padding = ((1 << (((width + 31) // 32) * 32)) - 1) ^ mask
        raw_a = a | (padding if index % 2 else 0)
        raw_b = b | (padding if index % 3 else 0)
        raw_amount = amount | (((1 << 160) - (1 << 129)) if index % 2 else 0)
        fields = [tick, reset, enable, select, narrow, small, *words(raw_a, width),
                  *words(raw_b, width), *words(raw_amount, 129)]
        lines.append(' '.join(f'{v:x}' for v in fields))
        if tick:
            state = [seed, 3, seed, 3] if reset else [
                state[0] ^ (chosen if enable else 0), state[0],
                state[2] ^ (b if enable else 0), state[2]]
        sum_bits, difference_bits, product_bits = (a + b) & mask, (a - b) & mask, (a * b) & mask
        results = [a & b, a | b, a ^ b, (~a) & mask, int(a == b), chosen,
                   a & ((1 << 63) - 1), a >> (width - 64),
                   (a >> 31) & ((1 << (65 if width > 128 else 33)) - 1),
                   narrow, signed(narrow, 64) & mask, signed(small, 5) & mask,
                   a, signed(a, width) & ((1 << (width + 33)) - 1), a, seed,
                   sum_bits, difference_bits, product_bits, int(a < b),
                   int(signed(a, width) < signed(b, width)),
                   sum_bits, difference_bits, product_bits,
                   *shifted(a, amount, width), *shifted(a, amount & ((1 << 64) - 1), width),
                   *shifted(narrow, amount, 64), *shifted(small, amount, 5),
                   (small << (2 * width + 64)) | (a << (width + 64)) | (b << 64) | narrow,
                   (small << 69) | (narrow << 5) | small, a, narrow,
                   *selection_results(width, a, b, amount, small, narrow, select), *state]
        expected.append(expected_words(results, width))
    return '\n'.join(lines) + '\n', expected


def driver(width, reference=False):
    model = f'VWide{width}' if reference else f'rsim_pWide{width}::Model'
    include = f'VWide{width}.h' if reference else f'Wide{width}.hpp'
    inputs = 'dut.' if reference else 'dut.inputs.p'
    read = []
    for name, bits in (('a', width), ('b', width), ('amount', 129)):
        if bits <= 64:
            read += [f'{inputs}{name} = 0;', f'for (unsigned i = 0; i < 2; ++i) {{ std::cin >> word; {inputs}{name} |= std::uint64_t(word) << (32*i); }}']
        else:
            access = f'{inputs}{name}' + ('' if reference else '.words')
            read += [f'for (unsigned i = 0; i < {(bits+31)//32}; ++i) {{ std::cin >> word; {access}[i] = word; }}']
            if reference and bits % 32:
                read += [f'{access}[{bits//32}] &= {(1 << (bits % 32)) - 1};']
    output = []
    for name, bits in output_widths(width):
        access = f'dut.{name}' if reference else 'dut.outputs().p' + name.replace('_', '_u')
        if bits > 64:
            access += '' if reference else '.words'
            output += [f'for (unsigned i = 0; i < {(bits+31)//32}; ++i) {{ std::cout << {access}[i] << " "; }}']
        else:
            output += [f'for (unsigned i = 0; i < {(bits+31)//32}; ++i) {{ std::cout << std::uint32_t(std::uint64_t({access}) >> (32*i)) << " "; }}']
    output_lines = '\n    '.join(output)
    return f'''// SPDX-License-Identifier: Apache-2.0
#include "{include}"
#include <cstdint>
#include <iostream>
int main() {{
  {model} dut;
  std::uint64_t tick, reset, enable, select, narrow, small;
  std::uint32_t word;
  std::cin >> std::hex; std::cout << std::hex;
  while (std::cin >> tick >> reset >> enable >> select >> narrow >> small) {{
    {'dut.clock = 0; dut.eval();' if reference else ''}
    {inputs}reset = reset; {inputs}enable = enable; {inputs}select = select;
    {inputs}narrow = narrow; {inputs}small = small;
    {' '.join(read)}
    dut.eval(); dut.eval();
    if (tick) {{ {'dut.clock = 1; dut.eval();' if reference else 'dut.tick();'} }}
    {output_lines}
    std::cout << "\\n";
  }}
}}
'''


def aggregate_layout(width):
    element = [('.pmark', 5, width), ('.pdata', width, 0)]
    vector = [(f'[{i}]' + path, bits, i * (width + 5) + low)
              for i in range(3) for path, bits, low in element]
    packet = [('.phead', 7, 3 * (width + 5))] + [('.pitems' + path, bits, low) for path, bits, low in vector]
    total = 7 + 3 * (width + 5)
    return total, element, vector, packet


def aggregate_ports(width):
    total, element, vector, packet = aggregate_layout(width)
    return [('echo', total, packet), ('packed_bits', total, [('', total, 0)]),
            ('unpacked', total, packet), ('roundtrip', total, [('', total, 0)]),
            ('relayout', total, [('.pfirst', width + 3, total - width - 3), ('.prest', total - width - 3, 0)]),
            ('rebuilt', total, packet), ('read', width + 5, element),
            *[(name, 3 * (width + 5), vector) for name in ('injected', 'written', 'reversed_writes', 'state')],
            ('captured', width + 5, element), ('fused', 3 * (width + 5), vector), ('onehot', total, packet),
            ('fill', 4*(width+5), [(f'[{i}]'+path, bits, i*(width+5)+low)
                                  for i in range(4) for path, bits, low in element]),
            ('gather', 4*width, [(f'[{i}]', width, i*width) for i in range(4)]),
            ('decoded', total, packet)]


def aggregate_stimuli(width):
    total, _, _, packet = aggregate_layout(width)
    rng = random.Random(914 + width)
    mask, elem_mask = (1 << total) - 1, (1 << (width + 5)) - 1
    initial = (1 << (3 * (width + 5) - 1)) + 0x12345
    state = initial
    captured = 0
    fused = initial
    rows, expected = [], []
    commands = [(0, 0, 0, 0)] + [(s, i, j, e) for s in range(4) for i in range(4)
                                 for j in range(4) for e in range(4)]
    commands += [tuple(rng.randrange(4) for _ in range(4)) for _ in range(128)]
    for n, (selector, i0, i1, enables) in enumerate(commands):
        tick, reset = int(n % 4 != 2), int(n == 0 or n % 37 == 0)
        value = [0, mask, 1 << (total - 1), rng.getrandbits(total)][n % 4]
        packed = rng.getrandbits(total)
        # Independently assembled leaf stimuli expose normalization and layout
        # mistakes; their native padding deliberately remains dirty.
        raw = []
        for _, bits, low in packet + [('', total, 0)]:
            v = ((packed if bits == total else value) >> low) & ((1 << bits) - 1)
            if n % 2:
                v |= ((1 << (((bits + 31) // 32) * 32)) - 1) ^ ((1 << bits) - 1)
            raw.extend(words(v, bits))
        rows.append(' '.join(f'{v:x}' for v in [tick, reset, selector, i0, i1, enables, *raw]))
        items = [(value >> (i * (width + 5))) & elem_mask for i in range(3)]
        replacement = (23 << width) | (items[2] & ((1 << width) - 1))
        active = [(index, datum) for port, (index, datum) in enumerate(((i0, replacement), (i1, items[0])))
                  if enables & (1 << port)]
        valid = all(index < 3 for index, _ in active) and len({index for index, _ in active}) == len(active)
        updates = dict(active) if valid else {}
        old = [(state >> (i * (width + 5))) & elem_mask for i in range(3)]
        if tick:
            captured = 0 if reset or selector >= 3 else old[selector]
            state = initial if reset else sum(updates.get(i, v) << (i * (width + 5)) for i, v in enumerate(old))
            if reset:
                fused = initial
            elif enables == 3:
                fused = sum(v << (i * (width + 5)) for i, v in enumerate(items))
            elif enables == 1 and selector < 3:
                previous = [(fused >> (i * (width + 5))) & elem_mask for i in range(3)]
                changed = list(previous)
                changed[selector] = replacement
                changed[0] = previous[2]
                fused = sum(v << (i * (width + 5)) for i, v in enumerate(changed))
        written = sum(updates.get(i, v) << (i * (width + 5)) for i, v in enumerate(items))
        injected = sum((replacement if i == selector else v) << (i * (width + 5)) for i, v in enumerate(items))
        rebuilt = ((value >> (total - 7)) << (total - 7)) | sum(v << (i * (width + 5)) for i, v in enumerate(reversed(items)))
        code = value & ((1 << (total - 1)) | (1 << (width - 1)) | 1)
        decoded = {0: mask, 1: initial}.get(code, (1 << (total - 1)) + 7)
        results = [value, value, packed, packed, value, rebuilt, items[selector] if selector < 3 else 0,
                   injected, written, written, state, captured, fused,
                   value if enables == 1 else packed if enables == 2 else None,
                   sum((items[selector] if selector < 3 else 0) << (i*(width+5)) for i in range(4)),
                   sum(((replacement+i) & ((1 << width)-1)) << (i*width) for i in range(4)), decoded]
        flat = []
        for v, (_, _, fields) in zip(results, aggregate_ports(width)):
            for _, bits, low in fields:
                flat.extend([None] * ((bits + 31) // 32) if v is None else words((v >> low) & ((1 << bits) - 1), bits))
        expected.append(flat)
    return '\n'.join(rows) + '\n', expected


def typed_driver(name, inputs, outputs, reference=False, clocked=False, controls=None):
    """Translate canonical packed SV ports and native typed leaves into word rows."""
    model = 'V' + name if reference else f'rsim_p{name}::Model'
    include = 'V' + name + '.h' if reference else name + '.hpp'
    read, show = [], []
    if controls is None:
        controls = dict(reset=1, selector=2, index0=2, index1=2, enables=2) if clocked else {}
    for control in controls:
        value = f'{control} & UINT64_C({(1 << controls[control]) - 1})' if reference else control
        read.append(f'dut.{control if reference else "inputs.p" + control.replace("_", "_u")} = {value};')
    for port, total, fields in inputs:
        base = f'dut.{port}' if reference else 'dut.inputs.p' + port.replace('_', '_u')
        if reference:
            read.append(f'for (unsigned j = 0; j < {(total+31)//32}; ++j) {base}[j] = 0;' if total > 64 else f'{base} = 0;')
        for path, bits, low in fields:
            target = base + path
            if not reference and bits <= 64:
                read.append(f'{target} = 0;')
            for i in range((bits + 31) // 32):
                read.append('std::cin >> word;')
                if reference:
                    assignment = f'{base}[bit/32] |= ((word >> k) & 1u) << (bit%32);' if total > 64 else f'{base} |= std::uint64_t((word >> k) & 1u) << bit;'
                    read.append(f'for (unsigned k = 0; k < {min(32, bits-32*i)}; ++k) {{ const unsigned bit = {low+32*i} + k; {assignment} }}')
                elif bits <= 64:
                    read.append(f'{target} |= std::uint64_t(word) << {32*i};')
                else:
                    read.append(f'{target}.words[{i}] = word;')
    for port, total, fields in outputs:
        base = f'dut.{port}' if reference else 'dut.outputs().p' + port.replace('_', '_u')
        for path, bits, low in fields:
            for i in range((bits + 31) // 32):
                if reference:
                    bit_expr = f'({base}[bit/32] >> (bit%32))' if total > 64 else f'(std::uint64_t({base}) >> bit)'
                    show.append(f'word = 0; for (unsigned k = 0; k < {min(32,bits-32*i)}; ++k) {{ const unsigned bit = {low+32*i} + k; word |= ({bit_expr} & 1u) << k; }} std::cout << word << " ";')
                else:
                    expr = f'{base}{path}.words[{i}]' if bits > 64 else f'std::uint32_t({base}{path} >> {32*i})'
                    show.append(f'std::cout << {expr} << " ";')
    return f'''// SPDX-License-Identifier: Apache-2.0
#include "{include}"
#include <cstdint>
#include <iostream>
int main() {{
  {model} dut;
  std::uint32_t word;
  std::uint64_t tick{''.join(', ' + name for name in controls)};
  std::cin >> std::hex; std::cout << std::hex;
  while (std::cin >> tick{''.join(' >> ' + name for name in controls)}) {{
    {'dut.clock = 0; dut.eval();' if reference and clocked else ''}
    {' '.join(read)}
    dut.eval(); dut.eval();
    {'if (tick) { ' + ('dut.clock = 1; dut.eval();' if reference else 'dut.tick();') + ' }' if clocked else ''}
    {' '.join(show)}
    std::cout << "\\n";
  }}
}}
'''


def run_aggregates(work, run, compare, differential):
    total_count = 0
    for width in (21, 65, 129, 512, None):
        if width is None:
            name = 'WideRelayout'
            inputs = [('value', 96, [(f'[{i}]', 32, i * 32) for i in range(3)])]
            outputs = [('result', 96, [('.phigh', 32, 64), ('.plow[0]', 32, 0), ('.plow[1]', 32, 32)])]
            patterns = [0, (1 << 96) - 1, *(1 << bit for bit in range(96))]
            vectors = '\n'.join('0 ' + ' '.join(f'{v:x}' for v in words(p, 96)) for p in patterns) + '\n'
            expected = [[p >> 64, p & 0xffffffff, (p >> 32) & 0xffffffff] for p in patterns]
        else:
            name = f'WideAggregate{width}'
            size, _, _, packet = aggregate_layout(width)
            inputs = [('value', size, packet), ('bits', size, [('', size, 0)])]
            outputs = aggregate_ports(width)
            vectors, expected = aggregate_stimuli(width)
        bench = work / f'{name}-bench.cpp'
        binary = work / name
        bench.write_text(typed_driver(name, inputs, outputs, clocked=width is not None))
        run(shlex.split(os.environ.get('CXX', 'c++')) +
            ['-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
             str(work / f'{name}.cpp'), str(bench), '-o', str(binary)], work, f'build-{name}')
        compare(run([str(binary)], work, name, vectors), expected, name)
        if differential:
            bench.write_text(typed_driver(name, inputs, outputs, reference=True, clocked=width is not None))
            run(['verilator', '--cc', '--exe', '--build', '-j', '2', '--top-module', name,
                 '--Mdir', str(work / f'obj{name}'), str(work / f'{name}.sv'), str(bench)], work, f'build-reference-{name}')
            compare(run([str(work / f'obj{name}/V{name}')], work, f'reference-{name}', vectors), expected, f'{name} direct SV')
        total_count += len(expected)
    return total_count


def run_constant_roms(work, run, compare, differential):
    """Check constant cast layouts, independent addresses and immutable ROMs."""
    total_count = 0
    for width in (5, 65):
        name = f'ConstantROM{width}'
        element = width + 5
        total = 3 * element
        entries = [((i + 3) << width) | (1 << (width - 1)) | (i + 1) for i in range(3)]
        packed = sum(value << (i * element) for i, value in enumerate(entries))
        inputs = [('data', width, [('', width, 0)])]
        outputs = [(f'{instance}_{port}', bits, [('', bits, 0)])
                   for instance in ('first', 'second')
                   for port, bits in (('selected', element), ('original', total), ('modified', total))]
        rows, expected = [], []
        for data in (0, (1 << width) - 1, 1 << (width - 1), 3, 0):
            for first in range(4):
                for second in range(4):
                    rows.append(' '.join(f'{v:x}' for v in (0, first, second, *words(data, width))))
                    values = []
                    for address in (first, second):
                        modified = sum((data if i == address else value) << (i * element)
                                       for i, value in enumerate(entries))
                        values += [entries[address] if address < 3 else 0, packed, modified]
                    expected.append([word for value, (_, bits, _) in zip(values, outputs)
                                     for word in words(value, bits)])
        vectors = '\n'.join(rows) + '\n'
        bench = work / f'{name}-bench.cpp'
        binary = work / name
        bench.write_text(typed_driver(name, inputs, outputs, controls=dict(first=2, second=2)))
        run(shlex.split(os.environ.get('CXX', 'c++')) +
            ['-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
             str(work / f'{name}.cpp'), str(bench), '-o', str(binary)], work, f'build-{name}')
        compare(run([str(binary)], work, name, vectors), expected, name)
        if differential:
            bench.write_text(typed_driver(name, inputs, outputs, reference=True, controls=dict(first=2, second=2)))
            run(['verilator', '--cc', '--exe', '--build', '-j', '2', '--top-module', name,
                 '--Mdir', str(work / f'obj{name}'), str(work / f'{name}.sv'), str(bench)], work, f'build-reference-{name}')
            compare(run([str(work / f'obj{name}/V{name}')], work, f'reference-{name}', vectors), expected, f'{name} direct SV')
        total_count += len(expected)
    return total_count


def run_memories(work, run, compare, differential):
    import memory
    import sync_memory
    configs = [(memory, w, record, None) for w in (65, 129, 512) for record in (False, True)]
    configs += [(sync_memory, w, record, granule) for w, record, granule in (
        (65, False, 1), (65, True, 3), (129, True, 1), (129, False, 129),
        (512, False, 8), (512, True, 7), (512, False, None), (512, False, 1), (64, True, 1))]
    total = 0
    for family, width, record, granule in configs:
        config = (3, record, granule) if family is sync_memory else (3, record)
        name = family.model_name(*config, width=width)
        raw_rows, raw_expected = family.stimuli([config], width=width)
        data_ports = ['a', 'b'] + (['mask_a', 'mask_b'] if family is sync_memory else [])
        controls = {p: (1 if p == "reset" else 2) if family is sync_memory else
                    (4 if p == "enables" else 1 if p in ("reset", "copy", "use_saved") else 2)
                    for p in family.INPUTS if p not in data_ports}
        mask_width = (2 * width + 5 if record else width) // granule if granule else 64
        inputs = [(p, bits, [('', bits, 0)]) for p in data_ports
                  for bits in [max(64, mask_width) if p.startswith('mask_') else width]]
        outputs = family.ports(3, record, width)
        rows, expected = [], []
        for index, row in enumerate(raw_rows.splitlines()):
            _, tick, *values = (int(v, 16) for v in row.split())
            raw = dict(zip(family.INPUTS, values))
            data = [tick, *(raw[p] for p in controls)]
            for p, bits, _ in inputs:
                value = raw[p]
                # Dirty padding reaches native inputs; the SV adapter copies
                # declared bits only, as required by Verilator's port ABI.
                if index % 2:
                    value |= ((1 << (((bits + 31) // 32) * 32)) - 1) ^ ((1 << bits) - 1)
                data.extend(words(value, bits))
            rows.append(' '.join(f'{v:x}' for v in data))
            flat = []
            fields = [bits for _, _, layout in outputs for _, bits, _ in layout]
            for value, bits in zip(raw_expected[index], fields):
                if value is None:
                    flat.extend([None] * ((bits + 31) // 32))
                elif isinstance(value, tuple):
                    v, known = value
                    known |= ((1 << (((bits + 31) // 32) * 32)) - 1) ^ ((1 << bits) - 1)
                    flat.extend(zip(words(v, bits), words(known, bits)))
                else:
                    flat.extend(words(value, bits))
            expected.append(flat)
        vectors = '\n'.join(rows) + '\n'
        bench = work / f'{name}-bench.cpp'
        binary = work / name
        bench.write_text(typed_driver(name, inputs, outputs, clocked=True, controls=controls))
        run(shlex.split(os.environ.get('CXX', 'c++')) +
            ['-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
             str(work / f'{name}.cpp'), str(bench), '-o', str(binary)], work, f'build-{name}')
        compare(run([str(binary)], work, name, vectors), expected, name)
        if differential:
            bench.write_text(typed_driver(name, inputs, outputs, reference=True, clocked=True, controls=controls))
            run(['verilator', '--cc', '--exe', '--build', '-j', '2', '--top-module', name,
                 '--Mdir', str(work / f'obj{name}'), str(work / f'{name}.sv'), str(bench)], work, f'build-reference-{name}')
            compare(run([str(work / f'obj{name}/V{name}')], work, f'reference-{name}', vectors), expected, f'{name} direct SV')
        total += len(expected)
    return total


def run_suite(work, run, differential):
    from run import compare
    work = work / 'wide'
    work.mkdir(exist_ok=True)
    run([str(ROOT / 'tools/run-racket.sh'), str(HERE / 'emit-wide.rhm'), str(work),
         'differential' if differential else 'native'], work, 'emit')
    # Include every model in one TU to verify shared support guards and namespaces.
    (work / 'all.cpp').write_text('\n'.join(f'#include "Wide{w}.hpp"' for w in WIDTHS))
    total = 0
    for width in WIDTHS:
        bench = work / f'bench{width}.cpp'
        bench.write_text(driver(width))
        binary = work / f'native{width}'
        run(shlex.split(os.environ.get('CXX', 'c++')) +
            ['-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
             str(work / f'Wide{width}.cpp'), str(work / 'all.cpp'), str(bench), '-o', str(binary)],
            work, f'build-native-{width}')
        vectors, expected = stimuli(width)
        compare(run([str(binary)], work, f'native-{width}', vectors), expected, f'wide rsim {width}')
        if differential:
            bench.write_text(driver(width, reference=True))
            run(['verilator', '--cc', '--exe', '--build', '-j', '2', '--top-module', f'Wide{width}',
                 '--Mdir', str(work / f'obj{width}'), str(work / f'Wide{width}.sv'), str(bench)],
                work, f'build-reference-{width}')
            compare(run([str(work / f'obj{width}/VWide{width}')], work, f'reference-{width}', vectors),
                    expected, f'wide direct SV {width}')
        total += len(expected)
    return (total + run_aggregates(work, run, compare, differential)
            + run_constant_roms(work, run, compare, differential)
            + run_memories(work, run, compare, differential))


if __name__ == '__main__':
    from run import run
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--differential', action='store_true')
    args = parser.parse_args()
    directory = Path(tempfile.mkdtemp(prefix='rhodium-rsim-wide-'))
    try:
        count = run_suite(directory, run, args.differential)
        print(f'Wide data/state: {count} observations passed' + (' on both backends' if args.differential else ' on rsim'))
    except BaseException:
        print(f'Wide artifacts and logs retained at {directory}')
        raise
    else:
        shutil.rmtree(directory)
