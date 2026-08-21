from __future__ import annotations

import re
from dataclasses import replace
from pathlib import Path

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QAbstractItemView,
    QFileDialog,
    QHBoxLayout,
    QHeaderView,
    QListWidgetItem,
    QTableWidgetItem,
)
from qfluentwidgets import (
    BodyLabel,
    FluentIcon,
    LineEdit,
    ListWidget,
    PrimaryPushButton,
    ProgressBar,
    PushButton,
    Slider,
    SwitchButton,
    TableWidget,
    TitleLabel,
)

from features.full_stage.models import ClipKind, FullStageAnalysis
from shared.config import cfg
from shared.i18n import tr
from shared.ui import FormCard, PageScrollArea, SmoothComboBox


class FullStagePage(PageScrollArea):
    analyze_requested = Signal()
    start_requested = Signal()
    cancel_requested = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("fullStagePage")
        self.language = "zh_cn"
        self.analysis: FullStageAnalysis | None = None
        self._updating_timeline = False

        self.title = TitleLabel()
        self.layout.addWidget(self.title)

        self.files = FormCard()
        self.stage_label, self.stage_edit, self.stage_button = BodyLabel(), LineEdit(), PushButton()
        self.stage_edit.setReadOnly(True)
        self.output_label, self.output_edit, self.output_button = (
            BodyLabel(),
            LineEdit(),
            PushButton(),
        )
        self.files.add_row(self.stage_label, self.stage_edit, self.stage_button)
        self.files.add_row(self.output_label, self.output_edit, self.output_button)
        self.layout.addWidget(self.files)

        self.sources_card = FormCard()
        self.sources_hint = BodyLabel()
        self.sources_hint.setWordWrap(True)
        self.sources_card.layout.addWidget(self.sources_hint)
        self.sources = ListWidget()
        self.sources.setMinimumHeight(150)
        self.sources.setDragDropMode(QAbstractItemView.DragDropMode.NoDragDrop)
        self.sources_card.layout.addWidget(self.sources)
        source_actions = QHBoxLayout()
        self.add_sources = PushButton(FluentIcon.ADD, "")
        self.remove_source = PushButton(FluentIcon.DELETE, "")
        for button in (self.add_sources, self.remove_source):
            source_actions.addWidget(button)
        source_actions.addStretch()
        self.sources_card.layout.addLayout(source_actions)
        self.layout.addWidget(self.sources_card)

        self.parameters = FormCard()
        self.strength_label, self.strength_value = BodyLabel(), BodyLabel("75%")
        self.strength = Slider(Qt.Orientation.Horizontal)
        self.strength.setRange(0, 100)
        self.strength.setValue(75)
        self.sigma_label, self.sigma = BodyLabel(), SmoothComboBox()
        self.align_label, self.align = BodyLabel(), SwitchButton()
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
        self.include_fragments_label, self.include_fragments = BodyLabel(), SwitchButton()
        self.include_fragments.setChecked(True)
        self.parameters.add_row(self.strength_label, self.strength, self.strength_value)
        self.parameters.add_row(self.sigma_label, self.sigma)
        self.parameters.add_row(self.align_label, self.align)
        self.parameters.add_row(self.center_extraction_label, self.center_extraction)
        self.parameters.add_row(self.weak_vocal_protection_label, self.weak_vocal_protection)
        self.parameters.add_row(self.include_fragments_label, self.include_fragments)
        self.layout.addWidget(self.parameters)

        self.timeline_card = FormCard()
        self.timeline_hint = BodyLabel()
        self.timeline_hint.setWordWrap(True)
        self.timeline_card.layout.addWidget(self.timeline_hint)
        self.timeline = TableWidget()
        self.timeline.setColumnCount(6)
        self.timeline.setMinimumHeight(260)
        self.timeline.setEditTriggers(
            QAbstractItemView.EditTrigger.DoubleClicked
            | QAbstractItemView.EditTrigger.EditKeyPressed
        )
        self.timeline.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.timeline.verticalHeader().hide()
        header = self.timeline.horizontalHeader()
        header.setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
        header.setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        header.setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
        header.setSectionResizeMode(3, QHeaderView.ResizeMode.ResizeToContents)
        header.setSectionResizeMode(4, QHeaderView.ResizeMode.ResizeToContents)
        header.setSectionResizeMode(5, QHeaderView.ResizeMode.Stretch)
        self.timeline_card.layout.addWidget(self.timeline)
        self.layout.addWidget(self.timeline_card)

        self.status_card = FormCard()
        self.status = BodyLabel()
        self.status.setWordWrap(True)
        self.progress = ProgressBar()
        self.status_card.layout.addWidget(self.status)
        self.status_card.layout.addWidget(self.progress)
        self.layout.addWidget(self.status_card)

        actions = QHBoxLayout()
        actions.addStretch()
        self.cancel_button = PushButton()
        self.analyze_button = PushButton(FluentIcon.SYNC, "")
        self.start_button = PrimaryPushButton(FluentIcon.PLAY, "")
        self.cancel_button.setEnabled(False)
        self.start_button.setEnabled(False)
        actions.addWidget(self.cancel_button)
        actions.addWidget(self.analyze_button)
        actions.addWidget(self.start_button)
        self.layout.addLayout(actions)
        self.layout.addStretch()

        self.stage_button.clicked.connect(self._select_stage)
        self.output_button.clicked.connect(self._select_output)
        self.add_sources.clicked.connect(self._add_sources)
        self.remove_source.clicked.connect(self._remove_source)
        self.analyze_button.clicked.connect(self.analyze_requested)
        self.start_button.clicked.connect(self.start_requested)
        self.cancel_button.clicked.connect(self.cancel_requested)
        self.strength.valueChanged.connect(lambda value: self.strength_value.setText(f"{value}%"))
        self.center_extraction.checkedChanged.connect(self._sync_enhancement_controls)
        self.timeline.itemChanged.connect(self._timeline_item_changed)

    def retranslate(self, language: str) -> None:
        self.language = language
        self.title.setText(tr(language, "nav_full_stage"))
        self.files.title_label.setText(tr(language, "stage_files"))
        self.stage_label.setText(tr(language, "stage_audio"))
        self.output_label.setText(tr(language, "output_file"))
        self.output_edit.setPlaceholderText(tr(language, "stage_output_hint"))
        self.stage_button.setText(tr(language, "browse"))
        self.output_button.setText(tr(language, "browse"))
        self.sources_card.title_label.setText(tr(language, "stage_sources"))
        self.sources_hint.setText(tr(language, "stage_sources_hint"))
        self.add_sources.setText(tr(language, "stage_add_sources"))
        self.remove_source.setText(tr(language, "stage_remove_source"))
        self.parameters.title_label.setText(tr(language, "params"))
        self.strength_label.setText(tr(language, "strength"))
        self.sigma_label.setText(tr(language, "reverb_label"))
        self.align_label.setText(tr(language, "auto_align"))
        self.center_extraction_label.setText(tr(language, "center_extraction"))
        self.weak_vocal_protection_label.setText(tr(language, "weak_vocal_protection"))
        self.include_fragments_label.setText(tr(language, "stage_include_fragments"))
        for switch in (
            self.align,
            self.center_extraction,
            self.weak_vocal_protection,
            self.include_fragments,
        ):
            switch.setOnText(tr(language, "switch_on"))
            switch.setOffText(tr(language, "switch_off"))
        self.align.setToolTip(tr(language, "auto_align_tip"))
        self.center_extraction.setToolTip(tr(language, "center_extraction_tip"))
        self.weak_vocal_protection.setToolTip(tr(language, "weak_vocal_protection_tip"))
        previous_sigma = self.sigma.currentData() or cfg.sigma.value
        self.sigma.clear()
        for value, key in ((1, "sigma_0"), (3, "sigma_1"), (8, "sigma_2"), (16, "sigma_3")):
            self.sigma.addItem(tr(language, key), userData=value)
        self.sigma.setCurrentIndex(max(0, self.sigma.findData(previous_sigma)))
        self.timeline_card.title_label.setText(tr(language, "stage_timeline"))
        self.timeline_hint.setText(tr(language, "stage_timeline_hint"))
        self.timeline.setHorizontalHeaderLabels(
            [
                tr(language, "stage_clip_enabled"),
                tr(language, "stage_clip_type"),
                tr(language, "stage_clip_time"),
                tr(language, "stage_source_time"),
                tr(language, "stage_confidence"),
                tr(language, "stage_clip_source"),
            ]
        )
        self.status_card.title_label.setText(tr(language, "status_group"))
        self.cancel_button.setText(tr(language, "cancel"))
        self.analyze_button.setText(tr(language, "stage_analyze"))
        self.start_button.setText(tr(language, "stage_start"))
        if self.progress.value() == 0:
            self.status.setText(tr(language, "stage_ready"))
        if self.analysis is not None:
            self._render_timeline()
        self._sync_enhancement_controls()

    def _sync_enhancement_controls(self, _checked: bool | None = None) -> None:
        center_enabled = self.center_extraction.isChecked()
        if not center_enabled:
            self.weak_vocal_protection.setChecked(False)
        self.weak_vocal_protection.setEnabled(center_enabled)

    def source_paths(self) -> tuple[Path, ...]:
        return tuple(
            Path(str(self.sources.item(index).data(Qt.ItemDataRole.UserRole)))
            for index in range(self.sources.count())
        )

    def normalized_output_path(self) -> Path | None:
        text = self.output_edit.text().strip()
        if not text:
            stage = self.stage_edit.text().strip()
            if not stage:
                return None
            source = Path(stage).expanduser().resolve()
            path = source.with_name(source.stem + "_full_stage_vocals.wav")
        else:
            path = Path(text).expanduser()
            if not path.is_absolute() and self.stage_edit.text().strip():
                path = Path(self.stage_edit.text()).expanduser().resolve().parent / path
        if path.suffix.lower() != ".wav":
            path = path.with_suffix(".wav")
        path = path.resolve()
        self.output_edit.setText(str(path))
        return path

    def set_running(self, running: bool) -> None:
        self.cancel_button.setEnabled(running)
        self.analyze_button.setEnabled(not running)
        self.start_button.setEnabled(not running and self.analysis is not None)
        for control in (
            self.stage_button,
            self.output_button,
            self.output_edit,
            self.add_sources,
            self.remove_source,
            self.sources,
            self.strength,
            self.sigma,
            self.align,
            self.center_extraction,
            self.weak_vocal_protection,
            self.include_fragments,
            self.timeline,
        ):
            control.setEnabled(not running)
        if not running:
            self._sync_enhancement_controls()

    def set_analysis(self, analysis: FullStageAnalysis) -> None:
        self.analysis = analysis
        self._render_timeline()
        self.start_button.setEnabled(True)
        self.status.setText(
            tr(
                self.language,
                "stage_analysis_summary",
                songs=len(analysis.song_clips),
                fragments=sum(clip.kind == ClipKind.FRAGMENT for clip in analysis.clips),
                missing=len(analysis.missing_sources),
            )
        )

    def invalidate_analysis(self, clear_timeline: bool = True) -> None:
        self.analysis = None
        self.start_button.setEnabled(False)
        if clear_timeline:
            self.timeline.setRowCount(0)

    def _select_stage(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self,
            tr(self.language, "stage_audio"),
            filter="Audio (*.wav *.flac *.mp3 *.m4a *.ogg *.opus)",
        )
        if not path:
            return
        self.stage_edit.setText(path)
        self.output_edit.setText(
            str(Path(path).with_name(Path(path).stem + "_full_stage_vocals.wav"))
        )
        self.invalidate_analysis()

    def _select_output(self) -> None:
        path, _ = QFileDialog.getSaveFileName(
            self,
            tr(self.language, "output_file"),
            self.output_edit.text(),
            "WAV (*.wav)",
        )
        if path:
            self.output_edit.setText(path)

    def _add_sources(self) -> None:
        paths, _ = QFileDialog.getOpenFileNames(
            self,
            tr(self.language, "stage_sources"),
            filter="Audio (*.wav *.flac *.mp3 *.m4a *.ogg *.opus)",
        )
        existing = {str(path.resolve()) for path in self.source_paths()}
        for path_text in paths:
            path = Path(path_text).expanduser().resolve()
            if str(path) in existing:
                continue
            item = QListWidgetItem(path.name)
            item.setData(Qt.ItemDataRole.UserRole, str(path))
            item.setToolTip(str(path))
            self.sources.addItem(item)
            existing.add(str(path))
        if paths:
            self.invalidate_analysis()

    def _remove_source(self) -> None:
        row = self.sources.currentRow()
        if row >= 0:
            self.sources.takeItem(row)
            self.invalidate_analysis()

    @staticmethod
    def _clock(seconds: float) -> str:
        milliseconds = max(0, round(seconds * 1000))
        whole, millis = divmod(milliseconds, 1000)
        minutes, secs = divmod(whole, 60)
        hours, minutes = divmod(minutes, 60)
        prefix = f"{hours:d}:{minutes:02d}" if hours else f"{minutes:02d}"
        return f"{prefix}:{secs:02d}.{millis:03d}"

    @staticmethod
    def _parse_clock(text: str) -> float:
        parts = text.strip().split(":")
        if not 1 <= len(parts) <= 3:
            raise ValueError("invalid time")
        values = [float(part.strip()) for part in parts]
        if any(value < 0 for value in values):
            raise ValueError("negative time")
        if len(values) == 1:
            return values[0]
        if len(values) == 2:
            return values[0] * 60 + values[1]
        return values[0] * 3600 + values[1] * 60 + values[2]

    @classmethod
    def _parse_range(cls, text: str) -> tuple[float, float]:
        parts = re.split(r"\s+-\s+", text.strip())
        if len(parts) != 2:
            raise ValueError("time range must contain a separated dash")
        start, end = (cls._parse_clock(part) for part in parts)
        if end <= start:
            raise ValueError("time range must be positive")
        return start, end

    def _timeline_item_changed(self, item: QTableWidgetItem) -> None:
        if self._updating_timeline or self.analysis is None:
            return
        row = item.row()
        if not 0 <= row < len(self.analysis.clips):
            return
        clip = self.analysis.clips[row]
        try:
            if item.column() == 0 and clip.kind != ClipKind.UNMATCHED:
                updated = replace(clip, enabled=item.checkState() == Qt.CheckState.Checked)
            elif item.column() == 2:
                start, end = self._parse_range(item.text())
                if end > self.analysis.duration_seconds:
                    raise ValueError("stage range exceeds duration")
                updated = replace(clip, stage_start=start, stage_end=end)
            elif item.column() == 3 and clip.kind != ClipKind.UNMATCHED:
                start, end = self._parse_range(item.text())
                updated = replace(clip, source_start=start, source_end=end)
            else:
                return
        except ValueError:
            self.status.setText(tr(self.language, "stage_invalid_edit"))
            self._render_timeline()
            return
        clips = list(self.analysis.clips)
        clips[row] = updated
        self.analysis = replace(self.analysis, clips=tuple(clips))
        self.status.setText(tr(self.language, "stage_manual_updated"))
        if item.column() == 0:
            self._render_timeline()

    def _render_timeline(self) -> None:
        analysis = self.analysis
        self._updating_timeline = True
        self.timeline.blockSignals(True)
        try:
            self.timeline.setRowCount(0 if analysis is None else len(analysis.clips))
            if analysis is None:
                return
            type_keys = {
                ClipKind.SONG: "stage_type_song",
                ClipKind.FRAGMENT: "stage_type_fragment",
                ClipKind.UNMATCHED: "stage_type_unmatched",
            }
            for row, clip in enumerate(analysis.clips):
                enabled = QTableWidgetItem()
                enabled.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
                enabled.setCheckState(
                    Qt.CheckState.Checked if clip.enabled else Qt.CheckState.Unchecked
                )
                enabled.setFlags(
                    Qt.ItemFlag.ItemIsEnabled | Qt.ItemFlag.ItemIsUserCheckable
                    if clip.kind != ClipKind.UNMATCHED
                    else Qt.ItemFlag.NoItemFlags
                )
                self.timeline.setItem(row, 0, enabled)
                source_range = (
                    "—"
                    if clip.kind == ClipKind.UNMATCHED
                    else f"{self._clock(clip.source_start)} - {self._clock(clip.source_end)}"
                )
                values = (
                    tr(self.language, type_keys[clip.kind]),
                    f"{self._clock(clip.stage_start)} - {self._clock(clip.stage_end)}",
                    source_range,
                    "—" if clip.kind == ClipKind.UNMATCHED else f"{clip.confidence:.0%}",
                    tr(self.language, "stage_unmatched_label")
                    if clip.source is None
                    else clip.source.name,
                )
                for column, value in enumerate(values, start=1):
                    item = QTableWidgetItem(value)
                    flags = Qt.ItemFlag.ItemIsEnabled | Qt.ItemFlag.ItemIsSelectable
                    if column == 2 or (column == 3 and clip.kind != ClipKind.UNMATCHED):
                        flags |= Qt.ItemFlag.ItemIsEditable
                    item.setFlags(flags)
                    if column in {1, 2, 3, 4}:
                        item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
                    self.timeline.setItem(row, column, item)
        finally:
            self.timeline.blockSignals(False)
            self._updating_timeline = False
