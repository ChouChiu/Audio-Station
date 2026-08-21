from __future__ import annotations

import argparse
import io
import logging
import signal
import sys
from contextlib import redirect_stdout
from pathlib import Path

from PySide6.QtCore import QCoreApplication

from app.version import __version__
from features.neural_separation import (
    DEFAULT_MODEL_ID,
    NeuralJob,
    model_catalog,
    run_neural_job,
)
from features.reference_removal import ReferenceAlgorithm, ReferenceJob, run_reference_job
from shared.logging import configure_logging, set_log_level
from shared.processing import CancellationToken, ProcessingCancelled, ProgressEvent


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="audio-station", description="Vocal and accompaniment separation"
    )
    parser.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    parser.add_argument("--selftest", action="store_true", help=argparse.SUPPRESS)
    commands = parser.add_subparsers(dest="command")

    reference = commands.add_parser("mr", help="reference-guided vocal extraction")
    reference.add_argument("song", type=Path)
    reference.add_argument("accompaniment", type=Path)
    reference.add_argument("output", type=Path)
    reference.add_argument(
        "--strength", type=int, choices=range(0, 101), default=75, metavar="0..100"
    )
    reference.add_argument("--sigma", type=int, choices=(1, 3, 8, 16), default=8)
    reference.add_argument(
        "--algorithm",
        choices=tuple(ReferenceAlgorithm),
        default=ReferenceAlgorithm.SPECTRAL_MASK,
        help="reference extractor (default: spectral_mask)",
    )
    reference.add_argument("--align", action=argparse.BooleanOptionalAction, default=True)
    reference.add_argument(
        "--center-extraction",
        action=argparse.BooleanOptionalAction,
        default=False,
        help="apply phantom-center vocal extraction after reference cancellation",
    )
    reference.add_argument(
        "--weak-vocal-protection",
        action=argparse.BooleanOptionalAction,
        default=False,
        help="protect a quiet center vocal; requires --center-extraction",
    )
    reference.add_argument("--lang", choices=("zh_cn", "ja_jp", "ko_kr"), default="zh_cn")

    neural = commands.add_parser("ai", help="MDX-Net vocal extraction")
    neural.add_argument("song", type=Path)
    neural.add_argument("--output-dir", type=Path)
    neural.add_argument(
        "--model", choices=[entry.id for entry in model_catalog()], default=DEFAULT_MODEL_ID
    )
    neural.add_argument("--models-dir", type=Path)
    neural.add_argument("--lang", choices=("zh_cn", "ja_jp", "ko_kr"), default="zh_cn")
    return parser


def _print_progress(event: ProgressEvent) -> None:
    logging.getLogger(__name__).info("progress: %3d%%  %s", event.value, event.message)


def main(argv: list[str] | None = None) -> int:
    configure_logging()
    args = build_parser().parse_args(argv)
    if args.command is None:
        # The open-source QFluentWidgets package prints a Pro advertisement at
        # import time.  Keep normal application startup output clean without
        # changing or patching the installed dependency.
        with redirect_stdout(io.StringIO()):
            from entrypoints.gui import run_gui

        return run_gui(args.selftest)
    app = QCoreApplication.instance() or QCoreApplication(sys.argv[:1])
    app.setApplicationName("Audio Station")
    app.setOrganizationName("Audio Station")
    with redirect_stdout(io.StringIO()):
        from shared.config import cfg, load_config

    load_config()
    set_log_level(str(cfg.log_level.value))
    token = CancellationToken()
    previous_handler = signal.signal(signal.SIGINT, lambda *_unused: token.cancel())
    try:
        if args.command == "mr":
            job = ReferenceJob(
                song=args.song,
                accompaniment=args.accompaniment,
                output=args.output,
                strength=args.strength,
                sigma=args.sigma,
                auto_align=args.align,
                language=args.lang,
                algorithm=args.algorithm,
                center_extraction=args.center_extraction,
                weak_vocal_protection=args.weak_vocal_protection,
            )
            result = run_reference_job(job, token, _print_progress)
        else:
            output_dir = args.output_dir or args.song.expanduser().resolve().parent
            job = NeuralJob(args.song, output_dir, args.model, args.models_dir, args.lang)
            result = run_neural_job(job, token, _print_progress)
        for output in result.outputs:
            print(output)
        return 0
    except ProcessingCancelled:
        logging.getLogger(__name__).warning("cancelled")
        return 130
    except (FileNotFoundError, KeyError, ValueError) as error:
        logging.getLogger(__name__).error("%s", error)
        return 2
    except Exception as error:
        logging.getLogger(__name__).exception("unhandled processing error: %s", error)
        return 1
    finally:
        signal.signal(signal.SIGINT, previous_handler)
