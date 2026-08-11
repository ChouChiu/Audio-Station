from __future__ import annotations

import logging
import os
import sys
from datetime import datetime
from typing import TextIO

from PySide6.QtCore import QtMsgType, qInstallMessageHandler

LOG_LEVELS = ("DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL")

_handler: logging.StreamHandler | None = None
_qt_handler_installed = False


class ApplicationLogFormatter(logging.Formatter):
    """Format every record as one stable, parseable line."""

    def format(self, record: logging.LogRecord) -> str:
        timestamp = datetime.fromtimestamp(record.created).strftime("%Y-%m-%d %H:%M:%S")
        centiseconds = int(record.msecs) // 10
        message = record.getMessage()
        if record.exc_info:
            traceback_text = self.formatException(record.exc_info)
            message = f"{message} | {traceback_text}"
        message = message.replace("\r", "\\r").replace("\n", "\\n")
        return f"{timestamp}.{centiseconds:02d} [{record.levelname}] {record.name}: {message}"


def normalise_log_level(level: str | int) -> int:
    if isinstance(level, int):
        return level
    name = str(level).upper()
    if name not in LOG_LEVELS:
        raise ValueError(f"unsupported log level: {level}")
    return logging.getLevelNamesMapping()[name]


def set_log_level(level: str | int) -> None:
    numeric_level = normalise_log_level(level)
    logging.getLogger().setLevel(numeric_level)
    if _handler is not None:
        _handler.setLevel(numeric_level)


def configure_logging(
    level: str | int = "INFO",
    *,
    stream: TextIO | None = None,
    install_qt_handler: bool = True,
) -> None:
    """Install the application handler once and update its active level."""

    global _handler, _qt_handler_installed
    if _handler is None:
        _handler = logging.StreamHandler(stream or sys.stderr)
        _handler.setFormatter(ApplicationLogFormatter())
        logging.getLogger().addHandler(_handler)
        logging.captureWarnings(True)
    elif stream is not None:
        _handler.setStream(stream)
    set_log_level(level)
    if install_qt_handler and not _qt_handler_installed:
        # Make Qt route libav diagnostics through qInstallMessageHandler rather
        # than allowing FFmpeg to write unformatted media dumps to stderr.
        os.environ.setdefault("QT_FFMPEG_DEBUG", "1")
        qInstallMessageHandler(_qt_message_handler)
        _qt_handler_installed = True


def _qt_message_handler(message_type, context, message: str) -> None:
    levels = {
        QtMsgType.QtDebugMsg: logging.DEBUG,
        QtMsgType.QtInfoMsg: logging.INFO,
        QtMsgType.QtWarningMsg: logging.WARNING,
        QtMsgType.QtCriticalMsg: logging.ERROR,
        QtMsgType.QtFatalMsg: logging.CRITICAL,
    }
    category = getattr(context, "category", None)
    module = "qt" if not category or category == "default" else f"qt.{category}"
    level = levels.get(message_type, logging.INFO)
    if message.lstrip('"').startswith("FFmpeg log:"):
        module = "qt.multimedia.ffmpeg"
        level = logging.DEBUG
    logging.getLogger(module).log(level, message)
