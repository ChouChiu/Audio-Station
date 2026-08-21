from enum import StrEnum

from .spectral import fft_frequencies, hann, istft, stft


class ReferenceAlgorithm(StrEnum):
    SPECTRAL_MASK = "spectral_mask"
    DIRECT = "direct"


__all__ = ["ReferenceAlgorithm", "fft_frequencies", "hann", "istft", "stft"]
