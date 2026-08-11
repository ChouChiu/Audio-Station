from contextlib import suppress
from pathlib import Path

import numpy as np
import soundfile as sf

from application.models import CancellationToken
from audio import (
    AudioData,
    create_pcm_audio,
    read_audio,
    resample_audio,
    write_wav_atomic,
)
from audio.io import _read_with_qt


def test_wav_roundtrip_and_resample(tmp_path: Path):
    sample_rate = 16_000
    signal = np.sin(2 * np.pi * 440 * np.arange(sample_rate) / sample_rate).astype(np.float32) * 0.5
    source = AudioData(np.stack([signal, signal]), sample_rate)
    path = tmp_path / "test.wav"
    write_wav_atomic(path, source, 24)
    decoded = read_audio(path)
    try:
        assert decoded.backing_path is not None and decoded.backing_path.is_file()
        assert decoded.sample_rate == sample_rate
        assert decoded.samples.shape == source.samples.shape
        assert np.max(np.abs(decoded.samples - source.samples)) < 2e-6
        resampled = resample_audio(decoded, 8_000)
        try:
            assert resampled.backing_path is not None and resampled.backing_path.is_file()
            assert resampled.sample_rate == 8_000
            assert abs(resampled.frames - 8_000) <= 1
        finally:
            resampled.cleanup()
    finally:
        backing = decoded.backing_path
        decoded.cleanup()
        assert backing is not None and not backing.exists()


def test_temporary_pcm_is_disk_backed_and_removable():
    audio = create_pcm_audio(2, 128, 8_000)
    path = audio.backing_path
    assert path is not None and path.is_file()
    audio.samples[:] = 0.25
    audio.cleanup()
    assert not path.exists()


def test_qt_audio_decoder_signal_api(tmp_path: Path, qtbot):
    path = tmp_path / "qt-decoder.wav"
    source = np.linspace(-0.5, 0.5, 800, dtype=np.float32)
    sf.write(path, source, 8_000, subtype="FLOAT")

    decoded = _read_with_qt(path, CancellationToken())
    try:
        assert decoded.sample_rate == 8_000
        assert decoded.channels == 1
        assert decoded.frames == source.size
        assert np.max(np.abs(decoded.samples[0] - source)) < 2e-6
    finally:
        decoded.cleanup()


def test_atomic_writer_preserves_existing_file_on_invalid_input(tmp_path: Path):
    path = tmp_path / "existing.wav"
    path.write_bytes(b"keep")
    audio = AudioData(np.ones((1, 100), dtype=np.float32), 8_000)
    with suppress(ValueError):
        write_wav_atomic(path, audio, 12)
    assert path.read_bytes() == b"keep"
