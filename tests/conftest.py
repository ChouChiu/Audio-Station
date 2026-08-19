import os

import pytest
from PySide6.QtCore import QStandardPaths

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
QStandardPaths.setTestModeEnabled(True)


def pytest_addoption(parser):
    parser.addoption("--runslow", action="store_true", help="run 15-minute acceptance benchmarks")


def pytest_collection_modifyitems(config, items):
    if config.getoption("--runslow"):
        return
    skip = pytest.mark.skip(reason="use --runslow to run resource-intensive acceptance tests")
    for item in items:
        if "slow" in item.keywords:
            item.add_marker(skip)
