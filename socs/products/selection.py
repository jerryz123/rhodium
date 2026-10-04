#!/usr/bin/env python3
# Resolves public selectors from the shared canonical table without loading hardware or Racket.
# SPDX-License-Identifier: Apache-2.0
import argparse
from pathlib import Path


# Loads the shared key-to-axis table, rejecting malformed or duplicate identities.
def selections():
    rows = [line.split() for line in Path(__file__).with_name('selections.tsv').read_text().splitlines()
            if line and not line.startswith('#')]
    if any(len(row) != 4 or row[0] != '-'.join(row[1:]) for row in rows):
        raise ValueError('invalid canonical selection table')
    result = {row[0]: tuple(row[1:]) for row in rows}
    if len(result) != len(rows):
        raise ValueError('duplicate canonical product key')
    return result


# Resolves an explicit key or separate axes, enforcing conflicts and required ISA selection.
def select(soc, core=None, isa=None, *, required=True):
    table = selections()
    if soc in table:
        axes = table[soc]
        if core is not None and core != axes[1]:
            raise ValueError('CORE conflicts with the explicit SOC product key')
        if isa and isa != axes[2]:
            raise ValueError('ISA conflicts with the explicit SOC product key')
        return axes
    if soc not in {axes[0] for axes in table.values()}:
        raise ValueError('SOC must be a canonical shape or shape-core-isa key')
    core = 'rv5stage' if core is None else core
    if not isa and required:
        raise ValueError('ISA is required; use ISA=<preset> or SOC=shape-core-isa')
    if not isa:
        if core not in {axes[1] for axes in table.values()}:
            raise ValueError('unsupported core')
        return soc, core, ''
    key = '-'.join((soc, core, isa))
    if key not in table:
        raise ValueError('unsupported SoC/core/ISA selection')
    return table[key]


# Emits the resolved axes for Make consumers, or a diagnostic with exit status two.
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--soc', required=True)
    parser.add_argument('--core')
    parser.add_argument('--isa')
    parser.add_argument('--optional', action='store_true')
    args = parser.parse_args()
    try:
        print(' '.join(select(args.soc, args.core, args.isa, required=not args.optional)))
    except ValueError as error:
        print(f'ERROR: {error}')
        raise SystemExit(2)


if __name__ == '__main__':
    main()
