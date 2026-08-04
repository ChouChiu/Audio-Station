#!/usr/bin/env python3
"""Validate translation key, value, and placeholder consistency."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys


PLACEHOLDER_PATTERN = re.compile(r"\{[^{}]+\}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("translation_dir", type=Path)
    return parser.parse_args()


def load_table(path: Path) -> dict[str, str]:
    with path.open(encoding="utf-8") as source:
        value = json.load(source)
    if not isinstance(value, dict) or not all(
        isinstance(key, str) and isinstance(text, str) and text
        for key, text in value.items()
    ):
        raise ValueError(f"{path}: expected a JSON object containing non-empty strings")
    return value


def main() -> int:
    translation_dir = parse_args().translation_dir
    paths = sorted(translation_dir.glob("*.json"))
    if not paths:
        print(f"error: no translations found in {translation_dir}", file=sys.stderr)
        return 2

    try:
        tables = {path.stem: load_table(path) for path in paths}
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    reference_name = "zh_cn" if "zh_cn" in tables else next(iter(tables))
    reference = tables[reference_name]
    failures: list[str] = []
    for name, table in tables.items():
        missing = sorted(reference.keys() - table.keys())
        extra = sorted(table.keys() - reference.keys())
        if missing:
            failures.append(f"{name}: missing keys: {', '.join(missing)}")
        if extra:
            failures.append(f"{name}: extra keys: {', '.join(extra)}")
        for key in reference.keys() & table.keys():
            expected = set(PLACEHOLDER_PATTERN.findall(reference[key]))
            actual = set(PLACEHOLDER_PATTERN.findall(table[key]))
            if actual != expected:
                failures.append(
                    f"{name}.{key}: placeholders {sorted(actual)} != {sorted(expected)}"
                )

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"Validated {len(tables)} translation tables with {len(reference)} keys")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
