from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import QFileDialog, QHBoxLayout
from qfluentwidgets import (
    BodyLabel,
    LineEdit,
    PrimaryPushButton,
    ProgressBar,
    PushButton,
    Slider,
    SwitchButton,
    TitleLabel,
)

from audio_station.application.i18n import tr
from audio_station.application.models import Algorithm
from audio_station.presentation.config import cfg
from audio_station.presentation.widgets import FormCard, PageScrollArea, SmoothComboBox


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
        for edit in (self.song_edit, self.acc_edit, self.output_edit):
            edit.setReadOnly(True)
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
        self.parameters.add_row(self.algorithm_label, self.algorithm)
        self.parameters.add_row(self.sigma_label, self.sigma)
        self.parameters.add_row(self.strength_label, self.strength, self.strength_value, self.align)
        self.layout.addWidget(self.parameters)

        self.status_card = FormCard()
        self.status = BodyLabel()
        self.progress = ProgressBar()
        self.status_card.layout.addWidget(self.status)
        self.status_card.layout.addWidget(self.progress)
        self.layout.addWidget(self.status_card)
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
        self.start_button.clicked.connect(self.start_requested)
        self.cancel_button.clicked.connect(self.cancel_requested)
        self.strength.valueChanged.connect(lambda value: self.strength_value.setText(f"{value}%"))

    def retranslate(self, language: str) -> None:
        self.language = language
        self.title.setText(tr(language, "nav_mr"))
        self.files.title_label.setText(tr(language, "file_select"))
        self.parameters.title_label.setText(tr(language, "params"))
        self.status_card.title_label.setText(tr(language, "status_group"))
        self.song_label.setText(tr(language, "song_label"))
        self.acc_label.setText(tr(language, "acc_label"))
        self.output_label.setText(tr(language, "output_file"))
        self.algorithm_label.setText(tr(language, "algorithm"))
        self.sigma_label.setText(tr(language, "reverb_label"))
        self.strength_label.setText(tr(language, "strength"))
        for button in (self.song_button, self.acc_button, self.output_button):
            button.setText(tr(language, "browse"))
        self.auto_find.setOnText(tr(language, "auto_find_on"))
        self.auto_find.setOffText(tr(language, "auto_find_off"))
        self.align.setOnText(tr(language, "auto_align"))
        self.align.setOffText(tr(language, "auto_align"))
        self.cancel_button.setText(tr(language, "cancel"))
        self.start_button.setText(tr(language, "start"))
        if self.progress.value() == 0:
            self.status.setText(tr(language, "ready"))
        previous = self.algorithm.currentData() or cfg.algorithm.value
        self.algorithm.clear()
        for key, text_key in (
            (Algorithm.LOSSLESS, "algo_lossless"),
            (Algorithm.SOFT_MASK, "algo_soft_mask"),
            (Algorithm.SPECTRAL_SUBTRACTION, "algo_spectral"),
            (Algorithm.WIENER_FILTER, "algo_wiener"),
            (Algorithm.FREQUENCY_WEIGHTED, "algo_freq_weight"),
            (Algorithm.BINARY_MASK, "algo_binary"),
            (Algorithm.PHASE_SENSITIVE, "algo_phase"),
        ):
            self.algorithm.addItem(tr(language, text_key), userData=key.value)
        self.algorithm.setCurrentIndex(max(0, self.algorithm.findData(previous)))
        previous_sigma = self.sigma.currentData() or cfg.sigma.value
        self.sigma.clear()
        for value, key in ((1, "sigma_0"), (3, "sigma_1"), (8, "sigma_2"), (16, "sigma_3")):
            self.sigma.addItem(tr(language, key), userData=value)
        self.sigma.setCurrentIndex(max(0, self.sigma.findData(previous_sigma)))

    def set_running(self, running: bool) -> None:
        self.start_button.setEnabled(not running)
        self.cancel_button.setEnabled(running)
        for control in (
            self.song_button,
            self.acc_button,
            self.output_button,
            self.algorithm,
            self.sigma,
            self.strength,
            self.align,
            self.auto_find,
        ):
            control.setEnabled(not running)

    def _select_song(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self,
            tr(self.language, "song_label"),
            filter="Audio (*.wav *.flac *.mp3 *.m4a *.ogg *.opus)",
        )
        if path:
            self.song_edit.setText(path)
            output = str(Path(path).with_name(Path(path).stem + "_vocals.wav"))
            self.output_edit.setText(output)
            self.song_changed.emit(path)

    def _select_acc(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self,
            tr(self.language, "acc_label"),
            filter="Audio (*.wav *.flac *.mp3 *.m4a *.ogg *.opus)",
        )
        if path:
            self.acc_edit.setText(path)

    def _select_output(self) -> None:
        path, _ = QFileDialog.getSaveFileName(
            self, tr(self.language, "output_file"), self.output_edit.text(), "WAV (*.wav)"
        )
        if path:
            self.output_edit.setText(path)
