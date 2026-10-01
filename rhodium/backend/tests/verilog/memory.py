# Models synchronous memory masks and timing and runs the corresponding SV bench.
# SPDX-License-Identifier: Apache-2.0
import random
from support import run, vector_table

def sync_memory_shapes(width):
    """Physical widths and granules deliberately cross aggregate field boundaries."""
    return (("plain", width, None), ("bits", width, 1), ("word", width, width),
            ("record", 2 * width + 2, 2), ("vector", 2 * width, width))


def sync_memory_ports(width):
    inputs, outputs = [], []
    for side in ("first", "second"):
        inputs += [(f"{side}_clock", 1), (f"{side}_reset", 1)]
        for depth in (1, 3, 4):
            aw = max(1, (depth - 1).bit_length())
            inputs += [(f"{side}_{name}_{depth}", size) for name, size in
                       (("read_address", aw), ("write_address", aw), ("read_enable", 1),
                        ("write_enable", 1), ("shared_enable", 1), ("shared_write", 1))]
            for kind, size, granule in sync_memory_shapes(width):
                prefix = f"{side}_{kind}_{depth}"
                inputs.append((f"{prefix}_data", size))
                if granule:
                    inputs.append((f"{prefix}_mask", size // granule))
                for mode in ("separate", "shared"):
                    outputs += [(f"{prefix}_{mode}_{name}", size) for name in ("data", "sampled")]
        outputs.append((f"{side}_read_only_zero", width))
    return inputs, outputs


def sync_memory_cases(width):
    """Track defined bits per word and sample old read results at each rising edge.

    Collision reads and inactive/read-write write-cycle outputs are unconstrained.
    Partial initialization defines only written granules; disabled invalid addresses
    must not corrupt storage. No expectation depends on simulator x resolution.
    """
    inputs, outputs = sync_memory_ports(width)
    controls = dict.fromkeys((name for name, _ in inputs), 0)
    sides, depths = ("first", "second"), (1, 3, 4)
    shapes = sync_memory_shapes(width)
    keys = [(side, depth, kind, mode) for side in sides for depth in depths
            for kind, _, _ in shapes for mode in ("separate", "shared")]
    words = {key: [(0, 0)] * key[1] for key in keys}
    reads = dict.fromkeys(keys, (0, 0))
    sampled = reads.copy()
    previous = dict.fromkeys(sides, 0)
    rng = random.Random(20261010 + width)

    def observe():
        for side in sides:
            clock = controls[f"{side}_clock"]
            if clock and not previous[side]:
                for depth in depths:
                    def control(name):
                        return controls[f"{side}_{name}_{depth}"]
                    for kind, size, granule in shapes:
                        full = (1 << size) - 1
                        prefix = f"{side}_{kind}_{depth}"
                        data = controls[f"{prefix}_data"]
                        write_mask = full if not granule else sum(
                            ((1 << granule) - 1) << (i * granule)
                            for i in range(size // granule) if controls[f"{prefix}_mask"] & (1 << i))
                        for mode in ("separate", "shared"):
                            key = side, depth, kind, mode
                            sampled[key] = (0, full) if controls[f"{side}_reset"] else reads[key]
                            writing = control("write_enable") if mode == "separate" else control("shared_enable") and control("shared_write")
                            reading = control("read_enable") if mode == "separate" else control("shared_enable") and not control("shared_write")
                            wa = control("write_address")
                            ra = control("read_address") if mode == "separate" else wa
                            collision = mode == "separate" and writing and wa == ra
                            reads[key] = words[key][ra] if reading and ra < depth and not collision else (0, 0)
                            if writing:
                                assert wa < depth  # enabled invalid writes need no specified semantics
                                old, known = words[key][wa]
                                words[key][wa] = ((old & ~write_mask) | (data & write_mask), known | write_mask)
            previous[side] = clock
        values = {}
        for key in keys:
            side, depth, kind, mode = key
            prefix = f"{side}_{kind}_{depth}_{mode}"
            values[f"{prefix}_data"] = reads[key]
            values[f"{prefix}_sampled"] = sampled[key]
        for side in sides:
            values[f"{side}_read_only_zero"] = (0, (1 << width) - 1)
        return (tuple(controls[name] for name, _ in inputs),
                tuple(values[name][0] for name, _ in outputs),
                tuple(values[name][1] for name, _ in outputs))

    def configure(cycle):
        for side_index, side in enumerate(sides):
            controls[f"{side}_reset"] = int((cycle + side_index) % 9 == 0)
            for depth in depths:
                prefix = f"{side}_"
                # First partially initialize, read, then fully initialize each
                # word. Later mix all enable/mode combinations and collisions.
                phase = cycle % 8
                warmup = cycle < 24
                writing = phase in (0, 2, 4, 6) if warmup else (cycle + side_index) % 3 != 0
                shared_write = writing if warmup else (cycle // 2 + side_index) % 2
                controls[f"{prefix}read_enable_{depth}"] = int(not writing) if warmup else cycle % 2
                controls[f"{prefix}write_enable_{depth}"] = int(writing)
                controls[f"{prefix}shared_enable_{depth}"] = 1 if warmup else int(cycle % 5 != side_index)
                controls[f"{prefix}shared_write_{depth}"] = int(shared_write)
                address = (cycle // 2 + side_index) % depth
                controls[f"{prefix}write_address_{depth}"] = address
                controls[f"{prefix}read_address_{depth}"] = address if warmup else (cycle + side_index) % depth
                for kind, size, granule in shapes:
                    base = f"{side}_{kind}_{depth}"
                    full = (1 << size) - 1
                    controls[f"{base}_data"] = (0, full, 1 << (cycle % size), rng.getrandbits(size))[(cycle + side_index) % 4]
                    if granule:
                        count = size // granule
                        masks = (0, (1 << count) - 1, 1, 1 << (count - 1),
                                 sum(1 << i for i in range(0, count, 2)), rng.getrandbits(count))
                        controls[f"{base}_mask"] = masks[(cycle // 2 + side_index) % len(masks)] if not warmup else (1 if cycle < 8 else (1 << count) - 1)
                # Exercise arbitrary disabled addresses, including depth-one and
                # non-power-of-two invalid codes, without enabled invalid writes.
                if not controls[f"{prefix}write_enable_{depth}"] and not (controls[f"{prefix}shared_enable_{depth}"] and shared_write):
                    if cycle % 7 == 0:
                        controls[f"{prefix}write_address_{depth}"] = (1 << max(1, (depth - 1).bit_length())) - 1
                if not controls[f"{prefix}read_enable_{depth}"]:
                    controls[f"{prefix}read_address_{depth}"] = (1 << max(1, (depth - 1).bit_length())) - 1

    yield observe()
    for cycle in range(120):
        configure(cycle)
        yield observe()
        controls["first_clock"] = 1
        if cycle % 3 == 0:
            controls["second_clock"] = 1
        yield observe()
        # Change data, addresses, modes, and reset at steady-high clocks. Outputs
        # stay at the sampled transaction; the later second edge sees new inputs.
        configure(cycle + 121)
        yield observe()
        controls["second_clock"] = 1
        yield observe()
        controls["first_clock"] = 0
        yield observe()
        controls["second_clock"] = 0
        yield observe()

def simulate_sync_memory(work, name, source, width, verilator):
    """Isolate CIRCT-generated memory module names and check synthesis-mode behavior."""
    inputs, outputs = sync_memory_ports(width)
    lines = ["module tb;"]
    lines += [f"logic [{size-1}:0] {port} = '0;" for port, size in inputs]
    lines += [f"wire [{size-1}:0] {port};" for port, size in outputs]
    connections = [f".{port}({port})" for port, _ in inputs + outputs]
    lines.append(f"SyncMemories{width} dut({', '.join(connections)});")
    declarations, steps, count = vector_table(work, f"sync_memory{width}", inputs, outputs,
                                             sync_memory_cases(width), 0, masked=True)
    lines += declarations + ["initial begin"] + steps
    lines += ['$display("PASS synchronous memory"); $finish;', "end", "endmodule"]
    sv, tb = work / f"{name}.sv", work / f"{name}_tb.sv"
    sv.write_text(source)
    tb.write_text("\n".join(lines))
    build = work / f"{name}-build"
    run([verilator, "--binary", "--timing", "--assert", "-DSYNTHESIS", "--build-jobs", "2",
         "--top-module", "tb", "--Mdir", build, "-o", "sim", sv, tb], work / f"{name}-build.log")
    output = run([build / "sim"], work / f"{name}-run.log", timeout=60)
    samples = [line for line in output.splitlines() if line.startswith("sample ")]
    if len(samples) != count or "PASS synchronous memory" not in output:
        raise RuntimeError(f"incomplete synchronous-memory simulation; see {name}-run.log")
    print(f"{name}: {count} synchronous-memory observations passed", flush=True)
    return samples
