from __future__ import annotations

import json
from functools import lru_cache

from audio_station.resources import resource_path

SUPPORTED_LANGUAGES = ("zh_cn", "ja_jp", "ko_kr")


@lru_cache(maxsize=3)
def translations(language: str) -> dict[str, str]:
    code = language if language in SUPPORTED_LANGUAGES else "zh_cn"
    path = resource_path(f"i18n/{code}.json")
    return json.loads(path.read_text(encoding="utf-8"))


def tr(language: str, key: str, **values: object) -> str:
    text = translations(language).get(key, key)
    for name, value in values.items():
        text = text.replace("{" + name + "}", str(value))
    return text
