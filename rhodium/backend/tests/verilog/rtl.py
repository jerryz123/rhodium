# Builds and checks the combined RTL behavioral bench using independent semantic oracles.
# SPDX-License-Identifier: Apache-2.0
import random
from support import WIDTHS, run, vector_table
from scalar_cases import (OUTPUTS, HIERARCHY_OUTPUTS, FLAGS, cases, expected, hierarchy_expected, ARITHMETIC_OUTPUTS, arithmetic_sizes, arithmetic_cases, arithmetic_expected, AGGREGATE_OUTPUTS, aggregate_sizes, aggregate_cases, aggregate_expected)
from state_cases import (SEQUENTIAL_INPUTS, SEQUENTIAL_OUTPUTS, sequential_cases, DYNAMIC_LENGTHS, dynamic_ports, dynamic_cases, WRITE_CONFIGS, write_set_ports, write_set_controls, write_set_cases, partial_ports, decode_expected, partial_cases, memory_ports, memory_cases, cdc_ports, cdc_cases)

def bench(direct, work):
    lines = ["module tb;"]
    statements = []
    count = 0
    for width in WIDTHS:
        input_ports = ("a", "b", "narrow", "same", "wide")
        inputs = [(f"arith_{name}{width}", size) for name, size in
                  zip(input_ports, (width, width, 1, width, width + 3))]
        outputs = [(f"arith_{name}{width}", size) for name, size in
                   zip(ARITHMETIC_OUTPUTS, arithmetic_sizes(width))]
        lines += [f"logic [{size-1}:0] {name};" for name, size in inputs]
        lines += [f"wire [{size-1}:0] {name};" for name, size in outputs]
        connections = [f".{name}(arith_{name}{width})" for name in input_ports + ARITHMETIC_OUTPUTS]
        lines.append(f"Arithmetic{width} arithmetic{width}({', '.join(connections)});")
        vectors = [(tuple(case), arithmetic_expected(width, *case)) for case in arithmetic_cases(width)]
        decls, steps, n = vector_table(work, f"arithmetic{width}", inputs, outputs, vectors, count)
        lines += decls
        statements += steps
        count += n
        inputs = [(f"a{width}", width), (f"b{width}", width),
                  (f"selector{width}", 2), (f"fallback{width}", width)]
        outputs = [(f"{name}{width}", 1 if name in FLAGS else width)
                   for name in OUTPUTS + HIERARCHY_OUTPUTS]
        lines += [f"logic [{size-1}:0] {name};" for name, size in inputs]
        lines += [f"wire [{size-1}:0] {name};" for name, size in outputs]
        connections = [f".a(a{width})", f".b(b{width})", f".selector(selector{width})",
                       f"._sv_0(fallback{width})"]
        connections += [f".{name}({name}{width})" for name in OUTPUTS]
        lines.append(f"Combinational{width} dut{width}({', '.join(connections)});")
        hierarchy_connections = [f".a(a{width})", f".b(b{width})", f"._sv_1(fallback{width})"]
        hierarchy_connections += [f".{name}({name}{width})" for name in HIERARCHY_OUTPUTS]
        lines.append(f"Hierarchy{width} hierarchy{width}({', '.join(hierarchy_connections)});")
        vectors = [(tuple(case), expected(width, *case) + hierarchy_expected(width, case[0], case[1], case[3]))
                   for case in cases(width)]
        decls, steps, n = vector_table(work, f"scalar{width}", inputs, outputs, vectors, count)
        lines += decls
        statements += steps
        count += n

        inputs = [(f"aa{width}", width), (f"ab{width}", width),
                  (f"asel{width}", 2), (f"packet{width}", 2 * width + 18)]
        outputs = [(f"agg_{name}{width}", size) for name, size in zip(AGGREGATE_OUTPUTS, aggregate_sizes(width))]
        lines += [f"logic [{size-1}:0] {name};" for name, size in inputs]
        lines += [f"wire [{size-1}:0] {name};" for name, size in outputs]
        connections = [f".a(aa{width})", f".b(ab{width})", f".selector(asel{width})", f".packet(packet{width})"]
        connections += [f".{name}(agg_{name}{width})" for name in AGGREGATE_OUTPUTS]
        lines.append(f"Aggregates{width} aggregate{width}({', '.join(connections)});")
        vectors = [(tuple(case), aggregate_expected(width, *case)) for case in aggregate_cases(width)]
        decls, steps, n = vector_table(work, f"aggregate{width}", inputs, outputs, vectors, count)
        lines += decls
        statements += steps
        count += n
        input_sizes = (1, 1, 1, width, width, 2 * width + 3)
        output_sizes = (width, width, width, 2 * width + 3, 2 * width, 1, 1)
        inputs = [(f"seq_{side}_{name}{width}", size) for side in ("first", "second")
                  for name, size in zip(SEQUENTIAL_INPUTS, input_sizes)]
        outputs = [(f"seq_{side}_{name}{width}", size) for side in ("first", "second")
                   for name, size in zip(SEQUENTIAL_OUTPUTS, output_sizes)]
        lines += [f"logic [{size-1}:0] {name} = '0;" for name, size in inputs]
        lines += [f"wire [{size-1}:0] {name};" for name, size in outputs]
        connections = [f".{side}_{name}(seq_{side}_{name}{width})" for side in ("first", "second")
                       for name in SEQUENTIAL_INPUTS + SEQUENTIAL_OUTPUTS]
        lines.append(f"Sequential{width} sequential{width}({', '.join(connections)});")
        decls, steps, n = vector_table(work, f"sequential{width}", inputs, outputs,
                                      sequential_cases(width), count, masked=True)
        lines += decls
        statements += steps
        count += n
        input_ports, output_ports = dynamic_ports(width)
        inputs = [(f"dyn_{name}{width}", size) for name, size in input_ports]
        outputs = [(f"dyn_{name}{width}", size) for name, size in output_ports]
        lines += [f"logic [{size-1}:0] {name} = '0;" for name, size in inputs]
        lines += [f"wire [{size-1}:0] {name};" for name, size in outputs]
        connections = [f".{name}(dyn_{name}{width})" for name, _ in input_ports + output_ports]
        lines.append(f"Dynamic{width} dynamic{width}({', '.join(connections)});")
        decls, steps, n = vector_table(work, f"dynamic{width}", inputs, outputs,
                                      dynamic_cases(width), count, masked=True)
        lines += decls
        statements += steps
        count += n
        input_ports, output_ports = write_set_ports(width)
        inputs = [(f"writes_{name}{width}", size) for name, size in input_ports]
        outputs = [(f"writes_{name}{width}", size) for name, size in output_ports]
        lines += [f"logic [{size-1}:0] {name} = '0;" for name, size in inputs]
        lines += [f"wire [{size-1}:0] {name};" for name, size in outputs]
        connections = [f".{name}(writes_{name}{width})" for name, _ in input_ports + output_ports]
        lines.append(f"WriteSet{width} write_set{width}({', '.join(connections)});")
        decls, steps, n = vector_table(work, f"write_set{width}", inputs, outputs,
                                      write_set_cases(width), count, masked=True)
        lines += decls
        statements += steps
        count += n
        input_ports, output_ports = partial_ports(width)
        inputs = [(f"partial_{name}{width}", size) for name, size in input_ports]
        outputs = [(f"partial_{name}{width}", size) for name, size in output_ports]
        lines += [f"logic [{size-1}:0] {name};" for name, size in inputs]
        lines += [f"wire [{size-1}:0] {name};" for name, size in outputs]
        connections = [f".{name}(partial_{name}{width})" for name, _ in input_ports + output_ports]
        lines.append(f"Partial{width} partial{width}({', '.join(connections)});")
        decls, steps, n = vector_table(work, f"partial{width}", inputs, outputs,
                                      partial_cases(width), count, masked=True)
        lines += decls
        statements += steps
        count += n
        input_ports, output_ports = memory_ports(width)
        inputs = [(f"mem_{name}{width}", size) for name, size in input_ports]
        outputs = [(f"mem_{name}{width}", size) for name, size in output_ports]
        lines += [f"logic [{size-1}:0] {name} = '0;" for name, size in inputs]
        lines += [f"wire [{size-1}:0] {name};" for name, size in outputs]
        connections = [f".{name}(mem_{name}{width})" for name, _ in input_ports + output_ports]
        lines.append(f"Memories{width} memories{width}({', '.join(connections)});")
        decls, steps, n = vector_table(work, f"memory{width}", inputs, outputs,
                                      memory_cases(width), count, masked=True)
        lines += decls
        statements += steps
        count += n
    input_ports, output_ports = cdc_ports()
    inputs = [(f"cdc_{name}", size) for name, size in input_ports]
    outputs = [(f"cdc_{name}", size) for name, size in output_ports]
    lines += [f"logic {name} = 0;" for name, _ in inputs]
    lines += [f"wire {name};" for name, _ in outputs]
    connections = [f".{name}(cdc_{name})" for name, _ in input_ports + output_ports]
    lines.append(f"CdcChecks cdc({', '.join(connections)});")
    decls, steps, n = vector_table(work, "cdc", inputs, outputs, cdc_cases(), count, masked=True)
    lines += decls
    statements += steps
    count += n
    # Fixed-width naming/layout fixture also proves separate artifacts' packages
    # coexist when combined in the same compilation unit for simulation.
    inputs = [("type_input", 21)]
    outputs = [("type_restored", 21), ("type_swapped", 21), ("type_matrix", 18), ("type_last", 3)]
    lines += ["logic [20:0] type_input;"]
    lines += [f"wire [{size-1}:0] {name};" for name, size in outputs]
    # CIRCT's unscoped typedef is hidden by a Packet port; its fixture renames
    # only that port. Direct emission retains and simulates the collision.
    type_result_port = "Packet" if direct else "restored"
    lines.append(f"TypeNames type_names(.TypeNames_types(type_input), .{type_result_port}(type_restored), "
                 ".swapped(type_swapped), .matrix(type_matrix), .last(type_last));")
    rng = random.Random(20260931)
    samples = [0, (1 << 21) - 1] + [1 << bit for bit in range(21)]
    samples += [rng.getrandbits(21) for _ in range(128)]
    vectors = []
    for value in samples:
        grid = value >> 3
        swapped = (((grid & 511) << 9) | (grid >> 9)) << 3 | (value & 7)
        vectors.append(((value,), (value, swapped, grid, (grid >> 6) & 7)))
    decls, steps, n = vector_table(work, "types", inputs, outputs, vectors, count)
    lines += decls
    statements += steps
    count += n
    if direct:
        lines += ["logic [4:0] keyword_input; wire [4:0] keyword_output;",
                  r"\module keyword_dut(.\input (keyword_input), .\output (keyword_output));"]
    lines.append("initial begin")
    if direct:
        lines += ["keyword_input = 5'h15; #1;",
                  "if (keyword_output !== 5'h15) $fatal(1, \"keyword port identity\");"]
    lines += statements
    lines += ['$display("PASS packed oracle"); $finish;', "end", "endmodule"]
    return "\n".join(lines) + "\n", count

def simulate(work, name, source, direct, verilator):
    sv = work / f"{name}.sv"
    sv.write_text(source)
    tb = work / f"{name}_tb.sv"
    content, count = bench(direct, work)
    tb.write_text(content)
    build = work / f"{name}-build"
    run([verilator, "--binary", "--timing", "--assert", "--build-jobs", "2",
         "--top-module", "tb", "--Mdir", build, "-o", "sim", sv, tb], work / f"{name}-build.log")
    output = run([build / "sim"], work / f"{name}-run.log", timeout=60)
    samples = [line for line in output.splitlines() if line.startswith("sample ")]
    if len(samples) != count or "PASS packed oracle" not in output:
        raise RuntimeError(f"incomplete simulation; see {name}-run.log")
    print(f"{name}: {count} input vectors passed independent expectations", flush=True)
    return samples
