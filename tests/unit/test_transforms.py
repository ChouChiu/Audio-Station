import numpy as np

from audio_station.dsp.transforms import fft_frequencies, hann, istft, stft


def test_stft_round_trip_and_helpers():
    rng = np.random.default_rng(42)
    signal = rng.normal(0, 0.2, 12_345)
    spectra = stft(signal, 512, 128)
    restored = istft(spectra, 128, signal.size)
    assert spectra.shape[1] == 257
    assert np.max(np.abs(restored - signal)) < 1e-10
    assert hann(0).size == 0
    assert fft_frequencies(16_000, 512)[-1] == 8_000
