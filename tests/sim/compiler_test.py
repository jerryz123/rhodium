#!/usr/bin/env python3
# Checks compiler interchange, packed guards, selected state updates, and wide arithmetic.
import copy
import json
import os
from pathlib import Path
import random
import subprocess
import sys
from runtime_test import Native, Options, library

OPCODES = ['constant', 'copy', 'not', 'and', 'or', 'xor', 'add', 'sub', 'mul',
           'shl', 'shru', 'shrs', 'eq', 'ult', 'slt', 'mux_lookup', 'onehot_mux',
           'extract', 'zext', 'sext', 'pack', 'vector_index', 'vector_inject',
           'vector_write_set', 'memory_read_async', 'decode', 'set_clear', 'balance',
           'counter_step', 'object_query', 'alu', 'byte_merge']


def check_regroup(compile_model, base, directory, lib, rng):
    """Compare packed regions and surviving scalar users against independent masks."""
    for width in [2, 3, 6, 7, 8, 11, 16, 32, 64]:
        model = copy.deepcopy(base)
        model.update(values=[], operations=[], ports=[])

        def source(bits, name):
            out = len(model['values'])
            model['values'].append(bits)
            model['ports'].append([0, out, name])
            return out

        def op(code, bits, args=(), imm=()):
            out = len(model['values'])
            model['values'].append(bits)
            model['operations'].append([code, out, list(args), list(imm)])
            model['origins'].append([out, 'top', out, f'v{out}', OPCODES[code], 0, bits])
            return out

        def output(value, name):
            model['ports'].append([1, value, name])

        target = source(width + 5, 'target')
        fallback = source(width, 'fallback')
        valid = source(1, 'valid')
        choose = source(1, 'choose')
        available = [source(1, f'avail_{i}') for i in range(width)]
        gated, adaptive, escapes = [], [], []
        for i in range(width):
            t = op(17, 1, [target], [i + 3])
            f = op(17, 1, [fallback], [i])
            # Alternate operand order to exercise common-guard alignment.
            request = op(3, 1, [valid, t] if i % 2 else [t, valid])
            escape = op(3, 1, [f, valid])
            ready = op(3, 1, [op(3, 1, [request, op(2, 1, [escape])]), available[i]])
            gated.append(request)
            adaptive.append(ready)
            escapes.append(escape)
        output(op(20, width, gated), 'gated')
        packed = op(20, width, adaptive)
        empty = op(12, 1, [packed, op(0, width, imm=[0])])
        eligible = [op(15, 1, [empty, adaptive[i], escapes[i]], [1]) for i in range(width)]
        output(op(20, width, eligible), 'eligible')
        merged = [op(3, 1, [op(4, 1, [gated[i], escapes[i]]),
                            op(2, 1, [adaptive[i]])]) for i in range(width)]
        output(op(20, width, [op(5, 1, [merged[i], eligible[i]])
                              for i in range(width)]), 'merged_xor')
        # Shared selectors with both lookup key conventions must preserve priority.
        for key in [0, 1]:
            selected = [op(15, 1, [choose, gated[i], eligible[i]], [key]) for i in range(width)]
            output(op(20, width, selected), f'selected_{key}')
        output(op(20, width, list(reversed(gated))), 'reversed')
        output(op(20, width, [valid] * width), 'broadcast')
        for i in sorted({0, width // 2, width - 1}):
            output(adaptive[i], f'bit_{i}')
        # Regrouping must preserve current/next-state and both bank parities.
        q = len(model['values'])
        model['values'].append(width)
        reset = op(0, 1, imm=[0])
        init = op(0, width, imm=[0])
        model['registers'] = [[q, packed, reset, init]]
        output(q, 'previous')
        binary = compile_model(model, f'regroup-{width}')
        reference = compile_model(model, f'regroup-reference-{width}', '--no-regroup')
        optimized = json.loads((directory / f'regroup-{width}.optimized.json').read_text())
        original = json.loads((directory / f'regroup-reference-{width}.optimized.json').read_text())
        outputs = {name: value for direction, value, name in optimized['ports'] if direction == 1}
        for direction, value, name in model['ports']:
            if direction == 1 and value != q:
                assert any(row[0] == outputs[name] and row[3] == f'v{value}'
                           for row in optimized['origins']), (width, name, 'lost origin')
        if width >= 6:
            assert len(optimized['operations']) < len(original['operations']), width
        for compiled in [False, True]:
            lib.compiled, lib.compiled_cache = compiled, {}
            lib.release, lib.shape_limit, lib.options = True, 512, Options(1, 69648)
            sim = Native(lib, binary)
            lib.compiled = False
            baseline = Native(lib, reference)
            previous = 0
            for trial in range(80):
                t, f = rng.getrandbits(width + 5), rng.getrandbits(width)
                v, c = trial % 2, (trial // 2) % 2
                avail = rng.getrandbits(width)
                for instance in [sim, baseline]:
                    for name, value in [('target', t), ('fallback', f), ('valid', v), ('choose', c)]:
                        instance.set(name, value)
                    for i in range(width):
                        instance.set(f'avail_{i}', (avail >> i) & 1)
                    instance.eval()
                mask = (1 << width) - 1
                gate = ((t >> 3) & mask) if v else 0
                ready = gate & ~f & avail
                eligible_value = ready if ready else (f if v else 0)
                expected = dict(gated=gate, eligible=eligible_value, previous=previous,
                                broadcast=mask if v else 0,
                                reversed=int(f'{gate:0{width}b}'[::-1], 2))
                expected['merged_xor'] = ((gate | (f if v else 0)) & ~ready) ^ eligible_value
                for key in [0, 1]:
                    expected[f'selected_{key}'] = eligible_value if c == key else gate
                for i in sorted({0, width // 2, width - 1}):
                    expected[f'bit_{i}'] = (ready >> i) & 1
                for name, value in expected.items():
                    assert sim.get(name) == baseline.get(name) == value, (width, compiled, trial, name)
                sim.advance()
                baseline.advance()
                previous = ready
            sim.close()
            baseline.close()

    # An asymmetric lane depends on an intermediate in its sibling. Redirecting
    # every scalar blindly would turn a legal DAG into a cycle.
    model = copy.deepcopy(base)
    model.update(values=[1] * 4, operations=[], ports=[[0, i, f'in_{i}'] for i in range(4)])
    def add(code, bits, args, imm=()):
        out = len(model['values'])
        model['values'].append(bits)
        model['operations'].append([code, out, list(args), list(imm)])
        return out
    chains = []
    for seed in [0, 1]:
        chain = [seed]
        for _ in range(8):
            chain.append(add(2, 1, [chain[-1]]))
        chains.append(chain)
    # The second lane's base depends on an interior node of the first lane.
    model['operations'][8][2] = [add(5, 1, [chains[0][4], 2])]
    # Restore input topological order after deliberately constructing the cross edge.
    model['operations'].insert(8, model['operations'].pop())
    root = add(20, 2, [chains[0][-1], chains[1][-1]])
    model['ports'].append([1, root, 'out'])
    binary = compile_model(model, 'regroup-dependent-lanes')
    lib.compiled, lib.options = False, Options(1, 0)
    sim = Native(lib, binary)
    for value in range(16):
        for i in range(4):
            sim.set(f'in_{i}', (value >> i) & 1)
        sim.eval()
        x, z = value & 1, (value >> 2) & 1
        assert sim.get('out') == x | ((x ^ z) << 1)
    sim.close()
    lib.compiled, lib.options = False, Options(1, 0)


def check_release(compile_model, base, directory, lib):
    """Release removes diagnostic cones while preserving reset and state updates."""
    m = copy.deepcopy(base)
    m.update(values=[8, 1, 8, 8, 8, 1, 1, 16, 8, 8, 1, 1],
             operations=[[0, 3, [], [1]], [6, 4, [2, 0], []],
                         [21, 9, [7, 8], [2, 8]], [12, 10, [9, 3], []],
                         [3, 11, [10, 5], []]],
             ports=[[0, 0, 'amount'], [0, 1, 'reset'], [1, 2, 'q'],
                    [0, 5, 'okay'], [0, 6, 'guard'], [0, 7, 'vector'], [0, 8, 'index']],
             registers=[[2, 4, 1, 3]], assertions=[[11, 1, 6, 'required_condition']])
    lib.compiled_cache, lib.shape_limit, lib.release = {}, 512, False
    debug = compile_model(m, 'release-debug')
    for extra in [(), ('--no-optimize',)]:
        stem = 'release-pruned' + ('-raw' if extra else '')
        release = compile_model(m, stem, '--release', *extra)
        report = json.loads((directory / (stem + '.optimized.json')).read_text())
        assert report['assertions'] == []
        assert all(op[0] != 21 for op in report['operations'])
        for compiled in [False, True]:
            lib.compiled = compiled
            a, b = Native(lib, debug), Native(lib, release)
            expected = 0
            for cycle in range(25):
                reset = cycle in [0, 11]
                for sim in [a, b]:
                    for name, value in [('amount', cycle + 1), ('reset', reset),
                                        ('okay', 1), ('guard', 1), ('vector', 0x0101), ('index', cycle & 1)]:
                        sim.set(name, value)
                    sim.advance()
                    sim.eval()
                expected = 1 if reset else (expected + cycle + 1) & 255
                assert a.get('q') == b.get('q') == expected
            a.set('okay', 0)
            assert lib.rds_advance(a.ptr) != 0
            a.eval()
            assert a.get('q') == expected
            b.set('okay', 0)
            b.advance()
            b.eval()
            assert b.get('q') == (expected + 25) & 255
            a.close()
            b.close()
    lib.compiled = False


def check_selected_updates(compile_model, base, directory, lib, rng):
    """Priority, holds, resets, and surviving candidate users across both banks."""
    for length, ew in [(1, 1), (32, 1), (8, 64), (4, 128), (3, 65)]:
        for key in [0, 1]:
            m = copy.deepcopy(base)
            m.update(values=[], operations=[], ports=[])
            def value(width, name=None, code=None, args=(), imm=()):
                out = len(m['values'])
                m['values'].append(width)
                if name:
                    m['ports'].append([0, out, name])
                if code is not None:
                    m['operations'].append([code, out, list(args), list(imm)])
                return out
            width, iw = length * ew, max(1, (length - 1).bit_length())
            q = value(width)
            reset, clear, first, second, enabled = [value(1, n) for n in
                                                  ['reset', 'clear', 'first', 'second', 'enabled']]
            ix, iy = value(iw, 'ix'), value(iw, 'iy')
            x, y = value(ew, 'x'), value(ew, 'y')
            zero = value(width, code=0, imm=[0] * ((width + 63) // 64))
            one = value(1, code=0, imm=[1])
            a = value(width, code=23, args=[q, enabled, ix, x], imm=[length, ew, 1, iw])
            b = value(width, code=23, args=[q, one, iy, y], imm=[length, ew, 1, iw])
            def mux(sel, yes, no):
                return value(width, code=15, args=[sel, no, yes] if key else [sel, yes, no], imm=[key])
            nxt = mux(clear, zero, mux(first, a, mux(second, b, q)))
            m['registers'] = [[q, nxt, reset, zero]]
            m['origins'] = [[nxt, 'top', nxt, 'selected_update', 'mux_lookup', 0, width]]
            m['ports'] += [[1, q, 'q'], [1, a, 'candidate']]
            stem = f'selected-update-{length}-{ew}-{key}'
            fused = compile_model(m, stem, '--release')
            original = compile_model(m, stem + '-original', '--release', '--no-update-fusion')
            report = json.loads((directory / (stem + '.optimized.json')).read_text())
            assert any(o[0] == report['registers'][0][1] and o[3] == 'selected_update'
                       for o in report['origins'])
            for compiled, release, workers, flags in [(False, False, 1, 0), (True, False, 1, 69648),
                                                      (True, True, 1, 200720), (True, True, 4, 65792)]:
                lib.compiled_cache, lib.shape_limit = {}, 512
                lib.compiled, lib.release, lib.options = compiled, release, Options(workers, flags)
                sim = Native(lib, fused)
                lib.compiled, lib.options = False, Options(1, 0)
                baseline = Native(lib, original)
                state = 0
                for cycle in range(96):
                    inputs = dict(reset=int(cycle in [0, 17, 40]), clear=int(cycle % 19 == 0),
                                  first=cycle & 1, second=(cycle >> 1) & 1, enabled=(cycle >> 2) & 1,
                                  ix=rng.randrange(length), iy=rng.randrange(length),
                                  x=rng.getrandbits(ew), y=rng.getrandbits(ew))
                    def update(index, data):
                        mask = ((1 << ew) - 1) << (index * ew)
                        return (state & ~mask) | (data << (index * ew))
                    candidate = update(inputs['ix'], inputs['x']) if inputs['enabled'] else state
                    for instance in [sim, baseline]:
                        for name, v in inputs.items():
                            instance.set(name, v)
                        instance.eval()
                        assert instance.get('q') == state, (stem, cycle, 'q')
                        assert instance.get('candidate') == candidate, (stem, cycle, 'candidate')
                        instance.advance()
                    if inputs['reset'] or inputs['clear']:
                        state = 0
                    elif inputs['first']:
                        state = candidate
                    elif inputs['second']:
                        state = update(inputs['iy'], inputs['y'])
                sim.close()
                baseline.close()
    # A failed direct update may dirty next-state but must never publish it.
    m = copy.deepcopy(base)
    m.update(values=[195, 2, 65, 1, 195],
             operations=[[23, 4, [0, 3, 1, 2], [3, 65, 1, 2]]],
             registers=[[0, 4, 4294967295, 4294967295]],
             ports=[[1, 0, 'q'], [0, 1, 'index'], [0, 2, 'data'], [0, 3, 'enable']])
    binary = compile_model(m, 'direct-update-invalid', '--no-optimize')
    lib.compiled, lib.release, lib.options = True, False, Options(1, 69648)
    sim = Native(lib, binary)
    for name, v in [('index', 0), ('data', (1 << 65) - 1), ('enable', 1)]:
        sim.set(name, v)
    sim.advance()
    lib.rds_set_strict(sim.ptr, 1)
    sim.set('index', 3)
    assert lib.rds_eval(sim.ptr) != 0
    assert b'vector write index out of range' in lib.rds_error(sim.ptr)
    sim.set('enable', 0)
    sim.eval()
    assert sim.get('q') == (1 << 65) - 1
    sim.advance()
    sim.eval()
    assert sim.get('q') == (1 << 65) - 1
    sim.close()
    lib.compiled, lib.release, lib.options = False, False, Options(1, 0)


def check_masked_sram(compile_model, base, lib, rng):
    """Check sparse writes above one mask word and whole-row/word lanes."""
    for width, granule in [(64, 8), (128, 8), (192, 8), (320, 8), (576, 8), (1024, 8), (256, 64), (53, 53)]:
        lanes = width // granule
        m = copy.deepcopy(base)
        m.update(values=[2, width, 1, lanes, width],
                 operations=[[24, 4, [0], [0]]], memories=[[width, 4]],
                 writes=[[0, 0, 1, 2, 3, granule]],
                 ports=[[0, 0, 'address'], [0, 1, 'data'], [0, 2, 'enable'],
                        [0, 3, 'mask'], [1, 4, 'old']])
        binary = compile_model(m, f'masked-sram-{width}-{granule}', '--release')
        for release in [False, True]:
            lib.compiled, lib.release, lib.options = True, release, Options(1, 200720)
            lib.compiled_cache, lib.shape_limit = {}, 512
            sim = Native(lib, binary)
            contents = [0] * 4
            for cycle in range(160):
                address, data, enabled = rng.randrange(4), rng.getrandbits(width), cycle % 5 != 0
                mask = [0, (1 << lanes) - 1, 1 << (cycle % lanes), rng.getrandbits(lanes)][cycle % 4]
                for name, val in [('address', address), ('data', data), ('enable', enabled), ('mask', mask)]:
                    sim.set(name, val)
                sim.eval()
                assert sim.get('old') == contents[address]
                sim.advance()
                if enabled:
                    bits = sum(((1 << granule) - 1) << (i * granule) for i in range(lanes) if mask >> i & 1)
                    contents[address] = (contents[address] & ~bits) | (data & bits)
                sim.eval()
                assert sim.get('old') == contents[address], (width, granule, release, cycle)
            sim.close()
    lib.compiled, lib.release, lib.options = False, False, Options(1, 0)


def main():
    directory = Path(sys.argv[1])
    compiler = os.environ['RDS_OPTIMIZER']
    lib = library(directory / 'librhodium_sim.so')
    lib.options = Options(1, 0)
    rng = random.Random(19472)
    base = dict(format='rhodium-simulation-ir-v1', opcodes=OPCODES,
                values=[32, 32], operations=[], ports=[[0, 0, 'a'], [1, 1, 'out']],
                registers=[], memories=[], writes=[], reads=[], assertions=[], objects=[],
                origins=[], inventory=[], occurrences=['top'], replicated_bytes=0, replicated_work=0)
    base['operations'] = [[1, 1, [0], []]]

    def compile_model(model, stem, *extra, valid=True):
        source = directory / (stem + '.json')
        source.write_text(json.dumps(model))
        command = [compiler, '--input', str(source), '--output', str(source.with_suffix('.rsim')),
                   '--report', str(directory / (stem + '.optimized.json')), *extra]
        result = subprocess.run(command, text=True, capture_output=True)
        assert result.returncode == (0 if valid else 1), (command, result.returncode, result.stderr)
        if not valid:
            assert 'rhodium-opt:' in result.stderr
        return source.with_suffix('.rsim')

    if (directory / 'compiler-extracted.json').exists():
        extracted = json.loads((directory / 'compiler-extracted.json').read_text())
        offline = compile_model(extracted, 'compiler-offline')
        assert offline.read_bytes() == (directory / 'arithmetic-65.rsim').read_bytes()
    compile_model(base, 'exchange', '--no-optimize')
    assert json.loads((directory / 'exchange.optimized.json').read_text()) == base
    for name, mutate in [
        ('version', lambda m: m.update(format='rhodium-simulation-ir-v99')),
        ('opcode-contract', lambda m: m['opcodes'].__setitem__(6, 'wrong-add')),
        ('width', lambda m: m['values'].__setitem__(0, 0)),
        ('negative-width', lambda m: m['values'].__setitem__(0, -1)),
        ('fractional-width', lambda m: m['values'].__setitem__(0, 3.5)),
        ('undefined-value', lambda m: m['values'].append(32)),
        ('operand-id', lambda m: m['operations'][0][2].__setitem__(0, 999)),
        ('cycle', lambda m: m['operations'][0][2].__setitem__(0, 1)),
        ('arity', lambda m: m['operations'][0][2].clear()),
        ('duplicate-definition', lambda m: m['operations'].append(m['operations'][0])),
        ('negative-immediate', lambda m: m['operations'][0].__setitem__(3, [-1])),
        ('oversized-immediate', lambda m: m['operations'][0].__setitem__(3, [1 << 64])),
        ('port-id', lambda m: m['ports'][1].__setitem__(1, 999)),
    ]:
        model = copy.deepcopy(base)
        mutate(model)
        compile_model(model, 'invalid-' + name, valid=False)

    for option, value in [('--cone-work', '-1'), ('--cone-work', '0'),
                          ('--replicate-bytes', '12x'), ('--replicate-work', str(1 << 64))]:
        compile_model(base, 'invalid-cli', option, value, valid=False)

    for width in [1, 7, 32, 63, 64, 65, 127, 128, 129, 257]:
        model = copy.deepcopy(base)
        model['values'] = [width, width]
        model['operations'] = []
        model['ports'] = [[0, 0, 'a'], [0, 1, 'b']]
        mask = (1 << width) - 1
        values = [0, 1]
        for index in range(120):
            out = len(model['values'])
            code = rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11])
            args, imm = [], []
            if code == 0:
                literal = rng.choice([0, 1, mask, rng.getrandbits(width)])
                imm = [(literal >> i) & ((1 << 64) - 1) for i in range(0, width, 64)]
            else:
                args = [rng.choice(values)] if code in [1, 2] else [rng.choice(values), rng.choice(values)]
            model['values'].append(width)
            model['operations'].append([code, out, args, imm])
            model['origins'].append([out, 'top', out, 'v', OPCODES[code], 0, width])
            values.append(out)
            if index % 11 == 0 or index == 119:
                model['ports'].append([1, out, f'out_{out}'])
        binary = compile_model(model, f'compiler-random-{width}', '--dump-passes', str(directory / f'passes-{width}'))
        again = compile_model(model, f'compiler-repeat-{width}')
        assert binary.read_bytes() == again.read_bytes()
        sim = Native(lib, binary)
        for trial in range(80):
            a, b = rng.getrandbits(width), rng.getrandbits(width)
            sim.set('a', a)
            sim.set('b', b)
            sim.eval()
            expected = [a, b]
            for code, out, args, imm in model['operations']:
                x = expected[args[0]] if args else 0
                y = expected[args[1]] if len(args) > 1 else 0
                if code == 0: value = sum(word << (64*i) for i, word in enumerate(imm))
                elif code == 1: value = x
                elif code == 2: value = ~x
                elif code == 3: value = x & y
                elif code == 4: value = x | y
                elif code == 5: value = x ^ y
                elif code == 6: value = x + y
                elif code == 7: value = x - y
                elif code == 8: value = x * y
                elif code == 9: value = x << y if y < width else 0
                elif code == 10: value = x >> y if y < width else 0
                else:
                    signed = x - (1 << width) if x >> (width-1) else x
                    value = signed >> min(y, width)
                assert out == len(expected)
                expected.append(value & mask)
            for direction, out, name in model['ports']:
                if direction == 1:
                    assert sim.get(name) == expected[out], (width, trial, name)
        sim.close()
        snapshots = sorted((directory / f'passes-{width}').glob('*.json'))
        assert len(snapshots) == 6
    check_release(compile_model, base, directory, lib)
    check_selected_updates(compile_model, base, directory, lib, rng)
    check_masked_sram(compile_model, base, lib, rng)
    check_regroup(compile_model, base, directory, lib, rng)
    print('Boolean regrouping passed 1,440 scheduled/compiled mask and state evaluations plus dependent-lane checks.')
    print('Standalone compiler round trips, deterministic output, malformed inputs, and 800 wide arithmetic evaluations passed.')


if __name__ == '__main__':
    main()
