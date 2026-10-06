#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare direct C++ scalar/state execution with integer oracles and optional SV."""
import argparse
import os
from pathlib import Path
import random
import shlex
import shutil
import subprocess
import tempfile
import arithmetic
import aggregate
import dynamic
import memory
import sync_memory
import selection
import membership
import mask_index
import assertions
import branches
import foreign
import uart
import sv_bridge
import chi_memory
import wide

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
WIDTHS = (1, 5, 8, 9, 16, 17, 32, 33, 63, 64)
SCALAR = ("sum", "inverted", "difference", "product", "both", "either",
          "parity", "equal", "less", "selected", "wrapped", "class", "__class__")
STATE = ("first_left", "first_right", "second_left", "second_right", "count", "sample")
ARITHMETIC = tuple(name for name, _ in arithmetic.output_widths(1))


def encoded(name):
    return "p" + name.replace("_", "_u")


def run(command, work, label, stdin=None):
    env = dict(os.environ, PLTCOLLECTS=str(ROOT) + os.pathsep)
    result = subprocess.run(command, cwd=ROOT, env=env, input=stdin, text=True,
                            capture_output=True, timeout=300)
    (work / f"{label}.log").write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f"{label} failed: {result.stderr[-1500:]}")
    return result.stdout


def stimuli():
    rng = random.Random(291)
    rows, expected = [], []
    for width in WIDTHS:
        aggregate_oracle = aggregate.Oracle(width)
        mask = (1 << width) - 1
        cases = [(1, 1, 0, 0, 0, 0),
                 (0, 0, 1, 1, mask, 0),  # eval must not load
                 (0, 0, 1, 1, mask, 0),
                 (1, 0, 1, 1, mask, 0),  # load wins over swap
                 (1, 0, 1, 0, 0, 0),
                 (1, 0, 1, 0, 0, 0),
                 (0, 1, 0, 0, 0, 0),   # eval must not reset
                 (1, 1, 1, 1, mask, mask)]
        edges = (0, 1, mask, mask >> 1, 1 << (width - 1))
        cases += [(1, 0, 0, 0, a, b) for a in edges for b in edges]
        cases += [(rng.randrange(2), int(rng.randrange(20) == 0),
                   rng.randrange(2), rng.randrange(2),
                   rng.getrandbits(64), rng.getrandbits(64)) for _ in range(250)]
        counts = (0, 1, width - 1, width, width + 1, 63, 64, 65,
                  1 << 32, 1 << 63, (1 << 64) - 1)
        cases = [(*case, counts[index % len(counts)]) for index, case in enumerate(cases)]
        # Cross all signed extrema with every shift boundary; random counts are
        # independent of operands, so count handling cannot hide arithmetic bugs.
        signed_edges = sorted(set(edges + (mask - 1,)))
        cases += [(0, 0, 0, 0, a, b, count)
                  for a in signed_edges for b in signed_edges for count in counts]
        cases += [(0, 0, 0, 0, rng.getrandbits(64), rng.getrandbits(64),
                   rng.randrange(width + 2) if index % 2 else rng.getrandbits(64))
                  for index in range(100)]
        left = right = other_left = other_right = count = sample = 0
        for tick, reset, enable, load, raw_a, raw_b, amount in cases:
            a, b = raw_a & mask, raw_b & mask
            if tick:
                sample = a
                if reset:
                    left, right, other_left, other_right, count = 1, mask - 1, 1, mask - 1, 0
                else:
                    if load:
                        left, right, other_left, other_right = a, b, b, a
                    elif enable:
                        left, right, other_left, other_right = right, left, other_right, other_left
                    count = (count + 1) & mask
            total = (a + b) & mask
            expected.append([total, (~total) & mask, (a - b) & mask, (a * b) & mask,
                             a & b, a | b, a ^ b, int(a == b), int(a < b),
                             b if load else a, int(total == 0), a, b,
                             left, right, other_left, other_right, count, sample] +
                            arithmetic.expected(width, raw_a, raw_b, amount) +
                            aggregate_oracle.observe(tick, reset, enable, load, raw_a, raw_b, amount))
            rows.append([width, tick, reset, enable, load, raw_a, raw_b, amount])
    return "\n".join(" ".join(f"{v:x}" for v in row) for row in rows) + "\n", expected


def native_driver():
    includes = "\n".join(f'#include "{kind}{w}.hpp"' for w in WIDTHS for kind in ("Scalar", "Swap", "Arithmetic", "Aggregate"))
    cases, startup, observers = [], [], []
    for width in WIDTHS:
        scalar = " << ' ' << ".join(f"comb.outputs().p{name.replace('_', '_u')}" for name in SCALAR)
        state = " << ' ' << ".join(f"dut.outputs().p{name.replace('_', '_u')}" for name in STATE)
        bits = " << ' ' << ".join(f"bits.outputs().{encoded(name)}" for name in ARITHMETIC)
        aggregate_values = " << ' ' << ".join(aggregate.native_outputs(width))
        # Default-initialize over dirty storage: static objects would hide a
        # missing initializer because the loader has already zeroed their bytes.
        zero_state = " || ".join(f"model->outputs().{encoded(name)} != 0" for name in STATE)
        zero_inputs = " || ".join(f"model->inputs.{encoded(name)} != 0"
                                 for name in ("reset", "enable", "load", "a", "b"))
        startup.append(f"""{{
  using Model = rsim_pSwap{width}::Model;
  alignas(Model) unsigned char storage[sizeof(Model)];
  for (int pattern : {{0x55, 0xaa}}) {{
    std::memset(storage, pattern, sizeof(storage));
    auto* model = ::new (static_cast<void*>(storage)) Model;
    if ({zero_inputs} || {zero_state}) return 4;
    model->eval();
    if ({zero_state}) return 5;
    model->~Model();
  }}
}}""")
        # Bound optimizer work in the sanitizer harness as the width matrix
        # grows; each observer keeps its own persistent model instances.
        cases.append(f"case {width}: if (int status = observe{width}(tick, reset, enable, load, a, b, amount)) return status; break;")
        observers.append(f"""[[gnu::noinline]] static int observe{width}(
    std::uint64_t tick, std::uint64_t reset, std::uint64_t enable,
    std::uint64_t load, std::uint64_t a, std::uint64_t b, std::uint64_t amount) {{
  {aggregate.native_setup(width)}
  static rsim_pScalar{width}::Model comb;
  static rsim_pSwap{width}::Model dut;
  static rsim_pSwap{width}::Model untouched;
  static rsim_pArithmetic{width}::Model bits;
  comb.inputs.pa = a; comb.inputs.pb = b; comb.inputs.pselect = load;
  comb.eval();
  bits.inputs.pa = a; bits.inputs.pb = b; bits.inputs.pamount = amount;
  bits.eval();
  dut.inputs.preset = reset; dut.inputs.penable = enable; dut.inputs.pload = load;
  dut.inputs.pa = a; dut.inputs.pb = b;
  if (tick) dut.tick(); else dut.eval();
  untouched.eval();
  if (untouched.outputs().pcount != 0 || untouched.outputs().pfirst_uleft != 0 ||
      untouched.outputs().pfirst_uright != 0 || untouched.outputs().psecond_uleft != 0 ||
      untouched.outputs().psecond_uright != 0 || untouched.outputs().psample != 0) return 2;
  std::cout << {scalar} << ' ' << {state} << ' ' << {bits} << ' ' << {aggregate_values} << '\\n';
  return 0;
}}""")
    return f"""// SPDX-License-Identifier: Apache-2.0
#include <iostream>
#include <cstdint>
#include <cstring>
#include <new>
{includes}
{" ".join(observers)}
int main() {{
  {" ".join(startup)}
  std::uint64_t width, tick, reset, enable, load, a, b, amount;
  std::cin >> std::hex; std::cout << std::hex;
  while (std::cin >> width >> tick >> reset >> enable >> load >> a >> b >> amount) {{
    switch (width) {{ {" ".join(cases)} default: return 3; }}
  }}
}}
"""


def reference_top():
    declarations, choices = [], []
    names = SCALAR + STATE + ARITHMETIC
    for width in WIDTHS:
        port_widths = [(name, 1 if name in ('equal', 'less', 'wrapped') else width)
                       for name in SCALAR + STATE] + list(arithmetic.output_widths(width))
        declarations += [f"wire [{size - 1}:0] w{width}_{encoded(name)};" for name, size in port_widths]
        scalar_ports = [f".\\{name} (w{width}_{encoded(name)})" for name in SCALAR]
        state_ports = [f".\\{name} (w{width}_{encoded(name)})" for name in STATE]
        bits_ports = [f".\\{name} (w{width}_{encoded(name)})" for name in ARITHMETIC]
        declarations.append(f"Scalar{width} scalar{width}(.a(a[{width-1}:0]), .b(b[{width-1}:0]), .select(load), {', '.join(scalar_ports)});")
        declarations.append(f"Swap{width} swap{width}(.clock(clock && width == 8'd{width}), .reset(reset), .enable(enable), .load(load), .a(a[{width-1}:0]), .b(b[{width-1}:0]), {', '.join(state_ports)});")
        declarations.append(f"Arithmetic{width} bits{width}(.a(a[{width-1}:0]), .b(b[{width-1}:0]), .amount(amount), {', '.join(bits_ports)});")
        assignments = " ".join(f"{encoded(name)} = 64'(w{width}_{encoded(name)});" for name in names)
        aggregate_declarations, aggregate_assignments = aggregate.reference(width)
        declarations.append(aggregate_declarations)
        choices.append(f"8'd{width}: begin {assignments} {aggregate_assignments} end")
    declaration_text = "\n".join(declarations)
    choice_text = "\n".join(choices)
    return f"""// SPDX-License-Identifier: Apache-2.0
module Reference(input logic clock, reset, enable, load,
                 input logic [7:0] width, input logic [63:0] a, b, amount,
                 {", ".join("output logic [63:0] " + encoded(name) for name in names)},
                 output logic [{aggregate.OUTPUT_COUNT * 64 - 1}:0] aggregates);
{declaration_text}
always_comb begin
  aggregates = 0;
  {" ".join(encoded(name) + " = 0;" for name in names)}
  case (width)
    {choice_text}
    default: begin end
  endcase
end
endmodule
"""


def reference_driver():
    output = " << ' ' << ".join("dut." + encoded(name) for name in SCALAR + STATE + ARITHMETIC)
    return f"""// SPDX-License-Identifier: Apache-2.0
#include "VReference.h"
#include <iostream>
#include <cstdint>
int main() {{
  VReference dut;
  std::uint64_t width, tick, reset, enable, load, a, b, amount;
  std::cin >> std::hex; std::cout << std::hex;
  while (std::cin >> width >> tick >> reset >> enable >> load >> a >> b >> amount) {{
    dut.clock = 0; dut.eval();
    dut.width = width; dut.reset = reset; dut.enable = enable; dut.load = load;
    dut.a = a; dut.b = b; dut.amount = amount; dut.eval();
    if (tick) {{ dut.clock = 1; dut.eval(); }}
    std::cout << {output};
    for (unsigned i = 0; i < {aggregate.OUTPUT_COUNT}; ++i) {{
      const auto word = std::uint64_t(dut.aggregates[2*i]) | (std::uint64_t(dut.aggregates[2*i+1]) << 32);
      std::cout << ' ' << word;
    }}
    std::cout << '\\n';
  }}
}}
"""


def compare(text, expected, label):
    actual = [[int(value, 16) for value in line.split()] for line in text.splitlines()]
    if len(actual) != len(expected):
        raise AssertionError(f"{label}: {len(actual)} observations, expected {len(expected)}")
    for index, (got, want) in enumerate(zip(actual, expected)):
        if len(got) != len(want):
            raise AssertionError(f"{label} observation {index}: {len(got)} fields, expected {len(want)}")
        for field, (actual, value) in enumerate(zip(got, want)):
            if value is None:
                continue
            # Memory oracles can know only some bits of a partially initialized
            # word. Ordinary expectations still compare the complete carrier.
            mismatch = (actual ^ value[0]) & value[1] if isinstance(value, tuple) else actual != value
            if mismatch:
                raise AssertionError(f"{label} observation {index} field {field}: {actual:x} != {value}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--differential", action="store_true")
    parser.add_argument("--region-budget", type=int,
                        help="force private rsim region boundaries in Builder fixtures")
    args = parser.parse_args()
    if args.region_budget is not None:
        if args.region_budget <= 0:
            parser.error("--region-budget must be positive")
        os.environ["RHODIUM_RSIM_TEST_REGION_BUDGET"] = str(args.region_budget)
    work = Path(tempfile.mkdtemp(prefix="rhodium-rsim-"))
    try:
        emit = [str(ROOT / "tools/run-racket.sh"), str(HERE / "emit-fixtures.rhm"), str(work)]
        run(emit + ["rsim"], work, "emit-rsim")
        source = work / "main.cpp"
        source.write_text(native_driver())
        run(shlex.split(os.environ.get("CXX", "c++")) +
            ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined",
             # Other emitted families compile in their own runners below. Limit
             # this executable to its baseline models as the width matrix grows.
             *[str(work / f"{kind}{width}.cpp") for width in WIDTHS
               for kind in ("Scalar", "Swap", "Arithmetic", "Aggregate")],
             str(source), "-o", str(work / "rsim")], work, "build")
        vectors, expected = stimuli()
        compare(run([str(work / "rsim")], work, "rsim", vectors), expected, "rsim")
        if args.differential:
            run(emit + ["verilog"], work, "emit-verilog")
            (work / "Reference.sv").write_text(reference_top())
            driver = work / "reference.cpp"
            driver.write_text(reference_driver())
            # The aggregate fixture intentionally uses the C++ keyword `class`
            # as an SV struct field to exercise each backend's name escaping.
            # Its projection feedback is acyclic by field, although Verilator's
            # initial whole-aggregate dependency analysis reports UNOPTFLAT.
            run(["verilator", "--cc", "--exe", "--build", "-j", "2", "--top-module", "Reference",
                 "-Wno-SYMRSVDWORD", "-Wno-UNOPTFLAT",
                 "--Mdir", str(work / "obj"), *map(str, sorted(work.glob("*.sv"))),
                 str(driver)], work, "verilator")
            compare(run([str(work / "obj/VReference")], work, "reference", vectors), expected, "Verilator")
        dynamic_count = dynamic.run_suite(work, run, compare, args.differential)
        memory_count = memory.run_suite(work, run, compare, args.differential)
        sync_count = sync_memory.run_suite(work, run, compare, args.differential)
        selection_count = selection.run_suite(work, run, compare, args.differential)
        membership_count = membership.run_suite(work, run, compare, args.differential)
        print(f"rsim: {membership_count} authored membership observations passed" + (" with SV comparison" if args.differential else ""))
        mask_count = mask_index.run_suite(work, run, compare, args.differential)
        print(f"rsim: {mask_count} authored mask indexing observations passed" + (" with SV comparison" if args.differential else ""))
        foreign_count = foreign.run_suite(work, run, compare, args.differential)
        assertion_count = assertions.run_suite(work, run, args.differential)
        branch_count = branches.run_suite(work, run, compare, args.differential)
        print(f"rsim: {branch_count} conditional register observations and execution counts passed" + (" with SV comparison" if args.differential else ""))
        conditional_count = branches.run_conditional_suite(work, run, compare, args.differential)
        print(f"rsim: {conditional_count} general conditional observations and execution counts passed" + (" with SV comparison" if args.differential else ""))
        wide_count = wide.run_suite(work, run, args.differential)
        print(f"rsim: {wide_count} wide data/state observations passed" + (" on both backends" if args.differential else ""))
        uart_count = uart.run_suite(work, run, args.differential)
        if args.differential:
            chi_count = chi_memory.run_suite(work, run)
            print(f"rsim: {chi_count} CHI memory cycle/response observations passed on both backends")
            bridge_count = sv_bridge.run_suite(work, run)
            print(f"rsim: {bridge_count} SV bridge scenarios passed")
        print(f"rsim: {uart_count} UART/PTY instances passed" + (" on both backends" if args.differential else ""))
        print(f"rsim: {len(expected)} baseline, {dynamic_count} dynamic, {memory_count} asynchronous memory, {sync_count} synchronous memory, and {selection_count} selection observations passed" + (" on both backends" if args.differential else ""))
        print(f"rsim: {foreign_count} foreign-call observations passed" + (" on both backends" if args.differential else ""))
        print(f"rsim: {assertion_count} assertion scenarios passed" + (" on both backends" if args.differential else ""))
    except BaseException:
        print(f"rsim artifacts and logs retained at {work}")
        raise
    else:
        shutil.rmtree(work)


if __name__ == "__main__":
    main()
