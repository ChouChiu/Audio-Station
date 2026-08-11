from __future__ import annotations

from qfluentwidgets import FluentIcon, OptionsSettingCard, SwitchSettingCard, TitleLabel

from application.i18n import tr
from presentation.config import cfg
from presentation.widgets import PageScrollArea


class SettingsPage(PageScrollArea):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("settingsPage")
        self.title = TitleLabel()
        self.layout.addWidget(self.title)
        self.language_card = OptionsSettingCard(
            cfg.language, FluentIcon.LANGUAGE, "", texts=["中文", "日本語", "한국어"]
        )
        self.theme_card = OptionsSettingCard(
            cfg.theme, FluentIcon.BRUSH, "", texts=["Light", "Dark", "Auto"]
        )
        self.log_level_card = OptionsSettingCard(
            cfg.log_level,
            FluentIcon.DEVELOPER_TOOLS,
            "",
            texts=["DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"],
        )
        self.update_card = SwitchSettingCard(FluentIcon.UPDATE, "", configItem=cfg.check_updates)
        self.update_card.setEnabled(False)
        self.layout.addWidget(self.language_card)
        self.layout.addWidget(self.theme_card)
        self.layout.addWidget(self.log_level_card)
        self.layout.addWidget(self.update_card)
        self.layout.addStretch()

    def retranslate(self, language: str) -> None:
        self.title.setText(tr(language, "nav_settings"))
        self.language_card.card.setTitle(tr(language, "lang_label"))
        self.theme_card.card.setTitle(tr(language, "theme_label"))
        self.log_level_card.card.setTitle(tr(language, "log_level_label"))
        self.log_level_card.card.setContent(tr(language, "log_level_description"))
        self.update_card.setTitle(tr(language, "update_check"))
        self.update_card.setContent(tr(language, "update_unavailable"))
