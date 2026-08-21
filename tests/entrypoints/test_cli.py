import pytest

from entrypoints.cli import build_parser


def test_reference_command_has_no_algorithm_option():
    parser = build_parser()
    defaults = parser.parse_args(["mr", "song.wav", "reference.flac", "output.wav"])

    assert not hasattr(defaults, "algorithm")
    with pytest.raises(SystemExit):
        parser.parse_args(
            ["mr", "song.wav", "reference.flac", "output.wav", "--algorithm", "direct"]
        )


def test_reference_enhancement_switches_default_off_and_can_be_enabled():
    parser = build_parser()
    defaults = parser.parse_args(["mr", "song.wav", "reference.flac", "output.wav"])
    assert not defaults.center_extraction
    assert not defaults.weak_vocal_protection

    enabled = parser.parse_args(
        [
            "mr",
            "song.wav",
            "reference.flac",
            "output.wav",
            "--center-extraction",
            "--weak-vocal-protection",
        ]
    )
    assert enabled.center_extraction
    assert enabled.weak_vocal_protection
