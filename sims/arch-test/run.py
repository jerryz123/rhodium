#!/usr/bin/env python3
# Checks fresh or reused simulator output before reporting one ACT completion.
# SPDX-License-Identifier: Apache-2.0
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'program-test'))
import execution


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulator", required=True)
    parser.add_argument("--max-cycles", type=int, default=10000000)
    parser.add_argument('--timeout', type=float, default=300, help='wall deadline imposed by the outer ACT runner')
    parser.add_argument("--cosim", action="store_true", help="require a nonempty successful Sail checker completion")
    parser.add_argument('--execution-cache', type=Path, help='reuse successful raw execution evidence')
    parser.add_argument('--force-fresh', action='store_true', help='ignore cached execution evidence')
    parser.add_argument("elf", type=Path)
    args = parser.parse_args()
    if args.max_cycles <= 0 or args.timeout <= 0 or not args.elf.is_file():
        parser.error("a positive cycle limit and an existing ELF are required")
    cache = execution.Evidence(args.simulator, 'arch', directory=args.execution_cache, force_fresh=args.force_fresh)
    key = cache.key(dict(max_cycles=args.max_cycles, timeout=args.timeout, cosim=args.cosim), dict(elf=args.elf))
    cached = cache.read(key)
    started = time.monotonic()
    temporary = tempfile.NamedTemporaryFile(mode='w+', prefix='rhodium-act-output-') if key is not None else None
    process = None if cached else subprocess.Popen(
        [args.simulator, "+permissive", f"+max-cycles={args.max_cycles}",
         "+permissive-off", str(args.elf.resolve())],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    confirmed = False
    completions = []
    stream = cached[1].open() if cached else process.stdout
    for line in stream:
        if temporary:
            temporary.write(line)
        confirmed |= line.strip() == "SoC harness simulation passed"
        completion = re.fullmatch(r"Sail cosim: checked ([0-9]+) scalar records", line.strip())
        if completion:
            completions.append(int(completion[1]))
        if line.startswith("RVCP-SUMMARY:"):
            continue
        print(line, end="", flush=True)
    stream.close()
    returncode = cached[0]['returncode'] if cached else process.wait()
    # Exit zero alone cannot prove that the target reached its HTIF pass macro.
    passed = returncode == 0 and confirmed
    if args.cosim and (len(completions) != 1 or completions[0] == 0):
        print("Missing, duplicate, or empty cosim completion", file=sys.stderr)
        passed = False
    if passed and not cached and key is not None:
        temporary.flush()
        cache.save(key, Path(temporary.name), returncode, time.monotonic() - started)
    if temporary:
        temporary.close()
    if key is not None:
        print(execution.MARKER + json.dumps(cache.info(key, cached)), flush=True)
    status = "PASSED" if passed else "FAILED"
    print(f'RVCP-SUMMARY: TEST {status} - Test File "{args.elf.stem}.S"', flush=True)
    return 0 if passed else (returncode if returncode > 0 else 1)


if __name__ == "__main__":
    sys.exit(main())
