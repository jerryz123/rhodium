# SPDX-License-Identifier: Apache-2.0
"""Field-level aggregate state oracle and typed/native versus packed/SV adapters."""

PACKETS = ('echo', 'built', 'picked', 'first_left', 'first_right', 'second_left', 'second_right',
           'mux_record', 'mux_other', 'mux_vector', 'mux_constant', 'mux_chain', 'mux_captured')
ARRAYS = (('gather', 8), ('reverse_gather', 8), ('stride_gather', 4),
          ('mixed_gather', 9), ('fill', 8), ('saved_gather', 8))


def encoded(name):
    return 'p' + name.replace('_', '_u')


def length(width):
    return 1 if width == 1 else 3 if width == 5 else 4


def packet_fields(width):
    # The fixture's record is {payload, flag, single}. Vector indices count
    # upward from the low end of payload; each element is {class, __class__}.
    fields = []
    for index in range(length(width)):
        low = width + 1 + index * (width + 5)
        fields.extend([(f'.ppayload[{index}].pclass', width, low + 5),
                       (f'.ppayload[{index}].p_u_uclass_u_u', 5, low)])
    return fields + [('.pflag', 1, width), ('.psingle[0]', width, 0)]


def ports(width):
    size = length(width) * (width + 5) + 1 + width
    result = [(name, size, packet_fields(width)) for name in PACKETS]
    result += [(name, 3*size,
                [(f'[{index}]{path}', bits, index*size+low)
                 for index in range(3) for path, bits, low in packet_fields(width)])
               for name in ('constructed', 'saved_vector')]
    result += [(name, count*(width+5),
                [(f'[{index}]{path}', bits, index*(width+5)+low)
                 for index in range(count) for path, bits, low in
                 [('.pclass', width, 5), ('.p_u_uclass_u_u', 5, 0)]])
               for name, count in ARRAYS]
    result += [('scalar_gather', 8*width, [(f'[{i}]', width, i*width) for i in range(8)])]
    result += [('scalar_stride', 4*width, [(f'[{i}]', width, i*width) for i in range(4)])]
    result += [('mux_whole', 2*size,
                [(f'.pa{path}', bits, low+size) for path, bits, low in packet_fields(width)] +
                [(f'.pb{path}', bits, low) for path, bits, low in packet_fields(width)])]
    sample = [(path.removeprefix('.ppayload'), bits, low - width - 1)
              for path, bits, low in packet_fields(width)[:-2]]
    result += [('sample', length(width) * (width + 5), sample),
               ('unpacked', 64, [('.phead', 1, 63)] + [(f'.planes[{i}]', 21, 21*i) for i in range(3)]),
               ('roundtrip', 64, [('', 64, 0)]), ('repacked', 64, [('', 64, 0)]),
               ('halves', 64, [('.phigh', 32, 32), ('.plow', 32, 0)]),
               ('cast_lane', 32, [('.pdata', 27, 5), ('.ptag', 5, 0)])]
    return result


OUTPUT_COUNT = sum(len(fields) for _, _, fields in ports(64))


def input_expressions(width, a, b):
    result = []
    for index in range(length(width)):
        result += [f'({a} + {index})', f'({b} ^ {3 * index})']
    return result + [b, f'({a} ^ {b})']


def native_setup(width):
    lines = [f'static rsim_pAggregate{width}::Model agg, idle_agg;']
    for port, a, b in [('a', 'a', 'b'), ('b', 'b', 'a')]:
        for (path, _, _), value in zip(packet_fields(width), input_expressions(width, a, b)):
            lines.append(f'agg.inputs.p{port}{path} = {value};')
    lines += ['agg.inputs.preset = reset; agg.inputs.penable = enable; agg.inputs.pload = load;',
              'agg.inputs.ppacked = amount;', 'if (tick) agg.tick(); else agg.eval();', 'idle_agg.eval();']
    # Scratch pointers are not model state: copies must refresh them before use,
    # even when the source of the copy has already been destroyed. Alternate
    # construction and assignment using the load stimulus; both paths recur at
    # both state-bank parities without adding another set of large model locals.
    lines += [f'auto copied = [&]() {{ auto temporary = agg; temporary.eval(); '
              f'if (load) {{ rsim_pAggregate{width}::Model result; result = temporary; return result; }} '
              f'return rsim_pAggregate{width}::Model(temporary); }}();', 'copied.eval();']
    # Populate pre-edge borrowed bindings before moving out of a destroyed
    # source. Re-evaluation and another edge must read the destination's state.
    lines += ['auto advanced = agg; advanced.tick();',
              f'auto moved = [&]() {{ auto temporary = agg; temporary.tick(); '
              f'if (load) {{ rsim_pAggregate{width}::Model result; result = static_cast<rsim_pAggregate{width}::Model&&>(temporary); return result; }} '
              f'return rsim_pAggregate{width}::Model(static_cast<rsim_pAggregate{width}::Model&&>(temporary)); }}();',
              'moved.eval();']
    for port, _, fields in ports(width):
        for path, _, _ in fields:
            lines.append(f'if (copied.outputs().{encoded(port)}{path} != '
                         f'agg.outputs().{encoded(port)}{path}) return 5;')
            lines.append(f'if (moved.outputs().{encoded(port)}{path} != '
                         f'advanced.outputs().{encoded(port)}{path}) return 6;')
        if port.startswith(('first_', 'second_')) or port in ('sample', 'mux_captured', 'saved_vector', 'saved_gather'):
            for path, _, _ in fields:
                lines.append(f'if (idle_agg.outputs().{encoded(port)}{path} != 0) return 4;')
    lines += ['moved.tick(); advanced.tick();']
    for port, _, fields in ports(width):
        for path, _, _ in fields:
            lines.append(f'if (moved.outputs().{encoded(port)}{path} != '
                         f'advanced.outputs().{encoded(port)}{path}) return 7;')
    return '\n'.join(lines)


def native_outputs(width):
    expressions = [f'agg.outputs().{encoded(port)}{path}' for port, _, fields in ports(width) for path, _, _ in fields]
    return expressions + ['UINT64_C(0)'] * (OUTPUT_COUNT - len(expressions))


def reference(width):
    size = length(width) * (width + 5) + 1 + width
    lines, connections = [], []
    for port, a, b in [('a', 'a', 'b'), ('b', 'b', 'a')]:
        net = f'agg{width}_{port}'
        lines.append(f'wire [{size-1}:0] {net};')
        connections.append(f'.{port}({net})')
        for (_, bits, low), value in zip(packet_fields(width), input_expressions(width, a, b)):
            lines.append(f'assign {net}[{low} +: {bits}] = {bits}\'({value});')
    assignments = []
    for port, bits, fields in ports(width):
        net = f'agg{width}_{port}'
        lines.append(f'wire [{bits-1}:0] {net};')
        connections.append(f'.{port}({net})')
        for _, count, low in fields:
            slot = len(assignments)
            assignments.append(f'aggregates[{slot*64} +: 64] = 64\'({net}[{low} +: {count}]);')
    lines.append(f'Aggregate{width} agg{width}(.clock(clock && width == 8\'d{width}), .reset(reset), '
                 f'.enable(enable), .load(load), .\\packed (amount), {", ".join(connections)});')
    return '\n'.join(lines), ' '.join(assignments)


def input_packet(width, a, b):
    mask = (1 << width) - 1
    return (tuple(((a + i) & mask, (b ^ (3*i)) & 31) for i in range(length(width))),
            b & 1, (a ^ b) & mask)


def initial(width, seed):
    mask = (1 << width) - 1
    return (tuple(((seed + i) & mask, (seed + i) & 31) for i in range(length(width))),
            seed & 1, seed & mask)


def flatten(packet):
    payload, flag, single = packet
    return [value for pair in payload for value in pair] + [flag, single]


class Oracle:
    def __init__(self, width):
        self.width = width
        zero = (tuple((0, 0) for _ in range(length(width))), 0, 0)
        self.left = self.right = self.other_left = self.other_right = zero
        self.captured = zero
        self.saved_vector = [zero, zero, zero]
        self.saved_gather = [(0, 0)] * 8
        self.sample = zero[0]

    def observe(self, tick, reset, enable, load, a, b, packed):
        width = self.width
        pa, pb = input_packet(width, a, b), input_packet(width, b, a)
        if tick:
            self.sample = pa[0]
            if reset:
                self.left = self.other_left = initial(width, 3)
                self.right = self.other_right = initial(width, 7)
            elif load:
                self.left, self.right, self.other_left, self.other_right = pa, pb, pb, pa
            elif enable:
                self.left, self.right = self.right, self.left
                self.other_left, self.other_right = self.other_right, self.other_left
        mask = (1 << width) - 1
        built = (tuple(((data + 1) & mask, tag ^ 31) for data, tag in reversed(pa[0])),
                 pa[1] ^ 1, pa[0][0][0])
        constant_mux = initial(width, 11) if enable else pa
        chain = (constant_mux, pb, initial(width, 19), built)[2 * enable + load]
        if tick:
            self.saved_gather = ([initial(width, i)[0][0] for i in range(8)] if reset else
                                 [initial(width, 21+i)[0][0] if load else (pa[0][0] if i % 2 == 0 else self.captured[0][0])
                                  for i in range(8)])
            self.saved_vector = ([initial(width, seed) for seed in (3, 7, 11)] if reset
                                 else [chain, self.captured, chain])
            self.captured = initial(width, 3) if reset else chain
        packets = [pa, built, pb if load else pa, self.left, self.right, self.other_left, self.other_right,
                   pb if enable else pa, pa if enable else pb, pb if load and enable else pa,
                   constant_mux, chain, self.captured]
        result = [value for packet in packets for value in flatten(packet)]
        result += [value for packet in [chain, self.captured, chain] + self.saved_vector
                   for value in flatten(packet)]
        gather = [initial(width, 21+i)[0][0] if load else (pa[0][0] if i % 2 == 0 else self.captured[0][0])
                  for i in range(8)]
        arrays = [gather, gather[::-1], gather[::2],
                  [gather[i] for i in (3, 4, 5, 6, 0, 6, 6, 6, 6)], [chain[0][0]]*8, self.saved_gather]
        result += [value for array in arrays for pair in array for value in pair]
        result += [((a & mask) + i) & mask for i in range(8)]
        result += [((a & mask) + 2*i) & mask for i in range(4)]
        result += flatten(pb if enable else pa) + flatten(pa)
        result += [value for pair in self.sample for value in pair]
        # Decode with integer division, independently of emitted C++ shifts.
        head = packed // (1 << 63)
        lanes = [(packed // (1 << (21*i))) % (1 << 21) for i in range(3)]
        repacked = head * (1 << 63) + sum(value * (1 << (21*i)) for i, value in enumerate(reversed(lanes)))
        result += [head, *lanes, packed, repacked, packed >> 32, packed & ((1 << 32) - 1)]
        result += [packed // (1 << 37), (packed // (1 << 32)) % 32]
        return result + [0] * (OUTPUT_COUNT - len(result))
