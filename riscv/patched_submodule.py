#!/usr/bin/env python3
# Materializes patched submodules and computes identities for their ordered patch series.
# SPDX-License-Identifier: Apache-2.0
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def read_series(series_path):
    series = Path(series_path).resolve(strict=True)
    if not series.is_file():
        raise ValueError(f"patch series is not a file: {series}")
    patches = []
    for line in series.read_text().splitlines():
        entry = line.strip()
        if entry and not entry.startswith("#"):
            patch = series.parent / entry
            if not patch.is_file():
                raise ValueError(f"ordered patch does not exist: {patch}")
            patches.append(patch)
    return series, patches


def path_contains(parent, child):
    try:
        child.relative_to(parent)
        return True
    except ValueError:
        return False


def normalized_output_path(output_path):
    output = Path(output_path).expanduser().absolute()
    if output == Path(output.anchor):
        raise ValueError("refusing to replace a filesystem root")
    return output.parent.resolve(strict=False) / output.name


def materialize(source_path, series_path, output_path, git="git"):
    source = Path(source_path).resolve(strict=True)
    if not source.is_dir():
        raise ValueError(f"upstream submodule is not initialized: {source}")
    series, patches = read_series(series_path)
    output = normalized_output_path(output_path)
    for protected in (source, series.parent):
        if path_contains(protected, output) or path_contains(output, protected):
            raise ValueError(f"patched output and {protected} must not contain one another")

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_root = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
    staged = temporary_root / "source"
    try:
        shutil.copytree(source, staged, symlinks=True, ignore=shutil.ignore_patterns(".git"))
        for patch in patches:
            subprocess.run(
                [git, "apply", "--no-index", "--whitespace=error-all", str(patch)],
                cwd=staged,
                check=True,
            )
        if output.is_symlink() or output.is_file():
            output.unlink()
        elif output.exists():
            shutil.rmtree(output)
        staged.replace(output)
    finally:
        shutil.rmtree(temporary_root, ignore_errors=True)
    return output


def git_output(git, repository, *arguments, input_text=None):
    return subprocess.run(
        [git, "-C", str(repository), *arguments],
        input=input_text,
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()


def identity(repository_path, submodule_path, series_path, git="git"):
    repository = Path(repository_path).resolve(strict=True)
    series, patches = read_series(series_path)
    gitlink = git_output(git, repository, "ls-files", "--stage", "--", submodule_path)
    fields = gitlink.split()
    if len(fields) < 4 or fields[0] != "160000":
        raise ValueError(f"{submodule_path} is not a recorded submodule")

    hashes = [git_output(git, repository, "hash-object", str(series))]
    hashes.extend(git_output(git, repository, "hash-object", str(patch)) for patch in patches)
    identity_input = "\n".join([fields[1], *hashes]) + "\n"
    return git_output(git, repository, "hash-object", "--stdin", input_text=identity_input)


def build_parser():
    parser = argparse.ArgumentParser(description="Manage pristine submodules with ordered patches")
    subparsers = parser.add_subparsers(dest="command", required=True)

    materialize_parser = subparsers.add_parser("materialize", help="create a patched build-local tree")
    materialize_parser.add_argument("--source", required=True)
    materialize_parser.add_argument("--series", required=True)
    materialize_parser.add_argument("--output", required=True)
    materialize_parser.add_argument("--git", default="git")

    identity_parser = subparsers.add_parser("identity", help="hash a gitlink and ordered patch series")
    identity_parser.add_argument("--repository", required=True)
    identity_parser.add_argument("--submodule", required=True)
    identity_parser.add_argument("--series", required=True)
    identity_parser.add_argument("--git", default="git")
    return parser


def main(arguments=None):
    options = build_parser().parse_args(arguments)
    try:
        if options.command == "materialize":
            materialize(options.source, options.series, options.output, options.git)
        else:
            print(identity(options.repository, options.submodule, options.series, options.git))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"patched-submodule: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
