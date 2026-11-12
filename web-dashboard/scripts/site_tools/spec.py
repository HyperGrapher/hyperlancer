"""Job input: the PHP worker writes data/jobs/<id>/spec.json and passes it with --spec."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


class SpecError(ValueError):
    pass


def load_spec(path: str | Path) -> dict[str, Any]:
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SpecError(f"Cannot read spec file {path}: {exc}") from exc
    if not isinstance(data, dict):
        raise SpecError("Spec must be a JSON object.")
    params = data.get("params", {})
    if not isinstance(params, dict):
        raise SpecError("Spec 'params' must be an object.")
    return data
