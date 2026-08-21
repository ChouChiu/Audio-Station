from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from shared.dsp import ReferenceAlgorithm


@dataclass(frozen=True, slots=True)
class ReferenceJob:
    song: Path
    accompaniment: Path
    output: Path
    strength: int = 75
    sigma: int = 8
    auto_align: bool = True
    language: str = "zh_cn"
    algorithm: ReferenceAlgorithm | str = ReferenceAlgorithm.SPECTRAL_MASK
    center_extraction: bool = False
    weak_vocal_protection: bool = False

    def __post_init__(self) -> None:
        if not 0 <= self.strength <= 100:
            raise ValueError("strength must be in [0, 100]")
        if self.sigma not in {1, 3, 8, 16}:
            raise ValueError("sigma must be one of 1, 3, 8, 16")
        object.__setattr__(self, "algorithm", ReferenceAlgorithm(self.algorithm))
        if self.weak_vocal_protection and not self.center_extraction:
            raise ValueError("weak vocal protection requires center extraction")


@dataclass(frozen=True, slots=True)
class AudioStats:
    duration_seconds: float
    sample_rate: int
    channels: int
    bit_depth: int
    peak_dbfs: float
    rms_dbfs: float
    file_size: int
