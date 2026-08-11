import json
from pathlib import Path

from application.finder import filename_similarity, find_best_match
from application.i18n import SUPPORTED_LANGUAGES, tr
from resources import resource_path


def test_accompaniment_finder(tmp_path: Path):
    song = tmp_path / "Artist - Song.wav"
    song.touch()
    expected = tmp_path / "Artist - Song instrumental.flac"
    expected.touch()
    (tmp_path / "unrelated.mp3").touch()
    assert filename_similarity("ABC", "abc") == 1
    assert find_best_match(song).path == expected


def test_accompaniment_finder_excludes_source_alias(tmp_path: Path):
    song = tmp_path / "Artist - Song.wav"
    song.touch()
    (tmp_path / "Artist - Song instrumental.wav").symlink_to(song)
    assert not find_best_match(song).found


def test_accompaniment_finder_excludes_generated_output(tmp_path: Path):
    song = tmp_path / "Artist - Song.wav"
    song.touch()
    (tmp_path / "Artist - Song_vocals.wav").touch()
    (tmp_path / "Artist - Song 消音.wav").touch()
    assert not find_best_match(song).found


def test_translation_tables_have_identical_keys():
    tables = [
        json.loads(resource_path(f"i18n/{lang}.json").read_text(encoding="utf-8"))
        for lang in SUPPORTED_LANGUAGES
    ]
    assert all(table.keys() == tables[0].keys() for table in tables[1:])
    assert tr("zh_cn", "done_status", path="/tmp/a.wav").endswith("/tmp/a.wav")
