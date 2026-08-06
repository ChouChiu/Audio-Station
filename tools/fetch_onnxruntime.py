#!/usr/bin/env python3
"""Fetch the onnxruntime C++ runtime into third_party/onnxruntime.

Downloads the official onnxruntime-linux-x64 release tarball (v1.24.3, MIT) and
extracts the shared library (lib/libonnxruntime.so.1) and C/C++ headers
(include/) into third_party/onnxruntime/. Required for the AI vocal extraction
feature; the shared library is not committed to git.

Usage:  python3 tools/fetch_onnxruntime.py [--version 1.24.3]
"""

from __future__ import annotations

import argparse
import sys
import tarfile
import urllib.request
from pathlib import Path

VERSION = "1.24.3"
BASE_URL = "https://github.com/microsoft/onnxruntime/releases/download"
TARBALL = f"onnxruntime-linux-x64-{VERSION}.tgz"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default=VERSION, help=f"onnxruntime version (default {VERSION})")
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    target = root / "third_party" / "onnxruntime"
    tarball = f"onnxruntime-linux-x64-{args.version}.tgz"
    url = f"{BASE_URL}/v{args.version}/{tarball}"
    print(f"downloading {url}")
    request = urllib.request.Request(url, headers={"User-Agent": "mr-remover/1.0"})
    with urllib.request.urlopen(request) as response:
        payload = response.read()
    print(f"  {len(payload) / 1e6:.1f} MB")

    prefix = f"onnxruntime-linux-x64-{args.version}"
    with tarfile.open(fileobj=__import__("io").BytesIO(payload), mode="r:gz") as archive:
        members = [m for m in archive.getmembers() if m.name.startswith(prefix + "/")]
        archive.extractall(target, members=members)

    extracted = target / prefix
    for name in ("lib", "include"):
        dest = target / name
        dest.mkdir(exist_ok=True)
        for item in (extracted / name).iterdir():
            dest_item = dest / item.name
            if dest_item.exists() and dest_item.is_dir():
                continue
            item.rename(dest_item)
    for name in ("LICENSE", "ThirdPartyNotices.txt", "README.md", "VERSION_NUMBER"):
        source = extracted / name
        if source.exists() and not (target / name).exists():
            source.rename(target / name)
    import shutil

    shutil.rmtree(extracted)
    print(f"installed into {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
