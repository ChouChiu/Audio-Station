from __future__ import annotations

import logging
from collections.abc import Callable

from PySide6.QtCore import QObject, Signal, Slot

from audio_station.application.models import CancellationToken, ProcessingCancelled, ProgressEvent


class ProcessingWorker(QObject):
    progress = Signal(int, str)
    succeeded = Signal(object)
    failed = Signal(str)
    cancelled = Signal()
    finished = Signal()

    def __init__(self, operation: Callable):
        super().__init__()
        self.operation = operation
        self.token = CancellationToken()

    def request_cancel(self) -> None:
        self.token.cancel()

    @Slot()
    def run(self) -> None:
        logger = logging.getLogger(__name__)
        try:
            logger.debug("processing worker started")
            result = self.operation(self.token, self._report)
            logger.info("processing worker completed")
            self.succeeded.emit(result)
        except ProcessingCancelled:
            logger.warning("processing worker cancelled")
            self.cancelled.emit()
        except Exception as error:
            logger.exception("processing worker failed: %s", error)
            self.failed.emit(str(error))
        finally:
            self.finished.emit()

    def _report(self, event: ProgressEvent) -> None:
        self.progress.emit(event.value, event.message)
