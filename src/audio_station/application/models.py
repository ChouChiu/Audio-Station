from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass, field
from enum import StrEnum
from pathlib import Path
from threading import Event


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

    def __post_init__(self) -> None:
        if not 0 <= self.strength <= 100:
            raise ValueError("strength must be in [0, 100]")
        if self.sigma not in {1, 3, 8, 16}:
            raise ValueError("sigma must be one of 1, 3, 8, 16")


@dataclass(frozen=True, slots=True)
class NeuralJob:
    song: Path
    output_dir: Path
    model_id: str = "mdxnet_1"
    models_dir: Path | None = None
    language: str = "zh_cn"


@dataclass(frozen=True, slots=True)
class ProgressEvent:
    value: int
    message: str


@dataclass(frozen=True, slots=True)
class AudioStats:
    duration_seconds: float
    sample_rate: int
    channels: int
    bit_depth: int
    peak_dbfs: float
    rms_dbfs: float
    file_size: int


@dataclass(frozen=True, slots=True)
class ProcessingResult:
    outputs: tuple[Path, ...]
    audio_stats: tuple[AudioStats, ...] = ()


@dataclass(slots=True)
class CancellationToken:
    _event: Event = field(default_factory=Event)

    def cancel(self) -> None:
        self._event.set()

    @property
    def cancelled(self) -> bool:
        return self._event.is_set()

    def raise_if_cancelled(self) -> None:
        if self.cancelled:
            raise ProcessingCancelled


class ProcessingCancelled(RuntimeError):
    pass


ProgressCallback = Callable[[ProgressEvent], None]
