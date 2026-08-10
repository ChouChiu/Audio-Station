import logging
from pathlib import Path

import numpy as np
import pytest
import soundfile as sf
from PySide6.QtCore import QPoint
from qfluentwidgets import MenuAnimationType
from qfluentwidgets.components.widgets.menu import DummyMenuAnimationManager

from audio_station.application.models import AudioStats
from audio_station.presentation.config import cfg, load_config
from audio_station.presentation.main_window import MainWindow
from audio_station.presentation.pages import MrPage
from audio_station.presentation.widgets import SmoothComboBox, SmoothComboBoxMenu


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
