from __future__ import annotations

import math
import mmap

import numpy as np
from scipy.ndimage import gaussian_filter, gaussian_filter1d, uniform_filter1d

from shared.dsp import ReferenceAlgorithm, fft_frequencies, istft, stft
from shared.processing import CancellationToken

_MASK_FLOOR = 0.05
_CONFIDENCE_LOW = 0.03
_CONFIDENCE_HIGH = 0.35
_DECISION_MEMORY = 0.8
_SPECTRAL_OVERSUBTRACTION = 1.5


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


def _spectral_smooth(
    values: np.ndarray,
    sigma_time: float,
    sigma_frequency: float = 1.0,
) -> np.ndarray:
    """Smooth long spectral contexts in O(N) time along the frame axis."""
    if np.iscomplexobj(values):
        return _spectral_smooth(values.real, sigma_time, sigma_frequency) + 1j * _spectral_smooth(
            values.imag,
            sigma_time,
            sigma_frequency,
        )
    window = max(round(6.0 * sigma_time), 1)
    smoothed = uniform_filter1d(values, size=window, axis=0, mode="reflect")
    if sigma_frequency > 0.0:
        smoothed = gaussian_filter1d(
            smoothed,
            sigma=sigma_frequency,
            axis=1,
            mode="reflect",
        )
    return smoothed


def _weighted_smooth(
    values: np.ndarray,
    weights: np.ndarray,
    sigma_time: float,
) -> np.ndarray:
    denominator = _spectral_smooth(weights, sigma_time) + 1e-12
    numerator = _spectral_smooth(values * weights, sigma_time)
    return numerator / denominator


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


def _direct_reference_cancel(
    song: np.ndarray,
    reference: np.ndarray,
    sample_rate: int,
    strength: float,
    sigma: float,
    token: CancellationToken,
) -> np.ndarray:
    """Apply the legacy direct reference cancellation path.

    Estimate only a slowly varying 2x2 real gain matrix, then subtract the
    resulting reference waveform directly. This path remains available only for
    explicit A/B comparison with the spectral-mask default.
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
    return residual


def _analysis_fft_size(sample_rate: int) -> int:
    target = max(0.046 * sample_rate, 1.0)
    exponent = round(math.log2(target))
    return int(np.clip(2**exponent, 512, 4096))


def _spectral_transfer_prediction(
    song_spectra: list[np.ndarray],
    reference_spectra: list[np.ndarray],
    sigma_frames: float,
    token: CancellationToken,
    weights: np.ndarray | None = None,
) -> list[np.ndarray]:
    """Predict reference-correlated spectra without subtracting them from the mix."""
    epsilon = 1e-12
    x0, x1 = reference_spectra[:2]
    if weights is None:
        weights = np.ones(x0.shape, dtype=np.float32)
    r00 = _weighted_smooth(np.abs(x0) ** 2, weights, sigma_frames).real
    r11 = _weighted_smooth(np.abs(x1) ** 2, weights, sigma_frames).real
    r01 = _weighted_smooth(x0 * np.conj(x1), weights, sigma_frames)
    loading = 2e-4 * 0.5 * (r00 + r11) + epsilon
    r00 += loading
    r11 += loading
    determinant = np.maximum((r00 * r11 - np.abs(r01) ** 2).real, epsilon)
    predicted: list[np.ndarray] = []
    for channel in song_spectra:
        token.raise_if_cancelled()
        ry0 = _weighted_smooth(channel * np.conj(x0), weights, sigma_frames)
        ry1 = _weighted_smooth(channel * np.conj(x1), weights, sigma_frames)
        h0 = (ry0 * r11 - ry1 * np.conj(r01)) / determinant
        h1 = (ry1 * r00 - ry0 * r01) / determinant
        row_gain = np.abs(h0) + np.abs(h1)
        gain_scale = np.minimum(1.0, 2.0 / (row_gain + epsilon))
        predicted.append((gain_scale * (h0 * x0 + h1 * x1)).astype(np.complex64))
    return predicted


def _spectral_mask_extract(
    song: np.ndarray,
    reference: np.ndarray,
    sample_rate: int,
    strength: float,
    sigma: float,
    token: CancellationToken,
) -> np.ndarray:
    """Extract reference-independent content with a confidence-weighted soft mask.

    The estimated complex reference transfer is used only to model accompaniment
    power. Reconstruction always multiplies the original mixture spectra by a
    linked real mask, so this path performs no waveform or complex-spectrum
    polarity subtraction.
    """
    length = min(song.shape[1], reference.shape[1])
    mix = np.asarray(song[:, :length], dtype=np.float64)
    accompaniment = np.asarray(reference[:, :length], dtype=np.float64)
    n_fft = _analysis_fft_size(sample_rate)
    hop = n_fft // 4
    song_spectra = [stft(channel, n_fft=n_fft, hop=hop).astype(np.complex64) for channel in mix]
    token.raise_if_cancelled()
    reference_spectra = [
        stft(channel, n_fft=n_fft, hop=hop).astype(np.complex64) for channel in accompaniment[:2]
    ]
    if not reference_spectra:
        return mix.copy()
    if len(reference_spectra) == 1:
        reference_spectra.append(reference_spectra[0])
    sigma_frames = max(float(sigma) * sample_rate / hop / 6.0, 1.0)

    predicted = _spectral_transfer_prediction(
        song_spectra,
        reference_spectra,
        sigma_frames,
        token,
    )
    residual_power = np.zeros(song_spectra[0].shape, dtype=np.float32)
    for mixture_channel, predicted_channel in zip(song_spectra, predicted, strict=True):
        residual_power += np.abs(mixture_channel - predicted_channel) ** 2
    local_residual = _spectral_smooth(residual_power, sigma_frames)
    robust_weights = np.minimum(1.0, 4.0 * local_residual / (residual_power + 1e-12)).astype(
        np.float32
    )
    predicted = _spectral_transfer_prediction(
        song_spectra,
        reference_spectra,
        sigma_frames,
        token,
        robust_weights,
    )
    token.raise_if_cancelled()

    mixture_power = np.zeros(song_spectra[0].shape, dtype=np.float32)
    removable_power = np.zeros_like(mixture_power)
    predicted_power = np.zeros_like(mixture_power)
    confidence_sigma = max(sigma_frames / 2.0, 1.0)
    for mixture_channel, predicted_channel in zip(song_spectra, predicted, strict=True):
        channel_power = np.abs(mixture_channel) ** 2
        reference_power = np.abs(predicted_channel) ** 2
        smoothed_mix = _spectral_smooth(channel_power, confidence_sigma)
        smoothed_reference = _spectral_smooth(reference_power, confidence_sigma)
        cross = _spectral_smooth(
            mixture_channel * np.conj(predicted_channel),
            confidence_sigma,
        )
        coherence = np.clip(
            np.abs(cross) ** 2 / (smoothed_mix * smoothed_reference + 1e-12),
            0.0,
            1.0,
        )
        confidence = _smoothstep(_CONFIDENCE_LOW, _CONFIDENCE_HIGH, coherence)
        mixture_power += channel_power
        predicted_power += reference_power
        removable_power += confidence * reference_power

    instantaneous = np.maximum(
        mixture_power - _SPECTRAL_OVERSUBTRACTION * removable_power,
        (_MASK_FLOOR**2) * mixture_power,
    )
    directed = np.empty_like(instantaneous)
    directed[0] = instantaneous[0]
    for frame in range(1, instantaneous.shape[0]):
        if frame % 256 == 0:
            token.raise_if_cancelled()
        directed[frame] = (
            _DECISION_MEMORY * directed[frame - 1] + (1.0 - _DECISION_MEMORY) * instantaneous[frame]
        )
    mask = np.sqrt(np.clip(directed / (mixture_power + 1e-12), _MASK_FLOOR**2, 1.0))
    # Keep frequency smoothing narrow: a full-bin blur refills tonal notches from
    # their untouched neighbours and leaves clearly audible reference harmonics.
    mask = gaussian_filter(mask, sigma=(0.75, 0.35), mode="reflect")

    mixture_magnitude = np.sqrt(mixture_power)
    reference_magnitude = np.sqrt(predicted_power)
    mixture_flux = np.maximum(
        np.diff(mixture_magnitude, axis=0, prepend=mixture_magnitude[:1]), 0.0
    )
    reference_flux = np.maximum(
        np.diff(reference_magnitude, axis=0, prepend=reference_magnitude[:1]),
        0.0,
    )
    novelty = np.clip(
        (mixture_flux - reference_flux) / (mixture_flux + 1e-12),
        0.0,
        1.0,
    )
    flux_activity = np.clip(mixture_flux / (mixture_magnitude + 1e-12), 0.0, 1.0)
    novelty *= _smoothstep(0.02, 0.2, flux_activity)
    reference_confidence = np.clip(
        removable_power / (predicted_power + 1e-12),
        0.0,
        1.0,
    )
    novelty *= 1.0 - _smoothstep(0.5, 0.9, reference_confidence)
    mask = np.maximum(mask, _smoothstep(0.15, 0.75, novelty))
    effective_mask = 1.0 - strength * (1.0 - np.clip(mask, _MASK_FLOOR, 1.0))
    token.raise_if_cancelled()
    return np.asarray(
        [istft(channel * effective_mask, hop=hop, length=length) for channel in song_spectra],
        dtype=np.float64,
    )


def process_audio(
    song: np.ndarray,
    reference: np.ndarray,
    sample_rate: int,
    strength: float,
    sigma: float,
    token: CancellationToken | None = None,
    output: np.ndarray | None = None,
    *,
    algorithm: ReferenceAlgorithm | str = ReferenceAlgorithm.SPECTRAL_MASK,
    center_extraction: bool = False,
    weak_vocal_protection: bool = False,
) -> np.ndarray:
    cancel = token or CancellationToken()
    mix = np.asarray(song, dtype=np.float32)
    accompaniment = np.asarray(reference, dtype=np.float32)
    if mix.ndim != 2 or accompaniment.ndim != 2 or sample_rate <= 0:
        raise ValueError("audio must have shape [channels, frames] and a positive sample rate")
    selected_algorithm = ReferenceAlgorithm(algorithm)
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
        if selected_algorithm == ReferenceAlgorithm.SPECTRAL_MASK:
            processed = _spectral_mask_extract(
                mix[:, start:end],
                accompaniment[:, start:end],
                sample_rate,
                strength,
                sigma,
                cancel,
            )
        else:
            processed = _direct_reference_cancel(
                mix[:, start:end],
                accompaniment[:, start:end],
                sample_rate,
                strength,
                sigma,
                cancel,
            )
        if center_extraction:
            # Preserve the confirmed 75% enhancement sound while making the
            # strength slider continuous near bypass. Above 75%, only the core
            # extractor becomes stronger instead of narrowing the image further.
            center_amount = min(strength / 0.75, 1.0)
            processed = _phantom_center_enhance(
                processed,
                sample_rate,
                center_amount,
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
        peak = max(peak, float(np.max(np.abs(view), initial=0.0)))
    peak_ceiling = 10 ** (-1.0 / 20.0)
    if peak > peak_ceiling:
        scale = peak_ceiling / peak
        for start in range(0, length, cleanup_block):
            result[:, start : start + cleanup_block] *= scale
    return result
