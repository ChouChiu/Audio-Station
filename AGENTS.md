# Repository Guidelines

## Project Overview

**Audio Station v1.0.0** (`audio-station`) — a desktop vocal/accompaniment separation tool written in Python 3.11+ / PySide6 with PySide6-Fluent-Widgets (Fluent Design UI). Three workflows share one codebase:

- **MR Remove** (`audio-station mr`): reference-guided cancellation — takes a known accompaniment as reference, aligns it (GCC-PHAT + clock-drift tracking), and cancels it with a confidence-weighted reference mask. Phantom-center focus is opt-in.
- **Full Stage** (GUI): matches multiple sources against a continuous stage recording, exposes an editable timeline, and applies reference cancellation only to enabled matched clips.
- **AI vocal extraction** (`audio-station ai`): UVR MDX-Net ONNX inference with no reference; models download on demand and are verified by SHA-256.

GUI and CLI are thin shells over the same `run_reference_job` / `run_neural_job` pipeline functions; full-stage cross-feature rendering is orchestrated in `src/app/full_stage_processing.py`. License: AGPL-3.0-or-later. README.md (Chinese) is the authoritative user-facing doc.

## Architecture & Data Flow

Strict one-way dependency layering, enforced by an AST import-boundary test (`tests/test_architecture.py`):

```
src/entrypoints (cli.py, gui.py)          — startup only
   └─> src/app (main_window.py, worker.py) — cross-feature orchestration
          └─> src/features/<feature>/      — self-contained: page.py, processing.py,
          │                                  models.py, dsp/, finder.py, catalog.py
          └─> src/shared/                  — depends on NOTHING (audio/, dsp/, ui/, i18n, config, logging, processing)
```

Invariants enforced by `tests/test_architecture.py`: `shared` must never import `app` or `features.*`; feature packages must never import one another. Adding a feature = one new `src/features/<feature>/` dir; it may import `shared` freely and must not be imported by other features.

**Reference pipeline** (`features/reference_removal/processing.py`): `read_audio` (SoundFile, Qt Multimedia fallback) → upmix to stereo → resample accompaniment to song rate (soxr) → optional `align_audio` (GCC-PHAT coarse lag + Lanczos warp) → `process_audio` (blocks of 30 s × sample rate, 2 s overlap, cos²/sin² crossfade) → audio stats (peak/RMS dBFS) → atomic 24-bit WAV write.

**Neural pipeline** (`features/neural_separation/processing.py`): resample song to 44.1 kHz → `ensure_model` (search: `--models-dir` override → `MR_REMOVER_MODELS` env → system app-data dir → repo `models/`; download from TRvlvr releases with SHA-256 verify) → `MdxNet.separate` (chunked overlap-add, hanning divider accumulation) → writes `<stem>_vocal.wav` + `<stem>_background.wav` (background = song − vocal).

**Concurrency**: pages define Qt `Signal()`s (`start_requested`, `cancel_requested`); `MainWindow` builds a job dataclass and spawns a `QThread` with `ProcessingWorker` (QObject adapter, `src/app/worker.py`) running `partial(run_*_job, job)`; worker emits `progress`/`succeeded`/`failed`/`cancelled`/`finished`. Cancellation is cooperative via `CancellationToken.raise_if_cancelled()` (`src/shared/processing.py`). CLI runs the same jobs synchronously with SIGINT → token cancel.

## Key Directories

| Path | Purpose |
|---|---|
| `src/entrypoints/` | `cli.py` (argparse: `mr`, `ai`, `--selftest`), `gui.py`, `__main__.py` (`python -m entrypoints`) |
| `src/app/` | `main_window.py` (FluentWindow shell, 4 pages, worker orchestration), `worker.py` (QThread adapter), `version.py` |
| `src/features/reference_removal/` | MR pipeline: `dsp/algorithms.py` (reference-mask cancellation), `dsp/alignment.py`, `finder.py` (auto accompaniment match), `processing.py`, `page.py`, `models.py` |
| `src/features/full_stage/` | Multi-source fingerprint matching, timeline models, and the full-stage page |
| `src/features/neural_separation/` | AI pipeline: `inference.py` (MdxNet ONNX wrapper), `model_store.py` (search/download/verify), `catalog.py` (4 shipped model entries), `processing.py`, `page.py` |
| `src/features/home/`, `src/features/settings/` | HomePage (brand + entry cards), SettingsPage (language/theme/log level) |
| `src/shared/audio/` | `io.py`: `AudioData` planar float32 (np.memmap temp files), `read_audio`, `resample_audio` (soxr), `write_wav_atomic` (temp + `os.replace`) |
| `src/shared/dsp/` | `spectral.py`: librosa-compatible `stft`/`istft`, `n_fft=2048`, `hop=512` |
| `src/shared/ui/` | `combo_box.py` (`SmoothComboBox`, qfw slide animation disabled), `cards.py` (FormCard rows) |
| `src/shared/` | `config.py` (QConfig), `i18n.py` (`tr()`), `logging.py` (single-line formatter), `processing.py` (token/progress types) |
| `src/resources/` | `i18n/{zh_cn,ja_jp,ko_kr}.json` (flat keys, must stay key-identical), `model_data.json` (MDX-Net spec table keyed by MD5), `__init__.py` (`resource_path` via `importlib.resources`) |
| `tests/` | Mirrors `src/` path-for-path (`tests/shared/` ↔ `src/shared/`, `tests/features/…`); `benchmarks/` for long/`--runslow` gates |
| `models/` | 4 prebuilt ONNX weights (gitignored, never committed); not shipped in wheels/standalone |
| `deployment/` | `main.py` — standalone Nuitka entry shim |

## Development Commands

```bash
uv sync --locked

# run
uv run --locked audio-station                      # GUI
uv run --locked python -m entrypoints              # GUI (same)
uv run --locked audio-station mr <song> <acc> <out.wav> --strength 75 --sigma 8 --align --lang zh_cn
uv run --locked audio-station ai <song> [--output-dir <dir>] [--model mdxnet_1] [--models-dir <dir>]
uv run --locked audio-station --selftest           # self-test smoke (offscreen-safe)

# checks (no lint/test without offscreen Qt platform)
uv run --locked ruff check src tests
uv run --locked ruff format --check src tests
QT_QPA_PLATFORM=offscreen uv run --locked pytest
QT_QPA_PLATFORM=offscreen uv run --locked pytest tests/benchmarks --runslow   # 15-min memory/quality gate
uv build                           # sdist+wheel

# Linux standalone (Nuitka via pyside6-deploy; output dist/)
uv sync --locked --group deploy
uv run --locked --group deploy pyside6-deploy -c pysidedeploy.spec
```

Language keys: `zh_cn`, `ja_jp`, `ko_kr`.

## Code Conventions & Common Patterns

- **Typing**: type hints everywhere (`from __future__ import annotations`, PEP 604 `X | None`, `collections.abc`); frozen+slots dataclasses for jobs/config. snake_case functions/vars; Qt widget attributes camelCase.
- **Errors**: raise `ValueError`/`KeyError`/`FileNotFoundError`/`RuntimeError`. Long jobs catch `(FileNotFoundError, KeyError, ValueError)` → CLI exit 2; generic → exit 1. GUI surfaces via worker `failed` signal + InfoBar. Cancellation via `CancellationToken.raise_if_cancelled()` — never swallow cancellation; documented fallbacks (e.g. alignment failure → `logger.warning` + fallback) are the only tolerated swallows.
- **Logging**: `logging.getLogger(__name__)`; single-line format `YYYY-MM-DD HH:MM:SS.xx [LEVEL] module: message` (`ApplicationLogFormatter`, `src/shared/logging.py`); CLI progress prints `progress: %3d%%`. Qt/FFmpeg messages routed to `qt.*` loggers.
- **i18n**: `tr(language, key, **values)` (`src/shared/i18n.py`); unknown key returns the key itself — never rely on that, and adding/removing a key MUST be mirrored in all three locale JSONs (test enforces key parity). UI language switching calls page `retranslate(language)` and rebuilds combo boxes.
- **Config**: qfluentwidgets `QConfig` (`src/shared/config.py`, `cfg` singleton, `config.json` in AppConfigLocation).
- **Qt**: pages declare `Signal()`s and never touch the worker directly; `MainWindow` connects them. Prefer `SmoothComboBox` (qfw slide animation is disabled there) and FormCard/PageScrollArea from `shared/ui/`. Preview = `QMediaPlayer` + `QAudioOutput`.
- **Memory discipline** (long audio): stream in 262 144-frame blocks, use `create_pcm_audio` memmap + `cleanup()`/`release_pages()`; never accumulate whole files in RAM; add a `QTimer` poll for cancellation inside decoder loops.
- **Adding a source dir**: register in THREE places or it silently ships nowhere: `[tool.hatch.build.targets.wheel] packages` + `[tool.pyside6-project] files` (both `pyproject.toml`) + `include-package` in `pysidedeploy.spec`.

## Important Files

| File | Role |
|---|---|
| `pyproject.toml` | hatchling packaging; ruff + pytest config; `[tool.pyside6-project] files` = authoritative shipped-source roster |
| `pysidedeploy.spec` | Nuitka standalone build (dir mode, `deployment/main.py` → `dist/`) |
| `src/entrypoints/cli.py` | `audio-station` entry point (`main`); mr/ai subcommands, `--selftest` |
| `src/app/main_window.py` | FluentWindow shell: navigation, i18n/theme, worker orchestration, auto-find |
| `src/app/worker.py` | `ProcessingWorker` QObject adapter (QThread lifecycle + signals) |
| `src/shared/processing.py` | `CancellationToken`, `ProcessingCancelled`, `ProgressEvent`, `ProcessingResult`, `ProgressCallback` |
| `src/shared/audio/io.py` | memmap audio loading, soxr resample, atomic WAV write (16/24-bit) |
| `src/shared/config.py` / `i18n.py` / `logging.py` | settings persistence, `tr()`, single-line log format |
| `src/features/reference_removal/dsp/algorithms.py` | Reference-mask cancellation, optional center focus, and linked peak protection |
| `src/features/reference_removal/dsp/alignment.py` | GCC-PHAT coarse alignment + local drift tracking + Lanczos warp |
| `src/features/neural_separation/inference.py` / `model_store.py` | MdxNet ONNX wrapper (chunked overlap-add); model search/download/SHA-256 |
| `src/resources/model_data.json` | 65-entry MDX-Net spec table keyed by model-MD5 (`compensate`, `mdx_dim_f_set`, `mdx_dim_t_set`, `mdx_n_fft_scale_set`, `primary_stem`) |
| `src/resources/i18n/*.json` | UI strings: flat snake_case keys, zh_cn/ja_jp/ko_kr (~120 keys, key-parity tested) |
| `tests/test_architecture.py` | AST import-boundary gate (shared isolation, feature isolation) |
| `tests/conftest.py` | forces `QT_QPA_PLATFORM=offscreen`, adds `--runslow`, auto-skips `slow` tests |

## Runtime/Tooling Preferences

- **Python ≥ 3.11**, pinned to 3.14 for development via `.python-version`. Package/environment manager: uv; commit `uv.lock`. The `dev` dependency group is synced by default and `deploy` is opt-in.
- **UI stack**: PySide6 ≥6.8 + `PySide6-Fluent-Widgets[full]` (vendored `qfluentwidgets`; never mix with other Fluent widget packages). Qt is mandatory for audio fallback decode (`QAudioDecoder`) — CLI tests still need `QT_QPA_PLATFORM=offscreen`.
- **DSP deps**: numpy ≥2, scipy, soundfile, soxr; neural: onnxruntime (CPU). All pinned to bounded ranges in `pyproject.toml`.
- **Lint/format**: ruff only (`line-length = 100`, rules E/F/I/UP/B/SIM/RUF, E501 ignored). Format = `ruff format`; there is no separate black/isort.
- **Deploy**: Nuitka 4.1.3 via `pyside6-deploy`; standalone dir mode only. ONNX weights are never packaged (downloaded at runtime; `models/*.onnx` gitignored).

## Testing & QA

- **Framework**: pytest ≥8.3 + pytest-qt ≥4.4. Run: `QT_QPA_PLATFORM=offscreen uv run --locked pytest` (offscreen mandatory; conftest sets it and adds `--runslow`, auto-skipping `@pytest.mark.slow` tests otherwise). Marker `model` is declared but currently unused; `--runslow` runs the 15-minute, 44.1 kHz stereo reference-cancellation benchmark (`tests/benchmarks/test_long_audio.py`) asserting seam smoothness and peak RSS ≤ 1.5 GiB.
- **Layout**: `tests/` mirrors `src/` path-for-path. Per-area coverage: audio IO/resample/atomic-write (`tests/shared/test_audio.py`), STFT round-trip (`test_spectral.py`), i18n key parity (`test_i18n.py`), log format (`test_logging.py`), reference-mask cancellation + alignment/MIMO (`tests/features/reference_removal/test_dsp.py`), drift/focus/jitter regressions (`test_dsp_regression.py`), finder similarity (`test_finder.py`), end-to-end reference job + AudioStats + same-input rejection (`test_processing.py`), neural chunked overlap-add identity (`test_neural.py`), CLI option handling (`tests/entrypoints/test_cli.py`), GUI navigation/theme/combos/stats via pytest-qt (`tests/app/test_gui.py`).
- **Architecture gate**: `tests/test_architecture.py` parses ASTs — any `shared → app/features` or feature↔feature import fails the suite.
- **Expectations**: synthetic DSP metrics are regression evidence only — they do not claim real-music quality (stated in README). Run `uv run --locked audio-station --selftest` for a quick pipeline smoke check before committing DSP changes.
