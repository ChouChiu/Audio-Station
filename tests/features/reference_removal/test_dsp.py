import numpy as np
import pytest

from features.reference_removal.dsp import align_audio, alignment, process_audio
from features.reference_removal.dsp.alignment import _spectral_flux_lag
from shared.audio import create_pcm_audio
from shared.processing import CancellationToken, ProcessingCancelled


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


def test_alignment_recovers_delay_from_inverted_reference():
    sample_rate = 22_050
    rng = np.random.default_rng(72)
    instrumental = np.convolve(rng.normal(0.0, 0.1, sample_rate * 3), np.ones(9) / 9, mode="same")
    time = np.arange(instrumental.size) / sample_rate
    mix = instrumental + 0.3 * np.sin(2 * np.pi * 443 * time)
    delay = int(0.04 * sample_rate)
    inverted = -np.pad(instrumental[:-delay], (delay, 0))

    aligned = align_audio(np.stack([mix, mix]), np.stack([inverted, inverted]), sample_rate)

    assert (
        corr(aligned[0, sample_rate : 2 * sample_rate], instrumental[sample_rate : 2 * sample_rate])
        < -0.99
    )


def test_alignment_proxy_rejects_aliased_high_frequency_vocals():
    sample_rate = 22_050
    length = sample_rate * 4
    time = np.arange(length) / sample_rate
    rng = np.random.default_rng(91)
    reference = (
        0.12 * np.sin(2 * np.pi * 170 * time)
        + 0.08 * np.sin(2 * np.pi * 310 * time)
        + 0.04 * rng.normal(size=length)
    )
    delay = sample_rate // 25
    delayed = np.pad(reference[:-delay], (delay, 0))
    # This strong component is above the 2 kHz proxy's Nyquist frequency.  A
    # strided proxy aliases it into the alignment band even though it is absent
    # from the reference.
    unrelated_vocal = 0.45 * np.sin(2 * np.pi * 7_130 * time)
    mixture = delayed + unrelated_vocal

    aligned = align_audio(
        np.stack([mixture, mixture]),
        np.stack([reference, reference]),
        sample_rate,
    )

    assert (
        corr(aligned[0, sample_rate : 3 * sample_rate], delayed[sample_rate : 3 * sample_rate])
        > 0.98
    )


def test_spectral_flux_alignment_handles_different_mastering_and_polarity():
    sample_rate = 8_000
    length = sample_rate * 12
    rng = np.random.default_rng(124)
    reference = np.convolve(
        rng.normal(0.0, 0.08, length),
        np.ones(9) / 9,
        mode="same",
    )
    delay = round(0.36 * sample_rate)
    shifted = -np.pad(reference[:-delay], (delay, 0))
    time = np.arange(length) / sample_rate
    stage = (
        np.tanh(1.3 * shifted) / 1.3
        + 0.03 * np.sin(2 * np.pi * 431 * time)
        + rng.normal(0.0, 0.005, length)
    )

    lag = _spectral_flux_lag(
        np.stack([stage, 0.97 * stage]),
        np.stack([reference, 0.92 * reference]),
        sample_rate,
    )

    assert lag == pytest.approx(delay, abs=0.02 * sample_rate)


def test_confident_musical_offset_is_not_overridden_by_gcc(monkeypatch):
    sample_rate = 8_000
    rng = np.random.default_rng(125)
    reference = rng.normal(0.0, 0.08, (2, sample_rate * 3))
    mixture = reference + rng.normal(0.0, 0.01, reference.shape)
    musical_lag = 0.8 * sample_rate

    monkeypatch.setattr(
        alignment,
        "_spectral_flux_lag",
        lambda *_args, **_kwargs: musical_lag,
    )

    def unexpected_gcc(*_args, **_kwargs):
        raise AssertionError("GCC must only run when musical onset matching fails")

    monkeypatch.setattr(alignment, "_gcc_phat_lag", unexpected_gcc)

    aligned = align_audio(mixture, reference, sample_rate)

    assert aligned.shape == reference.shape


def test_reference_cancellation_stereo_mimo_cancels_crossfeed():
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
    output = process_audio(mix, np.stack([left_ref, right_ref]), sample_rate, 1.0, 8)
    assert min(corr(output[0], left_vocal), corr(output[1], right_vocal)) > 0.96
    assert max(abs(corr(output[0], left_ref)), abs(corr(output[1], right_ref))) < 0.12


def test_reference_cancellation_cancels_inverted_reference_then_enhances_center():
    sample_rate = 16_000
    length = sample_rate * 4
    rng = np.random.default_rng(93)
    reference = rng.normal(0.0, 0.08, (2, length))
    time = np.arange(length) / sample_rate
    live = np.stack(
        [
            0.3 * np.sin(2 * np.pi * 431 * time),
            0.27 * np.sin(2 * np.pi * 431 * time),
        ]
    )
    inverted_transfer = np.asarray([[-0.72, 0.18], [-0.11, -0.83]])
    mixture = live + inverted_transfer @ reference

    output = process_audio(
        mixture,
        reference,
        sample_rate,
        1.0,
        8,
        center_extraction=True,
    )

    assert corr(output.ravel(), live.ravel()) > 0.995
    assert abs(corr(output.ravel(), reference.ravel())) < 0.01
    center_gain = np.dot(output.ravel(), live.ravel()) / np.dot(live.ravel(), live.ravel())
    assert 1.1 < center_gain < 1.3


def test_reference_cancellation_defaults_to_plain_cancellation_without_spatial_extraction():
    sample_rate = 16_000
    length = sample_rate * 2
    time = np.arange(length) / sample_rate
    centered = 0.2 * np.sin(2 * np.pi * 431 * time)
    mixture = np.stack(
        [
            centered + 0.05 * np.sin(2 * np.pi * 173 * time),
            centered + 0.05 * np.sin(2 * np.pi * 227 * time),
        ]
    )

    output = process_audio(
        mixture,
        np.zeros_like(mixture),
        sample_rate,
        0.75,
        8,
    )

    assert np.allclose(output, mixture, atol=1e-7)


def test_bypass_and_cancellation(scene):
    sample_rate, _vocal, instrumental, mix = scene
    stereo = np.stack([mix, mix])
    assert np.array_equal(
        process_audio(
            stereo,
            np.stack([instrumental] * 2),
            sample_rate,
            0,
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
            8,
            token,
        )


def test_reference_cancellation_applies_linked_minus_one_db_peak_protection():
    sample_rate = 16_000
    length = sample_rate * 2
    time = np.arange(length) / sample_rate
    centered = 0.98 * np.sin(2 * np.pi * 431 * time)
    reference = np.stack(
        [
            0.05 * np.sin(2 * np.pi * 123 * time),
            0.04 * np.sin(2 * np.pi * 181 * time),
        ]
    )
    mixture = np.stack([centered, 0.8 * centered]) + reference

    output = process_audio(
        mixture,
        reference,
        sample_rate,
        1.0,
        8,
    )

    ceiling = 10 ** (-1.0 / 20.0)
    assert np.max(np.abs(output)) <= ceiling + 1e-6
    assert np.max(np.abs(output[0])) == pytest.approx(ceiling, abs=1e-6)
    assert np.max(np.abs(output[1])) < ceiling


def test_reference_cancellation_strength_fades_center_enhancement_from_bypass():
    sample_rate = 16_000
    length = sample_rate * 2
    time = np.arange(length) / sample_rate
    centered = 0.25 * np.sin(2 * np.pi * 431 * time)
    wide = np.stack(
        [
            0.08 * np.sin(2 * np.pi * 173 * time),
            0.08 * np.sin(2 * np.pi * 227 * time),
        ]
    )
    mixture = np.stack([centered, centered]) + wide
    silent_reference = np.zeros_like(mixture)

    low = process_audio(
        mixture,
        silent_reference,
        sample_rate,
        0.01,
        8,
        center_extraction=True,
    )
    default = process_audio(
        mixture,
        silent_reference,
        sample_rate,
        0.75,
        8,
        center_extraction=True,
    )

    low_change = np.sqrt(np.mean((low - mixture) ** 2))
    default_change = np.sqrt(np.mean((default - mixture) ** 2))
    assert low_change < 0.03 * default_change


def test_reference_cancellation_clamps_strength_above_one(scene):
    sample_rate, _vocal, instrumental, mix = scene
    stereo_mix = np.stack([mix, mix])
    stereo_reference = np.stack([instrumental, instrumental])

    maximum = process_audio(
        stereo_mix,
        stereo_reference,
        sample_rate,
        1.0,
        8,
    )
    excessive = process_audio(
        stereo_mix,
        stereo_reference,
        sample_rate,
        5.0,
        8,
    )

    assert np.array_equal(excessive, maximum)


def test_process_audio_writes_supplied_disk_buffer(scene):
    sample_rate, _vocal, instrumental, mix = scene
    target = create_pcm_audio(2, mix.size, sample_rate)
    try:
        output = process_audio(
            np.stack([mix] * 2),
            np.stack([instrumental] * 2),
            sample_rate,
            0.5,
            8,
            output=target.samples,
        )
        assert output is target.samples
        assert np.isfinite(output).all()
    finally:
        target.cleanup()
