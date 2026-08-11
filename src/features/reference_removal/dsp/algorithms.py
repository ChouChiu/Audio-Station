from __future__ import annotations

import mmap

import numpy as np
from scipy.ndimage import gaussian_filter

from features.reference_removal.models import Algorithm
from shared.dsp import fft_frequencies, istft, stft
from shared.processing import CancellationToken


def _release_mapped_pages(values: np.ndarray) -> None:
    base: object = values
    mapping = None
    while isinstance(base, np.ndarray):
        mapping = getattr(base, "_mmap", mapping)
        if getattr(base, "base", None) is None:
            break
        base = base.base
    if mapping is not None:
        mapping.flush()
        if hasattr(mapping, "madvise") and hasattr(mmap, "MADV_DONTNEED"):
            mapping.madvise(mmap.MADV_DONTNEED)


def _smooth(values: np.ndarray, sigma_time: float, sigma_frequency: float = 1.0) -> np.ndarray:
    return gaussian_filter(values, sigma=(max(sigma_time, 0.0), sigma_frequency), mode="reflect")


def _smooth_complex(values: np.ndarray, sigma_time: float) -> np.ndarray:
    return _smooth(values.real, sigma_time) + 1j * _smooth(values.imag, sigma_time)


def _time_sigma(sigma: float, sample_rate: int, hop: int) -> float:
    """Keep smoothing duration stable when the input sample rate changes."""
    return max(float(sigma) * sample_rate / (44_100.0 * hop / 512.0), 0.0)


def _smoothstep(low: float, high: float, values: np.ndarray) -> np.ndarray:
    x = np.clip((values - low) / (high - low), 0.0, 1.0)
    return x * x * (3.0 - 2.0 * x)


def _phantom_center_focus(
    spectra: list[np.ndarray], sample_rate: int, sigma_time: float
) -> list[np.ndarray]:
    """Keep the coherent phantom center without treating Mid as the center.

    A hard-panned source contributes to Mid in a conventional M/S split.  Here it
    receives a low score because the opposite channel has neither matching power
    nor stable phase.  The statistics are deliberately smoothed before making a
    mask; this trades a little spatial precision for fewer musical-noise artifacts.
    """
    if len(spectra) < 2:
        return spectra
    left, right = spectra[:2]
    epsilon = 1e-10
    # Phantom Center's Smooth control is most useful as an artifact guard.  Keep
    # at least about 70 ms of context even when reference cancellation is set fast.
    smooth = max(float(sigma_time), 6.0 * sample_rate / 44_100.0)
    left_power = _smooth(np.abs(left) ** 2, smooth)
    right_power = _smooth(np.abs(right) ** 2, smooth)
    cross = _smooth_complex(left * np.conj(right), smooth)

    coherence = np.clip(np.abs(cross) ** 2 / (left_power * right_power + epsilon), 0.0, 1.0)
    phase_delta = np.angle(cross)
    half_delta = 0.5 * np.abs(phase_delta)
    phase_overlap = np.maximum(0.0, np.cos(half_delta) - np.sin(half_delta))
    coherence_gate = _smoothstep(0.03, 0.35, coherence)

    # The common amplitude follows Phantom Center's observable LCR behaviour:
    # equal in-phase channels pass unchanged, a same-phase panned source contributes
    # the quieter channel, and phase offsets taper to zero at 90 degrees.
    left_amplitude = np.sqrt(left_power)
    right_amplitude = np.sqrt(right_power)
    common_amplitude = np.minimum(left_amplitude, right_amplitude) * phase_overlap * coherence_gate
    left_to_center = common_amplitude / (left_amplitude + epsilon) * np.exp(-0.5j * phase_delta)
    right_to_center = common_amplitude / (right_amplitude + epsilon) * np.exp(0.5j * phase_delta)
    center = 0.5 * (left_to_center * left + right_to_center * right)

    frequencies = fft_frequencies(sample_rate, 2 * (left.shape[1] - 1))
    high_pass = _smoothstep(80.0, 160.0, frequencies)
    low_pass = 1.0 - _smoothstep(9_000.0, 14_000.0, frequencies)
    vocal_band = high_pass * low_pass

    # Live lead vocals are not a mathematically perfect center after venue reverb,
    # camera processing and reference cancellation.  Keep a generous dry floor and
    # lift only the coherent center so the spatial cleanup does not push the singer
    # behind the residual crowd.
    side_floor = 0.35
    center_gain = 1.25
    band = vocal_band[None, :]
    focused_left = side_floor * left + (center_gain - side_floor) * center
    focused_right = side_floor * right + (center_gain - side_floor) * center
    return [left + band * (focused_left - left), right + band * (focused_right - right)]


def _content_frequencies(ref_spec: np.ndarray, sample_rate: int, hop: int) -> np.ndarray:
    bins = ref_spec.shape[1]
    n_fft = 2 * (bins - 1)
    expected = 2 * np.pi * np.arange(bins) * hop / n_fft
    advance = np.angle(ref_spec[1:] * np.conj(ref_spec[:-1]))
    median = np.median(advance, axis=0)
    median += 2 * np.pi * np.round((expected - median) / (2 * np.pi))
    return np.clip(median * sample_rate / (2 * np.pi * hop), 0, sample_rate / 2)


def _weighted_median(values: np.ndarray, weights: np.ndarray) -> float:
    order = np.argsort(values)
    ordered_values, ordered_weights = values[order], weights[order]
    index = int(np.searchsorted(np.cumsum(ordered_weights), np.sum(ordered_weights) / 2))
    return float(ordered_values[min(index, ordered_values.size - 1)])


def _estimate_drift(
    song_spec: np.ndarray,
    ref_spec: np.ndarray,
    sample_rate: int,
    hop: int,
    frequency_limit: float | None,
) -> float:
    if song_spec.shape[0] < 4:
        return 0.0
    power = np.sum(np.abs(ref_spec) ** 2, axis=0)
    frequencies = _content_frequencies(ref_spec, sample_rate, hop)
    cross = song_spec * np.conj(ref_spec)
    phase_step = np.median(np.angle(cross[1:] * np.conj(cross[:-1])), axis=0)
    valid = (power > np.max(power, initial=0) * 1e-6) & (frequencies > 20)
    if frequency_limit:
        valid &= frequencies <= frequency_limit
    if not np.any(valid):
        return 0.0
    rates = phase_step[valid] * sample_rate / (2 * np.pi * frequencies[valid] * hop)
    finite = np.isfinite(rates) & (np.abs(rates) <= 0.03)
    if not np.any(finite):
        return 0.0
    return _weighted_median(rates[finite], power[valid][finite])


def _compensate_drift(
    song_spec: np.ndarray, ref_spec: np.ndarray, sample_rate: int, hop: int
) -> np.ndarray:
    frequencies = _content_frequencies(ref_spec, sample_rate, hop)
    corrected = ref_spec.copy()
    rate = 0.0
    time = np.arange(ref_spec.shape[0], dtype=np.float64) * hop / sample_rate
    for frequency_limit in (1200.0, None):
        phase = 2 * np.pi * time[:, None] * frequencies[None, :] * rate
        corrected = ref_spec * np.exp(1j * phase)
        rate += _estimate_drift(song_spec, corrected, sample_rate, hop, frequency_limit)
        rate = float(np.clip(rate, -0.03, 0.03))
    phase = 2 * np.pi * time[:, None] * frequencies[None, :] * rate
    return ref_spec * np.exp(1j * phase)


def _reference_center_channel(
    song_spec: np.ndarray,
    ref_spec: np.ndarray,
    strength: float,
    sigma: float,
    sample_rate: int,
    hop: int,
):
    epsilon = 1e-10
    ref_spec = _compensate_drift(song_spec, ref_spec, sample_rate, hop)
    smooth = _time_sigma(sigma, sample_rate, hop)
    cross = _smooth_complex(song_spec * np.conj(ref_spec), smooth)
    ref_power = _smooth(np.abs(ref_spec) ** 2, smooth)
    song_power = _smooth(np.abs(song_spec) ** 2, smooth)
    transfer = cross / (ref_power + epsilon)
    magnitude = np.abs(transfer)
    transfer *= np.minimum(1.0, 2.0 / (magnitude + epsilon))
    coherence = np.clip(np.abs(cross) ** 2 / (song_power * ref_power + epsilon), 0.0, 1.0)
    gate = _smoothstep(0.12, 0.62, coherence)
    prediction = transfer * ref_spec
    # Never subtract more predicted power than the observed mixture can support.
    prediction *= np.minimum(1.0, np.abs(song_spec) / (np.abs(prediction) + epsilon))
    return song_spec - strength * gate * prediction


def _reference_center_stereo(
    song: np.ndarray,
    reference: np.ndarray,
    sample_rate: int,
    strength: float,
    sigma: float,
    token: CancellationToken,
) -> np.ndarray:
    length = min(song.shape[1], reference.shape[1])
    if song.shape[0] < 2 or reference.shape[0] < 2:
        return np.stack(
            [
                process_channel(
                    song[c],
                    reference[min(c, reference.shape[0] - 1)],
                    sample_rate,
                    strength,
                    Algorithm.REFERENCE_CENTER,
                    sigma,
                    token=token,
                )
                for c in range(song.shape[0])
            ]
        )
    y = [stft(song[c, :length]) for c in range(2)]
    x = [stft(reference[c, :length]) for c in range(2)]
    combined_song = y[0] + y[1]
    combined_reference = x[0] + x[1]
    compensated = _compensate_drift(combined_song, combined_reference, sample_rate, 512)
    ratio = np.divide(
        compensated,
        combined_reference,
        out=np.ones_like(compensated),
        where=np.abs(combined_reference) > 1e-12,
    )
    x = [channel * ratio for channel in x]
    token.raise_if_cancelled()
    epsilon = 1e-9
    smooth = _time_sigma(sigma, sample_rate, 512)
    r11 = _smooth(np.abs(x[0]) ** 2, smooth)
    r22 = _smooth(np.abs(x[1]) ** 2, smooth)
    r12 = _smooth_complex(x[0] * np.conj(x[1]), smooth)
    trace = r11 + r22
    regularizer = 1e-5 * trace + epsilon
    a11, a22 = r11 + regularizer, r22 + regularizer
    determinant = a11 * a22 - np.abs(r12) ** 2
    determinant = np.maximum(determinant, epsilon)
    residuals = []
    for mixture in y:
        b1 = _smooth_complex(mixture * np.conj(x[0]), smooth)
        b2 = _smooth_complex(mixture * np.conj(x[1]), smooth)
        h1 = (b1 * a22 - b2 * np.conj(r12)) / determinant
        h2 = (b2 * a11 - b1 * r12) / determinant
        for transfer in (h1, h2):
            magnitude = np.abs(transfer)
            transfer *= np.minimum(1.0, 2.0 / (magnitude + epsilon))
        prediction = h1 * x[0] + h2 * x[1]
        mixture_power = _smooth(np.abs(mixture) ** 2, smooth)
        prediction_power = _smooth(np.abs(prediction) ** 2, smooth)
        reliability = np.clip(prediction_power / (mixture_power + epsilon), 0.0, 1.0)
        gate = _smoothstep(0.04, 0.35, reliability)
        prediction *= np.minimum(1.0, np.abs(mixture) / (np.abs(prediction) + epsilon))
        residuals.append(mixture - strength * gate * prediction)
    # The spatial stage only needs the two residual spectra.  Releasing MIMO
    # workspaces here keeps long-file peak RSS below the disk-buffered pipeline's
    # acceptance limit without changing any samples.
    del (
        a11,
        a22,
        b1,
        b2,
        combined_reference,
        combined_song,
        compensated,
        determinant,
        gate,
        h1,
        h2,
        magnitude,
        mixture,
        mixture_power,
        prediction,
        prediction_power,
        r11,
        r12,
        r22,
        ratio,
        reliability,
        regularizer,
        trace,
        transfer,
        x,
        y,
    )
    residuals = _phantom_center_focus(residuals, sample_rate, smooth)
    return np.asarray([istft(values, length=length) for values in residuals], dtype=np.float64)


def process_channel(
    song: np.ndarray,
    reference: np.ndarray,
    sample_rate: int,
    strength: float,
    algorithm: Algorithm,
    sigma: float,
    *,
    n_fft: int = 2048,
    hop: int = 512,
    token: CancellationToken | None = None,
) -> np.ndarray:
    cancel = token or CancellationToken()
    mix = np.asarray(song, dtype=np.float64)
    accompaniment = np.asarray(reference, dtype=np.float64)
    if mix.ndim != 1 or accompaniment.ndim != 1 or not mix.size or not accompaniment.size:
        return np.empty(0, dtype=np.float64)
    length = min(mix.size, accompaniment.size)
    strength = float(np.clip(strength, 0.0, 1.0))
    if strength == 0:
        return mix[:length].copy()
    y = stft(mix[:length], n_fft, hop)
    x = stft(accompaniment[:length], n_fft, hop)
    frames = min(y.shape[0], x.shape[0])
    y, x = y[:frames], x[:frames]
    cancel.raise_if_cancelled()
    y_mag, x_mag = np.abs(y), np.abs(x)
    adjusted = strength * x_mag
    epsilon = 1e-10
    if algorithm == Algorithm.REFERENCE_CENTER:
        output_spec = _reference_center_channel(y, x, strength, sigma, sample_rate, hop)
    elif algorithm == Algorithm.SOFT_MASK:
        mask = y_mag**2 / (y_mag**2 + adjusted**2 + epsilon)
        output_spec = y * mask
    elif algorithm == Algorithm.SPECTRAL_SUBTRACTION:
        magnitude = np.maximum(0.0, y_mag - adjusted)
        output_spec = magnitude * np.exp(1j * np.angle(y))
    elif algorithm == Algorithm.WIENER_FILTER:
        vocal_power = np.maximum(0.0, y_mag**2 - adjusted**2)
        mask = np.maximum(vocal_power / (vocal_power + adjusted**2 + epsilon), 0.01)
        output_spec = y * mask
    elif algorithm == Algorithm.FREQUENCY_WEIGHTED:
        frequencies = fft_frequencies(sample_rate, n_fft)
        weight = np.where((frequencies >= 80) & (frequencies <= 1100), 1.5, 1.0)
        weighted = y_mag * weight[None, :]
        mask = weighted**2 / (weighted**2 + adjusted**2 + epsilon)
        output_spec = y * mask
    elif algorithm == Algorithm.BINARY_MASK:
        mask = gaussian_filter((y_mag > 1.2 * adjusted).astype(np.float64), 0.5, mode="reflect")
        output_spec = y * mask
    elif algorithm == Algorithm.PHASE_SENSITIVE:
        difference = np.abs(np.angle(y) - np.angle(x))
        difference = np.minimum(difference, 2 * np.pi - difference) / np.pi
        amplitude = y_mag**2 / (y_mag**2 + adjusted**2 + epsilon)
        output_spec = y * np.clip(amplitude * (0.7 + 0.3 * difference), 0.0, 1.0)
    else:  # pragma: no cover - protected by Algorithm
        raise ValueError(f"unsupported algorithm: {algorithm}")
    cancel.raise_if_cancelled()
    return istft(output_spec, hop, length)


def _process_block(
    song: np.ndarray,
    reference: np.ndarray,
    sample_rate: int,
    strength: float,
    algorithm: Algorithm,
    sigma: float,
    token: CancellationToken,
) -> np.ndarray:
    if algorithm == Algorithm.REFERENCE_CENTER and song.shape[0] >= 2 and reference.shape[0] >= 2:
        return _reference_center_stereo(
            song[:2],
            reference[:2],
            sample_rate,
            strength,
            sigma,
            token,
        )
    return np.stack(
        [
            process_channel(
                song[channel],
                reference[min(channel, reference.shape[0] - 1)],
                sample_rate,
                strength,
                algorithm,
                sigma,
                token=token,
            )
            for channel in range(song.shape[0])
        ]
    )


def process_audio(
    song: np.ndarray,
    reference: np.ndarray,
    sample_rate: int,
    strength: float,
    algorithm: Algorithm,
    sigma: float,
    token: CancellationToken | None = None,
    output: np.ndarray | None = None,
) -> np.ndarray:
    cancel = token or CancellationToken()
    mix = np.asarray(song, dtype=np.float32)
    accompaniment = np.asarray(reference, dtype=np.float32)
    if mix.ndim != 2 or accompaniment.ndim != 2 or sample_rate <= 0:
        raise ValueError("audio must have shape [channels, frames] and a positive sample rate")
    length = min(mix.shape[1], accompaniment.shape[1])
    mix, accompaniment = mix[:, :length], accompaniment[:, :length]
    if output is None:
        result = np.empty((mix.shape[0], length), dtype=np.float32)
    else:
        if output.shape != (mix.shape[0], length) or output.dtype != np.float32:
            raise ValueError("output must be a float32 [channels, frames] array")
        result = output
    if strength <= 0:
        result[:] = mix
        return result
    block = 30 * sample_rate
    overlap = min(2 * sample_rate, block // 4)
    step = block - overlap
    starts = list(range(0, length, step))
    for index, start in enumerate(starts):
        cancel.raise_if_cancelled()
        end = min(start + block, length)
        processed = _process_block(
            mix[:, start:end],
            accompaniment[:, start:end],
            sample_rate,
            strength,
            algorithm,
            sigma,
            cancel,
        )
        fade = min(overlap, end - start) if index > 0 else 0
        if fade:
            phase = np.linspace(0, np.pi / 2, fade, dtype=np.float64)
            old_weight = np.cos(phase) ** 2
            new_weight = np.sin(phase) ** 2
            result[:, start : start + fade] = (
                result[:, start : start + fade] * old_weight + processed[:, :fade] * new_weight
            )
        result[:, start + fade : end] = processed[:, fade : end - start]
        _release_mapped_pages(mix)
        _release_mapped_pages(accompaniment)
        _release_mapped_pages(result)
        if end == length:
            break
    peak = 0.0
    cleanup_block = 262_144
    for start in range(0, length, cleanup_block):
        view = result[:, start : start + cleanup_block]
        np.nan_to_num(view, copy=False)
        if algorithm == Algorithm.REFERENCE_CENTER:
            peak = max(peak, float(np.max(np.abs(view), initial=0.0)))
        else:
            np.clip(view, -1.0, 1.0, out=view)
    if algorithm == Algorithm.REFERENCE_CENTER and peak > 0.999:
        scale = 0.999 / peak
        for start in range(0, length, cleanup_block):
            result[:, start : start + cleanup_block] *= scale
    return result
