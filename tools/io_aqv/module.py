"""io_aqv -- dual analog-output module (2x MCP4725 DAC, 0-10V range).

Driver contract, from colibri-drivers/io_aqv/{aqv.c,aqv-calibration.h}
(a separate repo from this one -- not vendored here):

  - calibration_t (128-byte calibration_data blob): uint32_t device1;
    float k1; float m1; uint32_t device2; float k2; float m2; -- device1/
    device2 are the two MCP4725 I2C addresses, packed as "<IffIff". The m1/m2
    fields stay in the wire layout but are unused and always written as 0.0:
    the DAC has no negative range, so there's no way to compensate an opamp
    offset bias by shifting output down -- only gain is correctable, so k is
    the only term. k is calibrated directly in DAC counts per volt (not a
    unitless ~1.0 gain factor): dac_value = wanted_voltage * k, clamped to
    the 12-bit 0-4095 range.
  - OUTPUT event: event.parameter selects the channel, 1 or 2 (1-indexed),
    matching the two subscriptions driver_init() makes.
  - event.value carries the wanted voltage as a raw 32-bit float bit
    pattern in its low 4 bytes (the event bus's value is an int64_t, not a
    float, so the sender packs a float's bits into an integer and the
    driver reinterprets them back -- see the memcpy in driver_event()).
    This gives full float precision (e.g. 0.5V exactly), not an integer
    unit like whole volts or millivolts.

The output1/output2 mixup in driver_event() case 1 has been fixed in that
repo -- both channels now compute and drive correctly.

Each channel is calibrated end-to-end (search for k -> validate) before
moving to the next channel, since the operator has to physically move the
DMM probe between the two output connectors -- interleaving both channels'
searches before validating either would mean moving the probe back and
forth an extra time.

Only one test point (CALIBRATION_VOLT) is used to compute k -- with m fixed
at 0, dac=0 exactly reproduces 0V regardless of k, so there's nothing to
learn from a low-volt measurement; one point fully determines the single
unknown k. Validation separately checks accuracy at VALIDATION_VOLTS (three
points spanning the range) and reports the error at each.

Since we can't be sure the very first k lands the DAC on its closest
achievable code (dac_value is an integer, so most targets aren't hit
exactly), calibration is a two-phase search:
  1. Seed at INITIAL_K_GUESS, measure, then jump once via the through-origin
     ratio correction (k_new = k_seed * target/measured) -- gets close in a
     single step.
  2. From there, nudge k by one DAC-count's worth of correction at a time
     (an "LSB step") in the direction that reduces error, remeasuring each
     time. As soon as a step doesn't improve on the best error seen so far
     (either it crossed to the other side of the target, or noise), stop and
     keep the best k found.
"""

from __future__ import annotations

import argparse
import struct
import time
from time import sleep

from calibrate_lib.module_base import Ctx, IOModule, Step, StepResult, ValidationResult

# COLIBRI_EVENT_TYPE_OUTPUT, from include/colibri-sdk/colibri-events.h
EVENT_TYPE_OUTPUT = 0x07

# The single point k is calibrated against.
CALIBRATION_VOLT = 9.0
# Points checked (and reported on) during validation.
VALIDATION_VOLTS = (9.0, 2.0, 0.5)
SETTLE_SECONDS = 3

# TODO: confirm the acceptable calibration tolerance with the hardware team.
TOLERANCE_VOLTS = 0.05

# Starting guess for k (DAC counts/volt), close to where real hardware lands
# -- avoids assuming the ideal 4095/10V DAC scale as a first guess.
INITIAL_K_GUESS = 309.0

# Bound on the +-1-LSB narrowing loop after the initial ratio jump.
MAX_TRIM_STEPS = 6

CAL_FMT = "<IffIff"  # device1, k1, m1, device2, k2, m2 -- matches calibration_t; m1/m2 always 0.0


def _i2c_addr(s: str) -> int:
    return int(s, 0)


def _volts_to_wire_value(volts: float) -> int:
    """Pack a float's raw 32-bit bit pattern into the event bus's int64_t
    value -- see the module docstring / driver_event()'s memcpy."""
    return struct.unpack("<I", struct.pack("<f", volts))[0]


def _publish_volts(ctx: Ctx, channel: int, volts: float) -> None:
    ctx.firmware.publish(ctx.slot, EVENT_TYPE_OUTPUT, channel, _volts_to_wire_value(volts))


class AqvModule(IOModule):
    module_id = "io_aqv"

    def __init__(self, args: argparse.Namespace):
        super().__init__(args)
        # Running k (DAC counts/volt) per channel -- starts at the seed
        # guess, updated as each channel's search step runs. Read by
        # pack_calibration_data() and report_fields() so the final
        # (redundant, harmless) write/report pass at the end of the
        # sequence reflects what's actually in EEPROM. m is always 0 (see
        # module docstring) so there's nothing to track for it.
        self._k = {1: INITIAL_K_GUESS, 2: INITIAL_K_GUESS}

    @classmethod
    def add_arguments(cls, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--dac-addr1", type=_i2c_addr, required=False, default=0x60,
                             help="MCP4725 I2C address for channel 1, default:0x60")
        parser.add_argument("--dac-addr2", type=_i2c_addr, required=False, default=0x61,
                             help="MCP4725 I2C address for channel 2, default:0x61")

    def steps(self) -> list[Step]:
        result = [self._step_identity()]
        for channel in (1, 2):
            result.append(self._step_calibrate_channel(channel))
            result.append(self._step_validate_channel(channel))
        return result

    def _pack(self) -> bytes:
        k1, k2 = self._k[1], self._k[2]
        return struct.pack(CAL_FMT, self.args.dac_addr1, k1, 0.0, self.args.dac_addr2, k2, 0.0)

    def _step_identity(self) -> Step:
        def run(ctx: Ctx) -> StepResult:
            self._k = {1: INITIAL_K_GUESS, 2: INITIAL_K_GUESS}
            ctx.firmware.write_calibration_data(ctx.slot, self._pack())
            sleep(0.1)
            ctx.firmware.rescan_slot(ctx.slot)
            return StepResult(
                "identity_calibration", True,
                data={"device1": hex(self.args.dac_addr1), "device2": hex(self.args.dac_addr2)},
            )

        return Step(
            id="identity_calibration",
            title=f"Write starting calibration (k={INITIAL_K_GUESS:g} counts/V) + I2C addresses",
            run=run,
            instructions="Puts both channels in a known, safe starting state before measuring.",
        )

    def _measure_at(self, ctx: Ctx, channel: int, k: float, settle: float = SETTLE_SECONDS) -> float:
        """Write k for `channel` (other channel's k untouched), rescan so
        the driver picks it up, publish CALIBRATION_VOLT, and return the
        DMM reading."""
        self._k[channel] = k
        ctx.firmware.write_calibration_data(ctx.slot, self._pack())
        sleep(0.1)
        ctx.firmware.rescan_slot(ctx.slot)
        _publish_volts(ctx, channel, CALIBRATION_VOLT)
        time.sleep(settle)
        measured = ctx.dmm.measure("VOLT:DC")
        print(f"  k={k:.3f} counts/V -> measured {measured:.4f}V")
        return measured

    def _step_calibrate_channel(self, channel: int) -> Step:
        step_id = f"channel{channel}_calibrate"

        def run(ctx: Ctx) -> StepResult:
            # Seed measurement, then one through-origin ratio jump.
            seed_measured = self._measure_at(ctx, channel, INITIAL_K_GUESS)
            k = INITIAL_K_GUESS * CALIBRATION_VOLT / seed_measured
            measured = self._measure_at(ctx, channel, k)
            best_k, best_err = k, abs(measured - CALIBRATION_VOLT)

            # Narrow down by +-1 DAC-count steps until a step stops helping.
            lsb = 1.0 / CALIBRATION_VOLT
            step = lsb if measured < CALIBRATION_VOLT else -lsb
            trim_steps = 0
            for _ in range(MAX_TRIM_STEPS):
                k_try = best_k + step
                measured = self._measure_at(ctx, channel, k_try)
                trim_steps += 1
                err = abs(measured - CALIBRATION_VOLT)
                if err >= best_err:
                    break  # crossed to the other side of the target, or no longer improving
                best_k, best_err = k_try, err

            # Make sure EEPROM ends up holding the best k found, not
            # whatever the last (possibly worse) trial step left behind.
            self._k[channel] = best_k
            ctx.firmware.write_calibration_data(ctx.slot, self._pack())
            sleep(0.1)
            ctx.firmware.rescan_slot(ctx.slot)

            print(f"Channel {channel}: converged k={best_k:.3f} counts/V "
                  f"(error {best_err:.4f}V, {trim_steps} trim step(s))")
            return StepResult(step_id, True, data={"k": best_k, "error": best_err, "trim_steps": trim_steps})

        return Step(
            id=step_id,
            title=f"Channel {channel}: search for best k at {CALIBRATION_VOLT:g}V",
            run=run,
            instructions=f"Connect the DMM (DC volts) to output {channel}. Leave it connected through validation.",
        )

    def _step_validate_channel(self, channel: int) -> Step:
        step_id = f"channel{channel}_validate"

        def run(ctx: Ctx) -> StepResult:
            points = {}
            problems = []
            for nominal in VALIDATION_VOLTS:
                _publish_volts(ctx, channel, nominal)
                time.sleep(SETTLE_SECONDS)
                measured = ctx.dmm.measure("VOLT:DC")
                error = abs(measured - nominal)
                points[nominal] = {"measured": measured, "error": error}
                print(f"Validate ch{channel}@{nominal:g}V: measured {measured:.4f}V (error {error:.4f}V)")
                if error > TOLERANCE_VOLTS:
                    problems.append(
                        f"{nominal:g}V: measured {measured:.4f}V (error {error:.4f}V > {TOLERANCE_VOLTS}V)"
                    )
            ok = not problems
            # Accuracy at each test point, always included (pass or fail) --
            # this is what ends up in the calibration report.
            accuracy = ", ".join(f"{v:g}V err={points[v]['error']:.4f}V" for v in VALIDATION_VOLTS)
            note = accuracy if ok else f"{accuracy} -- OUT OF TOLERANCE ({TOLERANCE_VOLTS}V): " + "; ".join(problems)
            return StepResult(step_id, ok, data=points, note=note)

        return Step(
            id=step_id,
            title=f"Channel {channel}: validate",
            run=run,
            instructions=f"Keep the DMM on output {channel} to validate before moving on.",
        )

    def pack_calibration_data(self, results: dict[str, StepResult]) -> bytes:
        # Coefficients are already written to EEPROM by each channel's
        # search step; this (called once more by tui.py at the end of the
        # sequence) just re-packs the same, final values.
        return self._pack()

    def validate(self, ctx: Ctx, results: dict[str, StepResult]) -> ValidationResult:
        # Per-channel validation already ran (and required the DMM probe to
        # still be on that channel) inside each channelN_validate step --
        # don't re-measure here, just fold those results (which already
        # carry the per-voltage accuracy in their note) together into one
        # detail string for the calibration report.
        ok = True
        summaries = []
        for channel in (1, 2):
            result = results.get(f"channel{channel}_validate")
            if result is None:
                ok = False
                summaries.append(f"ch{channel}: not run")
                continue
            ok = ok and result.ok
            summaries.append(f"ch{channel}: {result.note}")
        return ValidationResult(ok, "; ".join(summaries))

    def report_fields(self, results: dict[str, StepResult]) -> dict:
        fields = {"k1": self._k[1], "k2": self._k[2]}
        for channel in (1, 2):
            result = results.get(f"channel{channel}_validate")
            if not result:
                continue
            for volt, point in result.data.items():
                fields[f"ch{channel}_{volt:g}V_error"] = round(point["error"], 4)
        return fields
