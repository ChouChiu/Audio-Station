#!/usr/bin/env python3
"""Download UVR MDX-Net vocal extraction models for MR Remover.

与 src/neural/modelcatalog.cpp 的目录一致 (每个系列只收录最强模型)。
默认下载默认模型 (mdxnet_1); --all 下载全部。目标目录默认 <repo>/models,
可用 $MR_REMOVER_MODELS 或 --dir 覆盖。同时拉取 model_data.json (md5 超参查表)。

模型也可由应用在首次提取时自动下载 (无需本脚本)。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import urllib.request
from pathlib import Path

RELEASE_BASE = (
    "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/"
)
MODEL_DATA_URL = (
    "https://raw.githubusercontent.com/Anjok07/ultimatevocalremovergui/master/"
    "models/MDX_Net_Models/model_data/model_data.json"
)

# 与 modelcatalog.cpp 保持同步
CATALOG = [
    {
        "id": "mdxnet_1",
        "name": "UVR-MDX-NET 1",
        "file": "UVR_MDXNET_1_9703.onnx",
        "size": 29704436,
        "default": True,
    },
    {
        "id": "mdxnet_main",
        "name": "UVR-MDX-NET Main",
        "file": "UVR_MDXNET_Main.onnx",
        "size": 66759214,
        "default": False,
    },
    {
        "id": "kim_vocal",
        "name": "Kim Vocal 1",
        "file": "Kim_Vocal_1.onnx",
        "size": 66759214,
        "default": False,
    },
    {
        "id": "kuielab_b",
        "name": "kuielab B Vocals",
        "file": "kuielab_b_vocals.onnx",
        "size": 29703204,
        "default": False,
    },
]


def download(url: str, dest: Path, expected: int | None = None) -> None:
    print(f"downloading {url}")
    request = urllib.request.Request(url, headers={"User-Agent": "mr-remover/1.0"})
    total = 0
    done = 0
    with urllib.request.urlopen(request) as response, dest.open("wb") as out:
        total = int(response.headers.get("Content-Length", 0))
        while True:
            chunk = response.read(1 << 16)
            if not chunk:
                break
            out.write(chunk)
            done += len(chunk)
            if total:
                print(f"\r  {done / 1e6:.1f} / {total / 1e6:.1f} MB", end="", flush=True)
    print()
    if expected is not None and done != expected:
        print(f"warning: size mismatch (got {done}, expected {expected})", file=sys.stderr)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--model",
        action="append",
        help="model id to download (repeatable; default: default model)",
    )
    parser.add_argument("--all", action="store_true", help="download every catalog model")
    parser.add_argument(
        "--dir",
        default=os.environ.get("MR_REMOVER_MODELS", ""),
        help="target directory (default: <repo>/models, or $MR_REMOVER_MODELS)",
    )
    args = parser.parse_args()

    by_id = {entry["id"]: entry for entry in CATALOG}
    if args.all:
        selected = CATALOG
    elif args.model:
        selected = []
        for model_id in args.model:
            if model_id not in by_id:
                print(f"error: unknown model id: {model_id}", file=sys.stderr)
                print(f"available: {', '.join(by_id)}", file=sys.stderr)
                return 2
            selected.append(by_id[model_id])
    else:
        selected = [entry for entry in CATALOG if entry["default"]] or [CATALOG[0]]

    models_dir = Path(args.dir) if args.dir else Path(__file__).resolve().parent.parent / "models"
    models_dir.mkdir(parents=True, exist_ok=True)

    for entry in selected:
        dest = models_dir / entry["file"]
        download(RELEASE_BASE + entry["file"], dest, entry["size"])
        digest = hashlib.md5(dest.read_bytes()).hexdigest()
        print(f"saved {dest} ({entry['name']}, md5={digest})")

    data_path = models_dir / "model_data.json"
    download(MODEL_DATA_URL, data_path)
    try:
        table = json.loads(data_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        table = {}
        print(f"warning: could not read {data_path}: {error}", file=sys.stderr)
    for entry in selected:
        digest = hashlib.md5((models_dir / entry["file"]).read_bytes()).hexdigest()
        if table.get(digest):
            print(f"model_data.json: hyperparameters for {entry['id']}: {table[digest]}")
        else:
            print(
                f"note: {entry['id']} md5 not in model_data.json; the app falls back to "
                "the built-in parameter table.",
                file=sys.stderr,
            )
    print("done. The app also auto-downloads any catalog model on first use.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
