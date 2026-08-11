"""Operator-facing text report + packed EEPROM test_reports[256] blob."""

from __future__ import annotations

import pathlib
import struct
import time

TEST_REPORT_VERSION = 1
TEST_REPORT_SIZE = 256

# version(B) + 3 pad + timestamp(I, unix seconds) + passed(B) + 3 pad
# + operator(32s NUL-padded) + module_id(16s NUL-padded), remaining bytes
# are a free-form UTF-8 summary (truncated to fit).
_HEADER_FMT = "<B3xIB3x32s16s"
_HEADER_SIZE = struct.calcsize(_HEADER_FMT)


def pack_test_report(module_id: str, operator: str, passed: bool, summary: str) -> bytes:
    header = struct.pack(
        _HEADER_FMT,
        TEST_REPORT_VERSION,
        int(time.time()),
        1 if passed else 0,
        operator.encode("ascii", "replace")[:32],
        module_id.encode("ascii", "replace")[:16],
    )
    remaining = TEST_REPORT_SIZE - _HEADER_SIZE
    text = summary.encode("utf-8", "replace")[:remaining].ljust(remaining, b"\x00")
    return header + text


def operator_report(module_id: str, slot: int, operator: str, serial_number: int,
                     passed: bool, results: dict, validation) -> str:
    lines = [
        "Colibri calibration report",
        f"module:    {module_id}",
        f"slot:      {slot}",
        f"serial:    {serial_number}",
        f"operator:  {operator or '(unspecified)'}",
        f"timestamp: {time.strftime('%Y-%m-%d %H:%M:%S')}",
        f"result:    {'PASS' if passed else 'FAIL'} -- {validation.detail}",
        "",
        "step results:",
    ]
    for step_id, result in results.items():
        extra = f" {result.data}" if result.data else ""
        note = f" -- {result.note}" if result.note else ""
        lines.append(f"  {step_id}: {'ok' if result.ok else 'FAILED'}{extra}{note}")
    return "\n".join(lines) + "\n"


def save_report(reports_dir, serial_number: int, text: str) -> pathlib.Path:
    reports_dir = pathlib.Path(reports_dir)
    reports_dir.mkdir(parents=True, exist_ok=True)
    path = reports_dir / f"{serial_number}_{int(time.time())}.txt"
    path.write_text(text)
    return path
