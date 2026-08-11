import logging
import re

import pytest

from shared.logging import ApplicationLogFormatter, normalise_log_level


def test_application_log_formatter_is_single_line_and_parseable():
    record = logging.LogRecord(
        "audio.io",
        logging.WARNING,
        __file__,
        1,
        "decoder failed: %s\nretrying",
        ("unsupported codec",),
        None,
    )
    record.created = 1_786_275_296.789
    record.msecs = 789

    output = ApplicationLogFormatter().format(record)

    assert re.fullmatch(
        r"\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.78 "
        r"\[WARNING\] audio\.io: "
        r"decoder failed: unsupported codec\\nretrying",
        output,
    )


def test_log_level_validation():
    assert normalise_log_level("debug") == logging.DEBUG
    assert normalise_log_level("CRITICAL") == logging.CRITICAL
    with pytest.raises(ValueError, match="unsupported log level"):
        normalise_log_level("verbose")
