import numpy as np
import pytest

from audio_station.application.models import Algorithm
from audio_station.dsp import align_audio, process_audio


def correlation(first, second):
    return float(np.corrcoef(first, second)[0, 1])


@pytest.mark.parametrize("rate", [0.004, 0.01])
def test_lossless_tracks_tempo_drift(rate):
    sample_rate = 22_050
    length = sample_rate * 5
    time = np.arange(length) / sample_rate
    vocal = 0.5 * np.sin(2 * np.pi * 440 * time) + 0.2 * np.sin(2 * np.pi * 880 * time)
    reference = 0.4 * np.sin(2 * np.pi * 110 * time) + 0.1 * np.sin(2 * np.pi * 220 * time)
    drifted = np.interp(np.arange(length) / (1 + rate), np.arange(length), reference)
    mixture = vocal + drifted
    aligned = align_audio(np.stack([mixture] * 2), np.stack([reference] * 2), sample_rate)
    output = process_audio(
        np.stack([mixture] * 2), aligned, sample_rate, 1.0, Algorithm.LOSSLESS, 8
    )
    assert correlation(output[0], vocal) > 0.98
    assert abs(correlation(output[0], reference)) < 0.05


def test_lossless_preserves_non_reference_audience_noise():
    sample_rate = 22_050
    length = sample_rate * 5
    time = np.arange(length) / sample_rate
    vocal = 0.5 * np.sin(2 * np.pi * 440 * time)
    reference = 0.4 * np.sin(2 * np.pi * 110 * time)
    noise = np.random.default_rng(42).uniform(-0.15, 0.15, length)
    mixture = vocal + reference + noise
    output = process_audio(
        np.stack([mixture] * 2),
        np.stack([reference] * 2),
        sample_rate,
        1.0,
        Algorithm.LOSSLESS,
        8,
    )
    assert correlation(output[0], vocal) > 0.96
    assert correlation(output[0] - vocal, noise) > 0.98


def test_alignment_and_lossless_handle_segment_jitter():
    sample_rate = 22_050
    length = sample_rate * 5
    time = np.arange(length) / sample_rate
    vocal = 0.5 * np.sin(2 * np.pi * 440 * time)
    reference = 0.4 * np.sin(2 * np.pi * 110 * time)
    jittered = np.zeros_like(reference)
    for start in range(0, length, sample_rate):
        end = min(length, start + sample_rate)
        offset = ((start // sample_rate) % 5 - 2) * sample_rate // 500
        source = np.arange(start, end) + offset
        valid = (source >= 0) & (source < length)
        jittered[np.arange(start, end)[valid]] = reference[source[valid]]
    mixture = vocal + jittered
    aligned = align_audio(np.stack([mixture] * 2), np.stack([reference] * 2), sample_rate)
    output = process_audio(
        np.stack([mixture] * 2), aligned, sample_rate, 1.0, Algorithm.LOSSLESS, 8
    )
    assert correlation(output[0], vocal) > 0.96
    assert abs(correlation(output[0], reference)) < 0.08
