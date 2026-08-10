from audio_station.entrypoints.cli import build_parser


def test_legacy_reference_algorithm_names_are_migrated():
    parser = build_parser()
    for legacy in ("lossless", "lossless_center"):
        args = parser.parse_args(
            ["mr", "song.wav", "reference.flac", "output.wav", "--algorithm", legacy]
        )
        assert args.algorithm == "reference_center"
