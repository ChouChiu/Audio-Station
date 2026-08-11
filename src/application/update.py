from __future__ import annotations

from typing import Protocol


class UpdateChecker(Protocol):
    """Reserved update seam. No network implementation exists until a source is configured."""

    def check(self, current_version: str) -> str | None: ...


class DisabledUpdateChecker:
    def check(self, current_version: str) -> None:
        del current_version
        return None
