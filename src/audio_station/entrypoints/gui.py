from __future__ import annotations

import io
import signal
import sys
from contextlib import redirect_stdout

from PySide6.QtCore import QTimer
from PySide6.QtWidgets import QApplication

from audio_station import __version__
from audio_station.application.logging import configure_logging, set_log_level


def run_gui(selftest: bool = False) -> int:
    configure_logging()
    app = QApplication.instance() or QApplication(sys.argv[:1])
    app.setApplicationName("Audio Station")
    app.setApplicationVersion(__version__)
    app.setOrganizationName("Audio Station")
    # QFluentWidgets prints a Pro advertisement while its package is imported.
    # Keep stdout reserved for CLI result data even in standalone builds.
    with redirect_stdout(io.StringIO()):
        from audio_station.presentation.config import cfg, load_config

    load_config()
    set_log_level(str(cfg.log_level.value))
    import logging

    logging.getLogger(__name__).info("starting GUI")
    from audio_station.presentation.main_window import MainWindow

    window = MainWindow()
    window.show()
    if selftest:
        QTimer.singleShot(600, app.quit)
    previous_sigint_handler = signal.getsignal(signal.SIGINT)
    signal.signal(signal.SIGINT, lambda _signum, _frame: app.quit())
    signal_timer = QTimer()
    signal_timer.timeout.connect(lambda: None)
    signal_timer.start(250)
    try:
        return app.exec()
    finally:
        signal.signal(signal.SIGINT, previous_sigint_handler)
