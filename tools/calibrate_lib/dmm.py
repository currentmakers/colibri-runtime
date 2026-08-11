"""Minimal SCPI-over-raw-socket client.

Verified against a real Siglent SDM3065X on 192.168.255.215:5025 (plain
newline-terminated ASCII SCPI over TCP -- *IDN? -> "Siglent Technologies,
SDM3065X,...").  Kept generic (function/range as plain SCPI strings, no
per-model assumptions beyond that) so other bench instruments can reuse it.
"""

from __future__ import annotations

import socket
from typing import Optional


class ScpiError(Exception):
    pass


class Dmm:
    def __init__(self, host: str, port: int = 5025, timeout: float = 5.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self._buf = b""

    def close(self) -> None:
        self.sock.close()

    def _write(self, cmd: str) -> None:
        self.sock.sendall(cmd.encode("ascii") + b"\n")

    def _read_line(self) -> str:
        while b"\n" not in self._buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ScpiError("connection closed by instrument")
            self._buf += chunk
        line, self._buf = self._buf.split(b"\n", 1)
        return line.decode("ascii").strip()

    def command(self, cmd: str) -> None:
        self._write(cmd)

    def query(self, cmd: str) -> str:
        self._write(cmd)
        return self._read_line()

    def idn(self) -> str:
        return self.query("*IDN?")

    def configure(self, function: str, range_: str = "AUTO", resolution: Optional[str] = None) -> None:
        """e.g. configure("VOLT:DC", "AUTO") or configure("VOLT:DC", "10", "0.001")."""
        args = range_ if resolution is None else f"{range_},{resolution}"
        self.command(f"CONF:{function} {args}")

    def read(self) -> float:
        return float(self.query("READ?"))

    def measure(self, function: str, range_: str = "AUTO") -> float:
        """One-shot configure+read, e.g. measure("VOLT:DC")."""
        return float(self.query(f"MEAS:{function}? {range_}"))
