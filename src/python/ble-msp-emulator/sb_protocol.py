"""SpeedyBee BLE protocol, device side, plus MSP stream framing.

This module is the executable spec for the future C++ SpeedyBeeHandshake:
it implements the peripheral (FC BLE module) side of the proprietary
handshake the SpeedyBee app runs on characteristics ABF3/ABF4 before it
opens the serial channel on ABF1/ABF2.

Wire formats (from HCI snoop analysis via dunaevai135/speedybee_ble_bridge):
  app -> device (ABF3):  [cmd] [0x00] [protobuf-lite payload]
  device -> app (ABF4):  [cmd] [len_hi] [len_lo] [protobuf-lite payload]
Protobuf-lite fields: field1 varint (tag 0x08), field2 varint (tag 0x10)
or string (tag 0x12), field3 bytes (tag 0x1a).

Observed exchanges (no-password board, F405 V4):
  init:        app 02 00 08 03            -> dev 03 00 02 08 03
  device info: app 0e 00 08 0d 12 0a <sn> -> dev f6 00 <len> <info blob>
  session key: app 02 00 08 2d            -> dev 26 00 <len> <key blob>
Password boards answer init with 07 00 06 08 03 10 02 1a 00 instead; we
emulate the no-password flow by default.
"""

from __future__ import annotations

import logging
import os
import time
from dataclasses import dataclass, field

log = logging.getLogger("sb")


def uuid16(short: int) -> str:
    return f"0000{short:04x}-0000-1000-8000-00805f9b34fb"


# The genuine SpeedyBee module is Espressif's ble_spp_server ESP-IDF example
# (examples/bluetooth/bluedroid/ble/ble_spp_server) -- these UUIDs and the
# GATT layout below are verbatim from it, including the optional heartbeat.
SVC_SPEEDYBEE = uuid16(0xABF0)
CHR_SERIAL_TX = uuid16(0xABF1)   # SPP_DATA_RECEIVE:    READ + WRITE_NR, 512B
CHR_SERIAL_RX = uuid16(0xABF2)   # SPP_DATA_NOTIFY:     READ + NOTIFY + CCCD, 512B
CHR_SB_TX = uuid16(0xABF3)       # SPP_COMMAND_RECEIVE: READ + WRITE_NR, 20B
CHR_SB_RX = uuid16(0xABF4)       # SPP_COMMAND_NOTIFY:  READ + NOTIFY + CCCD, 20B
CHR_HEARTBEAT = uuid16(0xABF5)   # SPP_HEARTBEAT: only when SUPPORT_HEARTBEAT
HEARTBEAT_VALUE = b"Espressif"   # what the stock example notifies

# CAPTURED FROM A GENUINE SpeedyBee FC (nRF Connect, 2026-07-27):
#   advertised service UUID:  0x00FF   <-- NOT 0xABF0
#   GATT services:            0x1800, 0x1801, 0xABF0 (primary)
#   0xABF0 characteristics:   ABF1..ABF4 only -- no ABF5 heartbeat
#   no Device Information Service (0x180A)
# The app's scan filter keys on the ADVERTISED uuid, so 0x00FF is what makes
# a device visible as a flight controller; ABF0 is found after connecting.
SVC_ADVERTISED = uuid16(0x00FF)

# Raw advertisement the stock example emits (no scan response is configured):
#   02 01 06              flags: LE General Discoverable, BR/EDR not supported
#   03 03 F0 AB           COMPLETE list of 16-bit service UUIDs = 0xABF0
#   <len> 09 <name>       COMPLETE local name
# SpeedyBee substitutes the product name; lida2003/BleSppUart proves the app
# accepts an arbitrary name, so the service-UUID list is the load-bearing part.
ADV_FLAGS = bytes([0x02, 0x01, 0x06])
# Genuine SpeedyBee FC advertises 0x00FF (captured); the stock ESP-IDF example
# ships 0xABF0 here (03 03 F0 AB) -- SpeedyBee changed it, and the app follows.
ADV_SERVICE_UUID_LIST = bytes([0x03, 0x03, 0xFF, 0x00])


def build_adv_payload(name: str) -> bytes:
    """The 23-byte raw ADV payload for the stock 14-char name."""
    name_bytes = name.encode("ascii")[:29]
    return (ADV_FLAGS + ADV_SERVICE_UUID_LIST +
            bytes([len(name_bytes) + 1, 0x09]) + name_bytes)


# Long notify payloads are fragmented by the stock example as
#   '#' '#' <total_chunks> <chunk_index> <MTU-7 bytes>
# with ~20ms between chunks; the app implements the reassembly.
FRAG_HEADER = b"##"
FRAG_INTERVAL_S = 0.020


def fragment_notify(data: bytes, mtu: int) -> list[bytes]:
    """Split per the ble_spp_server convention. Short data goes unwrapped."""
    if len(data) <= mtu - 3:
        return [data]
    chunk = mtu - 7
    total = (len(data) + chunk - 1) // chunk
    return [FRAG_HEADER + bytes([total, i + 1]) + data[i * chunk:(i + 1) * chunk]
            for i in range(total)]


# ---------------------------------------------------------------- protobuf-lite

def encode_varint(value: int) -> bytes:
    out = bytearray()
    while value > 0x7F:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.append(value & 0x7F)
    return bytes(out)


def decode_varint(data: bytes, offset: int) -> tuple[int, int]:
    result = 0
    shift = 0
    while offset < len(data):
        b = data[offset]
        result |= (b & 0x7F) << shift
        offset += 1
        if not (b & 0x80):
            break
        shift += 7
    return result, offset


def encode_fields(*fields: tuple[int, str, int | bytes]) -> bytes:
    """encode_fields((1, 'varint', 3), (3, 'bytes', b'...')) -> payload."""
    out = bytearray()
    for num, kind, value in fields:
        if kind == "varint":
            out.append((num << 3) | 0)
            out.extend(encode_varint(value))
        elif kind == "bytes":
            out.append((num << 3) | 2)
            out.extend(encode_varint(len(value)))
            out.extend(value)
        else:
            raise ValueError(kind)
    return bytes(out)


def decode_fields(data: bytes) -> dict[int, int | bytes]:
    fields: dict[int, int | bytes] = {}
    offset = 0
    while offset < len(data):
        tag = data[offset]
        offset += 1
        num, wire = tag >> 3, tag & 0x07
        if wire == 0:
            fields[num], offset = decode_varint(data, offset)
        elif wire == 2:
            length, offset = decode_varint(data, offset)
            fields[num] = data[offset:offset + length]
            offset += length
        else:
            break
    return fields


def sb_response(cmd: int, payload: bytes) -> bytes:
    """Device->app packet: [cmd][len16 big-endian][payload]."""
    return bytes([cmd, (len(payload) >> 8) & 0xFF, len(payload) & 0xFF]) + payload


# ------------------------------------------------------------------- handshake

@dataclass
class DeviceInfo:
    # Defaults are a stand-in for bench work, not the shipped identity: the
    # firmware takes its name from the image's baked product_name/device_name
    # (src/lib/BLEMSP/DESIGN.md 2.0), which has no meaning host-side.
    product: str = "SpeedyBee F405 V4"
    name: str = "ELRS TX MSP"
    firmware: str = "1.0.0"
    serial: str = "ELRSTX0001"
    # Pad the info blob so its field3 length needs a 2-byte varint: the
    # reference client hard-skips a 3-byte proto header (cmd+len16+3),
    # which only lines up when len(blob) >= 128.
    pad_to: int = 128


class HandshakeResponder:
    """Table-driven ABF3 -> ABF4 responder, no-password device flavor.

    handle_write() returns the list of ABF4 notification payloads to send.
    Every branch is a tiny builder so bench iteration against the real app
    is a one-line change; this structure ports 1:1 to C++.
    """

    CMD_INIT_OR_KEY = 0x02
    CMD_PASSWORD = 0x08
    CMD_DEVICE_INFO = 0x0E

    INIT_ARG = 3
    SESSION_KEY_ARG = 0x2D

    def __init__(self, info: DeviceInfo | None = None, password: str | None = None):
        self.info = info or DeviceInfo()
        self.password = password  # None = never demand a password (default)
        self.session_ready = False
        self.authed = password is None

    def reset(self) -> None:
        self.session_ready = False
        self.authed = self.password is None

    def handle_write(self, data: bytes) -> list[bytes]:
        if len(data) < 2:
            log.warning("ABF3 runt packet: %s", data.hex())
            return []
        cmd = data[0]
        fields = decode_fields(data[2:])
        log.info("ABF3 <- cmd=0x%02x fields=%s raw=%s", cmd, _fmt(fields), data.hex())

        if cmd == self.CMD_INIT_OR_KEY:
            arg = fields.get(1)
            if arg == self.INIT_ARG:
                return [self._init_response()]
            if arg == self.SESSION_KEY_ARG:
                return [self._session_key_response()]
            log.warning("cmd 0x02 with unknown arg %s", arg)
            return [self._init_response()]  # best-effort ack

        if cmd == self.CMD_PASSWORD:
            return [self._password_response(fields.get(2, b""))]

        if cmd == self.CMD_DEVICE_INFO:
            return [self._device_info_response()]

        log.warning("Unknown ABF3 cmd 0x%02x -- no response", cmd)
        return []

    # Builders -- observed bytes noted where known.

    def _init_response(self) -> bytes:
        if self.password and not self.authed:
            # 07 00 06 08 03 10 02 1a 00 (password required)
            return sb_response(0x07, encode_fields(
                (1, "varint", 3), (2, "varint", 2), (3, "bytes", b"")))
        # Observed: 03 00 02 08 03
        return sb_response(0x03, encode_fields((1, "varint", 3)))

    def _password_response(self, submitted: bytes) -> bytes:
        ok = self.password is None or submitted.decode("ascii", "replace") == self.password
        self.authed = ok
        if ok:
            # Observed: 05 00 04 08 04 1a 00
            return sb_response(0x05, encode_fields((1, "varint", 4), (3, "bytes", b"")))
        return sb_response(0x07, encode_fields(
            (1, "varint", 4), (2, "varint", 2), (3, "bytes", b"")))

    def _device_info_response(self) -> bytes:
        # Response cmd is 0xf6. Blob layout is not fully known; we ship
        # null-terminated strings in field3, padded so the proto header is
        # 3 bytes (see DeviceInfo.pad_to). Adjust freely on the bench.
        blob = b"\x00".join(s.encode("ascii") for s in (
            self.info.product, self.info.name, self.info.firmware, self.info.serial))
        blob += b"\x00" * max(0, self.info.pad_to - len(blob))
        return sb_response(0xF6, encode_fields((1, "varint", 13), (3, "bytes", blob)))

    def _session_key_response(self) -> bytes:
        # Response cmd is 0x26 with key data; 16 random bytes in field3.
        self.session_ready = True
        return sb_response(0x26, encode_fields(
            (1, "varint", self.SESSION_KEY_ARG), (3, "bytes", os.urandom(16))))


def _fmt(fields: dict) -> str:
    return "{" + ", ".join(
        f"{k}: {v.hex() if isinstance(v, bytes) else v}" for k, v in fields.items()) + "}"


# ---------------------------------------------------------------- MSP framing

def build_msp_v1(direction: str, cmd: int, payload: bytes = b"") -> bytes:
    if len(payload) > 254:  # jumbo: len byte 0xFF, real u16 length leads payload
        body = bytes([0xFF, cmd, len(payload) & 0xFF, len(payload) >> 8]) + payload
    else:
        body = bytes([len(payload), cmd]) + payload
    csum = 0
    for b in body:
        csum ^= b
    return bytes([0x24, 0x4D, ord(direction)]) + body + bytes([csum])


def build_msp_v2(direction: str, cmd: int, payload: bytes = b"") -> bytes:
    body = bytes([0, cmd & 0xFF, cmd >> 8, len(payload) & 0xFF, len(payload) >> 8])
    body += payload
    crc = 0
    for b in body:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0xD5 if crc & 0x80 else crc << 1) & 0xFF
    return bytes([0x24, 0x58, ord(direction)]) + body + bytes([crc])


MSP_DIR_REQUEST = ord("<")
MSP_DIR_RESPONSE = ord(">")
MSP_DIR_ERROR = ord("!")


@dataclass
class MspFrame:
    raw: bytes
    version: int          # 1 or 2
    direction: int        # <, >, !
    cmd: int
    payload_len: int
    t: float = field(default_factory=time.monotonic)

    @property
    def is_request(self) -> bool:
        return self.direction == MSP_DIR_REQUEST


class MspStreamParser:
    """Splits a byte stream into MSP v1/v2 frames (incl. v1 jumbo).

    Non-MSP bytes (e.g. raw CLI traffic after '#') are emitted as ('raw', bytes)
    events -- mirroring what the firmware bridge's msp2crsf would reject.
    Events: ('msp', MspFrame) | ('raw', bytes).
    """

    def __init__(self, name: str = ""):
        self.name = name
        self.buf = bytearray()

    def feed(self, data: bytes) -> list[tuple[str, object]]:
        self.buf.extend(data)
        events: list[tuple[str, object]] = []
        while self.buf:
            start = self.buf.find(b"$")
            if start == -1:
                events.append(("raw", bytes(self.buf)))
                self.buf.clear()
                break
            if start > 0:
                events.append(("raw", bytes(self.buf[:start])))
                del self.buf[:start]
            frame = self._try_parse()
            if frame is None:      # incomplete -- wait for more bytes
                break
            if frame is False:     # not actually an MSP header; skip the '$'
                events.append(("raw", bytes(self.buf[:1])))
                del self.buf[:1]
                continue
            events.append(("msp", frame))
        return events

    def _try_parse(self):
        buf = self.buf
        if len(buf) < 3:
            return None
        kind, direction = buf[1], buf[2]
        if kind not in (ord("M"), ord("X")) or direction not in (
                MSP_DIR_REQUEST, MSP_DIR_RESPONSE, MSP_DIR_ERROR):
            return False
        if kind == ord("M"):
            if len(buf) < 5:
                return None
            plen, cmd = buf[3], buf[4]
            if plen == 0xFF:  # jumbo: real u16 length leads the payload
                if len(buf) < 7:
                    return None
                plen = buf[5] | (buf[6] << 8)
                total = 5 + 2 + plen + 1
            else:
                total = 5 + plen + 1
            if len(buf) < total:
                return None
            frame = MspFrame(bytes(buf[:total]), 1, direction, cmd, plen)
        else:
            if len(buf) < 8:
                return None
            cmd = buf[4] | (buf[5] << 8)
            plen = buf[6] | (buf[7] << 8)
            total = 8 + plen + 1
            if len(buf) < total:
                return None
            frame = MspFrame(bytes(buf[:total]), 2, direction, cmd, plen)
        del self.buf[:total]
        return frame
