"""High-level SMP operations for the calibration station.

Wraps colibri_smp.transact() with the specific request/response shapes the
firmware groups use: EEPROM read/write (group 64, src/management/eeprom.c)
and publish/wait_event/rescan_slot (group 65, src/management/io-test.c).
"""

import struct

import serial

from colibri_smp import (
    EEPROM_MANAGEMENT_GROUP_ID,
    IO_TEST_MANAGEMENT_GROUP_ID,
    MGMT_OP_READ,
    MGMT_OP_WRITE,
    transact,
)

# Firmware caps a single EEPROM request at this many bytes (a whole SMP
# frame must fit the UART MTU), so larger transfers are split into windows.
EEPROM_XFER_MAX = 128

# Offsets into eeprom_layout_t (include/colibri-sdk/colibri-io-eeprom.h).
OFF_SERIAL_NUMBER = 0x0004
CALIBRATION_DATA_OFFSET = 0x0080
CALIBRATION_DATA_SIZE = 128
TEST_REPORTS_OFFSET = 0x0100
TEST_REPORTS_SIZE = 256

EEPROM_ID_READ = 0    # eeprom_handlers[0].mh_read
EEPROM_ID_WRITE = 1   # eeprom_handlers[1].mh_write

IO_TEST_ID_PUBLISH = 0     # io_test_handlers[0].mh_write
IO_TEST_ID_WAIT_EVENT = 1  # io_test_handlers[1].mh_read
IO_TEST_ID_RESCAN = 2      # io_test_handlers[2].mh_write


class FirmwareError(Exception):
    """A device request returned a non-zero SMP rc."""

    def __init__(self, rc: int, context: str = ""):
        suffix = f" ({context})" if context else ""
        super().__init__(f"device returned rc={rc}{suffix}")
        self.rc = rc


class Firmware:
    def __init__(self, port: str, speed: int = 115200, timeout: float = 5.0):
        self.ser = serial.Serial(port, speed, timeout=timeout)

    def close(self) -> None:
        self.ser.close()

    # --- EEPROM (group 64) ---------------------------------------------------

    def eeprom_read(self, slot: int, addr: int, n: int) -> bytes:
        out = bytearray()
        off = 0
        while off < n:
            chunk = min(EEPROM_XFER_MAX, n - off)
            resp = transact(self.ser, MGMT_OP_READ, EEPROM_MANAGEMENT_GROUP_ID, EEPROM_ID_READ,
                             {"slot": slot, "addr": addr + off, "n": chunk})
            rc = resp.get("rc", 0)
            if rc != 0:
                raise FirmwareError(rc, f"eeprom_read slot={slot} addr={addr + off}")
            out += resp.get("data", b"")
            off += chunk
        return bytes(out)

    def eeprom_write(self, slot: int, addr: int, data: bytes) -> None:
        off = 0
        while off < len(data):
            chunk = data[off:off + EEPROM_XFER_MAX]
            resp = transact(self.ser, MGMT_OP_WRITE, EEPROM_MANAGEMENT_GROUP_ID, EEPROM_ID_WRITE,
                             {"slot": slot, "addr": addr + off, "values": list(chunk)})
            rc = resp.get("rc", 0)
            if rc != 0:
                raise FirmwareError(rc, f"eeprom_write slot={slot} addr={addr + off}")
            off += len(chunk)

    def read_serial_number(self, slot: int) -> int:
        return struct.unpack("<I", self.eeprom_read(slot, OFF_SERIAL_NUMBER, 4))[0]

    def write_calibration_data(self, slot: int, data: bytes) -> None:
        if len(data) > CALIBRATION_DATA_SIZE:
            raise ValueError(f"calibration_data is {len(data)} bytes, max {CALIBRATION_DATA_SIZE}")
        self.eeprom_write(slot, CALIBRATION_DATA_OFFSET, data.ljust(CALIBRATION_DATA_SIZE, b"\x00"))

    def write_test_report(self, slot: int, data: bytes) -> None:
        if len(data) > TEST_REPORTS_SIZE:
            raise ValueError(f"test report is {len(data)} bytes, max {TEST_REPORTS_SIZE}")
        self.eeprom_write(slot, TEST_REPORTS_OFFSET, data.ljust(TEST_REPORTS_SIZE, b"\x00"))

    # --- I/O event bus + rescan (group 65) ------------------------------------

    def publish(self, slot: int, event_type: int, param: int, value: int) -> None:
        resp = transact(self.ser, MGMT_OP_WRITE, IO_TEST_MANAGEMENT_GROUP_ID, IO_TEST_ID_PUBLISH,
                         {"slot": slot, "type": event_type, "param": param, "value": value})
        rc = resp.get("rc", 0)
        if rc != 0:
            raise FirmwareError(rc, f"publish slot={slot} type={event_type} param={param}")

    def wait_event(self, slot: int, event_type: int, param: int) -> int:
        """Block device-side (up to 1s, see io-test.c) for one matching event.

        Not needed for io_aqv (which just sleeps and reads the DMM), but
        available for modules that need to read a value the driver itself
        publishes, e.g. an analog-input module's MEASURED_VALUE.
        """
        resp = transact(self.ser, MGMT_OP_READ, IO_TEST_MANAGEMENT_GROUP_ID, IO_TEST_ID_WAIT_EVENT,
                         {"slot": slot, "type": event_type, "param": param})
        rc = resp.get("rc", 0)
        if rc != 0:
            raise FirmwareError(rc, f"wait_event slot={slot} type={event_type} param={param}")
        return resp["value"]

    def rescan_slot(self, slot: int) -> None:
        """Re-enumerate + reload the driver for one slot (src/slots/slots.c
        slots_reinit_one()) -- call after inserting a module or writing new
        calibration_data, since the driver only reads calibration_data once,
        at load."""
        resp = transact(self.ser, MGMT_OP_WRITE, IO_TEST_MANAGEMENT_GROUP_ID, IO_TEST_ID_RESCAN,
                         {"slot": slot})
        rc = resp.get("rc", 0)
        if rc != 0:
            raise FirmwareError(rc, f"rescan_slot slot={slot}")
