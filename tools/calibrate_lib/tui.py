"""Text UI: walks the operator through one module's test/calibration
sequence, either as a full run or step-by-step, then writes calibration
data + a test report to EEPROM and reports pass/fail. Loops back for the
next module until the operator stops.
"""

from __future__ import annotations

from calibrate_lib import report
from calibrate_lib.module_base import Ctx, IOModule


def _confirm(prompt: str) -> None:
    input(f"{prompt} [Enter to continue] ")


def _ask(prompt: str) -> str:
    return input(f"{prompt}: ")


def _run_step(ctx: Ctx, step) -> None:
    print(f"\n--- {step.title} ---")
    if step.instructions:
        print(step.instructions)
        ctx.confirm("Ready?")
    result = step.run(ctx)
    ctx.results[step.id] = result
    status = "OK" if result.ok else "FAILED"
    note = f": {result.note}" if result.note else ""
    print(f"[{status}] {step.title}{note}")


def run_full_sequence(ctx: Ctx, module: IOModule) -> bool:
    ctx.results.clear()
    for step in module.steps():
        _run_step(ctx, step)
        if not ctx.results[step.id].ok:
            print(f"Step '{step.id}' failed -- aborting sequence.")
            return False
    return True


def run_step_by_step(ctx: Ctx, module: IOModule) -> None:
    steps = module.steps()
    while True:
        print("\nSteps:")
        for i, step in enumerate(steps):
            done = "x" if ctx.results.get(step.id, None) and ctx.results[step.id].ok else " "
            print(f"  [{done}] {i + 1}. {step.title}")
        print("  [0] back")
        choice = _ask("Run which step").strip()
        if choice == "0":
            return
        try:
            step = steps[int(choice) - 1]
        except (ValueError, IndexError):
            print("invalid choice")
            continue
        _run_step(ctx, step)


def _sequence_complete(ctx: Ctx, module: IOModule) -> bool:
    return all(sid in ctx.results and ctx.results[sid].ok for sid in (s.id for s in module.steps()))


def main_loop(firmware, dmm, module: IOModule, slot: int, operator: str, reports_dir: str) -> None:
    ctx = Ctx(firmware, dmm, slot, _confirm, _ask)

    while True:
        print(f"\n=== {module.module_id}: insert module into slot {slot} ===")
        ctx.confirm("Insert module, press Enter when seated")
        firmware.rescan_slot(slot)
        try:
            serial_number = firmware.read_serial_number(slot)
            print(f"Detected slot {slot}: serial={serial_number}")
        except Exception as e:
            serial_number = 0
            print(f"warning: could not read slot metadata: {e}")

        ok = False
        while True:
            choice = _ask("[R]un full sequence, [S]tep-by-step, [A]bort module").strip().lower()
            if choice.startswith("r"):
                ok = run_full_sequence(ctx, module)
                break
            if choice.startswith("s"):
                run_step_by_step(ctx, module)
                if _sequence_complete(ctx, module) and _ask("All steps done -- finish and validate? (y/n)").strip().lower().startswith("y"):
                    ok = True
                    break
                continue
            if choice.startswith("a"):
                ok = False
                break
            print("invalid choice")

        if not ok:
            print("\nSequence incomplete/aborted -- calibration written so far (if any) is still on the "
                  "module; retrying restarts from identity.")
            if _ask("Retry this module? (y/n)").strip().lower().startswith("y"):
                continue
            if not _ask("Insert next module? (y/n)").strip().lower().startswith("y"):
                return
            continue

        cal_data = module.pack_calibration_data(ctx.results)
        firmware.write_calibration_data(slot, cal_data)
        firmware.rescan_slot(slot)  # driver reads calibration_data once, at load

        validation = module.validate(ctx, ctx.results)
        print(f"\nValidation: {'PASS' if validation.passed else 'FAIL'} -- {validation.detail}")

        text = report.operator_report(module.module_id, slot, operator, serial_number,
                                       validation.passed, ctx.results, validation)
        print("\n" + text)
        path = report.save_report(reports_dir, serial_number, text)
        print(f"Report saved: {path}")

        report_blob = report.pack_test_report(module.module_id, operator, validation.passed, validation.detail)
        firmware.write_test_report(slot, report_blob)

        print(f"\n=== RESULT: {'PASS' if validation.passed else 'FAIL'} ===")

        if not validation.passed and _ask("Retry this module? (y/n)").strip().lower().startswith("y"):
            continue

        if not _ask("\nNext module? (y/n)").strip().lower().startswith("y"):
            return
