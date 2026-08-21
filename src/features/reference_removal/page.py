from __future__ import annotations

import logging
import math
from pathlib import Path

from PySide6.QtCore import QPoint, Qt, QUrl, Signal
from PySide6.QtGui import QMouseEvent
from PySide6.QtMultimedia import QAudioOutput, QMediaDevices, QMediaPlayer
from PySide6.QtWidgets import (
    QFileDialog,
    QGridLayout,
    QHBoxLayout,
    QSizePolicy,
    QStyle,
    QStyleOptionSlider,
    QVBoxLayout,
    QWidget,
)
from qfluentwidgets import (
    BodyLabel,
    CaptionLabel,
    LineEdit,
    PrimaryPushButton,
    ProgressBar,
    PushButton,
    Slider,
    StrongBodyLabel,
    SwitchButton,
    TitleLabel,
)

from features.reference_removal.models import AudioStats
from shared.config import cfg
from shared.i18n import tr
from shared.ui import FormCard, PageScrollArea, SmoothComboBox

logger = logging.getLogger(__name__)


class SeekSlider(Slider):
    """Slider that seeks directly when the user clicks its groove."""

    seek_requested = Signal(int)

    def _value_at(self, point: QPoint) -> int:
        option = QStyleOptionSlider()
        self.initStyleOption(option)
        groove = self.style().subControlRect(
            QStyle.ComplexControl.CC_Slider,
            option,
            QStyle.SubControl.SC_SliderGroove,
            self,
        )
        handle = self.style().subControlRect(
            QStyle.ComplexControl.CC_Slider,
            option,
            QStyle.SubControl.SC_SliderHandle,
            self,
        )
        if self.orientation() == Qt.Orientation.Horizontal:
            slider_length = handle.width()
            slider_minimum = groove.left()
            slider_maximum = groove.right() - slider_length + 1
            position = point.x() - slider_length // 2
        else:
            slider_length = handle.height()
            slider_minimum = groove.top()
            slider_maximum = groove.bottom() - slider_length + 1
            position = point.y() - slider_length // 2
        return QStyle.sliderValueFromPosition(
            self.minimum(),
            self.maximum(),
            position - slider_minimum,
            max(slider_maximum - slider_minimum, 0),
            option.upsideDown,
        )

    def mousePressEvent(self, event: QMouseEvent) -> None:
        if event.button() != Qt.MouseButton.LeftButton:
            super().mousePressEvent(event)
            return
        option = QStyleOptionSlider()
        self.initStyleOption(option)
        handle = self.style().subControlRect(
            QStyle.ComplexControl.CC_Slider,
            option,
            QStyle.SubControl.SC_SliderHandle,
            self,
        )
        point = event.position().toPoint()
        if handle.contains(point):
            super().mousePressEvent(event)
            return
        value = self._value_at(point)
        self.setValue(value)
        self.seek_requested.emit(value)
        event.accept()


class MrPage(PageScrollArea):
    start_requested = Signal()
    cancel_requested = Signal()
    song_changed = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("mrPage")
        self.language = "zh_cn"
        self.title = TitleLabel()
        self.layout.addWidget(self.title)
        self.files = FormCard()
        self.song_label, self.song_edit, self.song_button = BodyLabel(), LineEdit(), PushButton()
        self.acc_label, self.acc_edit, self.acc_button = BodyLabel(), LineEdit(), PushButton()
        self.output_label, self.output_edit, self.output_button = (
            BodyLabel(),
            LineEdit(),
            PushButton(),
        )
        for edit in (self.song_edit, self.acc_edit):
            edit.setReadOnly(True)
        self.output_edit.setClearButtonEnabled(True)
        self._output_user_edited = False
        self.auto_find = SwitchButton()
        self.auto_find.setChecked(bool(cfg.auto_find.value))
        self.files.add_row(self.song_label, self.song_edit, self.song_button)
        self.files.add_row(self.acc_label, self.acc_edit, self.acc_button, self.auto_find)
        self.files.add_row(self.output_label, self.output_edit, self.output_button)
        self.layout.addWidget(self.files)

        self.parameters = FormCard()
        self.algorithm_label, self.algorithm = BodyLabel(), SmoothComboBox()
        self.sigma_label, self.sigma = BodyLabel(), SmoothComboBox()
        self.strength_label, self.strength_value = BodyLabel(), BodyLabel("75%")
        self.strength = Slider(Qt.Orientation.Horizontal)
        self.strength.setRange(0, 100)
        self.strength.setValue(75)
        self.align = SwitchButton()
        self.align.setChecked(bool(cfg.auto_align.value))
        self.center_extraction_label, self.center_extraction = BodyLabel(), SwitchButton()
        self.center_extraction.setChecked(bool(cfg.center_extraction.value))
        self.weak_vocal_protection_label, self.weak_vocal_protection = (
            BodyLabel(),
            SwitchButton(),
        )
        self.weak_vocal_protection.setChecked(
            bool(cfg.weak_vocal_protection.value) and self.center_extraction.isChecked()
        )
        self.parameters.add_row(self.algorithm_label, self.algorithm)
        self.parameters.add_row(self.sigma_label, self.sigma)
        self.parameters.add_row(self.strength_label, self.strength, self.strength_value, self.align)
        self.parameters.add_row(self.center_extraction_label, self.center_extraction)
        self.parameters.add_row(self.weak_vocal_protection_label, self.weak_vocal_protection)
        self.layout.addWidget(self.parameters)

        self.status_card = FormCard()
        self.status = BodyLabel()
        self.status.setWordWrap(True)
        self.status.setMinimumWidth(0)
        self.status.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Preferred)
        self.progress = ProgressBar()
        self.status_card.layout.addWidget(self.status)
        self.status_card.layout.addWidget(self.progress)
        self.layout.addWidget(self.status_card)

        self.preview_card = FormCard()
        self.preview_status = BodyLabel()
        self.preview_status.setMinimumWidth(0)
        self.preview_status.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Preferred)
        self.preview_seek = SeekSlider(Qt.Orientation.Horizontal)
        self.preview_seek.setRange(0, 1)
        self.preview_seek.setEnabled(False)
        self.preview_time = BodyLabel("00:00 / 00:00")
        self.preview_play = PrimaryPushButton()
        self.preview_stop = PushButton()
        self.preview_play.setEnabled(False)
        self.preview_stop.setEnabled(False)
        preview_header = QHBoxLayout()
        preview_header.addWidget(self.preview_status, 1)
        preview_header.addWidget(self.preview_time)
        self.preview_card.layout.addLayout(preview_header)
        self.preview_card.layout.addWidget(self.preview_seek)
        preview_controls = QHBoxLayout()
        preview_controls.addWidget(self.preview_play)
        preview_controls.addWidget(self.preview_stop)
        preview_controls.addStretch()
        self.preview_volume_label = BodyLabel()
        self.preview_volume = Slider(Qt.Orientation.Horizontal)
        self.preview_volume.setRange(0, 100)
        self.preview_volume.setValue(75)
        self.preview_volume.setFixedWidth(150)
        preview_controls.addWidget(self.preview_volume_label)
        preview_controls.addWidget(self.preview_volume)
        self.preview_card.layout.addLayout(preview_controls)
        self.layout.addWidget(self.preview_card)

        self.data_card = FormCard()
        self.stats_grid = QGridLayout()
        self.stats_grid.setHorizontalSpacing(24)
        self.stats_grid.setVerticalSpacing(14)
        self.stat_labels: dict[str, CaptionLabel] = {}
        self.stat_values: dict[str, StrongBodyLabel] = {}
        for index, key in enumerate(
            ("duration", "sample_rate", "channels", "bit_depth", "peak", "rms", "file_size")
        ):
            tile = QWidget(self.data_card)
            tile_layout = QVBoxLayout(tile)
            tile_layout.setContentsMargins(0, 0, 0, 0)
            tile_layout.setSpacing(3)
            label = CaptionLabel(tile)
            value = StrongBodyLabel("--", tile)
            tile_layout.addWidget(label)
            tile_layout.addWidget(value)
            self.stat_labels[key] = label
            self.stat_values[key] = value
            self.stats_grid.addWidget(tile, index // 4, index % 4)
        self.data_card.layout.addLayout(self.stats_grid)
        self.layout.addWidget(self.data_card)

        self.media_devices = QMediaDevices(self)
        self.audio_output = QAudioOutput(self)
        self.media_devices.audioOutputsChanged.connect(self._sync_preview_output_device)
        self._sync_preview_output_device()
        self.audio_output.setVolume(0.75)
        self.player = QMediaPlayer(self)
        self.player.setAudioOutput(self.audio_output)
        self._result_path: Path | None = None
        self._result_stats: AudioStats | None = None
        self._seeking = False

        actions = QHBoxLayout()
        actions.addStretch()
        self.cancel_button = PushButton()
        self.start_button = PrimaryPushButton()
        self.cancel_button.setEnabled(False)
        actions.addWidget(self.cancel_button)
        actions.addWidget(self.start_button)
        self.layout.addLayout(actions)
        self.layout.addStretch()

        self.song_button.clicked.connect(self._select_song)
        self.acc_button.clicked.connect(self._select_acc)
        self.output_button.clicked.connect(self._select_output)
        self.output_edit.textEdited.connect(self._output_edited)
        self.output_edit.editingFinished.connect(self.normalized_output_path)
        self.start_button.clicked.connect(self.start_requested)
        self.cancel_button.clicked.connect(self.cancel_requested)
        self.strength.valueChanged.connect(lambda value: self.strength_value.setText(f"{value}%"))
        self.center_extraction.checkedChanged.connect(self._sync_enhancement_controls)
        self.preview_play.clicked.connect(self._toggle_preview)
        self.preview_stop.clicked.connect(self.stop_preview)
        self.preview_volume.valueChanged.connect(
            lambda value: self.audio_output.setVolume(value / 100.0)
        )
        self.preview_seek.sliderPressed.connect(lambda: setattr(self, "_seeking", True))
        self.preview_seek.sliderReleased.connect(self._seek_preview)
        self.preview_seek.sliderMoved.connect(self._preview_slider_moved)
        self.preview_seek.seek_requested.connect(self._seek_preview_to)
        self.player.positionChanged.connect(self._preview_position_changed)
        self.player.durationChanged.connect(self._preview_duration_changed)
        self.player.playbackStateChanged.connect(lambda _state: self._update_preview_button())
        self.player.errorOccurred.connect(self._preview_error)

    def retranslate(self, language: str) -> None:
        self.language = language
        self.title.setText(tr(language, "nav_mr"))
        self.files.title_label.setText(tr(language, "file_select"))
        self.parameters.title_label.setText(tr(language, "params"))
        self.status_card.title_label.setText(tr(language, "status_group"))
        self.preview_card.title_label.setText(tr(language, "preview_title"))
        self.data_card.title_label.setText(tr(language, "audio_data_title"))
        self.song_label.setText(tr(language, "song_label"))
        self.acc_label.setText(tr(language, "acc_label"))
        self.output_label.setText(tr(language, "output_file"))
        self.output_edit.setPlaceholderText(tr(language, "output_name_hint"))
        self.algorithm_label.setText(tr(language, "algorithm_label"))
        self.sigma_label.setText(tr(language, "reverb_label"))
        self.strength_label.setText(tr(language, "strength"))
        self.center_extraction_label.setText(tr(language, "center_extraction"))
        self.weak_vocal_protection_label.setText(tr(language, "weak_vocal_protection"))
        for button in (self.song_button, self.acc_button, self.output_button):
            button.setText(tr(language, "browse"))
        self.auto_find.setOnText(tr(language, "auto_find_on"))
        self.auto_find.setOffText(tr(language, "auto_find_off"))
        self.align.setOnText(tr(language, "auto_align"))
        self.align.setOffText(tr(language, "auto_align"))
        for switch in (self.center_extraction, self.weak_vocal_protection):
            switch.setOnText(tr(language, "switch_on"))
            switch.setOffText(tr(language, "switch_off"))
        self.center_extraction.setToolTip(tr(language, "center_extraction_tip"))
        self.center_extraction_label.setToolTip(tr(language, "center_extraction_tip"))
        self.weak_vocal_protection.setToolTip(tr(language, "weak_vocal_protection_tip"))
        self.weak_vocal_protection_label.setToolTip(tr(language, "weak_vocal_protection_tip"))
        self.cancel_button.setText(tr(language, "cancel"))
        self.start_button.setText(tr(language, "start"))
        self.preview_stop.setText(tr(language, "preview_stop"))
        self.preview_volume_label.setText(tr(language, "preview_volume"))
        for key, label in self.stat_labels.items():
            label.setText(tr(language, f"audio_{key}"))
        self._update_preview_button()
        self._render_stats()
        if self._result_path is None:
            self.preview_status.setText(tr(language, "preview_empty"))
        if self.progress.value() == 0:
            self.status.setText(tr(language, "ready"))
        previous_algorithm = self.algorithm.currentData() or cfg.algorithm.value
        self.algorithm.clear()
        for value, key in (
            ("spectral_mask", "algorithm_spectral_mask"),
            ("direct", "algorithm_direct"),
        ):
            self.algorithm.addItem(tr(language, key), userData=value)
        self.algorithm.setCurrentIndex(max(0, self.algorithm.findData(previous_algorithm)))
        self.algorithm.setToolTip(tr(language, "algorithm_tip"))
        self.algorithm_label.setToolTip(tr(language, "algorithm_tip"))
        previous_sigma = self.sigma.currentData() or cfg.sigma.value
        self.sigma.clear()
        for value, key in ((1, "sigma_0"), (3, "sigma_1"), (8, "sigma_2"), (16, "sigma_3")):
            self.sigma.addItem(tr(language, key), userData=value)
        self.sigma.setCurrentIndex(max(0, self.sigma.findData(previous_sigma)))
        self._sync_enhancement_controls()

    def _sync_enhancement_controls(self, _checked: bool | None = None) -> None:
        center_enabled = self.center_extraction.isChecked()
        if not center_enabled:
            self.weak_vocal_protection.setChecked(False)
        self.weak_vocal_protection.setEnabled(center_enabled)

    def set_running(self, running: bool) -> None:
        self.start_button.setEnabled(not running)
        self.cancel_button.setEnabled(running)
        for control in (
            self.song_button,
            self.acc_button,
            self.output_button,
            self.output_edit,
            self.algorithm,
            self.sigma,
            self.strength,
            self.align,
            self.auto_find,
            self.center_extraction,
            self.weak_vocal_protection,
        ):
            control.setEnabled(not running)
        if not running:
            self._sync_enhancement_controls()

    def _select_song(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self,
            tr(self.language, "song_label"),
            filter="Audio (*.wav *.flac *.mp3 *.m4a *.ogg *.opus)",
        )
        if path:
            self.clear_result()
            self.song_edit.setText(path)
            if not self._output_user_edited or not self.output_edit.text().strip():
                output = str(Path(path).with_name(Path(path).stem + "_vocals.wav"))
                self.output_edit.setText(output)
                self._output_user_edited = False
            self.song_changed.emit(path)

    def _select_acc(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self,
            tr(self.language, "acc_label"),
            filter="Audio (*.wav *.flac *.mp3 *.m4a *.ogg *.opus)",
        )
        if path:
            self.clear_result()
            self.acc_edit.setText(path)

    def _select_output(self) -> None:
        path, _ = QFileDialog.getSaveFileName(
            self, tr(self.language, "output_file"), self.output_edit.text(), "WAV (*.wav)"
        )
        if path:
            self.clear_result()
            self._output_user_edited = True
            self.output_edit.setText(path)

    def _output_edited(self, _text: str) -> None:
        self._output_user_edited = True
        self.clear_result()

    def normalized_output_path(self) -> Path | None:
        text = self.output_edit.text().strip()
        if not text:
            song = self.song_edit.text().strip()
            if not song:
                return None
            source = Path(song).expanduser().resolve()
            path = source.with_name(source.stem + "_vocals.wav")
            self._output_user_edited = False
        else:
            path = Path(text).expanduser()
        if path.suffix.lower() != ".wav":
            path = path.with_suffix(".wav")
        if not path.is_absolute():
            song = self.song_edit.text().strip()
            base = Path(song).expanduser().resolve().parent if song else Path.cwd()
            path = base / path
        path = path.resolve()
        self.output_edit.setText(str(path))
        return path

    @staticmethod
    def _clock(milliseconds: int) -> str:
        total_seconds = max(0, int(milliseconds) // 1000)
        minutes, seconds = divmod(total_seconds, 60)
        hours, minutes = divmod(minutes, 60)
        return f"{hours:d}:{minutes:02d}:{seconds:02d}" if hours else f"{minutes:02d}:{seconds:02d}"

    def set_result(self, path: Path, stats: AudioStats | None = None) -> None:
        result = path.expanduser().resolve()
        if not result.is_file():
            return
        self.stop_preview()
        self._result_path = result
        self._result_stats = stats
        self.player.setSource(QUrl.fromLocalFile(str(result)))
        duration = round(stats.duration_seconds * 1000) if stats else 0
        self.preview_seek.setRange(0, max(duration, 1))
        self.preview_seek.setValue(0)
        self.preview_seek.setEnabled(True)
        self.preview_play.setEnabled(True)
        self.preview_stop.setEnabled(True)
        self.preview_status.setText(result.name)
        self.preview_status.setToolTip(str(result))
        self.preview_time.setText(f"00:00 / {self._clock(duration)}")
        self._render_stats()

    def clear_result(self) -> None:
        self.stop_preview()
        self.player.setSource(QUrl())
        self._result_path = None
        self._result_stats = None
        self.preview_seek.setRange(0, 1)
        self.preview_seek.setValue(0)
        self.preview_seek.setEnabled(False)
        self.preview_play.setEnabled(False)
        self.preview_stop.setEnabled(False)
        self.preview_status.setText(tr(self.language, "preview_empty"))
        self.preview_status.setToolTip("")
        self.preview_time.setText("00:00 / 00:00")
        self._render_stats()

    def _toggle_preview(self) -> None:
        if self._result_path is None:
            return
        if self.player.playbackState() == QMediaPlayer.PlaybackState.PlayingState:
            self.player.pause()
        else:
            self._sync_preview_output_device()
            if self.player.duration() and self.player.position() >= self.player.duration():
                self.player.setPosition(0)
            self.player.play()

    def _sync_preview_output_device(self) -> None:
        selected = QMediaDevices.defaultAudioOutput()
        if selected.isNull() or self.audio_output.device().id() == selected.id():
            return
        self.audio_output.setDevice(selected)
        logger.info("preview output device changed: %s", selected.description())

    def stop_preview(self) -> None:
        self.player.stop()
        self._update_preview_button()

    def _seek_preview(self) -> None:
        self._seek_preview_to(self.preview_seek.value())
        self._seeking = False

    def _seek_preview_to(self, position: int) -> None:
        self.player.setPosition(position)
        self._preview_slider_moved(position)

    def _preview_slider_moved(self, position: int) -> None:
        duration = max(self.player.duration(), self.preview_seek.maximum())
        self.preview_time.setText(f"{self._clock(position)} / {self._clock(duration)}")

    def _preview_position_changed(self, position: int) -> None:
        if not self._seeking:
            self.preview_seek.setValue(position)
        duration = max(self.player.duration(), self.preview_seek.maximum())
        self.preview_time.setText(f"{self._clock(position)} / {self._clock(duration)}")

    def _preview_duration_changed(self, duration: int) -> None:
        if duration > 0:
            self.preview_seek.setMaximum(duration)
        self._preview_position_changed(self.player.position())

    def _update_preview_button(self) -> None:
        key = (
            "preview_pause"
            if self.player.playbackState() == QMediaPlayer.PlaybackState.PlayingState
            else "preview_play"
        )
        self.preview_play.setText(tr(self.language, key))

    def _preview_error(self, *_unused: object) -> None:
        if self._result_path is not None:
            self.preview_status.setText(tr(self.language, "preview_error"))

    @staticmethod
    def _db(value: float) -> str:
        return f"{value:.1f} dBFS" if math.isfinite(value) else "-inf dBFS"

    @staticmethod
    def _file_size(size: int) -> str:
        if size < 1024 * 1024:
            return f"{size / 1024:.1f} KiB"
        return f"{size / (1024 * 1024):.1f} MiB"

    def _render_stats(self) -> None:
        stats = self._result_stats
        if stats is None:
            for value in self.stat_values.values():
                value.setText("--")
            return
        channels = (
            tr(self.language, "audio_mono")
            if stats.channels == 1
            else tr(self.language, "audio_stereo")
            if stats.channels == 2
            else tr(self.language, "audio_channel_count", count=stats.channels)
        )
        values = {
            "duration": self._clock(round(stats.duration_seconds * 1000)),
            "sample_rate": f"{stats.sample_rate / 1000:g} kHz",
            "channels": channels,
            "bit_depth": f"{stats.bit_depth}-bit PCM",
            "peak": self._db(stats.peak_dbfs),
            "rms": self._db(stats.rms_dbfs),
            "file_size": self._file_size(stats.file_size),
        }
        for key, value in values.items():
            self.stat_values[key].setText(value)
