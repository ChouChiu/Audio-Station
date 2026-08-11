import numpy as np

from features.neural_separation.catalog import DEFAULT_MODEL_ID, get_model, model_catalog
from features.neural_separation.inference import MdxNetSpec, demix_chunks
from shared.audio import create_pcm_audio
from shared.processing import CancellationToken


def test_catalog_is_unique_and_has_hashes():
    catalog = model_catalog()
    assert get_model(DEFAULT_MODEL_ID) in catalog
    assert len({entry.id for entry in catalog}) == len(catalog)
    assert all(len(entry.sha256) == 64 for entry in catalog)


def test_demix_identity_handles_multiple_chunks():
    spec = MdxNetSpec(n_fft=32, dim_f=12, dim_t=9, hop=8, segment_size=9)
    rng = np.random.default_rng(7)
    mix = rng.normal(0, 0.1, (2, 333)).astype(np.float32)
    calls = []
    output = demix_chunks(
        mix,
        spec,
        lambda chunk: chunk,
        CancellationToken(),
        lambda current, total: calls.append((current, total)),
    )
    assert output.shape == mix.shape
    assert np.max(np.abs(output - mix)) < 1e-6
    assert calls and calls[-1][0] == calls[-1][1]


def test_demix_identity_writes_supplied_disk_buffer():
    spec = MdxNetSpec(n_fft=32, dim_f=12, dim_t=9, hop=8, segment_size=9)
    mix = np.random.default_rng(9).normal(0, 0.1, (2, 173)).astype(np.float32)
    target = create_pcm_audio(2, mix.shape[1], spec.sample_rate)
    try:
        output = demix_chunks(
            mix,
            spec,
            lambda chunk: chunk,
            CancellationToken(),
            output=target.samples,
        )
        assert output is target.samples
        assert np.max(np.abs(output - mix)) < 1e-6
    finally:
        target.cleanup()
