#!/usr/bin/env python3
# Validates the stable CI gate against the planner and reusable-workflow results.
# SPDX-License-Identifier: Apache-2.0

import argparse
import json


def failures(plan, results):
    required = {
        "compile": plan["run_compile"],
        "checks": plan["run_checks"],
        "simulator": plan["run_simulator"],
        "simulation": plan["run_simulation"],
        "software": plan["run_program_native"] or plan["run_program_arch"],
    }
    problems = []
    for job, selected in required.items():
        result = results.get(job)
        if selected and result != "success":
            problems.append(f"selected job {job} finished with {result}")
        elif not selected and result not in ("success", "skipped"):
            problems.append(f"unselected job {job} finished with {result}")
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("plan", help="planner JSON")
    parser.add_argument("results", nargs="+", metavar="JOB=RESULT")
    args = parser.parse_args()
    results = dict(result.split("=", 1) for result in args.results)
    problems = failures(json.loads(args.plan), results)
    if problems:
        parser.exit(1, "\n".join(problems) + "\n")


if __name__ == "__main__":
    main()
