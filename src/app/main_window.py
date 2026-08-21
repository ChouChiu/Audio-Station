from __future__ import annotations

from functools import partial
from pathlib import Path

from PySide6.QtCore import QEvent, QThread
from PySide6.QtGui import QCloseEvent, QColor, QPalette
from PySide6.QtWidgets import QApplication
from qfluentwidgets import (
    FluentIcon,
    FluentWindow,
    InfoBar,
    InfoBarPosition,
    NavigationItemPosition,
    StateToolTip,
    Theme,
    setTheme,
    setThemeColor,
)

from app.full_stage_processing import analyze_full_stage_job, run_full_stage_job
from app.mr_workspace import MrWorkspace
from app.worker import ProcessingWorker
from features.full_stage import FullStageJob, FullStageResult
from features.full_stage.page import FullStagePage
from features.home import HomePage
from features.neural_separation import NeuralJob, get_model, run_neural_job
from features.neural_separation.page import AiPage
from features.reference_removal import (
    ReferenceJob,
    find_best_match,
    run_reference_job,
)
from features.reference_removal.page import MrPage
from features.settings import SettingsPage
from shared.config import cfg
from shared.i18n import tr
from shared.logging import set_log_level


class MainWindow(FluentWindow):
    def __init__(self):
        super().__init__()
        self.language = str(cfg.language.value)
        self.thread: QThread | None = None
        self.worker: ProcessingWorker | None = None
        self.active_page: MrPage | AiPage | FullStagePage | None = None
        self.state_tip: StateToolTip | None = None
        self.close_pending = False
        self.home = HomePage(self)
        self.mr = MrPage()
        self.full_stage = FullStagePage()
        self.mr_workspace = MrWorkspace(self.mr, self.full_stage, self)
        self.ai = AiPage(self)
        self.settings = SettingsPage(self)
        self.resize(1040, 760)
        self.setMinimumSize(820, 620)
        self._apply_theme(str(cfg.theme.value))
        self._build_navigation()
        self._connect()
        self.retranslate()

    def _build_navigation(self) -> None:
        self.home_nav = self.addSubInterface(self.home, FluentIcon.HOME, "")
        self.mr_nav = self.addSubInterface(self.mr_workspace, FluentIcon.MUSIC, "")
        self.ai_nav = self.addSubInterface(self.ai, FluentIcon.MIX_VOLUMES, "")
        self.settings_nav = self.addSubInterface(
            self.settings, FluentIcon.SETTING, "", NavigationItemPosition.BOTTOM
        )

    def _connect(self) -> None:
        self.home.mr_requested.connect(self._open_mr)
        self.home.ai_requested.connect(lambda: self.switchTo(self.ai))
        self.mr.start_requested.connect(self.start_reference)
        self.mr.cancel_requested.connect(self.cancel)
        self.full_stage.analyze_requested.connect(self.analyze_full_stage)
        self.full_stage.start_requested.connect(self.start_full_stage)
        self.full_stage.cancel_requested.connect(self.cancel)
        self.ai.start_requested.connect(self.start_neural)
        self.ai.cancel_requested.connect(self.cancel)
        self.mr.song_changed.connect(self._auto_find)
        cfg.language.valueChanged.connect(self._language_changed)
        cfg.theme.valueChanged.connect(lambda value: self._apply_theme(str(value)))
        cfg.log_level.valueChanged.connect(lambda value: set_log_level(str(value)))

    def _language_changed(self, value: object) -> None:
        self.language = str(value)
        self.retranslate()

    def _open_mr(self) -> None:
        self.mr_workspace.show_single()
        self.switchTo(self.mr_workspace)

    def _apply_theme(self, value: str) -> None:
        theme = {"light": Theme.LIGHT, "dark": Theme.DARK}.get(value, Theme.AUTO)
        setTheme(theme)
        self._apply_system_accent()

    @staticmethod
    def _system_accent_color() -> QColor | None:
        accent = QApplication.palette().color(QPalette.ColorRole.Highlight)
        if not accent.isValid() or accent.hsvSaturationF() < 0.08:
            return None
        return accent

    def _apply_system_accent(self) -> None:
        if accent := self._system_accent_color():
            setThemeColor(accent, save=False, lazy=False)

    def event(self, event: QEvent) -> bool:
        if event.type() == QEvent.Type.ApplicationPaletteChange:
            self._apply_system_accent()
        return super().event(event)

    def retranslate(self) -> None:
        self.setWindowTitle(tr(self.language, "window_title"))
        for page in (self.home, self.mr_workspace, self.ai, self.settings):
            page.retranslate(self.language)
        for navigation, key in (
            (self.home_nav, "nav_home"),
            (self.mr_nav, "nav_mr"),
            (self.ai_nav, "nav_ai"),
            (self.settings_nav, "nav_settings"),
        ):
            navigation.setText(tr(self.language, key))

    def _warning(self, key: str) -> None:
        InfoBar.warning(
            tr(self.language, "warn_title"),
            tr(self.language, key),
            duration=3500,
            position=InfoBarPosition.TOP_RIGHT,
            parent=self,
        )

    def _auto_find(self, song: str) -> None:
        if not self.mr.auto_find.isChecked():
            return
        match = find_best_match(Path(song))
        if match.found:
            self.mr.acc_edit.setText(str(match.path))
            self.mr.status.setText(tr(self.language, "auto_found"))
        else:
            self.mr.acc_edit.clear()
            self.mr.status.setText(tr(self.language, "auto_not_found"))

    def start_reference(self) -> None:
        if self.worker:
            return
        if not self.mr.song_edit.text():
            self._warning("warn_no_song")
            return
        if not self.mr.acc_edit.text():
            self._warning("warn_no_acc")
            return
        output = self.mr.normalized_output_path()
        if output is None:
            self._warning("warn_no_out")
            return
        song = Path(self.mr.song_edit.text()).expanduser().resolve()
        accompaniment = Path(self.mr.acc_edit.text()).expanduser().resolve()
        if song == accompaniment:
            self._warning("warn_same_inputs")
            return
        if output in {song, accompaniment}:
            self._warning("warn_output_conflict")
            return
        try:
            job = ReferenceJob(
                song=song,
                accompaniment=accompaniment,
                output=output,
                strength=self.mr.strength.value(),
                sigma=int(self.mr.sigma.currentData()),
                auto_align=self.mr.align.isChecked(),
                language=self.language,
                algorithm=str(self.mr.algorithm.currentData()),
                center_extraction=self.mr.center_extraction.isChecked(),
                weak_vocal_protection=self.mr.weak_vocal_protection.isChecked(),
            )
        except (ValueError, TypeError):
            self._warning("warn_invalid_parameters")
            return
        cfg.set(cfg.sigma, job.sigma)
        cfg.set(cfg.algorithm, str(job.algorithm))
        cfg.set(cfg.auto_align, job.auto_align)
        cfg.set(cfg.auto_find, self.mr.auto_find.isChecked())
        cfg.set(cfg.center_extraction, job.center_extraction)
        cfg.set(cfg.weak_vocal_protection, job.weak_vocal_protection)
        self.mr.clear_result()
        self._start_worker(self.mr, partial(run_reference_job, job))

    def start_neural(self) -> None:
        if self.worker:
            return
        if not self.ai.song_edit.text():
            self._warning("ai_need_song")
            return
        model_id = str(self.ai.model.currentData())
        try:
            get_model(model_id)
        except KeyError:
            self._warning("ai_invalid_model")
            return
        cfg.set(cfg.model, model_id)
        song = Path(self.ai.song_edit.text())
        job = NeuralJob(song, song.resolve().parent, model_id, language=self.language)
        self._start_worker(self.ai, partial(run_neural_job, job))

    def _full_stage_job(self) -> FullStageJob | None:
        if not self.full_stage.stage_edit.text():
            self._warning("stage_need_audio")
            return None
        sources = self.full_stage.source_paths()
        if not sources:
            self._warning("stage_need_sources")
            return None
        output = self.full_stage.normalized_output_path()
        if output is None:
            self._warning("warn_no_out")
            return None
        try:
            job = FullStageJob(
                stage=Path(self.full_stage.stage_edit.text()).expanduser().resolve(),
                sources=sources,
                output=output,
                strength=self.full_stage.strength.value(),
                sigma=int(self.full_stage.sigma.currentData()),
                language=self.language,
                include_fragments=self.full_stage.include_fragments.isChecked(),
                auto_align=self.full_stage.align.isChecked(),
                algorithm=str(self.full_stage.algorithm.currentData()),
                center_extraction=self.full_stage.center_extraction.isChecked(),
                weak_vocal_protection=self.full_stage.weak_vocal_protection.isChecked(),
            )
        except (TypeError, ValueError):
            self._warning("warn_output_conflict")
            return None
        cfg.set(cfg.sigma, job.sigma)
        cfg.set(cfg.algorithm, str(job.algorithm))
        cfg.set(cfg.auto_align, job.auto_align)
        cfg.set(cfg.center_extraction, job.center_extraction)
        cfg.set(cfg.weak_vocal_protection, job.weak_vocal_protection)
        return job

    def analyze_full_stage(self) -> None:
        if self.worker:
            return
        job = self._full_stage_job()
        if job is None:
            return
        self.full_stage.invalidate_analysis()
        self._start_worker(self.full_stage, partial(analyze_full_stage_job, job))

    def start_full_stage(self) -> None:
        if self.worker:
            return
        job = self._full_stage_job()
        if job is None:
            return
        if self.full_stage.analysis is None:
            self._warning("stage_need_analysis")
            return
        self._start_worker(
            self.full_stage,
            partial(run_full_stage_job, job, self.full_stage.analysis),
        )

    def _start_worker(self, page: MrPage | AiPage | FullStagePage, operation) -> None:
        thread = QThread(self)
        worker = ProcessingWorker(operation)
        worker.moveToThread(thread)
        thread.started.connect(worker.run)
        worker.progress.connect(self._progress)
        worker.succeeded.connect(self._success)
        worker.failed.connect(self._failure)
        worker.cancelled.connect(self._cancelled)
        worker.finished.connect(thread.quit)
        worker.finished.connect(worker.deleteLater)
        thread.finished.connect(thread.deleteLater)
        thread.finished.connect(self._thread_finished)
        self.thread, self.worker, self.active_page = thread, worker, page
        page.progress.setValue(0)
        page.set_running(True)
        self.state_tip = StateToolTip(
            tr(self.language, "processing"), tr(self.language, "loading_song"), self
        )
        self.state_tip.move(self.state_tip.getSuitablePos())
        self.state_tip.show()
        thread.start()

    def _progress(self, value: int, message: str) -> None:
        if self.active_page:
            self.active_page.progress.setValue(value)
            self.active_page.status.setText(message)
        if self.state_tip:
            self.state_tip.setContent(message)

    def _success(self, result) -> None:
        if self.state_tip:
            self.state_tip.setState(True)
            self.state_tip = None
        outputs = "\n".join(map(str, result.outputs))
        if isinstance(self.active_page, MrPage) and result.outputs:
            stats = result.audio_stats[0] if result.audio_stats else None
            self.active_page.set_result(result.outputs[0], stats)
        if isinstance(self.active_page, FullStagePage) and isinstance(result, FullStageResult):
            self.active_page.set_analysis(result.analysis)
            if result.outputs:
                self.active_page.status.setText(
                    tr(self.language, "done_status", path=result.outputs[0])
                )
            else:
                outputs = tr(
                    self.language,
                    "stage_analysis_summary",
                    songs=len(result.analysis.song_clips),
                    fragments=sum(clip.kind.value == "fragment" for clip in result.analysis.clips),
                    missing=len(result.analysis.missing_sources),
                )
        InfoBar.success(
            tr(self.language, "done_title"),
            outputs,
            duration=5000,
            position=InfoBarPosition.TOP_RIGHT,
            parent=self,
        )

    def _failure(self, message: str) -> None:
        if self.state_tip:
            self.state_tip.deleteLater()
            self.state_tip = None
        if self.active_page:
            self.active_page.status.setText(tr(self.language, "err_status", msg=message))
        InfoBar.error(
            tr(self.language, "err_title"),
            message,
            duration=6000,
            position=InfoBarPosition.TOP_RIGHT,
            parent=self,
        )

    def _cancelled(self) -> None:
        if self.active_page:
            self.active_page.status.setText(tr(self.language, "cancelled"))
        if self.state_tip:
            self.state_tip.deleteLater()
            self.state_tip = None

    def _thread_finished(self) -> None:
        if self.active_page:
            self.active_page.set_running(False)
        self.thread = None
        self.worker = None
        self.active_page = None
        if self.close_pending:
            self.close_pending = False
            self.close()

    def cancel(self) -> None:
        if self.worker:
            self.worker.request_cancel()

    def closeEvent(self, event: QCloseEvent) -> None:
        self.mr.stop_preview()
        if self.worker:
            self.close_pending = True
            self.worker.request_cancel()
            event.ignore()
            return
        super().closeEvent(event)
