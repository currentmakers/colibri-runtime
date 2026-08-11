"""Shared SMP (mcumgr) serial-console transport for Colibri desktop tools.

Talks to the custom management groups registered under src/management/ on
the device: CBOR encode/decode, CRC16-CCITT, and the base64 serial-console
framing (0609/0414 markers) that Zephyr's smp_shell/mcumgr serial transport
expects. Callers pick the group id and command id; this module only knows
about the wire format.

Only dependency is pyserial (`pip install pyserial`).
"""

import base64
import struct

import serial  # pyserial

# --- SMP group ids -------------------------------------------------------

EEPROM_MANAGEMENT_GROUP_ID = 64   # src/management/eeprom.c
IO_TEST_MANAGEMENT_GROUP_ID = 65  # src/management/io-test.c

MGMT_OP_READ = 0
MGMT_OP_WRITE = 2

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


def transact(ser: serial.Serial, op: int, group: int, cmd_id: int, payload: dict) -> dict:
    ser.reset_input_buffer()
    ser.write(encode_serial_frame(build_smp(op, group, cmd_id, payload)))
    ser.flush()
    resp = read_serial_response(ser)
    # resp = 8-byte header + CBOR body
    return cbor_decode(resp[8:])


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
