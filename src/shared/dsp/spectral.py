from __future__ import annotations

import numpy as np
from scipy.signal import get_window


def hann(size: int) -> np.ndarray:
    if size <= 0:
        return np.empty(0, dtype=np.float64)
    return get_window("hann", size, fftbins=True).astype(np.float64)


def stft(signal: np.ndarray, n_fft: int = 2048, hop: int = 512) -> np.ndarray:
    """librosa-compatible centered STFT returned as [frames, bins]."""
    values = np.asarray(signal, dtype=np.float64)
    if values.ndim != 1 or values.size == 0 or n_fft <= 0 or n_fft % 2 or hop <= 0:
        return np.empty((0, 0), dtype=np.complex128)
    if values.size == 1:
        padded = np.pad(values, (n_fft // 2, n_fft // 2), mode="edge")
    else:
        padded = np.pad(values, (n_fft // 2, n_fft // 2), mode="reflect")
    remainder = (padded.size - n_fft) % hop
    if remainder:
        padded = np.pad(padded, (0, hop - remainder), mode="reflect")
    frames = np.lib.stride_tricks.sliding_window_view(padded, n_fft)[::hop]
    return np.fft.rfft(frames * hann(n_fft), axis=1)


def istft(spectra: np.ndarray, hop: int = 512, length: int | None = None) -> np.ndarray:
    frames = np.asarray(spectra, dtype=np.complex128)
    if frames.ndim != 2 or frames.shape[0] == 0 or frames.shape[1] < 2 or hop <= 0:
        return np.empty(0, dtype=np.float64)
    n_fft = 2 * (frames.shape[1] - 1)
    window = hann(n_fft)
    output_size = hop * (frames.shape[0] - 1) + n_fft
    output = np.zeros(output_size, dtype=np.float64)
    normalizer = np.zeros(output_size, dtype=np.float64)
    time_frames = np.fft.irfft(frames, n=n_fft, axis=1) * window
    squared = window * window
    for frame_index, frame in enumerate(time_frames):
        start = frame_index * hop
        output[start : start + n_fft] += frame
        normalizer[start : start + n_fft] += squared
    valid = normalizer > np.finfo(np.float64).eps
    output[valid] /= normalizer[valid]
    output = output[n_fft // 2 :]
    if length is None:
        return output[: hop * (frames.shape[0] - 1)]
    if output.size < length:
        output = np.pad(output, (0, length - output.size))
    return output[:length]


def fft_frequencies(sample_rate: int, n_fft: int) -> np.ndarray:
    if sample_rate <= 0 or n_fft <= 0:
        return np.empty(0, dtype=np.float64)
    return np.fft.rfftfreq(n_fft, 1.0 / sample_rate)
