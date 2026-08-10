from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Signal
from PySide6.QtWidgets import QFileDialog, QHBoxLayout
from qfluentwidgets import (
    BodyLabel,
    LineEdit,
    PrimaryPushButton,
    ProgressBar,
    PushButton,
    TitleLabel,
)

from audio_station.application.i18n import tr
from audio_station.neural import DEFAULT_MODEL_ID, model_catalog
from audio_station.neural.model_store import find_model
from audio_station.presentation.config import cfg
from audio_station.presentation.widgets import FormCard, PageScrollArea, SmoothComboBox


class AiPage(PageScrollArea):
    start_requested = Signal()
    cancel_requested = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("aiPage")
        self.language = "zh_cn"
        self.title = TitleLabel()
        self.layout.addWidget(self.title)
        self.form = FormCard()
        self.song_label, self.song_edit, self.song_button = BodyLabel(), LineEdit(), PushButton()
        self.song_edit.setReadOnly(True)
        self.model_label, self.model = BodyLabel(), SmoothComboBox()
        self.model_status = BodyLabel()
        self.form.add_row(self.song_label, self.song_edit, self.song_button)
        self.form.add_row(self.model_label, self.model)
        self.form.layout.addWidget(self.model_status)
        self.layout.addWidget(self.form)
        self.status_card = FormCard()
        self.status = BodyLabel()
        self.progress = ProgressBar()
        self.status_card.layout.addWidget(self.status)
        self.status_card.layout.addWidget(self.progress)
        self.layout.addWidget(self.status_card)
        actions = QHBoxLayout()
        actions.addStretch()
        self.cancel_button, self.start_button = PushButton(), PrimaryPushButton()
        self.cancel_button.setEnabled(False)
        actions.addWidget(self.cancel_button)
        actions.addWidget(self.start_button)
        self.layout.addLayout(actions)
        self.layout.addStretch()
        self.song_button.clicked.connect(self._select_song)
        self.start_button.clicked.connect(self.start_requested)
        self.cancel_button.clicked.connect(self.cancel_requested)
        self.model.currentIndexChanged.connect(lambda _index: self._update_model_status())

    def retranslate(self, language: str) -> None:
        self.language = language
        self.title.setText(tr(language, "ai_title"))
        self.form.title_label.setText(tr(language, "file_select"))
        self.status_card.title_label.setText(tr(language, "status_group"))
        self.song_label.setText(tr(language, "song_label"))
        self.song_button.setText(tr(language, "browse"))
        self.model_label.setText(tr(language, "ai_model_label"))
        self.cancel_button.setText(tr(language, "cancel"))
        self.start_button.setText(tr(language, "ai_extract"))
        if self.progress.value() == 0:
            self.status.setText(tr(language, "ready"))
        previous = self.model.currentData() or cfg.model.value or DEFAULT_MODEL_ID
        self.model.clear()
        for entry in model_catalog():
            self.model.addItem(
                f"{entry.name} — {tr(language, entry.description_key)}", userData=entry.id
            )
        self.model.setCurrentIndex(max(0, self.model.findData(previous)))
        self._update_model_status()

    def _update_model_status(self) -> None:
        try:
            entry = next(item for item in model_catalog() if item.id == self.model.currentData())
        except StopIteration:
            return
        key = "ai_model_ready" if find_model(entry) else "ai_model_need_download"
        self.model_status.setText(tr(self.language, key))

    def set_running(self, running: bool) -> None:
        self.start_button.setEnabled(not running)
        self.cancel_button.setEnabled(running)
        self.song_button.setEnabled(not running)
        self.model.setEnabled(not running)

    def _select_song(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self,
            tr(self.language, "song_label"),
            filter="Audio (*.wav *.flac *.mp3 *.m4a *.ogg *.opus)",
        )
        if path:
            self.song_edit.setText(str(Path(path)))
