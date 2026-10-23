from __future__ import annotations

import atexit
import os
import selectors
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Sequence

from dashboard.data_models import EngineResponse, ProtocolError, parse_frame


class EngineProcessError(RuntimeError):
    """The C++ adapter failed, exited, or violated the protocol."""


class EngineClient:
    def __init__(self, command: str | Path | Sequence[str], timeout: float = 3.0):
        self.command = [str(command)] if isinstance(command, (str, Path)) else list(command)
        if not self.command:
            raise ValueError("Engine command cannot be empty")
        self.timeout = timeout
        self._buffer = bytearray()
        self._closed = False
        self._stderr = tempfile.TemporaryFile(mode="w+b")
        try:
            self.process = subprocess.Popen(
                self.command,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=self._stderr,
                bufsize=0,
            )
            ready = self._read_line()
            if ready != "READY\t1":
                raise EngineProcessError(f"Unexpected engine startup response: {ready!r}")
        except Exception as error:
            process = getattr(self, "process", None)
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            if process is not None:
                if process.stdin is not None:
                    process.stdin.close()
                if process.stdout is not None:
                    process.stdout.close()
            self._stderr.close()
            if isinstance(error, OSError):
                raise EngineProcessError(f"Unable to start engine process: {error}") from error
            raise
        atexit.register(self.close)

    @property
    def alive(self) -> bool:
        return not self._closed and self.process.poll() is None

    @property
    def diagnostic(self) -> str:
        self._stderr.flush()
        self._stderr.seek(0, os.SEEK_END)
        end = self._stderr.tell()
        self._stderr.seek(max(0, end - 4096))
        return self._stderr.read().decode("utf-8", errors="replace").strip()

    def _read_line(self) -> str:
        if self.process.stdout is None:
            raise EngineProcessError("Engine stdout is unavailable")
        deadline = time.monotonic() + self.timeout
        descriptor = self.process.stdout.fileno()
        with selectors.DefaultSelector() as selector:
            selector.register(descriptor, selectors.EVENT_READ)
            while True:
                newline = self._buffer.find(b"\n")
                if newline >= 0:
                    line = bytes(self._buffer[:newline])
                    del self._buffer[: newline + 1]
                    try:
                        return line.decode("utf-8", errors="strict")
                    except UnicodeDecodeError as error:
                        raise EngineProcessError("Engine emitted invalid UTF-8") from error
                if len(self._buffer) > 1_000_000:
                    raise EngineProcessError("Engine response line exceeded size limit")
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not selector.select(remaining):
                    raise EngineProcessError("Timed out waiting for engine output")
                chunk = os.read(descriptor, 4096)
                if not chunk:
                    raise EngineProcessError(self.diagnostic or "Engine closed its output pipe")
                self._buffer.extend(chunk)

    def _request(self, command: str) -> EngineResponse:
        if not self.alive:
            raise EngineProcessError(self.diagnostic or "Engine process is not running")
        if "\n" in command or "\r" in command:
            raise ValueError("Protocol command must be one line")
        try:
            assert self.process.stdin is not None
            self.process.stdin.write((command + "\n").encode("ascii"))
            self.process.stdin.flush()
            lines = [self._read_line()]
            while lines[-1] != "END":
                lines.append(self._read_line())
            return parse_frame(lines)
        except (OSError, UnicodeError, ProtocolError, AssertionError) as error:
            raise EngineProcessError(str(error)) from error

    def submit(self, order_id: int, side: str, price: int, quantity: int) -> EngineResponse:
        if side not in {"BUY", "SELL"}:
            raise ValueError("Side must be BUY or SELL")
        return self._request(f"SUBMIT\t{order_id}\t{side}\t{price}\t{quantity}")

    def cancel(self, order_id: int) -> EngineResponse:
        return self._request(f"CANCEL\t{order_id}")

    def snapshot(self) -> EngineResponse:
        return self._request("SNAPSHOT")

    def reset(self) -> EngineResponse:
        return self._request("RESET")

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        if self.process.poll() is None and self.process.stdin is not None:
            try:
                self.process.stdin.write(b"EXIT\n")
                self.process.stdin.flush()
                self.process.wait(timeout=1)
            except (OSError, subprocess.TimeoutExpired):
                self.process.terminate()
                try:
                    self.process.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
        if self.process.stdin is not None:
            self.process.stdin.close()
        if self.process.stdout is not None:
            self.process.stdout.close()
        self._stderr.close()
