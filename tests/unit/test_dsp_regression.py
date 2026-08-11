import numpy as np
import pytest

from application.models import Algorithm
from dsp import align_audio, process_audio


def correlation(first, second):
    return float(np.corrcoef(first, second)[0, 1])


@pytest.mark.parametrize("rate", [0.004, 0.01])
def test_reference_center_tracks_tempo_drift(rate):
    sample_rate = 22_050
    length = sample_rate * 5
    time = np.arange(length) / sample_rate
    vocal = 0.5 * np.sin(2 * np.pi * 440 * time) + 0.2 * np.sin(2 * np.pi * 880 * time)
    reference = 0.4 * np.sin(2 * np.pi * 110 * time) + 0.1 * np.sin(2 * np.pi * 220 * time)
    drifted = np.interp(np.arange(length) / (1 + rate), np.arange(length), reference)
    mixture = vocal + drifted
    aligned = align_audio(np.stack([mixture] * 2), np.stack([reference] * 2), sample_rate)
    output = process_audio(
        np.stack([mixture] * 2), aligned, sample_rate, 1.0, Algorithm.REFERENCE_CENTER, 8
    )
    assert correlation(output[0], vocal) > 0.98
    assert abs(correlation(output[0], reference)) < 0.05


def test_reference_center_preserves_centered_non_reference_content():
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
        Algorithm.REFERENCE_CENTER,
        8,
    )
    assert correlation(output[0], vocal) > 0.96
    assert correlation(output[0], vocal + noise) > 0.96


def test_smooth_center_focus_rejects_wide_audience_without_damaging_center_vocal():
    sample_rate = 22_050
    length = sample_rate * 5
    time = np.arange(length) / sample_rate
    vocal = 0.35 * np.sin(2 * np.pi * 440 * time)
    left_reference = 0.18 * np.sin(2 * np.pi * 123 * time)
    right_reference = 0.16 * np.sin(2 * np.pi * 181 * time)
    rng = np.random.default_rng(7)
    left_audience = rng.normal(0.0, 0.08, length)
    right_audience = rng.normal(0.0, 0.08, length)
    mixture = np.stack(
        [
            vocal + left_reference + left_audience,
            vocal + right_reference + right_audience,
        ]
    )
    reference = np.stack([left_reference, right_reference])
    focused = process_audio(mixture, reference, sample_rate, 1.0, Algorithm.REFERENCE_CENTER, 8)
    assert correlation(focused[0], vocal) > 0.98
    vocal_gain = np.dot(focused[0], vocal) / np.dot(vocal, vocal)
    assert vocal_gain >= 1.0
    focused_noise = focused - vocal_gain * vocal
    original_noise = np.stack([left_audience, right_audience])
    assert np.sqrt(np.mean(focused_noise**2)) < 0.70 * np.sqrt(np.mean(original_noise**2))


def test_alignment_and_reference_center_handle_segment_jitter():
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
        np.stack([mixture] * 2), aligned, sample_rate, 1.0, Algorithm.REFERENCE_CENTER, 8
    )
    assert correlation(output[0], vocal) > 0.96
    assert abs(correlation(output[0], reference)) < 0.08
