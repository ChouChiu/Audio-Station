from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QStandardPaths
from qfluentwidgets import (
    BoolValidator,
    ConfigItem,
    OptionsConfigItem,
    OptionsValidator,
    QConfig,
    qconfig,
)


class AppConfig(QConfig):
    language = OptionsConfigItem(
        "General", "Language", "zh_cn", OptionsValidator(["zh_cn", "ja_jp", "ko_kr"])
    )
    theme = OptionsConfigItem(
        "General", "Theme", "auto", OptionsValidator(["light", "dark", "auto"])
    )
    log_level = OptionsConfigItem(
        "General",
        "LogLevel",
        "INFO",
        OptionsValidator(["DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"]),
    )
    sigma = OptionsConfigItem("Reference", "Sigma", 8, OptionsValidator([1, 3, 8, 16]))
    auto_align = ConfigItem("Reference", "AutoAlign", True, BoolValidator())
    auto_find = ConfigItem("Reference", "AutoFind", True, BoolValidator())
    center_extraction = ConfigItem("Reference", "CenterExtraction", False, BoolValidator())
    weak_vocal_protection = ConfigItem("Reference", "WeakVocalProtection", False, BoolValidator())
    model = OptionsConfigItem(
        "Neural",
        "Model",
        "mdxnet_1",
        OptionsValidator(["mdxnet_1", "mdxnet_main", "kim_vocal", "kuielab_b"]),
    )
    check_updates = ConfigItem("Updates", "CheckAtStartup", False, BoolValidator())


cfg = AppConfig()


def load_config() -> None:
    directory = QStandardPaths.writableLocation(QStandardPaths.StandardLocation.AppConfigLocation)
    path = Path(directory or Path.home() / ".config/audio-station") / "config.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    qconfig.load(str(path), cfg)
