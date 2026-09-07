#!/usr/bin/env python3
# Adapts confirmed HTIF completion to ACT's summary protocol and preserves simulator failures.
import argparse
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulator", required=True)
    parser.add_argument("--max-cycles", type=int, default=10000000)
    parser.add_argument("elf", type=Path)
    args = parser.parse_args()
    if args.max_cycles <= 0 or not args.elf.is_file():
        parser.error("a positive cycle limit and an existing ELF are required")
    process = subprocess.Popen(
        [args.simulator, "+permissive", f"+max-cycles={args.max_cycles}",
         "+permissive-off", str(args.elf.resolve())],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    confirmed = False
    for line in process.stdout:
        print(line, end="", flush=True)
        confirmed |= line.strip() == "SoC harness simulation passed"
    returncode = process.wait()
    # Exit zero alone cannot prove that the target reached its HTIF pass macro.
    passed = returncode == 0 and confirmed
    status = "PASSED" if passed else "FAILED"
    print(f'RVCP-SUMMARY: TEST {status} - Test File "{args.elf.stem}.S"', flush=True)
    return 0 if passed else (returncode if returncode > 0 else 1)


if __name__ == "__main__":
    sys.exit(main())
