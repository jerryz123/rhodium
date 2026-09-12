#!/usr/bin/env python3
# Compares functional ALUs with raw physical controls, including unusual control combinations.
import pathlib
import random
import sys
from runtime_test import Native, Options, library


def main():
    directory = pathlib.Path(sys.argv[1])
    lib = library(directory / 'librhodium_sim.so')
    lib.compiled_cache, lib.shape_limit = {}, 512
    rng = random.Random(901177)
    for width in [32, 64]:
        path = directory / f'alu-{width}.rsim'
        lib.options, lib.compiled = Options(1, 1), False
        raw = Native(lib, path)
        lib.options = Options(1, 0)
        native = Native(lib, path)
        lib.compiled = True
        compiled = Native(lib, path)
        assert lib.rds_get_stats(native.ptr).objects == 0
        mask = (1 << width) - 1
        edges = [0, 1, mask, 1 << (width-1), 0x80000000, 0xffffffff, 0x0123456789abcdef & mask]
        for i in range(12000):
            left, right = rng.getrandbits(width), rng.getrandbits(width)
            if i < 4096:
                left = edges[i % len(edges)]
                right = [0, 1, 7, 31, 32, 63, mask][(i // len(edges)) % 7]
            control = rng.getrandbits(23)
            for sim in [raw, native, compiled]:
                for name, value in [('left', left), ('right', right), ('control', control)]:
                    sim.set(name, value)
                sim.eval()
            expected = raw.get('result')
            assert native.get('result') == compiled.get('result') == expected, (width, i, hex(left), hex(right), hex(control), hex(expected), hex(native.get('result')))
        for sim in [raw, native, compiled]:
            sim.close()
    print('Functional RV32/RV64 ALUs match raw RTL for 24,000 physical-control cases; compiled queries own no state.')


if __name__ == '__main__':
    main()
