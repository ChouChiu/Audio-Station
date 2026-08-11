import logging
from pathlib import Path

import numpy as np
import pytest
import soundfile as sf
from PySide6.QtCore import QEvent, QPoint
from PySide6.QtGui import QColor, QPalette
from PySide6.QtWidgets import QApplication
from qfluentwidgets import MenuAnimationType
from qfluentwidgets.components.widgets.menu import DummyMenuAnimationManager

from app.main_window import MainWindow
from features.reference_removal.models import AudioStats
from features.reference_removal.page import MrPage
from shared.config import cfg, load_config
from shared.ui import SmoothComboBox, SmoothComboBoxMenu


def test_main_window_has_four_unique_pages(qtbot):
    load_config()
    window = MainWindow()
    qtbot.addWidget(window)
    pages = (window.home, window.mr, window.ai, window.settings)
    assert len({page.objectName() for page in pages}) == 4
    assert window.mr.algorithm.count() == 7
    assert window.ai.model.count() == 4
    assert window.settings.log_level_card.configItem is cfg.log_level
    previous_level = cfg.log_level.value
    cfg.set(cfg.log_level, "ERROR")
    assert logging.getLogger().level == logging.ERROR
    cfg.set(cfg.log_level, previous_level)
    window.retranslate()


def test_home_page_presents_both_workflows(qtbot):
    load_config()
    window = MainWindow()
    qtbot.addWidget(window)
    window.home.retranslate("zh_cn")

    assert window.home.section_title.text() == "选择处理方式"
    assert "原曲 + 对应伴奏" in window.home.mr_card.meta.text()
    assert "仅原曲" in window.home.ai_card.meta.text()

    with qtbot.waitSignal(window.home.mr_requested):
        window.home.mr_card.open_button.click()
    with qtbot.waitSignal(window.home.ai_requested):
        window.home.ai_card.open_button.click()


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
