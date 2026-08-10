import logging

from PySide6.QtCore import QPoint
from qfluentwidgets import MenuAnimationType
from qfluentwidgets.components.widgets.menu import DummyMenuAnimationManager

from audio_station.presentation.config import cfg, load_config
from audio_station.presentation.main_window import MainWindow
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


def test_combo_boxes_use_stable_menu_without_opacity_animation(qtbot):
    combo = SmoothComboBox()
    qtbot.addWidget(combo)
    menu = combo._createComboMenu()
    assert isinstance(menu, SmoothComboBoxMenu)
    menu.exec(QPoint(0, combo.height()), aniType=MenuAnimationType.PULL_UP)
    assert isinstance(menu.aniManager, DummyMenuAnimationManager)
    menu.close()
