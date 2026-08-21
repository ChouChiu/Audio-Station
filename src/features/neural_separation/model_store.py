from __future__ import annotations

import hashlib
import os
import urllib.request
from pathlib import Path

from PySide6.QtCore import QStandardPaths

from features.neural_separation.catalog import ModelEntry
from shared.i18n import tr
from shared.processing import (
    CancellationToken,
    ProcessingCancelled,
    ProgressCallback,
    ProgressEvent,
)


def default_models_dir() -> Path:
    root = QStandardPaths.writableLocation(QStandardPaths.StandardLocation.AppLocalDataLocation)
    return Path(root or Path.home() / ".local/share/audio-station") / "models"


def candidate_model_dirs(override: Path | None = None) -> tuple[Path, ...]:
    candidates: list[Path] = []
    if override:
        candidates.append(override.expanduser())
    if env := os.environ.get("MR_REMOVER_MODELS"):
        candidates.append(Path(env).expanduser())
    candidates.append(default_models_dir())
    repository_models = Path(__file__).resolve().parents[3] / "models"
    candidates.append(repository_models)
    return tuple(dict.fromkeys(path.resolve() for path in candidates))


def find_model(entry: ModelEntry, override: Path | None = None) -> Path | None:
    for directory in candidate_model_dirs(override):
        candidate = directory / entry.filename
        if candidate.is_file():
            return candidate
    return None


def _sha256(path: Path, token: CancellationToken) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            token.raise_if_cancelled()
            digest.update(chunk)
    return digest.hexdigest()


def ensure_model(
    entry: ModelEntry,
    override: Path | None,
    language: str,
    token: CancellationToken,
    progress: ProgressCallback,
) -> Path:
    if existing := find_model(entry, override):
        return existing
    directory = (override or default_models_dir()).expanduser().resolve()
    directory.mkdir(parents=True, exist_ok=True)
    destination = directory / entry.filename
    partial = destination.with_suffix(destination.suffix + ".part")
    request = urllib.request.Request(entry.url, headers={"User-Agent": "Audio-Station/1.0"})
    try:
        with urllib.request.urlopen(request, timeout=120) as response, partial.open("wb") as output:
            total = int(response.headers.get("Content-Length", entry.size))
            downloaded = 0
            while chunk := response.read(1024 * 1024):
                token.raise_if_cancelled()
                output.write(chunk)
                downloaded += len(chunk)
                value = 15 + int(10 * downloaded / max(total, 1))
                progress(
                    ProgressEvent(min(value, 24), tr(language, "ai_downloading", name=entry.name))
                )
        if partial.stat().st_size != entry.size:
            raise RuntimeError("downloaded model size does not match catalog")
        if _sha256(partial, token) != entry.sha256:
            raise RuntimeError("downloaded model checksum does not match catalog")
        os.replace(partial, destination)
        return destination
    except ProcessingCancelled:
        raise
    except Exception as error:
        raise RuntimeError(tr(language, "ai_download_failed", msg=error)) from error
    finally:
        partial.unlink(missing_ok=True)
