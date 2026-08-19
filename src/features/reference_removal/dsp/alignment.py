from __future__ import annotations

import logging
import math

import numpy as np
from scipy.ndimage import median_filter
from scipy.signal import correlate, correlation_lags, resample_poly
from scipy.signal import stft as scipy_stft

from shared.processing import CancellationToken

logger = logging.getLogger(__name__)


def _mono(channels: np.ndarray) -> np.ndarray:
    return np.asarray(channels, dtype=np.float64).mean(axis=0)


def _proxy(channels: np.ndarray, length: int, source_rate: int, target_rate: int) -> np.ndarray:
    """Create an anti-aliased low-rate proxy for long-range alignment.

    Plain strided sampling folds high-frequency vocals, cymbals and crowd noise
    into the accompaniment band.  Polyphase resampling keeps those unrelated
    components from steering the local lag track and gives the proxy an exact
    rate, so proxy positions map back to source samples without clock error.
    """
    divisor = math.gcd(source_rate, target_rate)
    up = target_rate // divisor
    down = source_rate // divisor
    resampled = resample_poly(
        np.asarray(channels[:, :length], dtype=np.float32),
        up,
        down,
        axis=1,
    )
    return np.asarray(resampled, dtype=np.float64).mean(axis=0)


def _normalize(values: np.ndarray) -> np.ndarray | None:
    centered = values - np.mean(values)
    scale = np.std(centered)
    if not np.isfinite(scale) or scale < 1e-8:
        return None
    return centered / scale


def _gcc_phat_lag(song: np.ndarray, reference: np.ndarray, max_lag: int) -> float | None:
    size = 1 << int(np.ceil(np.log2(song.size + reference.size - 1)))
    song_fft = np.fft.rfft(song, size)
    reference_fft = np.fft.rfft(reference, size)
    cross = song_fft * np.conj(reference_fft)
    magnitude = np.abs(cross)
    valid = magnitude > np.finfo(np.float64).eps
    if not np.any(valid):
        return None
    cross[valid] /= magnitude[valid]
    cross[~valid] = 0
    correlation = np.fft.irfft(cross, size)
    correlation = np.concatenate((correlation[-max_lag:], correlation[: max_lag + 1]))
    score = np.abs(correlation)
    index = int(np.argmax(score))
    peak = float(score[index])
    if not np.isfinite(peak) or peak < 1e-5:
        return None
    delta = 0.0
    if 0 < index < score.size - 1:
        left, center, right = score[index - 1 : index + 2]
        denominator = left - 2 * center + right
        if abs(denominator) > 1e-12:
            delta = float(np.clip(0.5 * (left - right) / denominator, -1, 1))
    return float(index - max_lag) + delta


def _raw_lag(song: np.ndarray, reference: np.ndarray, max_lag: int) -> float | None:
    raw = correlate(song, reference, mode="full", method="fft")
    lags = correlation_lags(song.size, reference.size)
    valid = np.abs(lags) <= max_lag
    score = np.abs(raw)
    if not np.any(valid) or np.max(score[valid]) <= 1e-5:
        return None
    indices = np.flatnonzero(valid)
    index = int(indices[np.argmax(score[valid])])
    lag = float(lags[index])
    if 0 < index < score.size - 1:
        left, center, right = score[index - 1 : index + 2]
        denominator = left - 2 * center + right
        if abs(denominator) > 1e-12:
            lag += float(np.clip(0.5 * (left - right) / denominator, -1, 1))
    return lag


def _spectral_flux_lag(
    song: np.ndarray,
    reference: np.ndarray,
    sample_rate: int,
    max_lag_seconds: float = 20.0,
) -> float | None:
    """Estimate musical offset when two masters have weak waveform coherence.

    Live camera audio and the released master can share note attacks while their
    waveforms, EQ, compression, vocals and ambience are substantially different.
    Positive log-spectral flux retains those common attacks and ignores polarity.
    A separated runner-up check prevents an unrelated reference from producing a
    confident-looking offset merely because a long correlation was searched.
    """
    feature_rate = min(8000, sample_rate)
    common = min(song.shape[1], reference.shape[1], sample_rate * 60)
    if common < max(sample_rate // 2, 2048):
        return None

    def extract(channels: np.ndarray) -> np.ndarray | None:
        divisor = math.gcd(sample_rate, feature_rate)
        values = resample_poly(
            np.asarray(channels[:, :common], dtype=np.float32),
            feature_rate // divisor,
            sample_rate // divisor,
            axis=1,
        )
        n_fft = min(1024, values.shape[1])
        if n_fft < 128:
            return None
        hop = max(round(0.02 * feature_rate), 1)
        overlap = max(n_fft - hop, 0)
        channel_features: list[np.ndarray] = []
        for channel in values[:2]:
            _, _, spectrum = scipy_stft(
                channel,
                nperseg=n_fft,
                noverlap=overlap,
                boundary=None,
                padded=False,
            )
            magnitude = np.log1p(20.0 * np.abs(spectrum))
            flux = np.maximum(np.diff(magnitude, axis=1, prepend=magnitude[:, :1]), 0.0)
            edges = np.unique(np.geomspace(2, magnitude.shape[0], 13).astype(int))
            if edges.size < 3:
                return None
            bands = np.stack(
                [
                    np.mean(flux[edges[index] : edges[index + 1]], axis=0)
                    for index in range(edges.size - 1)
                ]
            )
            bands -= np.median(bands, axis=1, keepdims=True)
            raw_scale = np.median(np.abs(bands), axis=1, keepdims=True)
            if float(np.median(raw_scale)) < 1e-4:
                return None
            scale = raw_scale + 1e-6
            channel_features.append(np.clip(bands / scale, -8.0, 8.0))
        return np.concatenate(channel_features, axis=0)

    song_features = extract(song)
    reference_features = extract(reference)
    if song_features is None or reference_features is None:
        return None
    scores = None
    for song_band, reference_band in zip(song_features, reference_features, strict=True):
        correlation = correlate(song_band, reference_band, mode="full", method="fft")
        scores = correlation if scores is None else scores + correlation
    if scores is None:
        return None
    lags = correlation_lags(song_features.shape[1], reference_features.shape[1])
    hop_seconds = max(round(0.02 * feature_rate), 1) / feature_rate
    valid = np.abs(lags) <= round(max_lag_seconds / hop_seconds)
    if not np.any(valid):
        return None
    valid_lags = lags[valid]
    valid_scores = scores[valid]
    best = int(np.argmax(valid_scores))
    peak = float(valid_scores[best])
    separation = max(round(0.4 / hop_seconds), 1)
    competing = np.abs(valid_lags - valid_lags[best]) > separation
    runner_up = float(np.max(valid_scores[competing], initial=0.0))
    if not np.isfinite(peak) or peak <= 0.0 or peak < 1.08 * max(runner_up, 1e-12):
        return None
    return float(valid_lags[best] * hop_seconds * sample_rate)


def _local_track(song: np.ndarray, reference: np.ndarray, rate: int, initial: float):
    step = max(rate // 10, 1)
    half_window = max(rate // 20, 1)
    search = max(rate // 16, 2)
    positions = [0.0]
    lags = [initial]
    predicted = initial
    for center in range(half_window, min(song.size, reference.size), step):
        begin = max(center - half_window, 0)
        end = min(center + half_window, song.size)
        segment = song[begin:end]
        low = max(0, round(begin - predicted - search))
        high = min(reference.size, round(end - predicted + search))
        candidate = reference[low:high]
        if segment.size < 64 or candidate.size < segment.size:
            continue
        scores = correlate(candidate, segment, mode="valid", method="fft")
        denominator = np.linalg.norm(segment) * np.sqrt(
            np.convolve(candidate * candidate, np.ones(segment.size), mode="valid")
        )
        scores = np.divide(scores, denominator + 1e-12)
        similarities = np.abs(scores)
        candidate_lags = begin - (low + np.arange(scores.size))
        ranked = similarities - 0.25 * np.abs(candidate_lags - predicted) / max(search, 1)
        best = int(np.argmax(ranked))
        best_corr = float(similarities[best])
        lag = float(candidate_lags[best])
        predicted_index = int(np.argmin(np.abs(candidate_lags - predicted)))
        predicted_corr = float(similarities[predicted_index])
        if best_corr < 0.08 or best_corr < predicted_corr + 0.02:
            lag = predicted
        max_change = max(2.0, 0.02 * (center - positions[-1]))
        lag = float(np.clip(lag, predicted - max_change, predicted + max_change))
        positions.append(float(center))
        lags.append(lag)
        predicted = lag
    if len(lags) >= 3:
        lags = median_filter(np.asarray(lags), size=3, mode="nearest").tolist()
    positions.append(float(song.size - 1))
    lags.append(lags[-1])
    return np.asarray(positions), np.asarray(lags)


def _lanczos(values: np.ndarray, source: np.ndarray, radius: int = 3) -> np.ndarray:
    base = np.floor(source).astype(np.int64)
    offsets = np.arange(-radius + 1, radius + 1)
    indices = base[:, None] + offsets[None, :]
    distance = source[:, None] - indices
    weights = np.sinc(distance) * np.sinc(distance / radius)
    valid = (indices >= 0) & (indices < values.size) & (np.abs(distance) < radius)
    weights *= valid
    safe_indices = np.clip(indices, 0, max(values.size - 1, 0))
    denominator = np.sum(weights, axis=1)
    return np.divide(
        np.sum(values[safe_indices] * weights, axis=1),
        denominator,
        out=np.zeros(source.size, dtype=np.float64),
        where=np.abs(denominator) > 1e-12,
    )


def _warp(
    reference: np.ndarray,
    positions: np.ndarray,
    lags: np.ndarray,
    length: int,
    token: CancellationToken,
    output: np.ndarray | None,
):
    result = output if output is not None else np.empty((reference.shape[0], length), np.float32)
    if result.shape != (reference.shape[0], length) or result.dtype != np.float32:
        raise ValueError("alignment output must be a float32 [channels, frames] array")
    block_size = 262_144
    for start in range(0, length, block_size):
        token.raise_if_cancelled()
        end = min(start + block_size, length)
        destination = np.arange(start, end, dtype=np.float64)
        lag_curve = np.interp(destination, positions, lags)
        source = destination - lag_curve
        for channel, values in enumerate(reference):
            result[channel, start:end] = _lanczos(values, source)
    return result


def align_audio(
    song: np.ndarray,
    reference: np.ndarray,
    sample_rate: int,
    token: CancellationToken | None = None,
    output: np.ndarray | None = None,
) -> np.ndarray:
    cancel = token or CancellationToken()
    mix = np.asarray(song, dtype=np.float32)
    accompaniment = np.asarray(reference, dtype=np.float32)
    if mix.ndim != 2 or accompaniment.ndim != 2 or sample_rate <= 0:
        return accompaniment
    cancel.raise_if_cancelled()
    common = min(mix.shape[1], accompaniment.shape[1])
    if common < 64:
        return accompaniment
    proxy_rate = min(2000, sample_rate)
    mix_proxy = _proxy(mix, common, sample_rate, proxy_rate)
    ref_proxy = _proxy(accompaniment, common, sample_rate, proxy_rate)
    mix_norm = _normalize(mix_proxy)
    ref_norm = _normalize(ref_proxy)
    if mix_norm is None or ref_norm is None:
        return accompaniment
    full_check = min(common, sample_rate * 8)
    full_mix = _normalize(_mono(mix[:, :full_check]))
    full_reference = _normalize(_mono(accompaniment[:, :full_check]))
    if full_mix is None or full_reference is None:
        return accompaniment
    full_check = min(full_mix.size, full_reference.size)
    max_lag = min(sample_rate * 20, full_check - 1)
    near_limit = min(sample_rate // 2, max_lag)
    lag_samples = _spectral_flux_lag(mix, accompaniment, sample_rate)
    if lag_samples is not None:
        logger.info(
            "spectral-flux coarse alignment selected %.1f samples (%.3f s)",
            lag_samples,
            lag_samples / sample_rate,
        )
    else:
        lag_samples = _gcc_phat_lag(full_mix[:full_check], full_reference[:full_check], near_limit)
        if lag_samples is not None and abs(lag_samples) >= 0.95 * near_limit:
            lag_samples = _gcc_phat_lag(full_mix[:full_check], full_reference[:full_check], max_lag)
    if lag_samples is None:
        lag_samples = _raw_lag(full_mix[:full_check], full_reference[:full_check], max_lag)
    if lag_samples is None:
        return accompaniment
    scale = sample_rate / proxy_rate
    positions, lags = _local_track(mix_norm, ref_norm, proxy_rate, lag_samples / scale)
    logger.info(
        "local alignment tracked %.3f to %.3f s (median %.3f s)",
        float(lags[0] / proxy_rate),
        float(lags[-1] / proxy_rate),
        float(np.median(lags) / proxy_rate),
    )
    cancel.raise_if_cancelled()
    return _warp(
        accompaniment,
        positions * scale,
        lags * scale,
        mix.shape[1],
        cancel,
        output,
    )
