from __future__ import annotations

import hashlib
import json
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from audio_station.application.models import CancellationToken
from audio_station.audio import create_pcm_audio
from audio_station.dsp.transforms import istft, stft
from audio_station.resources import resource_path


@dataclass(frozen=True, slots=True)
class MdxNetSpec:
    n_fft: int = 7680
    dim_f: int = 3072
    dim_t: int = 256
    hop: int = 1024
    segment_size: int = 256
    compensate: float = 1.0
    sample_rate: int = 44_100
    primary_stem: str = "Vocals"


_FALLBACKS = {
    "UVR_MDXNET_Main.onnx": MdxNetSpec(compensate=1.75),
    "UVR_MDXNET_1_9703.onnx": MdxNetSpec(n_fft=6144, dim_f=2048, compensate=1.035),
    "Kim_Vocal_1.onnx": MdxNetSpec(compensate=1.035),
    "kuielab_b_vocals.onnx": MdxNetSpec(n_fft=6144, dim_f=2048, compensate=1.035),
}


def spec_for_model(path: Path) -> MdxNetSpec:
    digest = hashlib.md5(path.read_bytes()).hexdigest()
    try:
        table = json.loads(resource_path("model_data.json").read_text(encoding="utf-8"))
        entry = table.get(digest)
        if entry and "mdx_n_fft_scale_set" in entry:
            return MdxNetSpec(
                n_fft=int(entry.get("mdx_n_fft_scale_set", 7680)),
                dim_f=int(entry.get("mdx_dim_f_set", 3072)),
                dim_t=1 << int(entry.get("mdx_dim_t_set", 8)),
                compensate=float(entry.get("compensate", 1.0)),
                primary_stem=str(entry.get("primary_stem", "Vocals")),
            )
    except (OSError, ValueError, TypeError):
        pass
    try:
        return _FALLBACKS[path.name]
    except KeyError as error:
        raise ValueError(f"unknown MDX-Net model: {path.name}") from error


InferFunction = Callable[[np.ndarray], np.ndarray]


def demix_chunks(
    mix: np.ndarray,
    spec: MdxNetSpec,
    infer: InferFunction,
    token: CancellationToken,
    progress: Callable[[int, int], None] | None = None,
    output: np.ndarray | None = None,
) -> np.ndarray:
    audio = np.asarray(mix, dtype=np.float32)
    if audio.shape[0] != 2 or audio.ndim != 2:
        raise ValueError("MDX-Net expects stereo audio")
    chunk_size = spec.hop * (spec.segment_size - 1)
    trim = spec.n_fft // 2
    generated = chunk_size - 2 * trim
    step = chunk_size - spec.n_fft
    total = audio.shape[1]
    if total == 0:
        return np.empty_like(audio)
    pad = generated + trim - (total % generated)
    padded_length = total + trim + pad
    if output is None:
        result = np.zeros_like(audio, dtype=np.float32)
    else:
        if output.shape != audio.shape or output.dtype != np.float32:
            raise ValueError("output must be a float32 stereo array matching the input")
        result = output
        result[:] = 0
    divider_audio = create_pcm_audio(1, total, spec.sample_rate)
    divider = divider_audio.samples[0]
    divider[:] = 0
    starts = list(range(0, padded_length, step))
    try:
        for index, start in enumerate(starts):
            token.raise_if_cancelled()
            end = min(start + chunk_size, padded_length)
            actual = end - start
            chunk = np.zeros((2, chunk_size), dtype=np.float32)
            source_start = max(start - trim, 0)
            source_end = min(end - trim, total)
            if source_end > source_start:
                chunk_start = source_start + trim - start
                chunk[:, chunk_start : chunk_start + source_end - source_start] = audio[
                    :, source_start:source_end
                ]
            predicted = np.asarray(infer(chunk), dtype=np.float32)
            if predicted.shape != chunk.shape:
                raise RuntimeError(f"model returned {predicted.shape}, expected {chunk.shape}")
            target_start = max(start, trim)
            target_end = min(end, trim + total)
            if target_end > target_start:
                predicted_start = target_start - start
                output_start = target_start - trim
                count = target_end - target_start
                window = np.hanning(actual).astype(np.float32, copy=False)
                weights = window[predicted_start : predicted_start + count]
                result[:, output_start : output_start + count] += (
                    predicted[:, predicted_start : predicted_start + count] * weights
                )
                divider[output_start : output_start + count] += weights
            if progress:
                progress(index + 1, len(starts))
        block_size = 262_144
        for start in range(0, total, block_size):
            token.raise_if_cancelled()
            end = min(start + block_size, total)
            result[:, start:end] /= np.maximum(divider[start:end], 1e-12)
        return result
    finally:
        divider_audio.cleanup()


class MdxNet:
    def __init__(self, model_path: Path, spec: MdxNetSpec | None = None):
        import onnxruntime as ort

        self.model_path = model_path
        self.spec = spec or spec_for_model(model_path)
        self.session = ort.InferenceSession(str(model_path), providers=["CPUExecutionProvider"])
        self.input = self.session.get_inputs()[0]
        self.output = self.session.get_outputs()[0]
        shape = tuple(self.input.shape)
        if len(shape) != 4 or shape[1:] != (4, self.spec.dim_f, self.spec.dim_t):
            raise RuntimeError(
                f"model input shape mismatch: expected [1,4,{self.spec.dim_f},{self.spec.dim_t}], got {shape}"
            )

    def _infer_chunk(self, chunk: np.ndarray) -> np.ndarray:
        spectra = [stft(chunk[channel], self.spec.n_fft, self.spec.hop) for channel in range(2)]
        frames = spectra[0].shape[0]
        model_input = np.zeros((1, 4, self.spec.dim_f, frames), dtype=np.float32)
        model_input[0, 0] = spectra[0][:, : self.spec.dim_f].T.real
        model_input[0, 1] = spectra[0][:, : self.spec.dim_f].T.imag
        model_input[0, 2] = spectra[1][:, : self.spec.dim_f].T.real
        model_input[0, 3] = spectra[1][:, : self.spec.dim_f].T.imag
        model_input[:, :, :3] = 0
        model_output = self.session.run([self.output.name], {self.input.name: model_input})[0][0]
        restored = []
        bins = self.spec.n_fft // 2 + 1
        for channel in range(2):
            spectrum = np.zeros((frames, bins), dtype=np.complex128)
            real = model_output[channel * 2]
            imaginary = model_output[channel * 2 + 1]
            spectrum[:, : self.spec.dim_f] = (real + 1j * imaginary).T
            restored.append(istft(spectrum, self.spec.hop, chunk.shape[1]))
        return np.asarray(restored, dtype=np.float32)

    def separate(
        self,
        mix: np.ndarray,
        token: CancellationToken,
        progress: Callable[[int, int], None] | None = None,
        output: np.ndarray | None = None,
    ) -> np.ndarray:
        result = demix_chunks(mix, self.spec, self._infer_chunk, token, progress, output)
        block_size = 262_144
        for start in range(0, result.shape[1], block_size):
            result[:, start : start + block_size] *= self.spec.compensate
        return result
