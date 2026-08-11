from __future__ import annotations

import ast
from pathlib import Path

SOURCE_ROOT = Path(__file__).parents[1] / "src"


def _imports(path: Path) -> set[str]:
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    names: set[str] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            names.update(alias.name for alias in node.names)
        elif isinstance(node, ast.ImportFrom) and node.module:
            names.add(node.module)
    return names


def test_feature_dependency_boundaries():
    violations: list[str] = []
    for path in SOURCE_ROOT.rglob("*.py"):
        relative = path.relative_to(SOURCE_ROOT)
        imports = _imports(path)
        if relative.parts[0] == "shared":
            forbidden = {name for name in imports if name == "app" or name.startswith("features")}
        elif relative.parts[:2] == ("features", "reference_removal"):
            forbidden = {name for name in imports if name.startswith("features.neural_separation")}
        elif relative.parts[:2] == ("features", "neural_separation"):
            forbidden = {name for name in imports if name.startswith("features.reference_removal")}
        else:
            forbidden = set()
        violations.extend(f"{relative}: {name}" for name in sorted(forbidden))
    assert not violations, "invalid feature dependencies:\n" + "\n".join(violations)
