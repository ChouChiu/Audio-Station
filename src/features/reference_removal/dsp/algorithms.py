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


def _fit_direct_matrix(song: np.ndarray, reference: np.ndarray) -> np.ndarray:
    """Fit a robust real-valued stereo transfer without modifying the residual."""
    epsilon = 1e-12
    x = np.asarray(reference, dtype=np.float64)
    y = np.asarray(song, dtype=np.float64)
    x = x - np.mean(x, axis=1, keepdims=True)
    y = y - np.mean(y, axis=1, keepdims=True)
    reference_covariance = x @ x.T / max(x.shape[1], 1)
    trace = float(np.trace(reference_covariance))
    if not np.isfinite(trace) or trace < epsilon:
        return np.zeros((y.shape[0], x.shape[0]), dtype=np.float64)

    def solve(weights: np.ndarray | None = None) -> np.ndarray:
        if weights is None:
            weighted_x = x
            weighted_y = y
            weight_sum = x.shape[1]
        else:
            root = np.sqrt(weights)[None, :]
            weighted_x = x * root
            weighted_y = y * root
            weight_sum = float(np.sum(weights))
        covariance = weighted_x @ weighted_x.T / max(weight_sum, 1.0)
        cross = weighted_y @ weighted_x.T / max(weight_sum, 1.0)
        regularizer = 2e-4 * float(np.trace(covariance)) / max(x.shape[0], 1) + epsilon
        loaded = covariance + regularizer * np.eye(x.shape[0])
        return np.linalg.solve(loaded.T, cross.T).T

    matrix = solve()
    residual = y - matrix @ x
    residual_energy = np.sum(residual * residual, axis=0)
    scale = float(np.median(residual_energy)) + epsilon
    # Down-weight vocal, cheer and transient peaks while estimating the reference
    # path.  The final subtraction still operates directly on the untouched mix.
    weights = np.minimum(1.0, 4.0 * scale / (residual_energy + epsilon))
    matrix = solve(weights)
    row_gain = np.sum(np.abs(matrix), axis=1)
    matrix *= np.minimum(1.0, 2.0 / (row_gain + epsilon))[:, None]
    return matrix


def _smooth(values: np.ndarray, sigma_time: float, sigma_frequency: float = 1.0) -> np.ndarray:
    return gaussian_filter(values, sigma=(max(sigma_time, 0.0), sigma_frequency), mode="reflect")


def _smooth_complex(values: np.ndarray, sigma_time: float) -> np.ndarray:
    return _smooth(values.real, sigma_time) + 1j * _smooth(values.imag, sigma_time)


def _smoothstep(low: float, high: float, values: np.ndarray) -> np.ndarray:
    scaled = np.clip((values - low) / (high - low), 0.0, 1.0)
    return scaled * scaled * (3.0 - 2.0 * scaled)


def _phantom_center_enhance(
    audio: np.ndarray,
    sample_rate: int,
    amount: float,
    weak_vocal_protection: bool,
    token: CancellationToken,
) -> np.ndarray:
    """Apply the confirmed Audition/PhantomCenter-style vocal enhancement."""
    mix = float(np.clip(amount, 0.0, 1.0))
    if audio.shape[0] < 2 or mix <= 0.0:
        return audio
    spectra = [stft(audio[channel]) for channel in range(2)]
    token.raise_if_cancelled()
    left, right = spectra
    epsilon = 1e-10
    # Roughly 90 ms of temporal context at every sample rate suppresses musical
    # noise while retaining syllable attacks.
    smooth = max(0.09 * sample_rate / 512.0, 1.0)
    left_power = _smooth(np.abs(left) ** 2, smooth)
    right_power = _smooth(np.abs(right) ** 2, smooth)
    cross = _smooth_complex(left * np.conj(right), smooth)
    coherence = np.clip(
        np.abs(cross) ** 2 / (left_power * right_power + epsilon),
        0.0,
        1.0,
    )
    phase_delta = np.angle(cross)
    half_delta = 0.5 * np.abs(phase_delta)
    phase_overlap = np.maximum(0.0, np.cos(half_delta) - np.sin(half_delta))
    coherence_gate = _smoothstep(0.03, 0.35, coherence)
    left_amplitude = np.sqrt(left_power)
    right_amplitude = np.sqrt(right_power)
    common_amplitude = np.minimum(left_amplitude, right_amplitude) * phase_overlap * coherence_gate
    mean_power = 0.5 * (left_power + right_power)
    center_share = np.clip(common_amplitude**2 / (mean_power + epsilon), 0.0, 1.0)
    # A quiet live mic can be buried below wide backing and crowd energy. Applying
    # the fixed side floor in those bins reduced the already weak singer by up to
    # 9 dB. Use conventional Mid as a fallback center instead of fading the whole
    # spatial stage out: this keeps a buried center vocal while still suppressing
    # wide backing in sections where nobody is singing.
    center_presence = _smoothstep(0.02, 0.18, center_share)
    center = 0.5 * (
        common_amplitude / (left_amplitude + epsilon) * np.exp(-0.5j * phase_delta) * left
        + common_amplitude / (right_amplitude + epsilon) * np.exp(0.5j * phase_delta) * right
    )
    frequencies = fft_frequencies(sample_rate, 2 * (left.shape[1] - 1))
    vocal_band = _smoothstep(80.0, 160.0, frequencies) * (
        1.0 - _smoothstep(9_000.0, 14_000.0, frequencies)
    )
    side_floor = 0.35
    center_gain = 1.25
    fallback_center = 0.5 * (left + right)
    focused = []
    for channel in spectra:
        full_target = side_floor * channel + (center_gain - side_floor) * center
        if weak_vocal_protection:
            full_target += (1.0 - side_floor) * (1.0 - center_presence) * fallback_center
        target = channel + mix * (full_target - channel)
        focused.append(channel + vocal_band[None, :] * (target - channel))
    token.raise_if_cancelled()
    return np.asarray(
        [istft(channel, length=audio.shape[1]) for channel in focused],
        dtype=np.float64,
    )


def _reference_center_stereo(
    song: np.ndarray,
    reference: np.ndarray,
    sample_rate: int,
    strength: float,
    sigma: float,
    center_extraction: bool,
    weak_vocal_protection: bool,
    token: CancellationToken,
) -> np.ndarray:
    """Apply direct reference cancellation and optional center extraction.

    Estimate only a slowly varying 2x2 real gain matrix, then subtract the
    resulting reference waveform directly. Optional spatial stages are explicit
    so the base result remains a simple polarity-style reference cancellation.
    """
    length = min(song.shape[1], reference.shape[1])
    mix = np.asarray(song[:, :length], dtype=np.float64)
    accompaniment = np.asarray(reference[:, :length], dtype=np.float64)
    if mix.shape[0] < 2 or accompaniment.shape[0] < 2:
        matrix = _fit_direct_matrix(mix, accompaniment)
        return mix - strength * (matrix @ accompaniment)

    window = min(max(round(float(sigma) * sample_rate), 4096), length)
    step = max(window // 3, 1)
    positions: list[float] = []
    matrices: list[np.ndarray] = []
    for center in range(0, length, step):
        token.raise_if_cancelled()
        start = max(0, center - window // 2)
        end = min(length, start + window)
        start = max(0, end - window)
        positions.append(float(center))
        matrices.append(_fit_direct_matrix(mix[:, start:end], accompaniment[:, start:end]))
    if positions[-1] != length - 1:
        positions.append(float(length - 1))
        matrices.append(_fit_direct_matrix(mix[:, -window:], accompaniment[:, -window:]))

    transfer = np.asarray(matrices)
    if transfer.shape[0] >= 3:
        smoothed = transfer.copy()
        for index in range(1, transfer.shape[0] - 1):
            smoothed[index] = np.median(transfer[index - 1 : index + 2], axis=0)
        transfer = smoothed

    residual = mix.copy()
    block_size = 262_144
    for start in range(0, length, block_size):
        token.raise_if_cancelled()
        end = min(start + block_size, length)
        samples = np.arange(start, end, dtype=np.float64)
        for output_channel in range(mix.shape[0]):
            predicted = np.zeros(end - start, dtype=np.float64)
            for input_channel in range(accompaniment.shape[0]):
                gain = np.interp(
                    samples,
                    positions,
                    transfer[:, output_channel, input_channel],
                )
                predicted += gain * accompaniment[input_channel, start:end]
            residual[output_channel, start:end] -= strength * predicted
    if not center_extraction:
        return residual
    # Preserve the confirmed 75% default sound while making the strength slider
    # continuous near bypass. Above the default, only reference cancellation
    # becomes stronger; center enhancement does not keep narrowing the stereo image.
    center_amount = min(strength / 0.75, 1.0)
    return _phantom_center_enhance(
        residual,
        sample_rate,
        center_amount,
        weak_vocal_protection,
        token,
    )


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
    if algorithm == Algorithm.REFERENCE_CENTER:
        cancel.raise_if_cancelled()
        matrix = _fit_direct_matrix(mix[None, :length], accompaniment[None, :length])
        return mix[:length] - strength * matrix[0, 0] * accompaniment[:length]
    y = stft(mix[:length], n_fft, hop)
    x = stft(accompaniment[:length], n_fft, hop)
    frames = min(y.shape[0], x.shape[0])
    y, x = y[:frames], x[:frames]
    cancel.raise_if_cancelled()
    y_mag, x_mag = np.abs(y), np.abs(x)
    adjusted = strength * x_mag
    epsilon = 1e-10
    if algorithm == Algorithm.SOFT_MASK:
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
    center_extraction: bool,
    weak_vocal_protection: bool,
    token: CancellationToken,
) -> np.ndarray:
    if algorithm == Algorithm.REFERENCE_CENTER and song.shape[0] >= 2 and reference.shape[0] >= 2:
        return _reference_center_stereo(
            song[:2],
            reference[:2],
            sample_rate,
            strength,
            sigma,
            center_extraction,
            weak_vocal_protection,
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
    *,
    center_extraction: bool = False,
    weak_vocal_protection: bool = False,
) -> np.ndarray:
    cancel = token or CancellationToken()
    mix = np.asarray(song, dtype=np.float32)
    accompaniment = np.asarray(reference, dtype=np.float32)
    if mix.ndim != 2 or accompaniment.ndim != 2 or sample_rate <= 0:
        raise ValueError("audio must have shape [channels, frames] and a positive sample rate")
    strength = float(np.clip(strength, 0.0, 1.0))
    length = min(mix.shape[1], accompaniment.shape[1])
    mix, accompaniment = mix[:, :length], accompaniment[:, :length]
    if output is None:
        result = np.empty((mix.shape[0], length), dtype=np.float32)
    else:
        if output.shape != (mix.shape[0], length) or output.dtype != np.float32:
            raise ValueError("output must be a float32 [channels, frames] array")
        result = output
    if strength == 0:
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
            center_extraction,
            weak_vocal_protection,
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
    peak_ceiling = 10 ** (-1.0 / 20.0)
    if algorithm == Algorithm.REFERENCE_CENTER and peak > peak_ceiling:
        scale = peak_ceiling / peak
        for start in range(0, length, cleanup_block):
            result[:, start : start + cleanup_block] *= scale
    return result
