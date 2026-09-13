#!/usr/bin/env python3
# Verifies that the prototype wrapper still matches the pinned harness boundary contract.
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
import re
from pathlib import Path


DESIGN_NAME = "double_wide_openframe_project_wrapper"


def module_header(path: Path) -> str:
    text = path.read_text()
    match = re.search(
        rf"\bmodule\s+{DESIGN_NAME}\s*\((.*?)\);",
        text,
        flags=re.DOTALL,
    )
    if match is None:
        raise ValueError(f"{path}: cannot find {DESIGN_NAME} module header")
    header = re.sub(r"//.*?$", "", match.group(1), flags=re.MULTILINE)
    return re.sub(r"\s+", " ", header).strip()


def check_pin_template(path: Path) -> None:
    text = path.read_text()
    design = re.search(r"^DESIGN\s+(\S+)\s*;", text, flags=re.MULTILINE)
    pins = re.search(r"^PINS\s+(\d+)\s*;", text, flags=re.MULTILINE)
    if design is None or design.group(1) != DESIGN_NAME:
        raise ValueError(f"{path}: unexpected or missing DESIGN declaration")
    if pins is None or int(pins.group(1)) != 1216:
        raise ValueError(f"{path}: expected the current 1216-pin harness template")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--wrapper", required=True, type=Path)
    parser.add_argument("--harness-wrapper", required=True, type=Path)
    parser.add_argument("--pin-template", required=True, type=Path)
    args = parser.parse_args()

    if module_header(args.wrapper) != module_header(args.harness_wrapper):
        raise SystemExit(
            "prototype wrapper ports differ from the pinned harness wrapper ports"
        )
    check_pin_template(args.pin_template)
    print("OpenFrame contract OK: exact wrapper header and 1216-pin DEF template")


if __name__ == "__main__":
    main()
