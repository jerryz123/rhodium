#!/usr/bin/env python3
# Checks shared lookup projections against raw execution and source-mapped inspection output.
import importlib.util
import json
import pathlib
import random
import re
import sys
import os
import platform
import subprocess
from runtime_test import Native, Options, library


def main():
    directory = pathlib.Path(sys.argv[1])
    lib = library(directory / 'librhodium_sim.so')
    lib.compiled, lib.compiled_cache, lib.shape_limit = True, {}, 512
    lib.options = Options(1, 0)
    compiled = Native(lib, directory / 'lookup.rsim')
    lib.compiled = False
    lib.options = Options(1, 1)
    original = Native(lib, directory / 'lookup.rsim')
    rng = random.Random(98271)
    selectors = [i*73+1 for i in range(41)] + [0, 4095, 72, 3000]
    for i in range(600):
        selector = selectors[i % len(selectors)] if i < 180 else rng.randrange(4096)
        data = rng.getrandbits(64)
        index = (selector-1)//73
        expected = data ^ (index*0x123456789abc) if selector >= 1 and (selector-1)%73 == 0 and index < 41 else 0xdeadbeef
        for sim in [original, compiled]:
            sim.set('selector', selector)
            sim.set('data', data)
            sim.eval()
            assert sim.get('word') == expected
            assert all(sim.get(f'bit_{bit}') == (expected >> bit) & 1 for bit in range(64))
    original.close()
    compiled.close()
    # Different result values must retain sparse-key and default
    # behavior when the optimizer factors their common wide-key comparisons.
    lib.options = Options(1, 0)
    lib.compiled = True
    compiled = Native(lib, directory / 'shared-decoder.rsim')
    lib.compiled = False
    scheduled = Native(lib, directory / 'shared-decoder.rsim')
    lib.options = Options(1, 1)
    original = Native(lib, directory / 'shared-decoder.rsim')
    keys = [0, 1, 2, 73, 511, 2047, 4095, 1 << 64, (1 << 64) + 1, (1 << 65) - 1]
    for i in range(400):
        selector = keys[i % len(keys)] if i < 200 else rng.getrandbits(65)
        data = rng.getrandbits(65)
        for sim in [original, scheduled, compiled]:
            sim.set('selector', selector)
            sim.set('data', data)
            sim.eval()
            for lane in range(2):
                expected = data ^ ((lane + 1) * (keys.index(selector) + 1) * 0x123456789abcdef) if selector in keys else 0xdeadbeef + lane
                assert sim.get(f'lane_{lane}') == expected
    plan_path = directory / 'shared-decoder.plan.json'
    scheduled.check(lib.rds_emit_plan(scheduled.ptr, str(plan_path).encode()))
    plan = json.loads(plan_path.read_text())
    model = json.loads((directory / 'shared-decoder.optimized.json').read_text())
    assert sum(op[0] == 25 for op in model['operations']) == 1
    defined = {op[1] for op in model['operations']}
    assert all(instruction[4] in defined for worker in plan['workers'] for batch in worker['batches'] for instruction in batch[2])
    assert lib.rds_emit_plan(compiled.ptr, str(plan_path).encode()) != 0
    for sim in [original, scheduled, compiled]:
        sim.close()
    source = pathlib.Path(__file__).resolve().parents[2] / 'sims/native/inspect_ir.py'
    spec = importlib.util.spec_from_file_location('sim_inspect', source)
    inspector = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(inspector)
    if platform.machine() in ('x86_64', 'AMD64'):
        # Inspect real compiler output: only the memory-destination form is slow
        # on Zen 4. These functions are disassembled, never executed by the test.
        probe = directory / 'compress-audit.c'
        probe.write_text("""/* Exercises register and bounded-store alternatives to Zen 4 compress stores. */
#include <immintrin.h>
__m512i registers(__m512i x, __mmask32 mask) {
  return _mm512_maskz_compress_epi16(mask, x);
}
void slow(void *out, __m512i x, __mmask32 mask) {
  _mm512_mask_compressstoreu_epi16(out, mask, x);
}
void bounded(void *out, __m512i x, __mmask32 mask) {
  __m512i packed = _mm512_maskz_compress_epi16(mask, x);
  __asm__("" : "+v"(packed));
  __mmask32 tail = (__mmask32)((1ULL << __builtin_popcount(mask)) - 1);
  _mm512_mask_storeu_epi16(out, tail, packed);
}
""")
        binary = probe.with_suffix('.o')
        subprocess.run([os.getenv('CC', 'cc'), '-O3', '-mavx512bw', '-mavx512vbmi2',
                        '-c', str(probe), '-o', str(binary)], check=True)
        audit = inspector.inspect_binary(binary)
        assert {x['function'] for x in audit['zen4_compress_stores']} == {'slow'}, audit
        assert audit['opcodes'].get('vpcompressw', 0) >= 3, audit
    raw = json.loads((directory / 'lookup.original.json').read_text())
    optimized = json.loads((directory / 'lookup.optimized.json').read_text())
    report = inspector.inspect(raw, optimized)
    assert report['original']['opcodes']['mux_lookup'] == 65
    assert report['optimized']['opcodes']['mux_lookup'] == 1
    assert report['original']['duplicated_selectors'][0]['lookup_count'] == 65
    definitions = {o[1] for o in optimized['operations']}
    assert optimized['origins'] and all(origin[0] in definitions or origin[0] < len(optimized['values']) for origin in optimized['origins'])
    source = lib.compiled_cache[(directory / 'shared-decoder.rsim', 1, 0, 512)].with_suffix('.c').read_text()
    mapped = inspector.inspect(model, model, plan, source)['generated']['largest_commands']
    assert mapped and all(0 <= command['instruction_range'][0] < command['instruction_range'][1] <= sum(len(b[2]) for w in plan['workers'] for b in w['batches']) for command in mapped)
    selected = inspector.inspect(model, model, plan, source, [mapped[0]['command']])['generated']['requested_commands']
    assert selected == [mapped[0]]
    lib.options, lib.compiled = Options(1, 0), True
    table = Native(lib, directory / 'small-decode.rsim')
    lib.options = Options(1, 8192)
    linear = Native(lib, directory / 'small-decode.rsim')
    for value in range(256):
        for sim in [table, linear]:
            sim.set('a', value)
            sim.set('b', 255-value)
            sim.eval()
            for name, key in [('a', value), ('b', 255-value)]:
                for width in [7, 23, 64]:
                    assert sim.get(f'{name}_{width}') == ((key//16+1) << (width-4) if key//16 < 12 else 5)
    text = lib.compiled_cache[(directory / 'small-decode.rsim', 1, 0, 512)].with_suffix('.c').read_text()
    assert len(re.findall(r'static const uint(?:8|16|32|64)_t decode_\d+\[', text)) == 3
    table.close()
    linear.close()
    lib.options = Options(1, 0)
    tree = Native(lib, directory / 'tree-decode.rsim')
    lib.options = Options(1, 8192)
    linear = Native(lib, directory / 'tree-decode.rsim')
    lib.options = Options(1, 1)
    raw = Native(lib, directory / 'tree-decode.rsim')
    patterns = [(0x13, 0x707f), (0x1013, 0x707f), (0x33, 0xfe00707f),
                (0x40000033, 0xfe00707f), (0x23, 0xff), (0x1a3, 0x1ff)]
    for i in range(4000):
        value = rng.getrandbits(32)
        if i % 2:
            key, mask = patterns[i % len(patterns)]
            value = value & ~mask | key
        expected = next(((j+1) << 90 for j, (key, mask) in enumerate(patterns) if value & mask == key), 9)
        for sim in [tree, linear, raw]:
            sim.set('selector', value)
            sim.eval()
            assert sim.get('result') == expected
    for sim in [tree, linear, raw]:
        sim.close()
    print('Original/compiled lookup values, all slices, sparse/default selections, and source-mapped inspection passed.')


if __name__ == '__main__':
    main()
