import pytest

from entrypoints.cli import build_parser


def test_reference_command_defaults_to_spectral_mask_and_accepts_direct():
    parser = build_parser()
    defaults = parser.parse_args(["mr", "song.wav", "reference.flac", "output.wav"])
    direct = parser.parse_args(
        ["mr", "song.wav", "reference.flac", "output.wav", "--algorithm", "direct"]
    )

    assert defaults.algorithm == "spectral_mask"
    assert direct.algorithm == "direct"


def test_reference_command_rejects_unknown_algorithm():
    parser = build_parser()
    with pytest.raises(SystemExit):
        parser.parse_args(
            ["mr", "song.wav", "reference.flac", "output.wav", "--algorithm", "anything"]
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
