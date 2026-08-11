from __future__ import annotations

import logging
import math
from dataclasses import replace
from pathlib import Path

import numpy as np

from application.i18n import tr
from application.models import (
    AudioStats,
    CancellationToken,
    NeuralJob,
    ProcessingResult,
    ProgressCallback,
    ProgressEvent,
    ReferenceJob,
)
from audio import (
    AudioData,
    create_pcm_audio,
    read_audio,
    resample_audio,
    write_wav_atomic,
)
from dsp import align_audio, process_audio
from neural import MdxNet, get_model
from neural.model_store import ensure_model

logger = logging.getLogger(__name__)


def _dbfs(amplitude: float) -> float:
    return 20.0 * math.log10(amplitude) if amplitude > 0 else -math.inf


def _audio_stats(audio: AudioData, bit_depth: int, token: CancellationToken) -> AudioStats:
    peak = 0.0
    square_sum = 0.0
    sample_count = 0
    block_size = 262_144
    for start in range(0, audio.frames, block_size):
        token.raise_if_cancelled()
        end = min(start + block_size, audio.frames)
        values = np.asarray(audio.samples[:, start:end], dtype=np.float64)
        peak = max(peak, float(np.max(np.abs(values), initial=0.0)))
        square_sum += float(np.sum(values * values))
        sample_count += values.size
    rms = math.sqrt(square_sum / max(sample_count, 1))
    return AudioStats(
        duration_seconds=audio.frames / audio.sample_rate,
        sample_rate=audio.sample_rate,
        channels=audio.channels,
        bit_depth=bit_depth,
        peak_dbfs=_dbfs(peak),
        rms_dbfs=_dbfs(rms),
        file_size=0,
    )


def _emit(
    callback: ProgressCallback, value: int, language: str, key: str, **values: object
) -> None:
    callback(ProgressEvent(value, tr(language, key, **values)))


def _validate_distinct(*paths: Path) -> None:
    resolved = [path.expanduser().resolve() for path in paths]
    if len(set(resolved)) != len(resolved):
        raise ValueError("output path must not overwrite an input file")


def _validate_reference_paths(song: Path, accompaniment: Path, output: Path) -> None:
    resolved_song = song.expanduser().resolve()
    resolved_accompaniment = accompaniment.expanduser().resolve()
    resolved_output = output.expanduser().resolve()
    if resolved_song == resolved_accompaniment:
        raise ValueError("song and accompaniment must be different files")
    if resolved_output in {resolved_song, resolved_accompaniment}:
        raise ValueError("output path must not overwrite an input file")


def run_reference_job(
    job: ReferenceJob,
    token: CancellationToken,
    progress: ProgressCallback = lambda _event: None,
) -> ProcessingResult:
    logger.info(
        "starting reference job: song=%s accompaniment=%s algorithm=%s",
        job.song,
        job.accompaniment,
        job.algorithm.value,
    )
    _validate_reference_paths(job.song, job.accompaniment, job.output)
    song = reference = processed_audio = None
    try:
        _emit(progress, 0, job.language, "loading_song")
        song = read_audio(job.song, token).stereo()
        token.raise_if_cancelled()
        _emit(progress, 10, job.language, "loading_acc")
        reference = read_audio(job.accompaniment, token).stereo()
        if reference.sample_rate != song.sample_rate:
            _emit(progress, 18, job.language, "resampling")
            resampled = resample_audio(reference, song.sample_rate, token)
            reference.cleanup()
            reference = resampled
        token.raise_if_cancelled()
        if job.auto_align:
            _emit(progress, 25, job.language, "aligning")
            alignment = create_pcm_audio(reference.channels, song.frames, song.sample_rate)
            try:
                aligned = align_audio(
                    song.samples,
                    reference.samples,
                    song.sample_rate,
                    token,
                    alignment.samples,
                )
                if aligned is alignment.samples:
                    reference.cleanup()
                    reference = alignment
                else:
                    alignment.cleanup()
            except (ArithmeticError, ValueError) as error:
                alignment.cleanup()
                logger.warning("alignment failed; using original timeline: %s", error)
                _emit(progress, 28, job.language, "align_fail")
            except BaseException:
                alignment.cleanup()
                raise
        length = min(song.frames, reference.frames)
        processed_audio = create_pcm_audio(song.channels, length, song.sample_rate)
        _emit(progress, 32, job.language, "processing")
        process_audio(
            song.samples[:, :length],
            reference.samples[:, :length],
            song.sample_rate,
            job.strength / 100.0,
            job.algorithm,
            job.sigma,
            token,
            processed_audio.samples,
        )
        bit_depth = 24 if job.algorithm.value == "reference_center" else 16
        _emit(progress, 86, job.language, "analyzing_output")
        stats = _audio_stats(processed_audio, bit_depth, token)
        _emit(progress, 90, job.language, "saving")
        write_wav_atomic(job.output, processed_audio, bit_depth, token)
        stats = replace(stats, file_size=job.output.stat().st_size)
        _emit(progress, 100, job.language, "done_status", path=job.output)
        logger.info("reference job completed: %s", job.output.resolve())
        return ProcessingResult((job.output.resolve(),), (stats,))
    finally:
        for audio in (processed_audio, reference, song):
            if audio is not None:
                audio.cleanup()


def run_neural_job(
    job: NeuralJob,
    token: CancellationToken,
    progress: ProgressCallback = lambda _event: None,
) -> ProcessingResult:
    logger.info("starting neural job: song=%s model=%s", job.song, job.model_id)
    entry = get_model(job.model_id)
    base = job.output_dir.expanduser().resolve() / job.song.stem
    vocal_path = Path(str(base) + "_vocal.wav")
    background_path = Path(str(base) + "_background.wav")
    _validate_distinct(job.song, vocal_path, background_path)
    song = vocal_audio = background_audio = None
    try:
        _emit(progress, 0, job.language, "loading_song")
        song = read_audio(job.song, token).stereo()
        if song.sample_rate != 44_100:
            _emit(progress, 10, job.language, "ai_resampling")
            resampled = resample_audio(song, 44_100, token)
            song.cleanup()
            song = resampled
        model_path = ensure_model(entry, job.models_dir, token, progress)
        _emit(progress, 25, job.language, "ai_loading_model")
        network = MdxNet(model_path)
        _emit(progress, 27, job.language, "ai_inferring")

        def model_progress(current: int, total: int) -> None:
            value = 27 + int(58 * current / max(total, 1))
            _emit(progress, value, job.language, "ai_inferring")

        vocal_audio = create_pcm_audio(2, song.frames, 44_100)
        network.separate(song.samples, token, model_progress, vocal_audio.samples)
        background_audio = create_pcm_audio(2, song.frames, 44_100)
        block_size = 262_144
        for start in range(0, song.frames, block_size):
            token.raise_if_cancelled()
            end = min(start + block_size, song.frames)
            background_audio.samples[:, start:end] = (
                song.samples[:, start:end] - vocal_audio.samples[:, start:end]
            )
        _emit(progress, 87, job.language, "ai_saving")
        write_wav_atomic(vocal_path, vocal_audio, 16, token)
        _emit(progress, 94, job.language, "ai_saving")
        write_wav_atomic(background_path, background_audio, 16, token)
        _emit(progress, 100, job.language, "ai_done", vocal=vocal_path, background=background_path)
        logger.info("neural job completed: vocal=%s background=%s", vocal_path, background_path)
        return ProcessingResult((vocal_path, background_path))
    finally:
        for audio in (background_audio, vocal_audio, song):
            if audio is not None:
                audio.cleanup()
