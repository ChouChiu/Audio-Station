import json

from resources import resource_path
from shared.i18n import SUPPORTED_LANGUAGES, tr


def test_translation_tables_have_identical_keys():
    tables = [
        json.loads(resource_path(f"i18n/{lang}.json").read_text(encoding="utf-8"))
        for lang in SUPPORTED_LANGUAGES
    ]
    assert all(table.keys() == tables[0].keys() for table in tables[1:])
    assert tr("zh_cn", "done_status", path="/tmp/a.wav").endswith("/tmp/a.wav")
