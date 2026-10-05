# SPDX-License-Identifier: Apache-2.0
"""Prove a generated SV/DPI host for rsim against direct SV and an oracle."""
import os
from pathlib import Path
import resource
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent


def observations(output):
    # Independent callbacks have no ordering contract. Their per-scope sequence
    # numbers still distinguish reordered, duplicate, and missing edges.
    return sorted(line for line in output.splitlines()
                  if line.startswith(('CALL ', 'STATE ')))


def no_core_dump():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


WIDE_WIDTHS = (65, 129, 512, 513)


def wide_bench():
    declarations, connections, constants, drive, stable, sample, check = [], [], [], [], [], [], []
    for width in WIDE_WIDTHS:
        seed = (1 << (width - 1)) + (1 << 32) + 7
        declarations.append(f'''logic [{width - 1}:0] data{width}[2];
  wire [{width - 1}:0] out{width}[2], sample{width}[2], constant{width}[2];
  logic [{width - 1}:0] expected{width}[2], delayed{width}[2], external{width}[2];''')
        connections += [f'.data{width}(data{width}[s]), .out{width}(out{width}[s]), '
                        f'.sample{width}(sample{width}[s]), .constant{width}(constant{width}[s])']
        constants.append(f'if (constant{width}[s] != {width}\'h{seed:x}) $fatal(1, "wide initial constant {width}");')
        drive.append(f'''
      data{width}[s] = '0;
      for (int bit_index = 0; bit_index < {width}; bit_index += 32)
        data{width}[s] |= {width}'(32'hfedcba98 ^ (32'h10203041 * 32'(index + s * 7 + bit_index))) << bit_index;
      case (index % 5)
        0: data{width}[s] = '0;
        1: data{width}[s] = '1;
        2: data{width}[s] = {width}'(1) << ((index * 31 + s) % {width});
        default: begin end
      endcase''')
        stable.append(f'''if (initialized && (out{width}[s] != expected{width}[s] || sample{width}[s] != delayed{width}[s]))
        $fatal(1, "wide outputs changed without edge {width}");''')
        sample.append(f'external{width}[s] <= out{width}[s];')
        check.append(f'''
      if (initialized && external{width}[s] != expected{width}[s]) $fatal(1, "wide NBA publication {width}");
      delayed{width}[s] = reset[s] ? '0 : expected{width}[s];
      expected{width}[s] = reset[s] ? '0 : enable[s] ? data{width}[s] : expected{width}[s];
      if (out{width}[s] != expected{width}[s] || sample{width}[s] != delayed{width}[s])
        $fatal(1, "wide state oracle {width}");
      $display("STATE %0d %0d {width} %h %h", index, s, out{width}[s], sample{width}[s]);''')
    return f'''// SPDX-License-Identifier: Apache-2.0
module BridgeWideBench;
  bit clock = 0, fault = 0, initialized = 0;
  bit [1:0] reset = '1, enable = '0;
  {chr(10).join(declarations)}
  for (genvar s = 0; s < 2; s++) begin : instances
    RsimBridgeWide dut(.clock(clock), .reset(reset[s]), .enable(enable[s]), .fault(fault),
      {', '.join(connections)});
    always @(posedge clock) begin
      {chr(10).join(sample)}
    end
  end
  initial begin
    #1;
    for (int s = 0; s < 2; s++) begin
      {chr(10).join(constants)}
    end
    for (int index = 0; index < 80; index++) begin
      clock = 0;
      for (int s = 0; s < 2; s++) begin
        reset[s] = index == 0 || index == 17 + s * 3;
        enable[s] = (index + s) % 3 != 1;
        {chr(10).join(drive)}
      end
      #1;
      for (int s = 0; s < 2; s++) begin
        {chr(10).join(stable)}
      end
      clock = 1;
      #1;
      for (int s = 0; s < 2; s++) begin
        {chr(10).join(check)}
      end
      initialized = 1;
    end
    if ($test$plusargs("bridge-fail")) begin
      clock = 0; fault = 1; reset = 0; #1; clock = 1; #1;
      $fatal(1, "wide assertion was not checked");
    end
    $display("BRIDGE_WIDE_PASS");
    $finish;
  end
endmodule
'''


def wide_abi_driver():
    # Call the generated bridge against the actual DPI declarations, allowing
    # dirty padding that an SV caller would otherwise clean before the call.
    declarations, outs, ins, fill, verify, constants, padding = [], [], [], [], [], [], []
    for width in WIDE_WIDTHS:
        n = (width + 31) // 32
        mask = (1 << (width % 32 or 32)) - 1
        seed = (1 << (width - 1)) + (1 << 32) + 7
        declarations.append(f'std::array<std::uint32_t, {n}> data{width}{{}}, q{width}{{}}, delayed{width}{{}}, constant{width}{{}};')
        ins.append(f'data{width}.data()')
        outs += [f'{name}{width}.data()' for name in ('q', 'delayed', 'constant')]
        fill.append(f'for (unsigned i = 0; i < {n}; ++i) data{width}[i] = UINT32_C(0xabcdef01) ^ (UINT32_C(0x10203041) * i);')
        verify.append(f'''for (unsigned i = 0; i < {n}; ++i) {{
    const auto mask = i == {n - 1} ? UINT32_C({mask}) : UINT32_MAX;
    if (q{width}[i] != (data{width}[i] & mask) || delayed{width}[i] != 0) return 4;
  }}''')
        constants += [f'if (constant{width}[{i}] != UINT32_C({(seed >> (32*i)) & 0xffffffff})) return 2;' for i in range(n)]
        if width % 32:
            padding += [f'if (({name}{width}.back() & ~UINT32_C({mask})) != 0) return 3;' for name in ('q', 'delayed', 'constant')]
    output_names = [f'{name}{width}' for width in WIDE_WIDTHS for name in ('q', 'delayed', 'constant')]
    snapshot = ', '.join(output_names)
    suffix = ', '.join(outs + ['&error'])
    tick = f'rhodium_rsim_pRsimBridgeWide_tick(handle, reset, enable, fault, {", ".join(ins)}, {suffix})'
    return f'''// SPDX-License-Identifier: Apache-2.0
#include "VBridgeWideBench__Dpi.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <tuple>
int main() {{
  {chr(10).join(declarations)}
  void* handle = nullptr;
  const char* error = nullptr;
  if (rhodium_rsim_pRsimBridgeWide_create(&handle, {suffix}) != 0 || !handle || *error) return 1;
  {chr(10).join(constants)}
  {chr(10).join(padding)}
  unsigned long long reset = 1, enable = 1, fault = 0;
  if ({tick} != 0) return 1;
  {chr(10).join(fill)}
  reset = 0;
  if ({tick} != 0) return 1;
  {chr(10).join(verify)}
  {chr(10).join(padding)}
  const auto snapshot = [&]() {{ return std::make_tuple({snapshot}); }};
  const auto before = snapshot();
  {chr(10).join(f'for (auto& word : data{w}) word = ~word;' for w in WIDE_WIDTHS)}
  fault = 1;
  if ({tick} == 0 || !std::strstr(error, "bridge_wide_check") || snapshot() != before) return 5;
  fault = 0; enable = 0;
  if ({tick} != 0 || *error) return 6;
  {chr(10).join(f'if (q{w} != std::get<{i * 3}>(before) || q{w} != delayed{w}) return 7;' for i, w in enumerate(WIDE_WIDTHS))}
  rhodium_rsim_pRsimBridgeWide_destroy(handle);
}}
'''


def run_wide_abi(work, run):
    source = work / 'wide-abi.cpp'
    source.write_text(wide_abi_driver())
    root = run([os.environ.get('VERILATOR', 'verilator'), '--getenv', 'VERILATOR_ROOT'], work, 'verilator-root').strip()
    run(shlex.split(os.environ.get('CXX', 'c++')) + [
        '-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
        '-fno-sanitize-recover=all', '-I' + str(work / 'BridgeWideBench-rsim'),
        '-I' + str(Path(root) / 'include/vltstd'), '-include', 'VBridgeWideBench__Dpi.h',
        str(work / 'RsimBridgeWide.cpp'), str(work / 'RsimBridgeWide_bridge.cpp'), str(source),
        '-o', str(work / 'wide-abi')], work, 'build-wide-abi')
    run([str(work / 'wide-abi')], work, 'wide-abi')


def run_suite(work, run):
    work = work / 'sv-bridge'
    work.mkdir()
    run([str(ROOT / 'tools/run-racket.sh'), str(HERE / 'emit-sv-bridge.rhm'),
         str(work)], work, 'emit')
    (work / 'wide-bench.sv').write_text(wide_bench())
    for top in ('TestDriver', 'BridgeBench', 'BridgePortsBench', 'BridgeWideBench'):
        outputs = {}
        for mode in ('rsim', 'reference'):
            label = f'{top}-{mode}'
            obj = work / label
            port_bench = top in ('BridgePortsBench', 'BridgeWideBench')
            model = {'BridgePortsBench': 'RsimBridgePorts', 'BridgeWideBench': 'RsimBridgeWide'}.get(top, 'RsimBridgeCounter')
            rtl = work / (('' if mode == 'rsim' else 'reference-') + model + '.sv')
            sources = [str(rtl), str(work / 'wide-bench.sv' if top == 'BridgeWideBench' else HERE / 'sv-bridge-bench.sv')]
            if not port_bench:
                sources += [str(HERE / 'sv-bridge-host.cpp')]
            flags = ['-std=c++17', '-I' + shlex.quote(str(work))]
            if mode == 'rsim':
                sources += [str(work / (model + '.cpp')), str(work / (model + '_bridge.cpp'))]
            if top == 'TestDriver':
                sources += [str(ROOT / 'sims/TestDriver.v'), str(ROOT / 'sims/verilator/simulation_runtime.cc')]
                flags += ['-std=c++20']
            # Other benches force-include the generated ABI. The production runtime
            # declares noexcept entrypoints separately from Verilator's declarations.
            if top != 'TestDriver' and (mode == 'rsim' or not port_bench):
                flags += ['-include', f'V{top}__Dpi.h']
            run([os.environ.get('VERILATOR', 'verilator'), '--binary', '--timing', '--vpi',
                 '--assert', '-Wno-SYMRSVDWORD', '-j', '2', '--top-module', top, '--Mdir', str(obj),
                 '-CFLAGS', ' '.join(flags), *sources], work, 'build-' + label)
            executable = obj / ('V' + top)
            outputs[mode] = run([str(executable), '+bridge-token=42', '+max-cycles=20'], work, label)
            success = {'TestDriver': 'SoC harness simulation passed', 'BridgePortsBench': 'BRIDGE_PORTS_PASS', 'BridgeWideBench': 'BRIDGE_WIDE_PASS'}.get(top, 'BRIDGE_BENCH_PASS')
            if success not in outputs[mode]:
                raise AssertionError(f'{label}: missing completion marker')
            if top in ('TestDriver', 'BridgeWideBench'):
                result = subprocess.run([str(executable), '+bridge-token=42', '+bridge-fail'],
                                        cwd=work, text=True, capture_output=True, timeout=30,
                                        preexec_fn=no_core_dump)
                output = result.stdout + result.stderr
                (work / f'failure-{label}.log').write_text(output)
                diagnostic = 'bridge_wide_check' if top == 'BridgeWideBench' else 'bridge_counter_check'
                if (result.returncode == 0 or diagnostic not in output or success in output):
                    raise AssertionError(f'{label}: assertion did not terminate simulation')
                if mode == 'rsim' and 'rsim bridge: assertion failed:' not in output:
                    raise AssertionError('C++ assertion did not cross the bridge as an SV fatal')
            if port_bench:
                continue
            calls = [line.split() for line in outputs[mode].splitlines() if line.startswith('CALL ')]
            scopes = {row[1] for row in calls}
            expected_scopes = ({'TestDriver.dut.core'} if top == 'TestDriver'
                               else {'BridgeBench.first', 'BridgeBench.second'})
            # Versions differ on whether the synthetic TOP prefix is exposed.
            if {scope.removeprefix('TOP.') for scope in scopes} != expected_scopes:
                raise AssertionError(f'{label}: wrong callback scopes: {scopes}')
            # TestDriver asserts reset for three edges, then exits after four
            # increments. The separate bench clocks each instance 23 times.
            edges = 7 if top == 'TestDriver' else 23
            for scope in scopes:
                if [int(row[2]) for row in calls if row[1] == scope] != list(range(edges)):
                    raise AssertionError(f'{label}: missing or duplicate callback edges')
        if observations(outputs['rsim']) != observations(outputs['reference']):
            raise AssertionError(f'{top}: rsim and direct-SV cycle observations differ')
    run_wide_abi(work, run)
    return 13  # Four passing and two failing scenarios per route, plus native ABI checks.


if __name__ == '__main__':
    from run import run
    work = Path(tempfile.mkdtemp(prefix='rhodium-rsim-sv-bridge-'))
    try:
        count = run_suite(work, run)
        print(f'rsim SV bridge: {count} scenarios passed')
    except BaseException:
        print(f'SV bridge artifacts and logs retained at {work}')
        raise
    else:
        shutil.rmtree(work)
