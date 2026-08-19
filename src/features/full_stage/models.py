from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path


class ClipKind(StrEnum):
    SONG = "song"
    FRAGMENT = "fragment"
    UNMATCHED = "unmatched"


@dataclass(frozen=True, slots=True)
class TimelineClip:
    kind: ClipKind
    stage_start: float
    stage_end: float
    source: Path | None = None
    source_index: int | None = None
    source_start: float = 0.0
    source_end: float = 0.0
    confidence: float = 0.0
    enabled: bool = True

    def __post_init__(self) -> None:
        if self.stage_start < 0 or self.stage_end <= self.stage_start:
            raise ValueError("timeline clip must have a positive duration")
        if self.kind == ClipKind.UNMATCHED:
            if self.source is not None or self.source_index is not None:
                raise ValueError("unmatched clips must not reference a source")
            object.__setattr__(self, "enabled", False)
        elif self.source is None or self.source_index is None:
            raise ValueError("matched clips require a source")
        elif self.source_start < 0 or self.source_end <= self.source_start:
            raise ValueError("matched source range must have a positive duration")
        if not 0.0 <= self.confidence <= 1.0:
            raise ValueError("confidence must be in [0, 1]")

    @property
    def duration(self) -> float:
        return self.stage_end - self.stage_start


@dataclass(frozen=True, slots=True)
class FullStageAnalysis:
    duration_seconds: float
    clips: tuple[TimelineClip, ...]
    missing_sources: tuple[Path, ...] = ()

    @property
    def matched_clips(self) -> tuple[TimelineClip, ...]:
        return tuple(
            clip for clip in self.clips if clip.kind != ClipKind.UNMATCHED and clip.enabled
        )

    @property
    def song_clips(self) -> tuple[TimelineClip, ...]:
        return tuple(clip for clip in self.clips if clip.kind == ClipKind.SONG)


@dataclass(frozen=True, slots=True)
class FullStageJob:
    stage: Path
    sources: tuple[Path, ...]
    output: Path
    strength: int = 75
    sigma: int = 8
    language: str = "zh_cn"
    include_fragments: bool = True
    auto_align: bool = True
    center_extraction: bool = False
    weak_vocal_protection: bool = False

    def __post_init__(self) -> None:
        if not self.sources:
            raise ValueError("at least one source is required")
        if not 0 <= self.strength <= 100:
            raise ValueError("strength must be in [0, 100]")
        if self.sigma not in {1, 3, 8, 16}:
            raise ValueError("sigma must be one of 1, 3, 8, 16")
        if self.weak_vocal_protection and not self.center_extraction:
            raise ValueError("weak vocal protection requires center extraction")
        resolved_stage = self.stage.expanduser().resolve()
        resolved_output = self.output.expanduser().resolve()
        resolved_sources = tuple(source.expanduser().resolve() for source in self.sources)
        if resolved_output == resolved_stage or resolved_output in resolved_sources:
            raise ValueError("output path must not overwrite an input file")
        if resolved_stage in resolved_sources:
            raise ValueError("stage audio must not also be a source")


@dataclass(frozen=True, slots=True)
class FullStageResult:
    analysis: FullStageAnalysis
    outputs: tuple[Path, ...] = ()
    audio_stats: tuple[object, ...] = ()
