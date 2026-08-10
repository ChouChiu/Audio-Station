from __future__ import annotations

import resource

import numpy as np
import pytest

from audio_station.application.models import Algorithm, CancellationToken
from audio_station.audio import create_pcm_audio
from audio_station.dsp import process_audio


@pytest.mark.slow
def test_fifteen_minute_stereo_rss_length_and_seams():
    """Synthetic migration evidence only; this is not a real-music quality claim."""
    sample_rate = 44_100
    frames = 15 * 60 * sample_rate
    song = create_pcm_audio(2, frames, sample_rate)
    reference = create_pcm_audio(2, frames, sample_rate)
    output = create_pcm_audio(2, frames, sample_rate)
    try:
        fill_block = sample_rate * 10
        for start in range(0, frames, fill_block):
            end = min(start + fill_block, frames)
            time = np.arange(start, end, dtype=np.float64) / sample_rate
            accompaniment = (0.20 * np.sin(2 * np.pi * 110 * time)).astype(np.float32)
            vocal = (0.12 * np.sin(2 * np.pi * 440 * time)).astype(np.float32)
            reference.samples[:, start:end] = accompaniment
            song.samples[:, start:end] = accompaniment + vocal
        song.release_pages()
        reference.release_pages()

        result = process_audio(
            song.samples,
            reference.samples,
            sample_rate,
            0.75,
            Algorithm.REFERENCE_CENTER,
            8,
            CancellationToken(),
            output.samples,
        )
        assert result is output.samples
        assert result.shape == (2, frames)
        assert np.isfinite(result[:, ::sample_rate]).all()
        step = 28 * sample_rate
        seams = np.arange(step, frames, step)
        jumps = np.abs(result[0, seams] - result[0, seams - 1])
        assert float(np.max(jumps, initial=0.0)) < 0.05
        peak_rss_kib = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        print(f"15-minute benchmark peak RSS: {peak_rss_kib / 1024:.1f} MiB")
        assert peak_rss_kib <= 1.5 * 1024 * 1024
    finally:
        output.cleanup()
        reference.cleanup()
        song.cleanup()
