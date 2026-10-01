# Models edge state, partial values, indexed updates, and asynchronous memory behavior.
# SPDX-License-Identifier: Apache-2.0
import itertools
import random

SEQUENTIAL_INPUTS = ("clock", "reset", "enable", "data", "seed", "packet")
SEQUENTIAL_OUTPUTS = ("count", "p0", "p1", "packed", "lanes", "clock_echo", "reset_echo")


def sequential_cases(width):
    """Model both independent occurrences from pre-edge state, including definedness.

    Rows separate input setup from edges, then change inputs/reset while clocks
    stay high and finally lower clocks. Only a low-to-high transition can update
    state. Unknown startup values are masked until data or reset defines them.
    """
    mask = (1 << width) - 1
    packet_width = 2 * width + 3
    reset_packet = (5 << (2 * width)) | (1 << width) | mask
    sizes = (width, width, width, packet_width, 2 * width, 1, 1)
    states = [[None] * 5 for _ in range(2)]
    clocks = [0, 0]

    def observe(controls):
        stimulus, expected_values, known = [], [], []
        for index, (clock, reset, enable, data, seed, packet) in enumerate(controls):
            old_count, old_p0, _, old_packet, _ = states[index]
            if clock and not clocks[index]:
                count = seed if reset else ((old_count + 1) & mask if enable and old_count is not None else old_count)
                packed = reset_packet if reset else (packet if enable else old_packet)
                lanes = None if old_packet is None else old_packet & ((1 << (2 * width)) - 1)
                states[index] = [count, data, old_p0, packed, lanes]
            clocks[index] = clock
            stimulus.extend(controls[index])
            result = states[index] + [clock, reset]
            expected_values.extend(0 if value is None else value for value in result)
            known.extend(0 if value is None else (1 << size) - 1 for value, size in zip(result, sizes))
        return tuple(stimulus), tuple(expected_values), tuple(known)

    controls = [[0, 0, 0, 0, 0, 0] for _ in range(2)]
    yield observe(controls)
    rng = random.Random(20260930 + width)
    for cycle in range(224):
        # Long enabled runs wrap narrow counters. Near-maximum reset seeds also
        # exercise wide carry/wrap; reset occurs with enable both low and high.
        reset = cycle < 3 or cycle in (20, 21, 70) or (cycle >= 96 and rng.randrange(8) == 0)
        for index in range(2):
            enable = int(cycle % 7 != index)
            seed = (mask - index) & mask if cycle < 3 else rng.getrandbits(width)
            controls[index] = [0, int(reset), enable, rng.getrandbits(width),
                               seed, rng.getrandbits(packet_width)]
        yield observe(controls)  # setup/reset assertion between edges
        pulse = ((1, 0), (0, 1), (1, 1))[cycle % 3] if cycle < 96 else rng.choice(((1, 0), (0, 1), (1, 1)))
        for index in range(2):
            controls[index][0] = pulse[index]
        yield observe(controls)  # independent or simultaneous rising edges
        for control in controls:
            control[1] ^= 1
            control[2] ^= 1
            control[3:] = [rng.getrandbits(width), rng.getrandbits(width), rng.getrandbits(packet_width)]
        yield observe(controls)  # reset/data changes while clocks are steady
        for control in controls:
            control[0] = 0
        yield observe(controls)  # falling edges cannot update state
    assert all(value is not None for state in states for value in state)


DYNAMIC_LENGTHS = (1, 3, 4)


def dynamic_ports(width):
    """Mirror the fixture's public port order, retaining packed element widths."""
    inputs = [("clock", 1), ("reset", 1), ("enable", 1)]
    outputs = []
    for length in DYNAMIC_LENGTHS:
        index_width = max(1, (length - 1).bit_length())
        inputs += [(f"index_{length}", index_width), (f"read_index_{length}", index_width)]
        for kind, size in (("scalar", width), ("aggregate", 2 * width + 3)):
            prefix = f"{kind}_{length}"
            inputs += [(f"{prefix}_source", length * size), (f"{prefix}_element", size)]
            outputs += [(f"{prefix}_{name}", result_size) for name, result_size in
                        (("read", size), ("updated", length * size), ("after", size),
                         ("state", length * size), ("state_read", size))]
    return inputs, outputs


def dynamic_cases(width):
    """Track defined state per element; an invalid injection poisons the entire
    vector, while subsequent valid replacements can define individual elements.
    No expected value is assigned to an out-of-range read or injection result.
    """
    input_ports, _ = dynamic_ports(width)
    controls = {name: 0 for name, _ in input_ports}
    state = {(length, kind): [None] * length for length in DYNAMIC_LENGTHS
             for kind in ("scalar", "aggregate")}
    old_clock = 0

    def observe():
        nonlocal old_clock
        expected_values, masks = [], []

        def append(elements, size):
            value = mask = 0
            for index, element in enumerate(elements):
                if element is not None:
                    value |= element << (index * size)
                    mask |= ((1 << size) - 1) << (index * size)
            expected_values.append(value)
            masks.append(mask)

        def read(elements, index):
            return [elements[index] if index < len(elements) else None]

        for length in DYNAMIC_LENGTHS:
            index, read_index = controls[f"index_{length}"], controls[f"read_index_{length}"]
            for kind, size in (("scalar", width), ("aggregate", 2 * width + 3)):
                prefix = f"{kind}_{length}"
                source = [(controls[f"{prefix}_source"] >> (i * size)) & ((1 << size) - 1)
                          for i in range(length)]
                element = controls[f"{prefix}_element"]
                updated = source.copy() if index < length else [None] * length
                if index < length:
                    updated[index] = element
                stored = state[length, kind]
                if controls["clock"] and not old_clock:
                    if controls["reset"]:
                        stored = [0] * length
                    elif controls["enable"]:
                        stored = stored.copy() if index < length else [None] * length
                        if index < length:
                            stored[index] = element
                    state[length, kind] = stored
                append(read(source, index), size)
                append(updated, size)
                append(read(updated, read_index), size)
                append(stored, size)
                append(read(stored, read_index), size)
        old_clock = controls["clock"]
        return tuple(controls[name] for name, _ in input_ports), tuple(expected_values), tuple(masks)

    yield observe()
    rng = random.Random(20260929 + width)
    for cycle in range(96):
        controls.update(clock=0, reset=int(cycle in (0, 17, 33, 64, 95)), enable=int(cycle % 5 != 1))
        for length in DYNAMIC_LENGTHS:
            encodings = 1 << max(1, (length - 1).bit_length())
            controls[f"index_{length}"] = cycle % encodings
            controls[f"read_index_{length}"] = (cycle // encodings) % encodings
            for kind, size in (("scalar", width), ("aggregate", 2 * width + 3)):
                prefix = f"{kind}_{length}"
                controls[f"{prefix}_source"] = rng.getrandbits(length * size)
                controls[f"{prefix}_element"] = rng.getrandbits(size)
        yield observe()  # setup, including invalid encodings and reset assertion
        controls["clock"] = 1
        yield observe()  # state captures the dynamic replacement on this edge
        controls["reset"] ^= 1
        for length in DYNAMIC_LENGTHS:
            encodings = 1 << max(1, (length - 1).bit_length())
            controls[f"index_{length}"] = (controls[f"index_{length}"] + 1) % encodings
            for kind, size in (("scalar", width), ("aggregate", 2 * width + 3)):
                controls[f"{kind}_{length}_element"] = rng.getrandbits(size)
        yield observe()  # combinational outputs react; state must hold
        controls["clock"] = 0
        yield observe()  # falling edges must not update state
    assert all(element is not None for elements in state.values() for element in elements)


WRITE_CONFIGS = ((1, 1), (1, 3), (3, 2), (4, 3))


def write_set_ports(width):
    inputs = [("clock", 1), ("reset", 1)]
    outputs = []
    for length, ports in WRITE_CONFIGS:
        suffix = f"_{length}_{ports}"
        index_width = max(1, (length - 1).bit_length())
        inputs += [(f"enables{suffix}", ports), (f"indices{suffix}", ports * index_width)]
        for kind, size in (("scalar", width), ("aggregate", 2 * width + 3)):
            prefix = f"{kind}{suffix}"
            inputs += [(f"{prefix}_source", length * size), (f"{prefix}_elements", ports * size)]
            outputs += [(f"{prefix}_{name}", length * size)
                        for name in ("updated", "permuted", "state", "permuted_state")]
    return inputs, outputs


def write_set_controls(length, ports):
    """Enumerate every valid enable/index combination, retaining arbitrary
    disabled indices (including encodings outside non-power-of-two lengths).
    """
    index_width = max(1, (length - 1).bit_length())
    encodings = 1 << index_width
    rows = []
    for indices in itertools.product(range(encodings), repeat=ports):
        packed = sum(index << (p * index_width) for p, index in enumerate(indices))
        for enables in range(1 << ports):
            active = [index for p, index in enumerate(indices) if enables & (1 << p)]
            if all(index < length for index in active) and len(set(active)) == len(active):
                rows.append((enables, packed))
    return rows


def write_set_cases(width):
    """Model simultaneous replacements by address, independently of port order.
    The two emitted register chains share this oracle but have reversed ports.
    """
    input_ports, _ = write_set_ports(width)
    controls = {name: 0 for name, _ in input_ports}
    schedules = {config: write_set_controls(*config) for config in WRITE_CONFIGS}
    states = {(length, ports, kind): [None] * length for length, ports in WRITE_CONFIGS
              for kind in ("scalar", "aggregate")}
    old_clock = 0

    def observe():
        nonlocal old_clock
        expected_values, masks = [], []

        def append(elements, size):
            value = sum(element << (i * size) for i, element in enumerate(elements) if element is not None)
            mask = sum(((1 << size) - 1) << (i * size) for i, element in enumerate(elements) if element is not None)
            expected_values.append(value)
            masks.append(mask)

        for length, ports in WRITE_CONFIGS:
            suffix = f"_{length}_{ports}"
            index_width = max(1, (length - 1).bit_length())
            enables = controls[f"enables{suffix}"]
            indices = [(controls[f"indices{suffix}"] >> (p * index_width)) & ((1 << index_width) - 1)
                       for p in range(ports)]
            active = [p for p in range(ports) if enables & (1 << p)]
            assert all(indices[p] < length for p in active)
            assert len({indices[p] for p in active}) == len(active)
            for kind, size in (("scalar", width), ("aggregate", 2 * width + 3)):
                prefix = f"{kind}{suffix}"
                source = [(controls[f"{prefix}_source"] >> (i * size)) & ((1 << size) - 1)
                          for i in range(length)]
                replacements = {indices[p]: (controls[f"{prefix}_elements"] >> (p * size)) & ((1 << size) - 1)
                                for p in active}
                updated = [replacements.get(i, original) for i, original in enumerate(source)]
                state = states[length, ports, kind]
                if controls["clock"] and not old_clock:
                    state = ([0] * length if controls["reset"] else
                             [replacements.get(i, original) for i, original in enumerate(state)])
                    states[length, ports, kind] = state
                for result in (updated, updated, state, state):
                    append(result, size)
        old_clock = controls["clock"]
        return tuple(controls[name] for name, _ in input_ports), tuple(expected_values), tuple(masks)

    yield observe()
    rng = random.Random(20261001 + width)
    for cycle in range(max(len(rows) for rows in schedules.values())):
        controls.update(clock=0, reset=int(cycle % 61 == 0))
        for (length, ports), rows in schedules.items():
            suffix = f"_{length}_{ports}"
            enables, indices = rows[cycle % len(rows)]
            controls[f"enables{suffix}"], controls[f"indices{suffix}"] = enables, indices
            for kind, size in (("scalar", width), ("aggregate", 2 * width + 3)):
                prefix = f"{kind}{suffix}"
                controls[f"{prefix}_source"] = rng.getrandbits(length * size)
                controls[f"{prefix}_elements"] = rng.getrandbits(ports * size)
        yield observe()  # setup changes combinational values but cannot update state
        controls["clock"] = 1
        yield observe()  # all enabled disjoint writes take effect together
        controls["reset"] ^= 1
        for length, ports in WRITE_CONFIGS:
            for kind, size in (("scalar", width), ("aggregate", 2 * width + 3)):
                controls[f"{kind}_{length}_{ports}_elements"] = rng.getrandbits(ports * size)
        yield observe()  # high-clock data/reset changes must leave state alone
        controls["clock"] = 0
        yield observe()
    assert all(element is not None for elements in states.values() for element in elements)


def partial_ports(width):
    packet = 2 * width + 3
    inputs = [("hot", 3), ("code", 4)]
    inputs += [(f"data{i}", width) for i in range(3)]
    inputs += [(f"packet{i}", packet) for i in range(3)]
    outputs = list(zip(("scalar_choice", "packet_choice", "vector_choice", "single_choice",
                        "decoded", "reordered", "wide_decode", "empty_decode", "catch_all",
                        "raw_dc", "zero_dc", "ones_dc", "masked_dc", "masked_decode"),
                       (width, packet, 2 * width, width, packet, packet, 3 * (width + 1),
                        1, width, width, width, width, width, packet)))
    return inputs, outputs


def decode_expected(selector, rows, default):
    """Match a set of disjoint cubes, independent of table order and SV casez."""
    matches = [(value & care, care) for pattern, mask, value, care in rows
               if (selector & mask) == (pattern & mask)]
    assert len(matches) <= 1
    return matches[0] if matches else (default[0] & default[1], default[1])


def partial_cases(width):
    """Observe only specified bits; synthesis may choose all other bits freely."""
    packet = 2 * width + 3
    mask, packed_mask = (1 << width) - 1, (1 << packet) - 1
    wide_mask = (1 << (3 * (width + 1))) - 1
    care, high = packed_mask // 3, 1 << (packet - 1)
    rows = [(11, 12, packed_mask // 5, care), (7, 13, packed_mask, packed_mask), (0, 15, 0, 0)]
    wide_rows = [(0, high + 1, wide_mask, wide_mask),
                 (1, high + 1, wide_mask // 5, wide_mask // 3), (high, high + 1, 0, 0)]
    free = mask // 3
    forced = (mask ^ free) & (mask // 5)
    rng = random.Random(20261004 + width)
    scalar_samples = list(itertools.product(range(2), repeat=3)) if width == 1 else [
        (value, mask ^ value, (value + 1) & mask) for value in
        (range(32) if width == 5 else [0, mask] + [1 << bit for bit in range(width)])]
    packets = [0, 1, high, high + 1, packed_mask] + [1 << bit for bit in range(packet)]
    samples = [(scalars, (value, packed_mask ^ value, (value + 1) & packed_mask))
               for scalars, value in zip(itertools.cycle(scalar_samples), packets)]
    samples += [(scalars, tuple(rng.getrandbits(packet) for _ in range(3))) for scalars in scalar_samples]
    samples += [(tuple(rng.getrandbits(width) for _ in range(3)),
                 tuple(rng.getrandbits(packet) for _ in range(3))) for _ in range(64)]
    for scalars, payloads in samples:
        for code in range(16):
            decoded, dc = decode_expected(code, rows, (packed_mask // 7, packed_mask ^ care))
            wide, wc = decode_expected(payloads[0], wide_rows, (wide_mask // 7, wide_mask ^ (wide_mask // 3)))
            for choice in range(3):
                expected = (scalars[choice], payloads[choice], payloads[choice] & ((1 << (2 * width)) - 1),
                            scalars[0], decoded, decoded, wide, 1, free, 0, 0, mask, forced, decoded & care)
                masks = (mask, packed_mask, (1 << (2 * width)) - 1, mask, dc, dc, wc, 1, mask,
                         0, mask, mask, mask ^ free, dc | (packed_mask ^ care))
                yield ((1 << choice, code) + scalars + payloads, expected, masks)


def memory_ports(width):
    inputs, outputs = [], []
    for side in ("first", "second"):
        inputs += [(f"{side}_clock", 1), (f"{side}_reset", 1)]
        for depth in (1, 3, 4):
            address_width = max(1, (depth - 1).bit_length())
            inputs += [(f"{side}_{name}{port}_{depth}", size)
                       for name, size in (("read", address_width), ("address", address_width), ("enable", 1))
                       for port in range(2)]
            for kind, size in (("scalar", width), ("aggregate", 2 * width + 3)):
                prefix = f"{side}_{kind}_{depth}"
                inputs += [(f"{prefix}_data{port}", size) for port in range(2)]
                outputs += [(f"{prefix}_{name}", size) for name in ("read0", "read1", "sampled")]
        outputs.append((f"{side}_read_only_zero", width))
    return inputs, outputs


def memory_cases(width):
    """Track per-word definedness and pre-edge samples for independent memories.
    Only enabled, in-range, distinct writes are constrained by this oracle.
    """
    inputs, outputs = memory_ports(width)
    controls = {name: 0 for name, _ in inputs}
    sides, depths = ("first", "second"), (1, 3, 4)
    kinds = (("scalar", width), ("aggregate", 2 * width + 3))
    words = {(side, depth, kind): [None] * depth for side in sides for depth in depths for kind, _ in kinds}
    sampled = {key: None for key in words}
    previous_clocks = dict.fromkeys(sides, 0)
    rng = random.Random(20261005 + width)

    def observe():
        for side in sides:
            clock = controls[f"{side}_clock"]
            if clock and not previous_clocks[side]:
                for depth in depths:
                    # Build a simultaneous address map, not a port-priority chain.
                    active = [port for port in range(2) if controls[f"{side}_enable{port}_{depth}"]]
                    addresses = [controls[f"{side}_address{port}_{depth}"] for port in active]
                    assert all(address < depth for address in addresses)
                    assert len(set(addresses)) == len(addresses)
                    for kind, _ in kinds:
                        key = side, depth, kind
                        sampled[key] = 0 if controls[f"{side}_reset"] else words[key][controls[f"{side}_read0_{depth}"]]
                        updates = {address: controls[f"{side}_{kind}_{depth}_data{port}"]
                                   for port, address in zip(active, addresses)}
                        words[key] = [updates.get(address, old) for address, old in enumerate(words[key])]
            previous_clocks[side] = clock
        values = {}
        for side in sides:
            for depth in depths:
                for kind, _ in kinds:
                    key = side, depth, kind
                    prefix = f"{side}_{kind}_{depth}"
                    for port in range(2):
                        values[f"{prefix}_read{port}"] = words[key][controls[f"{side}_read{port}_{depth}"]]
                    values[f"{prefix}_sampled"] = sampled[key]
            values[f"{side}_read_only_zero"] = 0
        expected = tuple(values[name] if values[name] is not None else 0 for name, _ in outputs)
        masks = tuple((1 << size) - 1 if values[name] is not None else 0 for name, size in outputs)
        return tuple(controls[name] for name, _ in inputs), expected, masks

    def data_for(cycle):
        for side_index, side in enumerate(sides):
            for depth in depths:
                for kind, size in kinds:
                    mask = (1 << size) - 1
                    for port in range(2):
                        choices = (0, mask, 1 << ((cycle + port) % size), rng.getrandbits(size))
                        controls[f"{side}_{kind}_{depth}_data{port}"] = choices[(cycle + port + side_index) % 4]

    yield observe()  # all storage starts unconstrained
    # Define every word through writes while reset is asserted. Read/write the
    # same address and later capture it without reset to exercise NBA ordering.
    for cycle in range(8):
        data_for(cycle)
        for side in sides:
            controls[f"{side}_reset"] = int(cycle < 4)
            for depth in depths:
                controls[f"{side}_enable0_{depth}"] = 1
                controls[f"{side}_address0_{depth}"] = cycle % depth
                controls[f"{side}_read0_{depth}"] = cycle % depth
                controls[f"{side}_read1_{depth}"] = (cycle + 1) % depth
        yield observe()
        for side in sides: controls[f"{side}_clock"] = 1
        yield observe()
        for side in sides: controls[f"{side}_clock"] = 0
        yield observe()
    assert all(value is not None for memory in words.values() for value in memory)
    schedules = {depth: write_set_controls(depth, 2) for depth in depths}
    for cycle in range(2 * max(map(len, schedules.values()))):
        data_for(cycle)
        for side_index, side in enumerate(sides):
            controls[f"{side}_reset"] = int((cycle + side_index) % 5 == 0)
            for depth in depths:
                enables, addresses = schedules[depth][(cycle + side_index) % len(schedules[depth])]
                address_width = max(1, (depth - 1).bit_length())
                for port in range(2):
                    controls[f"{side}_enable{port}_{depth}"] = (enables >> port) & 1
                    controls[f"{side}_address{port}_{depth}"] = (addresses >> (port * address_width)) & ((1 << address_width) - 1)
                    controls[f"{side}_read{port}_{depth}"] = (cycle + port + side_index) % depth
        yield observe()
        controls["first_clock"] = 1
        if cycle % 4 == 0: controls["second_clock"] = 1
        yield observe()
        # Read addresses change while clocks are high; live reads respond and
        # sampled registers hold. Data/reset changes must not create writes.
        data_for(cycle + 1)
        controls["first_reset"] ^= 1
        for address in range(4):
            for side in sides:
                for depth in depths:
                    controls[f"{side}_read0_{depth}"] = address % depth
                    controls[f"{side}_read1_{depth}"] = (address + 1) % depth
            yield observe()
        controls["second_clock"] = 1
        yield observe()
        controls["first_clock"] = 0
        yield observe()
        controls["second_clock"] = 0
        yield observe()

def cdc_ports():
    inputs = [(f"{side}_{name}", 1) for side in ("first", "second")
              for name in ("clock", "source2", "source3", "data")]
    outputs = [(f"{side}_{name}", 1) for side in ("first", "second")
               for name in ("sync2", "sync3", "ordinary")]
    return inputs, outputs


def cdc_cases():
    """Model only digital edge behavior; no metastability timing is simulated."""
    inputs, outputs = cdc_ports()
    controls = dict.fromkeys((name for name, _ in inputs), 0)
    previous = {side: 0 for side in ("first", "second")}
    stages = {(side, name): [None] * length for side in previous
              for name, length in (("sync2", 2), ("sync3", 3), ("ordinary", 1))}

    def observe():
        for side in previous:
            clock = controls[f"{side}_clock"]
            if clock and not previous[side]:
                for name, source in (("sync2", "source2"), ("sync3", "source3"), ("ordinary", "data")):
                    key = side, name
                    stages[key] = [controls[f"{side}_{source}"]] + stages[key][:-1]
            previous[side] = clock
        results = [stages[side, name][-1] for side in previous for name in ("sync2", "sync3", "ordinary")]
        return (tuple(controls[name] for name, _ in inputs),
                tuple(0 if value is None else value for value in results),
                tuple(int(value is not None) for value in results))

    yield observe()
    for cycle in range(96):
        # Sources remain stable for several destination edges; different periods
        # and opposite levels distinguish the chains and the two occurrences.
        for index, side in enumerate(previous):
            controls[f"{side}_source2"] = ((cycle // 4) + index) & 1
            controls[f"{side}_source3"] = ((cycle // 6) + index) & 1
            controls[f"{side}_data"] = (cycle + index) & 1
        yield observe()
        controls["first_clock"] = 1
        if cycle % 3 == 0: controls["second_clock"] = 1
        yield observe()
        # Ordinary data toggles at steady high; all stored values must hold.
        controls["first_data"] ^= 1
        controls["second_data"] ^= 1
        yield observe()
        controls["second_clock"] = 1
        yield observe()
        controls["first_clock"] = 0
        yield observe()
        controls["second_clock"] = 0
        yield observe()
    assert all(value is not None for chain in stages.values() for value in chain)
