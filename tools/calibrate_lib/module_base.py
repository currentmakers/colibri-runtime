"""Plugin contract for io_* test/calibration modules.

A module lives in an io_*/module.py file next to calibrate.py (discovered
dynamically by calibrate_lib.registry) and subclasses IOModule. Subclassing
self-registers it -- see __init_subclass__ below -- so registry.discover()
only needs to import the file, not call anything in it.
"""

from __future__ import annotations

import argparse
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Callable, Optional


@dataclass
class StepResult:
    step_id: str
    ok: bool
    data: dict = field(default_factory=dict)
    note: str = ""


@dataclass
class ValidationResult:
    passed: bool
    detail: str


class Ctx:
    """What a Step's run() callable needs: firmware/DMM access, the slot
    under test, results-so-far, and hooks back into the TUI for operator
    interaction -- kept as plain callables so step logic stays testable
    without importing the TUI module."""

    def __init__(self, firmware, dmm, slot: int,
                 confirm: Callable[[str], None], ask: Callable[[str], str]):
        self.firmware = firmware
        self.dmm = dmm
        self.slot = slot
        self.confirm = confirm
        self.ask = ask
        self.results: dict[str, StepResult] = {}


@dataclass
class Step:
    id: str
    title: str
    run: Callable[[Ctx], StepResult]
    instructions: Optional[str] = None


_REGISTRY: dict[str, type["IOModule"]] = {}


class IOModule(ABC):
    """Base class for one I/O module type's test/calibration behavior.

    module_id must match both the --module CLI value and the io_* directory
    name (e.g. "io_aqv") -- registry.discover() relies on that convention
    to report a useful error if they ever drift apart.
    """

    module_id: str

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        if getattr(cls, "module_id", None):
            _REGISTRY[cls.module_id] = cls

    def __init__(self, args: argparse.Namespace):
        self.args = args

    @classmethod
    def add_arguments(cls, parser: argparse.ArgumentParser) -> None:
        """Override to contribute module-specific CLI arguments (e.g. I2C
        device addresses that vary per board and shouldn't be guessed)."""

    @abstractmethod
    def steps(self) -> list[Step]:
        """The full test/calibration sequence, in order."""

    @abstractmethod
    def pack_calibration_data(self, results: dict[str, StepResult]) -> bytes:
        """Pack computed calibration coefficients into the module's
        calibration_data byte layout (<=128 bytes)."""

    @abstractmethod
    def validate(self, ctx: Ctx, results: dict[str, StepResult]) -> ValidationResult:
        """Re-check the calibration just written and report pass/fail."""

    def report_fields(self, results: dict[str, StepResult]) -> dict:
        """Extra fields to fold into the operator report. Optional."""
        return {}


def registry() -> dict[str, type[IOModule]]:
    return dict(_REGISTRY)
