#!/usr/bin/env python3
# Materializes a clean ACT source tree and applies Rhodium's ordered patch series.
# SPDX-License-Identifier: Apache-2.0
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def read_series(series_path):
    patches = []
    for line in series_path.read_text().splitlines():
        entry = line.strip()
        if entry and not entry.startswith("#"):
            patches.append(series_path.parent / entry)
    missing = [str(patch) for patch in patches if not patch.is_file()]
    if missing:
        raise ValueError(f"missing ACT patches: {', '.join(missing)}")
    return patches


def validate_paths(source, output):
    source = source.resolve()
    output = output.resolve()
    if not source.is_dir():
        raise ValueError(f"ACT source directory does not exist: {source}")
    if output == source or source in output.parents:
        raise ValueError("patched ACT output must be outside the upstream source tree")
    if output == Path(output.anchor):
        raise ValueError("refusing to replace a filesystem root")
    return source, output


def materialize(source, output, series_path):
    source, output = validate_paths(source, output)
    patches = read_series(series_path.resolve())
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_root = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
    staged = temporary_root / "source"
    try:
        shutil.copytree(source, staged, symlinks=True, ignore=shutil.ignore_patterns(".git"))
        for patch in patches:
            subprocess.run(
                ["git", "apply", "--no-index", "--whitespace=error-all", str(patch)],
                cwd=staged,
                check=True,
            )
        if output.exists():
            shutil.rmtree(output)
        staged.replace(output)
    finally:
        shutil.rmtree(temporary_root, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description="Materialize Rhodium's patched ACT source tree")
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--series", type=Path, required=True)
    args = parser.parse_args()
    materialize(args.source, args.output, args.series)


if __name__ == "__main__":
    main()
