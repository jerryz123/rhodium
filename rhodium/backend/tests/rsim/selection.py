# SPDX-License-Identifier: Apache-2.0
"""Check partial selection and decode using packed integers and per-bit care masks."""
import os
import random
import shlex

WIDTHS = (1, 5, 63, 64)
INPUTS = ('reset', 'a', 'b', 'c', 'd', 'tag', 'code', 'sel1', 'sel3', 'sel64', 'guard')
NAMES = ('one', 'partial', 'free', 'forced', 'known', 'decoded', 'reordered', 'picked',
         'widepick', 'guarded', 'wide_decode', 'empty', 'catchall', 'sample_decoded',
         'sample_picked', 'sample_wide_decode', 'sample_partial')
AGGREGATES = {'decoded', 'reordered', 'picked', 'widepick', 'guarded', 'catchall',
              'sample_decoded', 'sample_picked'}
MASK64 = (1 << 64) - 1


def encoded(name):
    return 'p' + name.replace('_', '_u')


def ports(width):
    layout = [('.ptag', 5, 2*(width+3)), ('.pdata[0].plo', width, 3), ('.pdata[0].pmark', 3, 0),
              ('.pdata[1].plo', width, width+6), ('.pdata[1].pmark', 3, width+3)]
    return [(name, 2*width+11 if name in AGGREGATES else width,
             layout if name in AGGREGATES else [('', width, 0)]) for name in NAMES]


OUTPUT_COUNT = sum(len(fields) for _, _, fields in ports(64))


def packet(width, a, b, tag, mark0, mark1):
    mask = (1 << width) - 1
    return ((tag & 31) << (2*(width+3))) | ((((b & mask) << 3) | mark1) << (width+3)) | ((a & mask) << 3) | mark0


def decode(selector, cases, default):
    for value, care, out, out_care in cases:
        if selector & care == value & care:
            return out, out_care
    return default


def combinational(width, raw):
    full = (1 << width) - 1
    bits = 2*width+11
    packet_full = (1 << bits) - 1
    a, b, c, d = (raw[name] & full for name in ('a', 'b', 'c', 'd'))
    tag, code, sel3 = raw['tag'] & 31, raw['code'] & 31, raw['sel3'] & 7
    packets = [packet(width, a, b, tag, 1, 2), packet(width, b+1, ~a, ~tag, 3, 4),
               packet(width, c, d, 17, 5, 6)]
    low = (1 << (width+3))-1
    rows = [(0, 3, packet_full, packet_full ^ low),
            (1, 3, (1 << (width+2))+5, packet_full),
            (2, 7, packet_full ^ (1 << (width+3)), low | (1 << (bits-1)))]
    decoded = decode(code, rows, ((1 << (bits-1))+9, packet_full ^ (1 << (bits-2))))
    tag_bit = 1 << (2*(width+3))
    input_care = tag_bit | (1 << (2*width+5)) | 8
    wide = decode(packets[0], [(0, input_care, full, full),
                              (tag_bit, input_care, full-1, full-1),
                              (8, input_care, 1, full)], (0, full))
    unknown = (0, 0)
    picked = (packets[sel3.bit_length()-1], packet_full) if sel3 in (1, 2, 4) else unknown
    sel64 = raw['sel64']
    widepick = (packets[(sel64.bit_length()-1) % 3], packet_full) if sel64 and sel64 & (sel64-1) == 0 else unknown
    return dict(one=(a, full) if raw['sel1'] & 1 else unknown,
                partial=(a, full) if sel3 == 1 else ((b, full) if sel3 == 4 else unknown),
                free=unknown, forced=(full, full), known=(a, full), decoded=decoded,
                reordered=decoded, picked=picked, widepick=widepick,
                guarded=widepick if raw['guard'] & 1 else (packets[2], packet_full),
                wide_decode=wide, empty=(full, full-1), catchall=(17, packet_full))


def stimuli():
    rng = random.Random(4019)
    rows, expected = [], []
    for config, width in enumerate(WIDTHS):
        commands = []

        def add(tick=1, **changes):
            controls = {name: rng.getrandbits(64) for name in INPUTS}
            controls.update(reset=0)
            controls.update(changes)
            commands.append((tick, controls))

        add(tick=0)
        add(reset=1)
        # Exhaust the small decoder and three-choice one-hot selector, including
        # partial inputs on unselected branches. Each edge has an eval-only probe.
        for code in range(32):
            for sel3 in range(8):
                add(code=code, sel3=sel3, sel1=1, sel64=1 << ((code+sel3) % 64),
                    reset=int(code % 11 == 0), guard=sel3 & 1)
                add(tick=0, code=31-code, sel3=sel3 ^ 7, sel1=0, sel64=0, guard=0)
        # Address the highest selector bit and every choice; no invalid selector
        # receives a promised result, but its guarded-away value is still checked.
        for index in range(64):
            add(sel64=1 << index, guard=1)
            add(sel64=(1 << index) | (1 << ((index+1) % 64)), guard=0)
        # Exhaust the cared bits of the aggregate-input decoder. Uncared fields
        # vary independently, including across the 64-bit carrier boundaries.
        for tag_bit in range(2):
            for a_low in range(2):
                for b_high in range(2):
                    for _ in range(8):
                        a = (rng.getrandbits(64) & ~1) | a_low
                        b = (rng.getrandbits(64) & ~(1 << (width-1))) | (b_high << (width-1))
                        tag = (rng.getrandbits(64) & ~1) | tag_bit
                        add(a=a, b=b, tag=tag)
        sampled = {f'sample_{name}': (0, 0) for name in ('decoded', 'picked', 'wide_decode', 'partial')}
        for tick, raw in commands:
            observed = combinational(width, raw)
            if tick:
                for name in sampled:
                    bits = 2*width+11 if name in AGGREGATES else width
                    sampled[name] = (0, (1 << bits)-1) if raw['reset'] & 1 else observed[name.removeprefix('sample_')]
            observed.update(sampled)
            flat = []
            for name, _, fields in ports(width):
                value, care = observed[name]
                flat.extend(((value >> low) & ((1 << count)-1), (care >> low) & ((1 << count)-1))
                            for _, count, low in fields)
            expected.append(flat)
            rows.append([config, tick, *(raw[name] for name in INPUTS)])
    return '\n'.join(' '.join(f'{value:x}' for value in row) for row in rows) + '\n', expected


def native_driver():
    includes, cases = [], []
    for config, width in enumerate(WIDTHS):
        includes.append(f'#include "Selection{width}.hpp"')
        assignments = ' '.join(f'dut.inputs.{encoded(port)} = {port};' for port in INPUTS)
        outputs = " << ' ' << ".join(f'dut.outputs().{encoded(port)}{path}'
                                    for port, _, fields in ports(width) for path, _, _ in fields)
        cases.append(f'''case {config}: {{
  static rsim_pSelection{width}::Model dut;
  {assignments}
  if (tick) dut.tick(); else dut.eval();
  std::cout << {outputs} << '\\n'; break;
}}''')
    headers, branches = '\n'.join(includes), '\n'.join(cases)
    return f'''// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <iostream>
{headers}
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
    for config, width in enumerate(WIDTHS):
        sizes = {name: width for name in ('a', 'b', 'c', 'd')}
        sizes.update(reset=1, tag=5, code=5, sel1=1, sel3=3, sel64=64, guard=1)
        connections = [f'.{port}({port}[{sizes[port]-1}:0])' for port in INPUTS]
        assignments = []
        for port, bits, fields in ports(width):
            net = f'c{config}_{port}'
            lines.append(f'wire [{bits-1}:0] {net};')
            connections.append(f'.{port}({net})')
            for _, count, low in fields:
                slot = len(assignments)
                assignments.append(f'observed[{slot*64} +: 64] = 64\'({net}[{low} +: {count}]);')
        lines.append(f'Selection{width} c{config}(.clock(clock && config_id == 8\'d{config}), {", ".join(connections)});')
        cases.append(f'8\'d{config}: begin {" ".join(assignments)} end')
    declarations, branches = '\n'.join(lines), '\n'.join(cases)
    return f'''// SPDX-License-Identifier: Apache-2.0
module SelectionReference(input logic clock, input logic [7:0] config_id,
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
#include "VSelectionReference.h"
#include <iostream>
#include <cstdint>
int main() {{
  VSelectionReference dut;
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
    source = work / 'selection-main.cpp'
    source.write_text(native_driver())
    run(shlex.split(os.environ.get('CXX', 'c++')) +
        ['-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
         '-fno-sanitize-recover=all', *map(str, sorted(work.glob('Selection*.cpp'))), str(source),
         '-o', str(work / 'selection')], work, 'selection-build')
    vectors, expected = stimuli()
    compare(run([str(work / 'selection')], work, 'selection-run', vectors), expected, 'rsim selection')
    if differential:
        (work / 'SelectionReference.sv').write_text(reference_top())
        driver = work / 'selection-reference.cpp'
        driver.write_text(reference_driver())
        run(['verilator', '--cc', '--exe', '--build', '-j', '2', '--top-module', 'SelectionReference',
             '--Mdir', str(work / 'selection-obj'), *map(str, sorted(work.glob('Selection*.sv'))),
             str(driver)], work, 'selection-verilator')
        compare(run([str(work / 'selection-obj/VSelectionReference')], work, 'selection-reference', vectors),
                expected, 'Verilator selection')
    return len(expected)
