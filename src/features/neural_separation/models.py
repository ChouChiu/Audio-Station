from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True, slots=True)
class NeuralJob:
    song: Path
    output_dir: Path
    model_id: str = "mdxnet_1"
    models_dir: Path | None = None
    language: str = "zh_cn"
