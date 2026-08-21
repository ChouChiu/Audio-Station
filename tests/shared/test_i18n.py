import ast
import json
from pathlib import Path

from resources import resource_path
from shared.i18n import SUPPORTED_LANGUAGES, tr


def _locale_tables() -> dict[str, dict[str, str]]:
    return {
        lang: json.loads(resource_path(f"i18n/{lang}.json").read_text(encoding="utf-8"))
        for lang in SUPPORTED_LANGUAGES
    }


def test_translation_tables_have_identical_keys():
    tables = list(_locale_tables().values())
    assert tables
    assert all(table.keys() == tables[0].keys() for table in tables[1:])


def test_all_supported_languages_render_and_interpolate():
    for language in SUPPORTED_LANGUAGES:
        assert tr(language, "params")
        assert tr(language, "done_status", path="/tmp/a.wav").endswith("/tmp/a.wav")
        assert "{path}" not in tr(language, "done_status", path="/tmp/a.wav")


def test_unknown_language_falls_back_to_zh_cn():
    assert tr("xx_yy", "params") == tr("zh_cn", "params")


def _literal_tr_keys() -> set[str]:
    keys: set[str] = set()
    for path in Path("src").rglob("*.py"):
        tree = ast.parse(path.read_text(encoding="utf-8"))
        for node in ast.walk(tree):
            if (
                isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id == "tr"
                and len(node.args) >= 2
            ):
                key = node.args[1]
                if isinstance(key, ast.Constant) and isinstance(key.value, str):
                    keys.add(key.value)
    return keys


def test_every_literal_tr_key_exists_in_every_locale():
    tables = list(_locale_tables().values())
    missing = sorted(key for key in _literal_tr_keys() if any(key not in table for table in tables))
    assert not missing, f"tr() keys missing from some locales: {missing}"
