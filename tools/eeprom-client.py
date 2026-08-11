#!/usr/bin/env python3
"""EEPROM client over the SMP (mcumgr) serial-console transport.

Talks to the custom management group registered in src/management/eeprom.c:

    group id = 64 (MGMT_GROUP_ID_PERUSER)
    command 0 = read   (op READ,  payload {"addr", "n"})
    command 1 = write  (op WRITE, payload {"addr", "values"})

The device replies with a CBOR map: {"rc": <int>[, "data": <bytes>]}.
This script prints that reply as a single-line JSON object.

Only dependency is pyserial (`pip install pyserial`).

Examples:
    ./eeprom-client.py --port /dev/ttyACM0 read  2 0 16
    ./eeprom-client.py --port /dev/ttyACM0 write 2 0 firmware.bin

(the first positional is the I/O slot number, 0-31)
"""

import argparse
import json
import sys

import serial  # pyserial

from colibri_smp import (
    EEPROM_MANAGEMENT_GROUP_ID,
    MGMT_OP_READ,
    MGMT_OP_WRITE,
    to_jsonable,
    transact,
)

# --- SMP constants -----------------------------------------------------------

EEPROM_ID_READ = 0             # eeprom_handlers[0].mh_read
EEPROM_ID_WRITE = 1           # eeprom_handlers[1].mh_write

# Firmware caps a single request at EEPROM_XFER_MAX bytes (a whole SMP frame
# must fit the UART MTU), so larger transfers are split into windows.
EEPROM_XFER_MAX = 128


def eeprom_read(ser: serial.Serial, slot: int, addr: int, n: int) -> bytes:
    """Read n bytes, splitting into EEPROM_XFER_MAX windows."""
    out = bytearray()
    off = 0
    while off < n:
        chunk = min(EEPROM_XFER_MAX, n - off)
        resp = transact(ser, MGMT_OP_READ, EEPROM_MANAGEMENT_GROUP_ID, EEPROM_ID_READ,
                        {"slot": slot, "addr": addr + off, "n": chunk})
        rc = resp.get("rc", 0)
        if rc != 0:
            raise EepromError(rc)
        out += resp.get("data", b"")
        off += chunk
    return bytes(out)


def eeprom_write(ser: serial.Serial, slot: int, addr: int, data: bytes) -> None:
    """Write data, splitting into EEPROM_XFER_MAX windows."""
    off = 0
    while off < len(data):
        chunk = data[off:off + EEPROM_XFER_MAX]
        resp = transact(ser, MGMT_OP_WRITE, EEPROM_MANAGEMENT_GROUP_ID, EEPROM_ID_WRITE,
                        {"slot": slot, "addr": addr + off, "values": list(chunk)})
        rc = resp.get("rc", 0)
        if rc != 0:
            raise EepromError(rc)
        off += len(chunk)


class EepromError(Exception):
    """A device request returned a non-zero SMP rc."""

    def __init__(self, rc: int):
        super().__init__(f"device returned rc={rc}")
        self.rc = rc


# --- output ------------------------------------------------------------------

def emit(result: dict) -> None:
    print(json.dumps(to_jsonable(result), separators=(",", ":")))


# --- CLI ---------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(description="EEPROM read/write over SMP serial.")
    ap.add_argument("--port", required=True, help="serial port, e.g. /dev/ttyACM0")
    ap.add_argument("--speed", type=int, default=115200, help="baud rate (default 115200)")
    ap.add_argument("--timeout", type=float, default=5.0, help="response timeout in seconds")

    sub = ap.add_subparsers(dest="cmd", required=True)

    r = sub.add_parser("read", help="read N bytes from EEPROM")
    r.add_argument("slot", type=int, help="I/O slot number (0-31)")
    r.add_argument("addr", type=int, help="start address")
    r.add_argument("n", type=int, help="number of bytes to read")

    w = sub.add_parser("write", help="write a file's bytes to EEPROM")
    w.add_argument("slot", type=int, help="I/O slot number (0-31)")
    w.add_argument("addr", type=int, help="start address")
    w.add_argument("filename", help="file whose bytes are written to EEPROM")

    args = ap.parse_args()

    if not 0 <= args.slot <= 31:
        print("error: slot must be in range 0-31", file=sys.stderr)
        return 1

    try:
        ser = serial.Serial(args.port, args.speed, timeout=args.timeout)
    except serial.SerialException as e:
        print(f"error: cannot open {args.port}: {e}", file=sys.stderr)
        return 1

    try:
        if args.cmd == "read":
            if args.n <= 0:
                print("error: n must be positive", file=sys.stderr)
                return 1
            data = eeprom_read(ser, args.slot, args.addr, args.n)
            result = {"rc": 0, "slot": args.slot, "addr": args.addr,
                      "n": len(data), "data": data}
        else:  # write
            with open(args.filename, "rb") as f:
                data = f.read()
            if not data:
                print("error: file is empty", file=sys.stderr)
                return 1
            eeprom_write(ser, args.slot, args.addr, data)
            result = {"rc": 0, "slot": args.slot, "addr": args.addr, "n": len(data)}
    except EepromError as e:
        emit({"rc": e.rc, "slot": args.slot, "addr": args.addr})
        return 2
    except (TimeoutError, ValueError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    finally:
        ser.close()

    emit(result)
    return 0


if __name__ == "__main__":
    sys.exit(main())
