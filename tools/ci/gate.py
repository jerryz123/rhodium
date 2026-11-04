#!/usr/bin/env python3
# Validates the stable CI gate against selected Moon endpoints and static workflow results.
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import os
from pathlib import Path


def receipt_id(key):
    # Keep hosted evidence independent of the Moon shard target spelling.
    parent, separator, shard = key.rpartition(':shard-')
    return f'{parent}#{shard}' if separator and shard.isdecimal() else key


def failures(results):
    # Static jobs may intentionally skip. Selected execution cannot disappear:
    # receipts independently require every endpoint in Moon's selected graph.
    return [f"job {job} finished with {result}" for job, result in sorted(results.items())
            if result not in ('success', 'skipped') or job == 'select' and result != 'success']


def receipt_failures(endpoints, directory):
    actual = {json.loads(p.read_text()) for p in directory.glob("*.json")}
    required = set(endpoints)
    return (["missing endpoint: " + k for k in sorted(required - actual)] +
            ["unexpected endpoint: " + k for k in sorted(actual - required)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("selection", help="selected Moon tasks and expected receipts")
    parser.add_argument("results", help="one job and result per line")
    args = parser.parse_args()
    results = dict(line.split() for line in args.results.splitlines() if line.strip())
    selection = json.loads(args.selection)
    problems = failures(results)
    receipts = Path(os.environ["RUNNER_TEMP"]) / "endpoint-receipts"
    problems += receipt_failures(selection["endpoints"], receipts)
    if problems:
        parser.exit(1, "\n".join(problems) + "\n")


if __name__ == "__main__":
    main()
