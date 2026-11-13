"""NDJSON events on stdout: the only thing a tool prints in job mode (docs/PRD.md §9.2)."""

from __future__ import annotations

import json
import sys
import threading
from typing import Any, TextIO


class Emitter:
    """Thread-safe event writer. A disabled emitter drops everything."""

    def __init__(self, enabled: bool = True, stream: TextIO | None = None) -> None:
        self.enabled = enabled
        self._stream = stream
        self._lock = threading.Lock()

    def emit(self, event_type: str, **fields: Any) -> None:
        if not self.enabled:
            return
        line = json.dumps({"type": event_type, **fields}, ensure_ascii=False, separators=(",", ":"))
        stream = self._stream or sys.stdout
        with self._lock:
            stream.write(line + "\n")
            stream.flush()

    def progress(self, current: int, total: int | None, label: str | None = None) -> None:
        self.emit("progress", current=current, total=total, label=label)

    def stats(self, **counters: int | float) -> None:
        self.emit("stats", **counters)

    def log(self, level: str, message: str) -> None:
        self.emit("log", level=level, message=message)

    def result(self, **fields: Any) -> None:
        self.emit("result", **fields)
