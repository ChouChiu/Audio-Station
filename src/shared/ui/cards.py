from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QHBoxLayout, QVBoxLayout, QWidget
from qfluentwidgets import CardWidget, ScrollArea, StrongBodyLabel


class FormCard(CardWidget):
    def __init__(self, title: str = "", parent: QWidget | None = None):
        super().__init__(parent)
        self.layout = QVBoxLayout(self)
        self.layout.setContentsMargins(20, 18, 20, 18)
        self.layout.setSpacing(14)
        self.title_label = StrongBodyLabel(title, self)
        self.layout.addWidget(self.title_label)

    def add_row(self, label: QWidget, control: QWidget, *extras: QWidget) -> QHBoxLayout:
        row = QHBoxLayout()
        row.setSpacing(12)
        label.setMinimumWidth(110)
        row.addWidget(label)
        row.addWidget(control, 1)
        for extra in extras:
            row.addWidget(extra)
        self.layout.addLayout(row)
        return row


class PageScrollArea(ScrollArea):
    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        self.setWidgetResizable(True)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setStyleSheet("QScrollArea{background: transparent; border: none}")
        self.content = QWidget(self)
        self.content.setStyleSheet("QWidget{background: transparent}")
        self.layout = QVBoxLayout(self.content)
        self.layout.setContentsMargins(36, 28, 36, 28)
        self.layout.setSpacing(16)
        self.setWidget(self.content)
