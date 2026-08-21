import logging
from pathlib import Path

import numpy as np
import pytest
import soundfile as sf
from PySide6.QtCore import QEvent, QPoint, Qt
from PySide6.QtGui import QColor, QPalette
from PySide6.QtMultimedia import QMediaDevices
from PySide6.QtWidgets import QApplication, QListWidgetItem
from qfluentwidgets import MenuAnimationType
from qfluentwidgets.components.widgets.menu import DummyMenuAnimationManager

from app.main_window import MainWindow
from features.full_stage import ClipKind, FullStageAnalysis, TimelineClip
from features.full_stage.page import FullStagePage
from features.reference_removal.models import AudioStats
from features.reference_removal.page import MrPage
from shared.config import cfg, load_config
from shared.ui import SmoothComboBox, SmoothComboBoxMenu


def test_main_window_has_mr_workspace_with_two_subpages(qtbot):
    load_config()
    window = MainWindow()
    qtbot.addWidget(window)
    pages = (window.home, window.mr_workspace, window.ai, window.settings)
    assert len({page.objectName() for page in pages}) == 4
    assert window.mr_workspace.stack.count() == 2
    assert window.mr_workspace.stack.currentWidget() is window.mr
    window.mr_workspace.show_full_stage()
    assert window.mr_workspace.stack.currentWidget() is window.full_stage
    window.mr_workspace.show_single()
    assert window.mr_workspace.stack.currentWidget() is window.mr
    assert not hasattr(window.mr, "algorithm")
    window.mr.center_extraction.setChecked(False)
    assert not window.mr.weak_vocal_protection.isEnabled()
    window.mr.center_extraction.setChecked(True)
    assert window.mr.weak_vocal_protection.isEnabled()
    assert window.ai.model.count() == 4
    assert window.settings.log_level_card.configItem is cfg.log_level
    previous_level = cfg.log_level.value
    cfg.set(cfg.log_level, "ERROR")
    assert logging.getLogger().level == logging.ERROR
    cfg.set(cfg.log_level, previous_level)
    window.retranslate()


def test_home_page_presents_mr_workspace_and_ai(qtbot):
    load_config()
    window = MainWindow()
    qtbot.addWidget(window)
    window.home.retranslate("zh_cn")

    assert window.home.section_title.text() == "选择处理方式"
    assert "单曲或全场" in window.home.mr_card.meta.text()
    assert not hasattr(window.home, "full_stage_card")
    assert "仅原曲" in window.home.ai_card.meta.text()

    with qtbot.waitSignal(window.home.mr_requested):
        window.home.mr_card.open_button.click()
    with qtbot.waitSignal(window.home.ai_requested):
        window.home.ai_card.open_button.click()


def test_full_stage_page_orders_sources_and_renders_analysis(qtbot, tmp_path: Path):
    page = FullStagePage()
    qtbot.addWidget(page)
    page.retranslate("zh_cn")
    for name in ("first.wav", "second.wav"):
        path = (tmp_path / name).resolve()
        item = QListWidgetItem(path.name)
        item.setData(Qt.ItemDataRole.UserRole, str(path))
        page.sources.addItem(item)
    assert [path.name for path in page.source_paths()] == ["first.wav", "second.wav"]
    assert not hasattr(page, "move_up")
    page.center_extraction.setChecked(False)
    assert not page.weak_vocal_protection.isEnabled()
    page.center_extraction.setChecked(True)
    assert page.weak_vocal_protection.isEnabled()

    source = (tmp_path / "second.wav").resolve()
    analysis = FullStageAnalysis(
        12.0,
        (
            TimelineClip(ClipKind.UNMATCHED, 0.0, 2.0),
            TimelineClip(ClipKind.SONG, 2.0, 10.0, source, 0, 0.0, 8.0, 0.9),
            TimelineClip(ClipKind.UNMATCHED, 10.0, 12.0),
        ),
    )
    page.set_analysis(analysis)
    assert page.timeline.rowCount() == 3
    assert page.timeline.item(1, 0).checkState() == Qt.CheckState.Checked
    assert page.timeline.item(1, 1).text() == "完整歌曲"
    assert page.timeline.item(1, 4).text() == "90%"
    assert page.start_button.isEnabled()

    page.timeline.item(1, 0).setCheckState(Qt.CheckState.Unchecked)
    assert not page.analysis.clips[1].enabled
    assert not page.analysis.matched_clips
    page.timeline.item(1, 2).setText("00:03.000 - 00:09.000")
    page.timeline.item(1, 3).setText("00:01.000 - 00:07.000")
    assert page.analysis.clips[1].stage_start == 3.0
    assert page.analysis.clips[1].stage_end == 9.0
    assert page.analysis.clips[1].source_start == 1.0
    assert page.analysis.clips[1].source_end == 7.0


def test_full_stage_job_forwards_normal_mr_parameters(qtbot, tmp_path: Path):
    load_config()
    window = MainWindow()
    qtbot.addWidget(window)
    stage = (tmp_path / "stage.wav").resolve()
    source = (tmp_path / "source.wav").resolve()
    window.full_stage.stage_edit.setText(str(stage))
    window.full_stage.output_edit.setText(str(tmp_path / "output.wav"))
    item = QListWidgetItem(source.name)
    item.setData(Qt.ItemDataRole.UserRole, str(source))
    window.full_stage.sources.addItem(item)
    window.full_stage.align.setChecked(False)
    window.full_stage.center_extraction.setChecked(True)
    window.full_stage.weak_vocal_protection.setChecked(True)

    job = window._full_stage_job()

    assert job is not None
    assert not job.auto_align
    assert job.center_extraction
    assert job.weak_vocal_protection


def test_system_accent_color_is_applied_and_tracks_palette_changes(qtbot, monkeypatch):
    window = MainWindow()
    qtbot.addWidget(window)
    applied = []
    monkeypatch.setattr(
        "app.main_window.setThemeColor", lambda color, **_options: applied.append(color)
    )
    original = QApplication.palette()
    palette = QPalette(original)
    accent = QColor("#d52b8c")
    palette.setColor(QPalette.ColorRole.Highlight, accent)

    try:
        QApplication.setPalette(palette)
        QApplication.sendEvent(window, QEvent(QEvent.Type.ApplicationPaletteChange))
        assert applied
        assert applied[-1].name() == accent.name()
    finally:
        QApplication.setPalette(original)


def test_neutral_system_highlight_is_ignored(qtbot, monkeypatch):
    window = MainWindow()
    qtbot.addWidget(window)
    applied = []
    monkeypatch.setattr(
        "app.main_window.setThemeColor", lambda color, **_options: applied.append(color)
    )
    original = QApplication.palette()
    palette = QPalette(original)
    palette.setColor(QPalette.ColorRole.Highlight, QColor("#808080"))

    try:
        QApplication.setPalette(palette)
        QApplication.sendEvent(window, QEvent(QEvent.Type.ApplicationPaletteChange))
        assert not applied
    finally:
        QApplication.setPalette(original)


def test_reference_start_rejects_same_input_before_worker(qtbot, tmp_path: Path, monkeypatch):
    load_config()
    window = MainWindow()
    qtbot.addWidget(window)
    song = tmp_path / "song.wav"
    window.mr.song_edit.setText(str(song))
    window.mr.acc_edit.setText(str(song))
    window.mr.output_edit.setText(str(tmp_path / "output.wav"))
    warnings = []
    monkeypatch.setattr(window, "_warning", warnings.append)
    monkeypatch.setattr(
        window,
        "_start_worker",
        lambda *_args, **_kwargs: pytest.fail("worker must not start"),
    )

    window.start_reference()

    assert warnings == ["warn_same_inputs"]


def test_reference_start_forwards_explicit_enhancement_switches(qtbot, tmp_path: Path, monkeypatch):
    load_config()
    window = MainWindow()
    qtbot.addWidget(window)
    window.mr.song_edit.setText(str(tmp_path / "song.wav"))
    window.mr.acc_edit.setText(str(tmp_path / "reference.wav"))
    window.mr.output_edit.setText(str(tmp_path / "output.wav"))
    window.mr.center_extraction.setChecked(True)
    window.mr.weak_vocal_protection.setChecked(True)
    started = []
    monkeypatch.setattr(window, "_start_worker", lambda page, operation: started.append(operation))

    window.start_reference()

    assert len(started) == 1
    job = started[0].args[0]
    assert job.center_extraction
    assert job.weak_vocal_protection


def test_combo_boxes_use_stable_menu_without_opacity_animation(qtbot):
    combo = SmoothComboBox()
    qtbot.addWidget(combo)
    menu = combo._createComboMenu()
    assert isinstance(menu, SmoothComboBoxMenu)
    menu.exec(QPoint(0, combo.height()), aniType=MenuAnimationType.PULL_UP)
    assert isinstance(menu.aniManager, DummyMenuAnimationManager)
    menu.close()


def test_mr_output_rename_preview_and_audio_data(qtbot, tmp_path: Path):
    page = MrPage()
    qtbot.addWidget(page)
    assert page.audio_output.device().id() == QMediaDevices.defaultAudioOutput().id()
    page.retranslate("zh_cn")
    page.song_edit.setText(str(tmp_path / "concert.wav"))
    page.output_edit.setText("自定义消音结果")
    output = page.normalized_output_path()
    assert output == (tmp_path / "自定义消音结果.wav").resolve()
    assert not page.output_edit.isReadOnly()

    samples = np.zeros((8_000, 2), dtype=np.float32)
    sf.write(output, samples, 8_000, subtype="PCM_24")
    stats = AudioStats(1.0, 8_000, 2, 24, -1.2, -18.5, output.stat().st_size)
    page.set_result(output, stats)
    assert page.preview_play.isEnabled()
    assert page.preview_status.text() == output.name
    assert page.stat_values["duration"].text() == "00:01"
    assert page.stat_values["sample_rate"].text() == "8 kHz"
    assert page.stat_values["channels"].text() == "立体声"
    assert page.stat_values["bit_depth"].text() == "24-bit PCM"
    assert page.stat_values["peak"].text() == "-1.2 dBFS"
    assert page.stat_values["rms"].text() == "-18.5 dBFS"
    page.clear_result()
    assert not page.preview_play.isEnabled()


def test_empty_output_uses_default_next_to_song(qtbot, tmp_path: Path):
    page = MrPage()
    qtbot.addWidget(page)
    song = tmp_path / "concert.performance.m4a"
    page.song_edit.setText(str(song))
    page.output_edit.clear()

    output = page.normalized_output_path()

    assert output == (tmp_path / "concert.performance_vocals.wav").resolve()
    assert page.output_edit.text() == str(output)
    assert not page._output_user_edited


def test_preview_tracks_audio_output_changes(qtbot, monkeypatch):
    calls = []
    monkeypatch.setattr(MrPage, "_sync_preview_output_device", lambda self: calls.append(self))
    page = MrPage()
    qtbot.addWidget(page)

    assert calls == [page]
    page.media_devices.audioOutputsChanged.emit()
    assert calls == [page, page]


def test_preview_seek_groove_click_jumps_to_clicked_position(qtbot):
    page = MrPage()
    qtbot.addWidget(page)
    page.preview_seek.setRange(0, 10_000)
    page.preview_seek.setValue(0)
    page.preview_seek.setEnabled(True)
    page.preview_seek.setFixedWidth(400)
    page.show()
    qtbot.waitExposed(page)
    QApplication.processEvents()
    requested: list[int] = []
    page.preview_seek.seek_requested.connect(requested.append)

    qtbot.mouseClick(
        page.preview_seek,
        Qt.MouseButton.LeftButton,
        pos=QPoint(3 * page.preview_seek.width() // 4, page.preview_seek.height() // 2),
    )

    assert len(requested) == 1
    assert requested[0] == page.preview_seek.value()
    assert requested[0] == pytest.approx(7_500, abs=600)
    assert page.preview_time.text() == f"{page._clock(requested[0])} / 00:10"


def test_long_output_path_does_not_expand_page_width(qtbot, tmp_path: Path):
    page = MrPage()
    qtbot.addWidget(page)
    page.resize(800, 700)
    page.show()
    qtbot.waitExposed(page)
    page.retranslate("zh_cn")
    QApplication.processEvents()
    baseline = page.content.sizeHint().width()
    long_path = tmp_path.joinpath(*(["very-long-directory-name"] * 40), "result.wav")

    page.output_edit.setText(str(long_path))
    page.status.setText(f"处理完成! 文件已保存到: {long_path}")
    page.preview_status.setText("very-long-output-name-" * 80 + ".wav")
    page.content.layout().invalidate()
    page.content.layout().activate()
    QApplication.processEvents()

    assert page.content.sizeHint().width() <= baseline
