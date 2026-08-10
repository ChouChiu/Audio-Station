from __future__ import annotations

from dataclasses import dataclass

_BASE = "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/"


@dataclass(frozen=True, slots=True)
class ModelEntry:
    id: str
    name: str
    filename: str
    size: int
    sha256: str
    description_key: str

    @property
    def url(self) -> str:
        return _BASE + self.filename


_CATALOG = (
    ModelEntry(
        "mdxnet_1",
        "UVR-MDX-NET 1",
        "UVR_MDXNET_1_9703.onnx",
        29_704_436,
        "229ad3bb96a037e89d8ed86732d6d3675856e6a07c3e3f02896eac01ec7ee4be",
        "mdl_mdxnet_1",
    ),
    ModelEntry(
        "mdxnet_main",
        "UVR-MDX-NET Main",
        "UVR_MDXNET_Main.onnx",
        66_759_214,
        "8289784cda38543ff431add4070662813311a8cccfc0112ca82f76d9dba2b4ca",
        "mdl_mdxnet_main",
    ),
    ModelEntry(
        "kim_vocal",
        "Kim Vocal 1",
        "Kim_Vocal_1.onnx",
        66_759_214,
        "f313140ef8fecc3041881b60ecb993d985a0281a138b2fb634aa8901aebc38cb",
        "mdl_kim_vocal",
    ),
    ModelEntry(
        "kuielab_b",
        "kuielab B Vocals",
        "kuielab_b_vocals.onnx",
        29_703_204,
        "9b7dcb9d878acb0f3e64ff3fd27750faae96577013f6d50f5996875bf4250713",
        "mdl_kuielab_b",
    ),
)

DEFAULT_MODEL_ID = "mdxnet_1"


def model_catalog() -> tuple[ModelEntry, ...]:
    return _CATALOG


def get_model(model_id: str) -> ModelEntry:
    for entry in _CATALOG:
        if entry.id == model_id:
            return entry
    raise KeyError(model_id)
