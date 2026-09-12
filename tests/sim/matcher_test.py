#!/usr/bin/env python3
# Compares packed matching against independent priorities and original RTL, including feedback and reset.
import pathlib
import random
import sys
from runtime_test import Native, Options, library


def grants_for(requests, accepts, priorities, rows, columns):
    taken, grants = 0, 0
    updated = priorities.copy()
    for col in range(columns):
        for distance in range(rows):
            row = (priorities[col]+distance) % rows
            if (requests >> (row*columns+col)) & 1 and not (taken >> row) & 1:
                grants |= 1 << (row*columns+col)
                taken |= 1 << row
                if (accepts >> col) & 1:
                    updated[col] = (row+1) % rows
                break
    return grants, updated


def main():
    directory = pathlib.Path(sys.argv[1])
    lib = library(directory / 'librhodium_sim.so')
    lib.compiled_cache, lib.shape_limit = {}, 512
    rng = random.Random(95217)
    for rows, columns in [(1, 1), (3, 2), (3, 5), (8, 4), (11, 11), (64, 2)]:
        sims = []
        for workers, flags, compiled in [(1, 1, False), (1, 0, False), (1, 0, True), (4, 576, True)]:
            lib.options, lib.compiled = Options(workers, flags), compiled
            sims.append(Native(lib, directory / f'matcher-{rows}-{columns}.rsim'))
        priorities = [0]*columns
        for cycle in range(700):
            requests = rng.getrandbits(rows*columns)
            accepts = rng.getrandbits(columns) if cycle % 9 else 0
            reset = int(cycle % 83 == 0)
            expected, updated = grants_for(requests, accepts, priorities, rows, columns)
            for sim in sims:
                sim.set('reset', reset)
                sim.set('requests', requests)
                sim.set('accepts', accepts)
                sim.eval()
                assert sim.get('grants') == expected, (rows, columns, cycle)
                sim.advance()
            priorities = [0]*columns if reset else updated
        for sim in sims:
            sim.close()
    sims = []
    for flags, compiled in [(1, False), (0, True)]:
        lib.options, lib.compiled = Options(1, flags), compiled
        sims.append(Native(lib, directory / 'matcher-feedback.rsim'))
    for cycle in range(250):
        for sim in sims:
            sim.set('reset', int(cycle % 31 == 0))
            sim.set('requests', cycle % 4)
            sim.set('accepts', cycle % 4)
            sim.eval()
        assert sims[0].get('grants') == sims[1].get('grants')
        for sim in sims:
            sim.advance()
    for sim in sims:
        sim.close()
    print('Rotating matchers: raw RTL, native, compiled, parallel, priority rotation, stalls, reset and leaf feedback passed.')


if __name__ == '__main__':
    main()
