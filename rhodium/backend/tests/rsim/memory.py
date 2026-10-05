# SPDX-License-Identifier: Apache-2.0
"""Track memory definedness and simultaneous pre-edge sampling across repeated instances."""
import itertools
import os
import random
import shlex

CONFIGS = [(depth, record) for depth in (1, 3, 4) for record in (False, True)]
INPUTS = ('reset', 'read0', 'read1', 'address0', 'address1', 'enables', 'copy', 'use_saved', 'a', 'b')


def model_name(depth, record, width=64):
    return f'Memory{"Record" if record else "Scalar"}{depth}' + (f'W{width}' if width != 64 else '')


def ports(depth, record, width=64):
    bits = 2 * width + 5 if record else width
    fields = [('.ptag', 5, 2 * width), ('.planes[0]', width, 0), ('.planes[1]', width, width)] if record else [('', width, 0)]
    address_width = 1 if depth == 1 else 2
    return [(f'side{side}_{name}', size, layout)
            for side in range(2)
            for name, size, layout in [('out0', bits, fields), ('out1', bits, fields), ('sample', bits, fields),
                                       ('address', address_width, [('', address_width, 0)]), ('masked', width, [('', width, 0)])]]


OUTPUT_COUNT = sum(len(fields) for _, _, fields in ports(4, True))


def encoded(name):
    return 'p' + name.replace('_', '_u')


def packet(a, b, record):
    return (a & 31, a, b) if record else (a,)


def stimuli(configs=CONFIGS, width=64):
    data_full = (1 << width) - 1
    rng = random.Random(703)
    rows, expected = [], []
    for config, (depth, record) in enumerate(configs):
        encodings = 2 if depth == 1 else 4
        # Command: tick, reset, read0, read1, address0, address1, enables,
        # copy, use_saved, a, b, permit_invalid_writes.
        commands = [(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, False)]
        for address in range(depth):
            data = (1 + address, data_full - address)
            controls = (1, address, (address + 1) % depth, address, 0, 5, 0, 0, *data, False)
            commands += [(0, *controls), (1, *controls), (0, *controls)]
        if depth > 1:
            commands += [(1, 0, 0, 1, 0, 1, 15, 1, 0, 0, 0, False),  # simultaneous swap from old reads
                         (0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, False)]
        # Store the last address, then change/reset that register while writing
        # through its old value. Reset must neither clear memory nor gate writes.
        commands += [(1, 0, 0, 0, depth - 1, 0, 0, 0, 0, 0, 0, False),
                     (1, 1, depth - 1, 0, 0, 0, 5, 0, 1, 71, 93, False),
                     (0, 0, depth - 1, 0, 0, 0, 0, 0, 0, 0, 0, False)]
        for read, address0, address1, enables in itertools.product(range(encodings), range(encodings), range(encodings), range(16)):
            commands.append((rng.randrange(2), int(rng.randrange(19) == 0), read, (read + 1) % encodings,
                             address0, address1, enables, rng.randrange(2), rng.randrange(2),
                             rng.getrandbits(width), rng.getrandbits(width), False))
        commands += [(rng.randrange(2), int(rng.randrange(17) == 0), *(rng.getrandbits(64) for _ in range(6)),
                      rng.randrange(2), rng.getrandbits(width), rng.getrandbits(width), False) for _ in range(80)]
        # Exercise guarded enabled-invalid addresses and collisions under native
        # sanitizers. Conservatively discard all knowledge of the affected memory.
        commands += [(1, 0, 0, 0, encodings - 1, 0, 5, 0, 0, 11, 17, True),
                     (1, 0, 0, 0, 0, 0, 15, 0, 0, 23, 31, True),
                     (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, False)]
        words = [[None] * depth for _ in range(2)]
        sampled, saved = [None, None], [None, None]
        zero = (0, 0, 0) if record else (0,)
        for tick, reset, raw_r0, raw_r1, raw_a0, raw_a1, raw_en, raw_copy, raw_saved, a, b, permit_invalid in commands:
            r0, r1, a0, a1 = (value & (encodings - 1) for value in (raw_r0, raw_r1, raw_a0, raw_a1))
            copy, use_saved = raw_copy & 1, raw_saved & 1
            enables = raw_en & 15
            if not permit_invalid:
                for side in range(2):
                    target0 = saved[side] if use_saved else a0
                    if target0 is None or target0 >= depth:
                        enables &= ~(1 << (2*side))
                    if a1 >= depth or (enables & (1 << (2*side)) and target0 == a1):
                        enables &= ~(1 << (2*side + 1))
            for side in range(2):
                old = words[side]
                read0, read1 = (old[index] if index < depth else None for index in (r0, r1))
                target0 = saved[side] if use_saved else a0
                addresses = (target0, a1)
                values = (read1, read0) if copy else (packet(a, b, record), packet(b, a, record))[::(-1 if side else 1)]
                active = [port for port in range(2) if enables & (1 << (side*2 + port))]
                destinations = [addresses[port] for port in active]
                if tick:
                    sampled[side] = zero if reset else read0
                    saved[side] = 0 if reset else a0
                    if any(index is None or index >= depth for index in destinations) or len(set(destinations)) != len(destinations):
                        words[side] = [None] * depth
                    else:
                        updates = {addresses[port]: values[port] for port in active}
                        words[side] = [updates.get(index, value) for index, value in enumerate(old)]
            flat = []
            for side in range(2):
                observed = [words[side][index] if index < depth else None for index in (r0, r1)] + [sampled[side]]
                for value in observed:
                    flat += [None] * len(zero) if value is None else list(value)
                flat += [saved[side], 0]
            expected.append(flat + [0] * (OUTPUT_COUNT - len(flat)))
            rows.append([config, tick, reset, raw_r0, raw_r1, raw_a0, raw_a1, (raw_en & ~15) | enables, raw_copy, raw_saved, a, b])
    return '\n'.join(' '.join(f'{value:x}' for value in row) for row in rows) + '\n', expected


def native_driver():
    includes, cases = [], []
    for config, (depth, record) in enumerate(CONFIGS):
        name = model_name(depth, record)
        includes.append(f'#include "{name}.hpp"')
        assignments = ' '.join(f'dut.inputs.{encoded(port)} = {port};' for port in INPUTS)
        values = [f'dut.outputs().{encoded(port)}{path}' for port, _, fields in ports(depth, record) for path, _, _ in fields]
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
    for config, (depth, record) in enumerate(CONFIGS):
        width = 1 if depth == 1 else 2
        connections = [f'.{port}({port}[{width-1}:0])' for port in ('read0', 'read1', 'address0', 'address1')]
        connections += [f'.{port}({port}[{bits-1}:0])' for port, bits in [('reset', 1), ('enables', 4), ('copy', 1), ('use_saved', 1), ('a', 64), ('b', 64)]]
        assignments = []
        for port, bits, fields in ports(depth, record):
            net = f'c{config}_{port}'
            lines.append(f'wire [{bits-1}:0] {net};')
            connections.append(f'.{port}({net})')
            for _, count, low in fields:
                slot = len(assignments)
                assignments.append(f'observed[{slot*64} +: 64] = 64\'({net}[{low} +: {count}]);')
        lines.append(f'{model_name(depth, record)} c{config}(.clock(clock && config_id == 8\'d{config}), {", ".join(connections)});')
        cases.append(f'8\'d{config}: begin {" ".join(assignments)} end')
    declarations, branches = '\n'.join(lines), '\n'.join(cases)
    return f'''// SPDX-License-Identifier: Apache-2.0
module MemoryReference(input logic clock, input logic [7:0] config_id,
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
#include "VMemoryReference.h"
#include <iostream>
#include <cstdint>
int main() {{
  VMemoryReference dut;
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
    source = work / 'memory-main.cpp'
    source.write_text(native_driver())
    run(shlex.split(os.environ.get('CXX', 'c++')) +
        ['-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
         '-fno-sanitize-recover=all', *map(str, sorted(work.glob('Memory*.cpp'))), str(source),
         '-o', str(work / 'memory')], work, 'memory-build')
    vectors, expected = stimuli()
    compare(run([str(work / 'memory')], work, 'memory-run', vectors), expected, 'rsim memory')
    if differential:
        (work / 'MemoryReference.sv').write_text(reference_top())
        driver = work / 'memory-reference.cpp'
        driver.write_text(reference_driver())
        run(['verilator', '--cc', '--exe', '--build', '-j', '2', '--top-module', 'MemoryReference',
             '--Mdir', str(work / 'memory-obj'), *map(str, sorted(work.glob('Memory*.sv'))),
             str(driver)], work, 'memory-verilator')
        compare(run([str(work / 'memory-obj/VMemoryReference')], work, 'memory-reference', vectors),
                expected, 'Verilator memory')
    return len(expected)
