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
import base64
import json
import struct
import sys

import serial  # pyserial

# --- SMP constants -----------------------------------------------------------

EEPROM_GROUP_ID = 64            # MGMT_GROUP_ID_PERUSER + 0

MGMT_OP_READ = 0
MGMT_OP_WRITE = 2

EEPROM_ID_READ = 0             # eeprom_handlers[0].mh_read
EEPROM_ID_WRITE = 1           # eeprom_handlers[1].mh_write

# Firmware caps a single request at EEPROM_XFER_MAX bytes (a whole SMP frame
# must fit the UART MTU), so larger transfers are split into windows.
EEPROM_XFER_MAX = 128

# Serial-console framing markers (see Zephyr smp_shell / mcumgr serial spec).
FRAME_START = b"\x06\x09"     # first fragment of a packet
FRAME_CONT = b"\x04\x14"     # continuation fragment
FRAME_END = b"\x0a"          # newline terminates each physical line

# Keep each physical line comfortably small: marker(2) + b64 + newline(1).
MAX_B64_PER_LINE = 124


# --- CRC16-CCITT (XMODEM: poly 0x1021, init 0x0000) --------------------------

def crc16_ccitt(data: bytes) -> int:
    crc = 0x0000
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


# --- Minimal CBOR encode/decode ---------------------------------------------
# Only the subset needed here: unsigned/negative ints, text strings, byte
# strings, arrays and maps.

def _cbor_head(major: int, value: int) -> bytes:
    if value < 24:
        return bytes([(major << 5) | value])
    elif value < 0x100:
        return bytes([(major << 5) | 24, value])
    elif value < 0x10000:
        return bytes([(major << 5) | 25]) + struct.pack(">H", value)
    elif value < 0x100000000:
        return bytes([(major << 5) | 26]) + struct.pack(">I", value)
    else:
        return bytes([(major << 5) | 27]) + struct.pack(">Q", value)


def cbor_encode(obj) -> bytes:
    if isinstance(obj, bool):
        return b"\xf5" if obj else b"\xf4"
    if isinstance(obj, int):
        if obj >= 0:
            return _cbor_head(0, obj)
        return _cbor_head(1, -1 - obj)
    if isinstance(obj, str):
        raw = obj.encode("utf-8")
        return _cbor_head(3, len(raw)) + raw
    if isinstance(obj, (bytes, bytearray)):
        return _cbor_head(2, len(obj)) + bytes(obj)
    if isinstance(obj, (list, tuple)):
        out = _cbor_head(4, len(obj))
        for item in obj:
            out += cbor_encode(item)
        return out
    if isinstance(obj, dict):
        out = _cbor_head(5, len(obj))
        for k, v in obj.items():
            out += cbor_encode(k) + cbor_encode(v)
        return out
    raise TypeError(f"cannot CBOR-encode {type(obj)!r}")


# Sentinel for the CBOR "break" byte (0xFF) that ends an indefinite-length item.
_CBOR_BREAK = object()


def _cbor_read(data: bytes, i: int):
    ib = data[i]
    major = ib >> 5
    minor = ib & 0x1F
    i += 1

    # Indefinite length (minor 31): the Zephyr SMP framework uses this for the
    # top-level response map unless canonical CBOR is configured.
    if minor == 31:
        if major == 7:
            return _CBOR_BREAK, i          # break marker
        if major == 2:                      # indefinite byte string
            out = bytearray()
            while True:
                chunk, i = _cbor_read(data, i)
                if chunk is _CBOR_BREAK:
                    return bytes(out), i
                out += chunk
        if major == 3:                      # indefinite text string
            out = ""
            while True:
                chunk, i = _cbor_read(data, i)
                if chunk is _CBOR_BREAK:
                    return out, i
                out += chunk
        if major == 4:                      # indefinite array
            arr = []
            while True:
                item, i = _cbor_read(data, i)
                if item is _CBOR_BREAK:
                    return arr, i
                arr.append(item)
        if major == 5:                      # indefinite map
            m = {}
            while True:
                k, i = _cbor_read(data, i)
                if k is _CBOR_BREAK:
                    return m, i
                v, i = _cbor_read(data, i)
                m[k] = v
            return m, i
        raise ValueError(f"unexpected indefinite-length major type {major}")

    if minor < 24:
        value = minor
    elif minor == 24:
        value = data[i]; i += 1
    elif minor == 25:
        value = struct.unpack_from(">H", data, i)[0]; i += 2
    elif minor == 26:
        value = struct.unpack_from(">I", data, i)[0]; i += 4
    elif minor == 27:
        value = struct.unpack_from(">Q", data, i)[0]; i += 8
    else:
        raise ValueError(f"reserved CBOR additional-info {minor}")

    if major == 0:
        return value, i
    if major == 1:
        return -1 - value, i
    if major == 2:
        return data[i:i + value], i + value
    if major == 3:
        return data[i:i + value].decode("utf-8"), i + value
    if major == 4:
        arr = []
        for _ in range(value):
            item, i = _cbor_read(data, i)
            arr.append(item)
        return arr, i
    if major == 5:
        m = {}
        for _ in range(value):
            k, i = _cbor_read(data, i)
            v, i = _cbor_read(data, i)
            m[k] = v
        return m, i
    if major == 7:
        if minor == 20:
            return False, i
        if minor == 21:
            return True, i
        if minor == 22:
            return None, i
        return value, i
    raise ValueError(f"unsupported CBOR major type {major}")


def cbor_decode(data: bytes):
    obj, _ = _cbor_read(data, 0)
    return obj


# --- SMP framing -------------------------------------------------------------

def build_smp(op: int, group: int, cmd_id: int, payload: dict, seq: int = 0) -> bytes:
    body = cbor_encode(payload)
    # header: op(1) flags(1) len(2 BE) group(2 BE) seq(1) id(1)
    header = struct.pack(">BBHHBB", op, 0, len(body), group, seq, cmd_id)
    return header + body


def encode_serial_frame(smp_pkt: bytes) -> bytes:
    """Wrap an SMP packet into base64 serial-console line(s)."""
    crc = crc16_ccitt(smp_pkt)
    inner = smp_pkt + struct.pack(">H", crc)
    framed = struct.pack(">H", len(inner)) + inner   # 2-byte total-length prefix
    b64 = base64.b64encode(framed).decode("ascii")

    out = b""
    first = True
    for pos in range(0, len(b64), MAX_B64_PER_LINE):
        chunk = b64[pos:pos + MAX_B64_PER_LINE].encode("ascii")
        out += (FRAME_START if first else FRAME_CONT) + chunk + FRAME_END
        first = False
    return out


def read_serial_response(ser: serial.Serial) -> bytes:
    """Read one full SMP packet back, reassembling fragments."""
    b64 = b""
    started = False
    while True:
        line = ser.readline()          # relies on serial timeout
        if not line:
            raise TimeoutError("timed out waiting for device response")
        line = line.rstrip(b"\r\n")
        if line.startswith(FRAME_START):
            b64 = line[2:]
            started = True
        elif line.startswith(FRAME_CONT):
            if not started:
                continue
            b64 += line[2:]
        else:
            # console noise / log output before the frame -- ignore.
            continue

        # Try to decode what we have; a complete packet has a valid length+CRC.
        try:
            framed = base64.b64decode(b64)
        except Exception:
            continue
        if len(framed) < 2:
            continue
        total = struct.unpack_from(">H", framed, 0)[0]
        if len(framed) - 2 < total:
            continue                    # more fragments to come
        inner = framed[2:2 + total]
        smp_pkt, crc = inner[:-2], struct.unpack(">H", inner[-2:])[0]
        if crc16_ccitt(smp_pkt) != crc:
            raise ValueError("CRC mismatch in device response")
        return smp_pkt


def transact(ser: serial.Serial, op: int, cmd_id: int, payload: dict) -> dict:
    ser.reset_input_buffer()
    ser.write(encode_serial_frame(build_smp(op, EEPROM_GROUP_ID, cmd_id, payload)))
    ser.flush()
    resp = read_serial_response(ser)
    # resp = 8-byte header + CBOR body
    return cbor_decode(resp[8:])


class EepromError(Exception):
    """A device request returned a non-zero SMP rc."""

    def __init__(self, rc: int):
        super().__init__(f"device returned rc={rc}")
        self.rc = rc


def eeprom_read(ser: serial.Serial, slot: int, addr: int, n: int) -> bytes:
    """Read n bytes, splitting into EEPROM_XFER_MAX windows."""
    out = bytearray()
    off = 0
    while off < n:
        chunk = min(EEPROM_XFER_MAX, n - off)
        resp = transact(ser, MGMT_OP_READ, EEPROM_ID_READ,
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
        resp = transact(ser, MGMT_OP_WRITE, EEPROM_ID_WRITE,
                        {"slot": slot, "addr": addr + off, "values": list(chunk)})
        rc = resp.get("rc", 0)
        if rc != 0:
            raise EepromError(rc)
        off += len(chunk)


# --- output ------------------------------------------------------------------

def to_jsonable(obj):
    """Render CBOR result as JSON-friendly types (byte strings -> hex)."""
    if isinstance(obj, (bytes, bytearray)):
        return obj.hex()
    if isinstance(obj, dict):
        return {k: to_jsonable(v) for k, v in obj.items()}
    if isinstance(obj, list):
        return [to_jsonable(v) for v in obj]
    return obj


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
