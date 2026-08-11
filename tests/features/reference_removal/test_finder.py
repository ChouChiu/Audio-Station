from pathlib import Path

from features.reference_removal.finder import filename_similarity, find_best_match


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
