# SPDX-License-Identifier: Apache-2.0
"""Track per-bit memory definedness, typed masks, and stored read timing."""
import itertools
import os
import random
import shlex

CONFIGS = [(depth, record, granule) for depth in (1, 3, 4) for record in (False, True)
           for granule in ((None, 7, 19, 133) if record else (None, 1, 8, 64))]
ADDRESSES = ('read_address', 'write_address', 'shared_address')
CONTROLS = ('read_enable', 'write_enable', 'shared_enable', 'write_mode', 'copy', 'feedback', 'mask_from_read')
INPUTS = ('reset', *ADDRESSES, *CONTROLS, 'a', 'b', 'mask_a', 'mask_b')


def model_name(depth, record, granule, width=64):
    suffix = "" if granule is None else f"Mask{granule}"
    return f'SyncMemory{"Record" if record else "Scalar"}{depth}{suffix}' + (f'W{width}' if width != 64 else '')


def ports(depth, record, width=64):
    bits = 2 * width + 5 if record else width
    fields = [('.ptag', 5, 2 * width), ('.planes[0]', width, 0), ('.planes[1]', width, width)] if record else [('', width, 0)]
    names = ('separate_result', 'separate_sample', 'shared_result', 'shared_sample', 'readonly_result', 'readonly_sample')
    return [(f'side{side}_{name}', size, layout)
            for side in range(2)
            for name, size, layout in [(name, bits, fields) for name in names] + [('masked', width, [('', width, 0)])]]


OUTPUT_COUNT = sum(len(fields) for _, _, fields in ports(4, True))


def encoded(name):
    return 'p' + name.replace('_', '_u')


def packet(a, b, record, width=64):
    return ((a & 31) << (2 * width)) | (b << width) | a if record else a


def expanded_mask(value, bits, granule):
    """Use a packed Python integer oracle independent of C++ leaf traversal."""
    return sum(((1 << granule) - 1) << low
               for low in range(0, bits, granule) if value & (1 << (low // granule)))


def merge(old, data, write_mask, bits, granule):
    if granule is None:
        return data
    selected = expanded_mask(write_mask[0], bits, granule)
    mask_known = expanded_mask(write_mask[1], bits, granule)
    # Unknown mask granules are conservatively unknown, even if a particular
    # backend happens to resolve the mask to zero or preserve equal old bits.
    return ((old[0] & ~selected) | (data[0] & selected),
            ((old[1] & ~selected) | (data[1] & selected)) & mask_known)


def stimuli(configs=CONFIGS, width=64):
    data_full = (1 << width) - 1
    rng = random.Random(1703)
    rows, expected = [], []
    for config, (depth, record, granule) in enumerate(configs):
        encodings = 2 if depth == 1 else 4
        bits = 2 * width + 5 if record else width
        full = (1 << bits) - 1
        mask_width = bits // granule if granule else 64
        mask_full = (1 << mask_width) - 1
        commands = []

        def add(tick=1, **changes):
            controls = dict.fromkeys(INPUTS, 0)
            controls.update(a=rng.getrandbits(width), b=rng.getrandbits(width), mask_a=mask_full, mask_b=mask_full)
            controls.update(changes)
            commands.append((tick, controls))

        add(tick=0)
        if granule:
            # Partially initialize every granule and inspect after each update.
            # This catches dropping defined bits in otherwise unknown words,
            # mask ordering, scalar bit 63, and cross-field aggregate granules.
            for index in range(mask_width):
                add(reset=int(index % 3 == 0), write_enable=3, shared_enable=3,
                    write_mode=3, mask_a=1 << index,
                    mask_b=1 << (mask_width - 1 - index))
                add(read_enable=3, shared_enable=3, mask_a=0, mask_b=0)
                add(tick=0, write_enable=3, shared_enable=3, write_mode=3,
                    mask_a=mask_full, mask_b=mask_full)
                add(read_enable=3, shared_enable=3)
            # Zero-mask writes cannot alter initialized storage, even when reset
            # is active or write data is an unspecified preceding read result.
            add(reset=1, write_enable=3, shared_enable=3, write_mode=3,
                mask_a=0, mask_b=0)
            add(write_enable=3, shared_enable=3, write_mode=3, copy=3,
                mask_a=0, mask_b=0)
            add(read_enable=3, shared_enable=3)
            add(read_enable=3, shared_enable=3)
        # Initialize both writable memories explicitly. Reset only clears the
        # consuming registers; it must not suppress any memory writes.
        for address in range(depth):
            add(reset=1, write_address=address, shared_address=address,
                write_enable=3, shared_enable=3, write_mode=3,
                a=(address + 1) % depth, b=(address + 2) % depth)
        if granule:
            # Alternate disabled writes and shared read mode with partial writes.
            # Each re-enabled merge must preserve the current word's unselected
            # granules, never reuse a preceding frame's pending data.
            for index in range(4):
                add(write_enable=0, shared_enable=0, write_mode=3,
                    a=data_full, b=data_full)
                add(read_enable=3, shared_enable=3, write_mode=0,
                    a=0, b=0)
                add(write_enable=3, shared_enable=3, write_mode=3,
                    mask_a=1 << (index % mask_width),
                    mask_b=1 << ((mask_width - 1 - index) % mask_width))
                add(read_enable=3, shared_enable=3)
        for address in range(depth):
            add(read_address=address, shared_address=address, read_enable=3, shared_enable=3)
            # Changing controls, including reset, between edges cannot advance
            # either the stored read or its consuming register.
            add(tick=0, reset=1, read_address=(address + 1) % depth,
                shared_address=(address + 1) % depth, read_enable=0,
                shared_enable=3, write_mode=3, write_enable=3)
            add(tick=0, read_address=address, shared_address=address)
        # Consume the previous result as an address; the feedback must not
        # become a combinational cycle or use the newly read word.
        for _ in range(depth + 2):
            add(read_enable=3, shared_enable=3, feedback=3)
        # Copy old read results while a different separate-port read is sampled.
        add(read_enable=3, shared_enable=3)
        add(read_address=(1 % depth), write_address=(2 % depth),
            shared_address=(1 % depth), read_enable=3, write_enable=3,
            shared_enable=3, write_mode=3, copy=3)
        for address in range(depth):
            add(read_address=address, shared_address=address, read_enable=3, shared_enable=3)
        if granule:
            # Distinct old read values supply both write data and masks. Sample
            # another read on the same edge, then observe the masked write.
            add(read_enable=3, shared_enable=3)
            add(read_address=(1 % depth), write_address=(2 % depth),
                shared_address=(1 % depth), read_enable=3, write_enable=3,
                shared_enable=3, write_mode=3, mask_from_read=3)
            for address in range(depth):
                add(read_address=address, shared_address=address, read_enable=3, shared_enable=3)
        # Reset during enabled reads must not clear memory or the read results.
        add(reset=1, read_enable=3, shared_enable=3)
        add(read_enable=3, shared_enable=3)
        # All side-local enable/mode combinations, with and without an edge.
        # Include disabled and enabled invalid reads, but guard writes here so
        # known contents survive long enough to validate later reads.
        for ren, sen, mode, address in itertools.product(range(4), range(4), range(4), range(encodings)):
            changes = dict(read_enable=ren, shared_enable=sen,
                           write_mode=mode, read_address=address,
                           shared_address=address % depth)
            add(**changes)
            add(tick=0, **dict(changes, read_address=(address + 1) % encodings, write_mode=mode ^ 3))
        for index in range(250):
            add(tick=rng.randrange(2), reset=int(index % 19 == 0),
                read_address=rng.randrange(encodings), write_address=rng.randrange(depth),
                shared_address=rng.randrange(depth), read_enable=rng.randrange(4),
                write_enable=rng.randrange(4), shared_enable=rng.randrange(4),
                write_mode=rng.randrange(4), copy=3 if index % 7 == 0 else 0,
                feedback=3 if index % 9 == 0 else 0,
                mask_a=rng.getrandbits(mask_width), mask_b=rng.getrandbits(mask_width),
                mask_from_read=3 if index % 11 == 0 else 0)
        # Host inputs wider than their hardware type must be normalized before
        # use, including addresses and two-bit per-instance controls.
        for _ in range(40):
            changes = {name: rng.getrandbits(width if name in ("a", "b") else max(64, mask_width) if name in ("mask_a", "mask_b") else 64) for name in INPUTS}
            for name in ('write_address', 'shared_address'):
                changes[name] = (changes[name] & ~(encodings - 1)) | rng.randrange(depth)
            add(tick=rng.randrange(2), **changes)
        # Enabled invalid writes are deliberately unspecified; exercise native
        # guards and forget affected contents, without imposing a value oracle.
        add(write_address=encodings-1, shared_address=encodings-1,
            write_enable=3, shared_enable=3, write_mode=3)
        add(read_enable=3, shared_enable=3)
        unknown = (0, 0)
        words = [[[unknown] * depth for _ in range(2)] for _ in range(2)]
        results = [[unknown] * 3 for _ in range(2)]
        samples = [[unknown] * 3 for _ in range(2)]
        zero = (0, full)
        for tick, raw in commands:
            control = dict(raw)
            control['reset'] &= 1
            for name in ADDRESSES:
                control[name] &= encodings - 1
            for name in CONTROLS:
                control[name] &= 3
            if tick:
                for side in range(2):
                    ren, wen, sen, mode, copy, feedback, mask_from_read = ((control[name] >> side) & 1 for name in CONTROLS)
                    old_separate, old_shared, _ = results[side]
                    samples[side] = [zero] * 3 if control['reset'] else results[side][:]
                    address = control['read_address']
                    if feedback:
                        address = old_separate[0] & (encodings - 1) if old_separate[1] & (encodings - 1) == encodings - 1 else None
                    wa, sa = control['write_address'], control['shared_address']
                    separate, shared = words[side]
                    external = (packet(raw['b'] & data_full, raw['a'] & data_full, record, width) if side else packet(raw['a'] & data_full, raw['b'] & data_full, record, width), full)
                    mask_input = (raw['mask_b' if side else 'mask_a'] & mask_full, mask_full)
                    separate_mask = old_separate if mask_from_read else mask_input
                    shared_mask = old_shared if mask_from_read else mask_input
                    separate_data = old_separate if copy else external
                    shared_data = old_shared if copy else external
                    # Read result and downstream sample are distinct old-state
                    # transactions. Never compare a disabled/colliding read.
                    result = separate[address] if ren and address is not None and address < depth and not (wen and address == wa) else unknown
                    shared_result = shared[sa] if sen and not mode and sa < depth else unknown
                    results[side] = [result, shared_result, unknown]
                    if wen:
                        if wa < depth:
                            separate[wa] = merge(separate[wa], separate_data, separate_mask, bits, granule)
                        else:
                            words[side][0] = [unknown] * depth
                    if sen and mode:
                        if sa < depth:
                            shared[sa] = merge(shared[sa], shared_data, shared_mask, bits, granule)
                        else:
                            words[side][1] = [unknown] * depth
            observed = [value for side in range(2)
                        for value in [value for pair in zip(results[side], samples[side]) for value in pair] + [(0, data_full)]]
            flat = []
            for (_, _, fields), (value, known) in zip(ports(depth, record, width), observed):
                for _, leaf_width, low in fields:
                    mask = (1 << leaf_width) - 1
                    flat.append(((value >> low) & mask, (known >> low) & mask))
            expected.append(flat + [0] * (OUTPUT_COUNT - len(flat)))
            rows.append([config, tick, *(raw[name] for name in INPUTS)])
    return '\n'.join(' '.join(f'{value:x}' for value in row) for row in rows) + '\n', expected


def native_driver():
    includes, cases = [], []
    for config, (depth, record, granule) in enumerate(CONFIGS):
        name = model_name(depth, record, granule)
        includes.append(f'#include "{name}.hpp"')
        assignments = ' '.join(f'dut.inputs.{encoded(port)} = {port};' for port in INPUTS)
        values = [f'std::uint64_t(dut.outputs().{encoded(port)}{path})' for port, _, fields in ports(depth, record) for path, _, _ in fields]
        values += ['UINT64_C(0)'] * (OUTPUT_COUNT - len(values))
        output = " << ' ' << ".join(values)
        cases.append(f'''case {config}: {{
  static rsim_p{name}::Model dut;
  {assignments}
  if (tick) dut.tick(); else dut.eval();
  std::cout << {output} << '\\n';
  break;
}}''')
    header, branches = '\n'.join(includes), '\n'.join(cases)
    return f'''// SPDX-License-Identifier: Apache-2.0
#include <iostream>
#include <cstdint>
{header}
int main() {{
  std::uint64_t config, tick, {', '.join(INPUTS)};
  std::cin >> std::hex; std::cout << std::hex;
  while (std::cin >> config >> tick >> {' >> '.join(INPUTS)}) {{
    switch (config) {{ {branches} default: return 2; }}
  }}
}}
'''


def reference_top():
    lines, cases = [], []
    for config, (depth, record, granule) in enumerate(CONFIGS):
        width = 1 if depth == 1 else 2
        connections = [f'.{port}({port}[{width-1}:0])' for port in ADDRESSES]
        connections += [f'.{port}({port}[{bits-1}:0])' for port, bits in [('reset', 1), *((name, 2) for name in CONTROLS), ('a', 64), ('b', 64), ('mask_a', 64), ('mask_b', 64)]]
        assignments = []
        for port, bits, fields in ports(depth, record):
            net = f'c{config}_{port}'
            lines.append(f'wire [{bits-1}:0] {net};')
            connections.append(f'.{port}({net})')
            for _, count, low in fields:
                slot = len(assignments)
                assignments.append(f'observed[{slot*64} +: 64] = 64\'({net}[{low} +: {count}]);')
        lines.append(f'{model_name(depth, record, granule)} c{config}(.clock(clock && config_id == 8\'d{config}), {", ".join(connections)});')
        cases.append(f'8\'d{config}: begin {" ".join(assignments)} end')
    declarations, branches = '\n'.join(lines), '\n'.join(cases)
    return f'''// SPDX-License-Identifier: Apache-2.0
module SyncMemoryReference(input logic clock, input logic [7:0] config_id,
                       input logic [63:0] {', '.join(INPUTS)}, output logic [{OUTPUT_COUNT*64-1}:0] observed);
{declarations}
always_comb begin
  observed = 0;
  case (config_id)
    {branches}
    default: begin end
  endcase
end
endmodule
'''


def reference_driver():
    assignments = ' '.join(f'dut.{port} = {port};' for port in INPUTS)
    return f'''// SPDX-License-Identifier: Apache-2.0
#include "VSyncMemoryReference.h"
#include <iostream>
#include <cstdint>
int main() {{
  VSyncMemoryReference dut;
  std::uint64_t config, tick, {', '.join(INPUTS)};
  std::cin >> std::hex; std::cout << std::hex;
  while (std::cin >> config >> tick >> {' >> '.join(INPUTS)}) {{
    dut.clock = 0; dut.eval();
    dut.config_id = config; {assignments} dut.eval();
    if (tick) {{ dut.clock = 1; dut.eval(); }}
    for (unsigned i = 0; i < {OUTPUT_COUNT}; ++i) {{
      if (i) std::cout << ' ';
      std::cout << (std::uint64_t(dut.observed[2*i]) | (std::uint64_t(dut.observed[2*i+1]) << 32));
    }}
    std::cout << '\\n';
  }}
}}
'''


def run_suite(work, run, compare, differential):
    source = work / 'sync-memory-main.cpp'
    source.write_text(native_driver())
    run(shlex.split(os.environ.get('CXX', 'c++')) +
        ['-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
         '-fno-sanitize-recover=all', *map(str, sorted(work.glob('SyncMemory*.cpp'))), str(source),
         '-o', str(work / 'sync-memory')], work, 'sync-memory-build')
    vectors, expected = stimuli()
    compare(run([str(work / 'sync-memory')], work, 'sync-memory-run', vectors), expected, 'rsim synchronous memory')
    if differential:
        (work / 'SyncMemoryReference.sv').write_text(reference_top())
        driver = work / 'sync-memory-reference.cpp'
        driver.write_text(reference_driver())
        run(['verilator', '--cc', '--exe', '--build', '-j', '2', '--top-module', 'SyncMemoryReference',
             '--Mdir', str(work / 'sync-memory-obj'), *map(str, sorted(work.glob('SyncMemory*.sv'))),
             str(driver)], work, 'sync-memory-verilator')
        compare(run([str(work / 'sync-memory-obj/VSyncMemoryReference')], work, 'sync-memory-reference', vectors),
                expected, 'Verilator synchronous memory')
    return len(expected)
