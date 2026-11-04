#!/usr/bin/env python3
# Maps physical compilation roots to stable diagnostic, macro, and debug filenames.
# SPDX-License-Identifier: Apache-2.0
import argparse
import os
from pathlib import Path
import shlex

ROOT = Path(__file__).resolve().parents[2]


def prefix_flags(build=None, *, sources=(), toolchain=None, cwd=None):
    """Keep logical filenames independent of checkout, staging, and compiler prefixes."""
    cwd = Path(cwd or Path.cwd()).absolute()
    mappings = [(ROOT, '/rhodium')]
    if toolchain:
        mappings.append((Path(toolchain), '/rhodium/toolchain'))
    if build:
        mappings.append((Path(build), '/rhodium/build'))
    mappings.extend(sources)
    prefixes = {}
    for physical, logical in mappings:
        physical = Path(physical).absolute()
        # Both spellings matter on hosts where /tmp or the checkout is a symlink.
        for path in (physical, physical.resolve()):
            prefixes[str(path)] = logical
            relative = os.path.relpath(path, cwd)
            if relative != '.':
                prefixes[relative + '/'] = logical + '/'
                prefixes['./' + relative + '/'] = logical + '/'
            else:
                prefixes['./'] = logical + '/'
    # GCC and Clang use the last matching prefix: narrower roots must win.
    return [f'-ffile-prefix-map={old}={new}'
            for old, new in sorted(prefixes.items(), key=lambda item: (len(item[0]), item[0]))]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path)
    parser.add_argument('--toolchain', type=Path)
    parser.add_argument('--cwd', type=Path)
    parser.add_argument('--map', action='append', default=[], metavar='PHYSICAL=LOGICAL')
    parser.add_argument('--format', choices=('lines', 'shell'), default='lines')
    args = parser.parse_args()
    mappings = []
    for mapping in args.map:
        physical, separator, logical = mapping.partition('=')
        if not separator or not physical or not logical.startswith('/'):
            parser.error('--map requires PHYSICAL=/stable/logical/path')
        mappings.append((physical, logical))
    flags = prefix_flags(args.build, sources=mappings, toolchain=args.toolchain, cwd=args.cwd)
    print(shlex.join(flags) if args.format == 'shell' else '\n'.join(flags))


if __name__ == '__main__':
    main()
