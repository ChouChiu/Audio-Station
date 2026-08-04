#!/usr/bin/env python3
"""Run clang-tidy only for first-party C++ translation units."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys


UNSUPPORTED_CLANG_FLAGS = {"-mno-direct-extern-access"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang-tidy", required=True)
    parser.add_argument("--source-dir", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    return parser.parse_args()


def is_first_party(source: Path, source_dir: Path) -> bool:
    try:
        relative = source.resolve().relative_to(source_dir.resolve())
    except ValueError:
        return False
    return relative.suffix in {".cc", ".cpp", ".cxx"} and relative.parts[0] in {
        "src",
        "tests",
    }


def sanitize_entry(entry: dict[str, object]) -> dict[str, object]:
    sanitized = dict(entry)
    arguments = sanitized.get("arguments")
    if isinstance(arguments, list):
        sanitized["arguments"] = [
            argument for argument in arguments if argument not in UNSUPPORTED_CLANG_FLAGS
        ]
        return sanitized

    command = sanitized.get("command")
    if isinstance(command, str):
        arguments = shlex.split(command)
        sanitized["command"] = shlex.join(
            argument for argument in arguments if argument not in UNSUPPORTED_CLANG_FLAGS
        )
    return sanitized


def entry_source(entry: dict[str, object]) -> Path | None:
    source_value = entry.get("file")
    if not isinstance(source_value, str):
        return None
    source = Path(source_value)
    if source.is_absolute():
        return source.resolve()
    directory = entry.get("directory")
    if not isinstance(directory, str):
        return None
    return (Path(directory) / source).resolve()


def main() -> int:
    args = parse_args()
    database_path = args.build_dir / "compile_commands.json"
    if not database_path.is_file():
        print(f"error: compilation database not found: {database_path}", file=sys.stderr)
        return 2

    with database_path.open(encoding="utf-8") as database_file:
        database = json.load(database_file)

    sources = set()
    for entry in database:
        if not isinstance(entry, dict):
            continue
        source = entry_source(entry)
        if source is not None and is_first_party(source, args.source_dir):
            sources.add(source)
    sources = sorted(sources)
    if not sources:
        print("error: no first-party translation units found", file=sys.stderr)
        return 2

    lint_build_dir = args.build_dir / "meson-private" / "clang-tidy"
    lint_build_dir.mkdir(parents=True, exist_ok=True)
    with (lint_build_dir / "compile_commands.json").open("w", encoding="utf-8") as output:
        json.dump([sanitize_entry(entry) for entry in database], output, indent=2)

    config = args.source_dir / ".clang-tidy"

    def run(source: Path) -> tuple[Path, subprocess.CompletedProcess[str]]:
        command = [
            args.clang_tidy,
            "--quiet",
            f"-p={lint_build_dir}",
            f"--config-file={config}",
            str(source),
        ]
        return source, subprocess.run(command, text=True, capture_output=True, check=False)

    failures = 0
    jobs = min(len(sources), os.cpu_count() or 1)
    print(f"Running clang-tidy on {len(sources)} files with {jobs} workers")
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as executor:
        for source, result in executor.map(run, sources):
            output = result.stdout + result.stderr
            if output:
                print(f"\n[{source.relative_to(args.source_dir)}]\n{output.rstrip()}")
            if result.returncode != 0:
                failures += 1

    if failures:
        print(f"clang-tidy failed for {failures} translation unit(s)", file=sys.stderr)
        return 1
    print("clang-tidy passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
