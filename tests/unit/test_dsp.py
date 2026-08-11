import numpy as np
import pytest

from application.models import Algorithm, CancellationToken, ProcessingCancelled
from audio import create_pcm_audio
from dsp import align_audio, process_audio
from dsp.algorithms import process_channel


def corr(first, second):
    return float(np.corrcoef(first, second)[0, 1])


@pytest.fixture(scope="module")
def scene():
    sample_rate = 22_050
    time = np.arange(sample_rate * 3) / sample_rate
    vocal = 0.5 * np.sin(2 * np.pi * 440 * time) + 0.2 * np.sin(2 * np.pi * 880 * time)
    instrumental = 0.4 * np.sin(2 * np.pi * 110 * time) + 0.1 * np.sin(2 * np.pi * 220 * time)
    return sample_rate, vocal, instrumental, vocal + instrumental


def test_alignment_recovers_integer_and_fractional_delay(scene):
    sample_rate, _vocal, instrumental, mix = scene
    delay = int(0.05 * sample_rate)
    delayed = np.pad(instrumental[:-delay], (delay, 0))
    aligned = align_audio(np.stack([mix, mix]), np.stack([delayed, delayed]), sample_rate)
    assert corr(aligned[0, :sample_rate], instrumental[:sample_rate]) > 0.99


@pytest.mark.parametrize("algorithm", list(Algorithm))
def test_all_algorithms_retain_more_vocal_than_reference(scene, algorithm):
    sample_rate, vocal, instrumental, mix = scene
    output = process_channel(mix, instrumental, sample_rate, 0.5, algorithm, 8)
    assert output.shape == mix.shape
    assert np.isfinite(output).all()
    assert corr(output, vocal) > corr(output, instrumental)


def test_reference_center_stereo_mimo_cancels_crossfeed():
    sample_rate = 16_000
    time = np.arange(sample_rate * 2) / sample_rate
    left_ref = np.sin(2 * np.pi * 123 * time) * 0.25
    right_ref = np.sin(2 * np.pi * 181 * time) * 0.23
    left_vocal = np.sin(2 * np.pi * 431 * time) * 0.35
    right_vocal = left_vocal * 0.95
    mix = np.stack(
        [
            left_vocal + 0.72 * left_ref + 0.38 * right_ref,
            right_vocal - 0.26 * left_ref + 0.81 * right_ref,
        ]
    )
    output = process_audio(
        mix, np.stack([left_ref, right_ref]), sample_rate, 1.0, Algorithm.REFERENCE_CENTER, 8
    )
    assert min(corr(output[0], left_vocal), corr(output[1], right_vocal)) > 0.96
    assert max(abs(corr(output[0], left_ref)), abs(corr(output[1], right_ref))) < 0.12


def test_bypass_and_cancellation(scene):
    sample_rate, _vocal, instrumental, mix = scene
    stereo = np.stack([mix, mix])
    assert np.array_equal(
        process_audio(
            stereo,
            np.stack([instrumental] * 2),
            sample_rate,
            0,
            Algorithm.REFERENCE_CENTER,
            8,
        ),
        stereo.astype(np.float32),
    )
    token = CancellationToken()
    token.cancel()
    with pytest.raises(ProcessingCancelled):
        process_audio(
            stereo,
            np.stack([instrumental] * 2),
            sample_rate,
            1,
            Algorithm.REFERENCE_CENTER,
            8,
            token,
        )


def test_process_audio_writes_supplied_disk_buffer(scene):
    sample_rate, _vocal, instrumental, mix = scene
    target = create_pcm_audio(2, mix.size, sample_rate)
    try:
        output = process_audio(
            np.stack([mix] * 2),
            np.stack([instrumental] * 2),
            sample_rate,
            0.5,
            Algorithm.SOFT_MASK,
            8,
            output=target.samples,
        )
        assert output is target.samples
        assert np.isfinite(output).all()
    finally:
        target.cleanup()
