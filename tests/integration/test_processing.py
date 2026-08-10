from pathlib import Path

import numpy as np
import soundfile as sf

from audio_station.application.models import Algorithm, CancellationToken, ReferenceJob
from audio_station.application.processing import run_reference_job


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
        ReferenceJob(song, accompaniment, output, Algorithm.LOSSLESS, 100, 8, False),
        CancellationToken(),
        events.append,
    )
    assert result.outputs == (output.resolve(),)
    assert output.is_file()
    data, rate = sf.read(output, always_2d=True)
    assert rate == sample_rate and data.shape == (sample_rate, 2)
    assert events[-1].value == 100
