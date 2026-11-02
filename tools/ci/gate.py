#!/usr/bin/env python3
# Validates the stable CI gate against the planner and reusable-workflow results.
# SPDX-License-Identifier: Apache-2.0

import argparse
import json


def failures(plan, results):
    required = {
        "compile-racket": plan["run_compile"],
        "sail": plan["run_sail"],
        "targets": plan["run_targets"],
        "linux-plan": plan["run_linux_build"],
        "linux-builds": plan["run_linux_build"],
        "native": plan["run_native"],
        "software-checks": bool(plan['software_checks']),
        "harness-isa": 'isa' in plan['harness_build_suites'],
        "harness-benchmark": 'benchmark' in plan['harness_build_suites'],
        "checks": plan["run_checks"],
        "program-plan": plan["run_program_native"],
        "platform-plan": bool(plan['platform_build_suites']),
        "platform-builds-opensbi": 'opensbi' in plan['platform_build_suites'],
        "platform-builds-litmus": 'litmus' in plan['platform_build_suites'],
    }
    for group, matrix in plan['simulator_groups'].items():
        required['sim-' + group] = bool(matrix['include'])
    for group in plan['program_build_groups']:
        required['program-builds-' + group] = True
    for soc, config in plan['config_jobs'].items():
        required['sim-' + soc] = True
        for group in config['program_groups']:
            required['program-' + group + '-' + soc] = True
        if config['arch']:
            required['act-build-' + soc] = required['act-' + soc] = True
        if config['platform']:
            required['platform-' + soc] = True
    problems = []
    for job in sorted(required.keys() | results.keys()):
        selected = required.get(job, False)
        result = results.get(job)
        if selected and result != "success":
            problems.append(f"selected job {job} finished with {result}")
        elif not selected and result not in ("success", "skipped"):
            problems.append(f"unselected job {job} finished with {result}")
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("plan", help="planner JSON")
    parser.add_argument("results", help="GitHub needs JSON for every root job")
    args = parser.parse_args()
    results = {job: value['result'] for job, value in json.loads(args.results).items()}
    problems = failures(json.loads(args.plan), results)
    if problems:
        parser.exit(1, "\n".join(problems) + "\n")


if __name__ == "__main__":
    main()
