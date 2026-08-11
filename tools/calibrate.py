#!/usr/bin/env python3
"""Manufacturing test/calibration tool for Colibri I/O modules.

Walks an operator through testing and calibrating one I/O module type at a
time in a given slot: insert module -> rescan -> run test/calibration
sequence -> write calibration_data + a test report to the module's EEPROM ->
pass/fail -> repeat for the next module.

Module-specific behavior (what steps to run, how to pack calibration_data,
how to validate) lives in io_*/module.py plugins discovered dynamically next
to this script -- see calibrate_lib/module_base.py for the plugin contract.

Only dependency is pyserial (`pip install pyserial`); the DMM talks plain
SCPI over a TCP socket, no extra library needed.

Example:
    ./calibrate.py --port /dev/ttyACM0 --slot 2 --module io_aqv \\
                    --dmm-host 192.168.255.215 \\
                    --dac-addr1 0x60 --dac-addr2 0x61 --operator jd
"""

import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from calibrate_lib import registry, tui
from calibrate_lib.dmm import Dmm
from calibrate_lib.firmware import Firmware


def main() -> int:
    base_dir = pathlib.Path(__file__).resolve().parent
    registry.discover(base_dir)

    # First pass: resolve --module only, so its class can contribute its own
    # CLI arguments (e.g. I2C addresses) before the real parse below --
    # which is the one that handles --help fully, module args included.
    pre = argparse.ArgumentParser(add_help=False)
    pre.add_argument("--module")
    known, _ = pre.parse_known_args()

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="serial port to the Colibri board, e.g. /dev/ttyACM0")
    ap.add_argument("--speed", type=int, default=115200, help="baud rate (default 115200)")
    ap.add_argument("--slot", type=int, required=True, help="I/O slot number under test")
    ap.add_argument("--module", required=True, help="module id, e.g. io_aqv")
    ap.add_argument("--dmm-host", required=True, help="Siglent SDM3065X (or SCPI-compatible) IP address")
    ap.add_argument("--dmm-port", type=int, default=5025, help="SCPI-raw socket port (default 5025)")
    ap.add_argument("--operator", default="", help="operator name/id, recorded in the report")
    ap.add_argument("--reports-dir", default=str(base_dir / "reports"),
                     help="where operator-facing text reports are saved")

    module_cls = None
    if known.module:
        try:
            module_cls = registry.get(known.module)
        except KeyError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        module_cls.add_arguments(ap)

    args = ap.parse_args()

    if module_cls is None:
        try:
            module_cls = registry.get(args.module)
        except KeyError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1

    module = module_cls(args)

    firmware = Firmware(args.port, args.speed)
    dmm = Dmm(args.dmm_host, args.dmm_port)
    try:
        print(f"DMM: {dmm.idn()}")
        tui.main_loop(firmware, dmm, module, args.slot, args.operator, args.reports_dir)
    finally:
        firmware.close()
        dmm.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
