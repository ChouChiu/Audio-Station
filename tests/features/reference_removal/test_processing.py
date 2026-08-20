from pathlib import Path

import numpy as np
import pytest
import soundfile as sf

from features.reference_removal.models import ReferenceJob
from features.reference_removal.processing import run_reference_job
from shared.processing import CancellationToken


def test_reference_pipeline_end_to_end(tmp_path: Path):
    sample_rate = 8_000
    time = np.arange(sample_rate) / sample_rate
    vocal = 0.3 * np.sin(2 * np.pi * 440 * time)
    reference = 0.2 * np.sin(2 * np.pi * 110 * time)
    song = tmp_path / "song.wav"
    accompaniment = tmp_path / "instrumental.wav"
    output = tmp_path / "vocals.wav"
    sf.write(song, np.stack([vocal + reference] * 2, axis=1), sample_rate)
    sf.write(accompaniment, np.stack([reference] * 2, axis=1), sample_rate)
    events = []
    result = run_reference_job(
        ReferenceJob(song, accompaniment, output, 100, 8, False),
        CancellationToken(),
        events.append,
    )
    assert result.outputs == (output.resolve(),)
    assert len(result.audio_stats) == 1
    stats = result.audio_stats[0]
    assert stats.duration_seconds == 1
    assert stats.sample_rate == sample_rate
    assert stats.channels == 2
    assert stats.bit_depth == 24
    assert stats.peak_dbfs <= 0
    assert stats.rms_dbfs < stats.peak_dbfs
    assert stats.file_size == output.stat().st_size
    assert output.is_file()
    data, rate = sf.read(output, always_2d=True)
    assert rate == sample_rate and data.shape == (sample_rate, 2)
    assert events[-1].value == 100


def test_reference_pipeline_rejects_same_song_and_accompaniment(tmp_path: Path):
    song = tmp_path / "song.wav"
    song.touch()
    job = ReferenceJob(
        song,
        song,
        tmp_path / "output.wav",
        100,
        8,
        False,
    )

    with pytest.raises(ValueError, match="song and accompaniment must be different"):
        run_reference_job(job, CancellationToken())


def test_weak_vocal_protection_requires_center_extraction(tmp_path: Path):
    with pytest.raises(ValueError, match="requires center extraction"):
        ReferenceJob(
            song=tmp_path / "song.wav",
            accompaniment=tmp_path / "reference.wav",
            output=tmp_path / "output.wav",
            weak_vocal_protection=True,
        )
