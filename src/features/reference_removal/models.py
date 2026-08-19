from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path


class Algorithm(StrEnum):
    REFERENCE_CENTER = "reference_center"
    SOFT_MASK = "soft_mask"
    SPECTRAL_SUBTRACTION = "spectral_subtraction"
    WIENER_FILTER = "wiener_filter"
    FREQUENCY_WEIGHTED = "frequency_weighted"
    BINARY_MASK = "binary_mask"
    PHASE_SENSITIVE = "phase_sensitive"


@dataclass(frozen=True, slots=True)
class ReferenceJob:
    song: Path
    accompaniment: Path
    output: Path
    algorithm: Algorithm = Algorithm.REFERENCE_CENTER
    strength: int = 75
    sigma: int = 8
    auto_align: bool = True
    language: str = "zh_cn"
    center_extraction: bool = False
    weak_vocal_protection: bool = False

    def __post_init__(self) -> None:
        if not 0 <= self.strength <= 100:
            raise ValueError("strength must be in [0, 100]")
        if self.sigma not in {1, 3, 8, 16}:
            raise ValueError("sigma must be one of 1, 3, 8, 16")
        if self.weak_vocal_protection and not self.center_extraction:
            raise ValueError("weak vocal protection requires center extraction")
        if self.algorithm != Algorithm.REFERENCE_CENTER and (
            self.center_extraction or self.weak_vocal_protection
        ):
            raise ValueError("center options require the reference_center algorithm")


@dataclass(frozen=True, slots=True)
class AudioStats:
    duration_seconds: float
    sample_rate: int
    channels: int
    bit_depth: int
    peak_dbfs: float
    rms_dbfs: float
    file_size: int
