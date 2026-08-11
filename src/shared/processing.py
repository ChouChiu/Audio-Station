from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path
from threading import Event


@dataclass(frozen=True, slots=True)
class ProgressEvent:
    value: int
    message: str


@dataclass(frozen=True, slots=True)
class ProcessingResult:
    outputs: tuple[Path, ...]
    audio_stats: tuple[object, ...] = ()


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
