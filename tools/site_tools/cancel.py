"""Cooperative cancellation: a cancel file (written by the PHP worker) or SIGTERM."""

from __future__ import annotations

import signal
import threading
from pathlib import Path


class Cancelled(Exception):
    pass


class CancelToken:
    def __init__(self, cancel_file: str | Path | None = None) -> None:
        self._file = Path(cancel_file) if cancel_file else None
        self._event = threading.Event()

    def install_signal_handlers(self) -> None:
        def handler(signum, frame):  # type: ignore[no-untyped-def]
            self._event.set()

        for name in ("SIGTERM", "SIGINT", "SIGBREAK"):  # SIGBREAK exists on Windows only
            sig = getattr(signal, name, None)
            if sig is not None:
                try:
                    signal.signal(sig, handler)
                except (ValueError, OSError):
                    pass  # not in the main thread

    @property
    def cancelled(self) -> bool:
        if self._event.is_set():
            return True
        if self._file is not None and self._file.exists():
            self._event.set()
            return True
        return False

    def check(self) -> None:
        if self.cancelled:
            raise Cancelled()
