from __future__ import annotations

from PySide6.QtWidgets import QHBoxLayout, QSizePolicy, QStackedWidget, QVBoxLayout, QWidget
from qfluentwidgets import FluentIcon, SegmentedWidget

from features.full_stage.page import FullStagePage
from features.reference_removal.page import MrPage
from shared.i18n import tr


class MrWorkspace(QWidget):
    def __init__(
        self,
        single_page: MrPage,
        full_stage_page: FullStagePage,
        parent: QWidget | None = None,
    ):
        super().__init__(parent)
        self.setObjectName("mrWorkspace")
        self.single_page = single_page
        self.full_stage_page = full_stage_page

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 14, 0, 0)
        layout.setSpacing(4)
        tab_bar = QWidget(self)
        tab_bar.setObjectName("mrTabBar")
        tab_bar.setStyleSheet("QWidget#mrTabBar { background: transparent; }")
        tab_layout = QHBoxLayout(tab_bar)
        tab_layout.setContentsMargins(36, 0, 36, 0)
        tab_layout.setSpacing(0)

        self.tabs = SegmentedWidget(tab_bar)
        self.tabs.setMaximumWidth(320)
        self.tabs.setSizePolicy(QSizePolicy.Policy.Maximum, QSizePolicy.Policy.Fixed)
        self.tabs.addItem(
            "single",
            "",
            lambda: self.set_current("single"),
            FluentIcon.MUSIC,
        )
        self.tabs.addItem(
            "full_stage",
            "",
            lambda: self.set_current("full_stage"),
            FluentIcon.ALBUM,
        )
        tab_layout.addStretch(1)
        tab_layout.addWidget(self.tabs)
        tab_layout.addStretch(1)
        layout.addWidget(tab_bar)

        self.stack = QStackedWidget(self)
        self.stack.addWidget(single_page)
        self.stack.addWidget(full_stage_page)
        layout.addWidget(self.stack, 1)
        self.tabs.currentItemChanged.connect(self._tab_changed)
        self.set_current("single")

    def _tab_changed(self, route: str) -> None:
        page = self.single_page if route == "single" else self.full_stage_page
        self.stack.setCurrentWidget(page)

    def set_current(self, route: str) -> None:
        if route not in {"single", "full_stage"}:
            raise ValueError(f"unknown MR page: {route}")
        self.tabs.setCurrentItem(route)
        self._tab_changed(route)

    def show_single(self) -> None:
        self.set_current("single")

    def show_full_stage(self) -> None:
        self.set_current("full_stage")

    def retranslate(self, language: str) -> None:
        self.tabs.setItemText("single", tr(language, "mr_tab_single"))
        self.tabs.setItemText("full_stage", tr(language, "mr_tab_full_stage"))
        self.single_page.retranslate(language)
        self.full_stage_page.retranslate(language)
