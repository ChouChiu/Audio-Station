from __future__ import annotations

from PySide6.QtCore import Signal
from PySide6.QtWidgets import QHBoxLayout
from qfluentwidgets import BodyLabel, FluentIcon, PrimaryPushButton, SubtitleLabel, TitleLabel

from application.i18n import tr
from presentation.widgets import FormCard, PageScrollArea


class HomePage(PageScrollArea):
    mr_requested = Signal()
    ai_requested = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("homePage")
        self.title = TitleLabel()
        self.subtitle = SubtitleLabel()
        self.intro = BodyLabel()
        self.intro.setWordWrap(True)
        self.layout.addWidget(self.title)
        self.layout.addWidget(self.subtitle)
        card = FormCard()
        card.layout.addWidget(self.intro)
        buttons = QHBoxLayout()
        self.mr_button = PrimaryPushButton(FluentIcon.MUSIC, "")
        self.ai_button = PrimaryPushButton(FluentIcon.MIX_VOLUMES, "")
        self.mr_button.clicked.connect(self.mr_requested)
        self.ai_button.clicked.connect(self.ai_requested)
        buttons.addWidget(self.mr_button)
        buttons.addWidget(self.ai_button)
        card.layout.addLayout(buttons)
        self.layout.addWidget(card)
        self.layout.addStretch()

    def retranslate(self, language: str) -> None:
        self.title.setText(tr(language, "home_greeting"))
        self.subtitle.setText("Audio Station")
        self.intro.setText(tr(language, "home_intro"))
        self.mr_button.setText(tr(language, "nav_mr"))
        self.ai_button.setText(tr(language, "nav_ai"))
