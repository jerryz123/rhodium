# SPDX-License-Identifier: Apache-2.0
"""Exercise dynamic vector operations with masked partial results and independent state expectations."""
import itertools
import os
import random
import shlex

CONFIGS = [(length, record) for length in (1, 3, 4) for record in (False, True)]
MASK64 = (1 << 64) - 1


def model_name(length, record):
    return f'Dynamic{"Record" if record else "Scalar"}{length}'


def element_fields(record):
    return [('.ptag', 5, 128), ('.planes[0]', 64, 0), ('.planes[1]', 64, 64)] if record else [('', 64, 0)]


def ports(length, record):
    bits = 133 if record else 64
    element = element_fields(record)
    vector = [(f'[{index}]' + path, width, index * bits + low)
              for index in range(length) for path, width, low in element]
    return [('read', bits, element), ('injected', length*bits, vector),
            ('written', length*bits, vector), ('reversed', length*bits, vector),
            ('state', length*bits, vector), ('captured', bits, element),
            ('expanded', length*bits, vector), ('expandedlane', bits, element),
            ('updatedstate', length*bits, vector)]


OUTPUT_COUNT = sum(len(fields) for _, _, fields in ports(4, True))


def element(a, b, seed, record):
    data = (a + seed) & MASK64
    return (data & 31, data, b ^ seed) if record else (data,)


def stimuli():
    rng = random.Random(439)
    rows, expected = [], []
    for config, (length, record) in enumerate(CONFIGS):
        encodings = 2 if length == 1 else 4
        # Cover every selector encoding, both write indices, and enable mask.
        commands = [(1, 1, 0, 0, 0, 0, 0, 0)]
        for selector, i0, i1, enables in itertools.product(range(encodings), range(encodings), range(encodings), range(4)):
            commands.append((rng.randrange(2), 0, selector, i0, i1, enables,
                             rng.choice((0, 1, MASK64, 1 << 63)), rng.getrandbits(64)))
        commands += [(rng.randrange(2), int(rng.randrange(17) == 0), rng.getrandbits(64),
                      rng.getrandbits(64), rng.getrandbits(64), rng.getrandbits(64),
                      rng.getrandbits(64), rng.getrandbits(64)) for _ in range(100)]
        zero = (0, 0, 0) if record else (0,)
        state, captured, updated_state = [zero] * length, zero, [zero] * length
        for tick, reset, raw_selector, raw_i0, raw_i1, raw_enables, a, b in commands:
            selector, i0, i1 = (x & (encodings - 1) for x in (raw_selector, raw_i0, raw_i1))
            enables = raw_enables & 3
            destinations = [index for port, index in enumerate((i0, i1)) if enables & (1 << port)]
            valid = all(index < length for index in destinations) and len(set(destinations)) == len(destinations)
            replacements = [element(a, b, seed, record) for seed in (17, 29)]
            # A mapping models unordered valid writes, with no port priority.
            writes = {index: replacements[port] for port, index in enumerate((i0, i1))
                      if enables & (1 << port)} if valid else {}
            if tick:
                updated_state = [zero] * length if reset else [element(a, b, 11, record) if i == selector else old for i, old in enumerate(state)]
                captured = zero if reset or selector >= length else state[selector]
                state = [zero] * length if reset else [writes.get(index, old) for index, old in enumerate(state)]
            base = [element(a, b, index, record) for index in range(length)]
            read = base[selector] if selector < length else zero
            injected = [element(a, b, 11, record) if index == selector else old for index, old in enumerate(base)]
            written = [writes.get(index, old) for index, old in enumerate(base)]
            expanded = [element(a, b, 11, record) if i == selector else old for i, old in enumerate(state)]
            results = [read] + injected + written + written + state + [captured] + expanded + [expanded[0]] + updated_state
            flat = [value for result in results for value in result]
            expected.append(flat + [0] * (OUTPUT_COUNT - len(flat)))
            rows.append([config, tick, reset, raw_selector, raw_i0, raw_i1, raw_enables, int(valid), a, b])
    return '\n'.join(' '.join(f'{value:x}' for value in row) for row in rows) + '\n', expected


def native_driver():
    includes, cases = [], []
    for config, (length, record) in enumerate(CONFIGS):
        name = model_name(length, record)
        includes.append(f'#include "{name}.hpp"')
        values = [f'dut.outputs().p{port}{path}' for port, _, fields in ports(length, record) for path, _, _ in fields]
        values += ['UINT64_C(0)'] * (OUTPUT_COUNT - len(values))
        output = " << ' ' << ".join(values)
        cases.append(f'''case {config}: {{
  static rsim_p{name}::Model dut;
  dut.inputs.preset = reset; dut.inputs.pselector = selector;
  dut.inputs.pindex0 = i0; dut.inputs.pindex1 = i1;
  dut.inputs.penables = enables; dut.inputs.pvalid_uwrite = valid;
  dut.inputs.pa = a; dut.inputs.pb = b;
  if (tick) dut.tick(); else dut.eval();
  std::cout << {output} << '\\n';
  break;
}}''')
    header = '\n'.join(includes)
    branches = '\n'.join(cases)
    return f'''// SPDX-License-Identifier: Apache-2.0
#include <iostream>
#include <cstdint>
{header}
int main() {{
  std::uint64_t config, tick, reset, selector, i0, i1, enables, valid, a, b;
  std::cin >> std::hex; std::cout << std::hex;
  while (std::cin >> config >> tick >> reset >> selector >> i0 >> i1 >> enables >> valid >> a >> b) {{
    switch (config) {{ {branches} default: return 2; }}
  }}
}}
'''


def reference_top():
    lines, cases = [], []
    for config, (length, record) in enumerate(CONFIGS):
        connections, assignments = [], []
        for port, bits, fields in ports(length, record):
            net = f'c{config}_{port}'
            lines.append(f'wire [{bits-1}:0] {net};')
            connections.append(f'.{port}({net})')
            for _, width, low in fields:
                slot = len(assignments)
                assignments.append(f'observed[{slot*64} +: 64] = 64\'({net}[{low} +: {width}]);')
        width = 1 if length == 1 else 2
        lines.append(f'{model_name(length, record)} c{config}(.clock(clock && config_id == 8\'d{config}), '
                     f'.reset(reset), .selector(selector[{width-1}:0]), .index0(i0[{width-1}:0]), .index1(i1[{width-1}:0]), '
                     f'.enables(enables[1:0]), .valid_write(valid), .a(a), .b(b), {", ".join(connections)});')
        cases.append(f'8\'d{config}: begin {" ".join(assignments)} end')
    declarations = '\n'.join(lines)
    branches = '\n'.join(cases)
    return f'''// SPDX-License-Identifier: Apache-2.0
module DynamicReference(input logic clock, reset, valid, input logic [7:0] config_id,
                        input logic [63:0] selector, i0, i1, enables, a, b,
                        output logic [{OUTPUT_COUNT*64-1}:0] observed);
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
    return f'''// SPDX-License-Identifier: Apache-2.0
#include "VDynamicReference.h"
#include <iostream>
#include <cstdint>
int main() {{
  VDynamicReference dut;
  std::uint64_t config, tick, reset, selector, i0, i1, enables, valid, a, b;
  std::cin >> std::hex; std::cout << std::hex;
  while (std::cin >> config >> tick >> reset >> selector >> i0 >> i1 >> enables >> valid >> a >> b) {{
    dut.clock = 0; dut.eval();
    dut.config_id = config; dut.reset = reset; dut.selector = selector;
    dut.i0 = i0; dut.i1 = i1; dut.enables = enables; dut.valid = valid;
    dut.a = a; dut.b = b; dut.eval();
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
    source = work / 'dynamic-main.cpp'
    source.write_text(native_driver())
    # Keep eagerly evaluated partial operations visible to bounds sanitizers;
    # the baseline scalar/aggregate harness separately covers optimized builds.
    run(shlex.split(os.environ.get('CXX', 'c++')) +
        ['-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
         '-fno-sanitize-recover=all', *map(str, sorted(work.glob('Dynamic*.cpp'))), str(source),
         '-o', str(work / 'dynamic')], work, 'dynamic-build')
    vectors, expected = stimuli()
    compare(run([str(work / 'dynamic')], work, 'dynamic-run', vectors), expected, 'rsim dynamic')
    if differential:
        (work / 'DynamicReference.sv').write_text(reference_top())
        driver = work / 'dynamic-reference.cpp'
        driver.write_text(reference_driver())
        run(['verilator', '--cc', '--exe', '--build', '-j', '2', '--top-module', 'DynamicReference',
             '--Mdir', str(work / 'dynamic-obj'), *map(str, sorted(work.glob('Dynamic*.sv'))),
             str(driver)], work, 'dynamic-verilator')
        compare(run([str(work / 'dynamic-obj/VDynamicReference')], work, 'dynamic-reference', vectors),
                expected, 'Verilator dynamic')
    return len(expected)
