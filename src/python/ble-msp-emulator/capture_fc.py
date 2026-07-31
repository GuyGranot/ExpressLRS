#!/usr/bin/env python3
"""Capture a genuine SpeedyBee FC's handshake responses, byte for byte.

Our firmware's ABF4 replies were reconstructed from a third-party client and
protocol notes, not observed. The device-info response (cmd 0xF6) is the part
the iOS app rejects, and its exact shape was always a guess. This connects to
a real flight controller as a BLE central -- the same role the phone plays --
walks the handshake, and dumps every response in hex so the firmware can
reproduce the real bytes instead of an inference.

Read-only: it sends only handshake commands, exactly what the app sends on
connect. It writes no configuration and touches no MSP settings.

    python capture_fc.py --scan                    # list nearby devices
    python capture_fc.py --name "SpeedyBee F405 V4"
    python capture_fc.py --addr AA:BB:CC:DD:EE:FF --password 1234

Output goes to fc_handshake.txt (and stdout). Needs bleak.
"""

import argparse
import asyncio
import sys

try:
    from bleak import BleakClient, BleakScanner
except ImportError:
    sys.exit("bleak is missing: pip install bleak")


# Wire primitives come from sb_protocol, this project's canonical
# implementation and the one selftest.py covers -- a third copy of the varint
# encoder in this directory would have no test protection.
from sb_protocol import (  # noqa: E402
    CHR_SERIAL_RX,
    CHR_SB_RX as CHR_CMD_RX,
    CHR_SB_TX as CHR_CMD_TX,
    encode_fields,
)


def sb_packet(cmd: int, field1=None, field2=None) -> bytes:
    """App -> device framing: [cmd][0x00][proto].

    Two bytes, unlike the [cmd][varint len][proto] the device uses for its
    replies -- the protocol is asymmetric, confirmed against a real board in
    fc_handshake.txt.
    """
    fields = []
    if field1 is not None:
        fields.append((1, "varint", field1))
    if field2 is not None:
        fields.append((2, "bytes", field2))
    return bytes([cmd, 0x00]) + encode_fields(*fields)


out_lines = []


def emit(text=""):
    print(text)
    out_lines.append(text)


def hexdump(data: bytes, indent="    "):
    for off in range(0, len(data), 16):
        chunk = data[off:off + 16]
        hexpart = " ".join(f"{b:02x}" for b in chunk).ljust(47)
        asciipart = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in chunk)
        emit(f"{indent}{off:04x}  {hexpart}  |{asciipart}|")


async def do_scan():
    emit("Scanning 8s ...")
    devices = await BleakScanner.discover(timeout=8.0, return_adv=True)
    for addr, (dev, adv) in sorted(devices.items()):
        svcs = ",".join(u[4:8] for u in adv.service_uuids) or "-"
        name = adv.local_name or dev.name or "(no name)"
        mfg = ""
        for cid, payload in (adv.manufacturer_data or {}).items():
            mfg = f" mfg=<{cid:04X}> {payload.hex()}"
        emit(f"  {addr}  {name:28} services=[{svcs}]{mfg}")


async def capture(address: str, password: str | None):
    emit(f"Connecting to {address} ...")
    async with BleakClient(address) as client:
        emit(f"Connected. MTU={getattr(client, 'mtu_size', '?')}")
        emit()
        emit("Services:")
        for svc in client.services:
            emit(f"  {svc.uuid}")
            for ch in svc.characteristics:
                emit(f"      {ch.uuid}  {','.join(ch.properties)}")
        emit()

        replies: asyncio.Queue = asyncio.Queue()

        def on_cmd(_h, data: bytearray):
            replies.put_nowait(bytes(data))

        def on_serial(_h, data: bytearray):
            emit("  [ABF2 serial notify]")
            hexdump(bytes(data))

        await client.start_notify(CHR_CMD_RX, on_cmd)
        await client.start_notify(CHR_SERIAL_RX, on_serial)

        async def step(label, pkt):
            emit(f"=== {label}")
            emit("  --> ABF3 write")
            hexdump(pkt)
            await client.write_gatt_char(CHR_CMD_TX, pkt, response=False)
            try:
                resp = await asyncio.wait_for(replies.get(), timeout=6.0)
            except asyncio.TimeoutError:
                emit("  <-- (no response within 6s)")
                return None
            emit(f"  <-- ABF4 notify, {len(resp)} bytes, cmd=0x{resp[0]:02x}")
            hexdump(resp)
            emit()
            return resp

        await step("step 1: init  cmd=0x02 field1=3", sb_packet(0x02, field1=3))

        if password:
            await step(f"step 2: password cmd=0x08 field1=4 '{password}'",
                       sb_packet(0x08, field1=4, field2=password.encode()))

        # The app sends a random 10-char hex serial here; fixed value so the
        # capture is reproducible and diffable across runs.
        await step("step 3: device info  cmd=0x0E field1=13 field2=serial",
                   sb_packet(0x0E, field1=13, field2=b"0123456789"))

        await step("step 4: session key  cmd=0x02 field1=0x2D",
                   sb_packet(0x02, field1=0x2D))

        emit("Listening 3s for anything further ...")
        await asyncio.sleep(3.0)
        await client.stop_notify(CHR_CMD_RX)
        await client.stop_notify(CHR_SERIAL_RX)
        emit("Done.")


async def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scan", action="store_true")
    ap.add_argument("--name", help="device local name to match")
    ap.add_argument("--addr", help="device address")
    ap.add_argument("--password", help="BLE password if the board requires one")
    args = ap.parse_args()

    try:
        if args.scan or (not args.name and not args.addr):
            await do_scan()
            if not args.name and not args.addr:
                emit()
                emit("Re-run with --name or --addr to capture.")
            return

        address = args.addr
        if address is None:
            emit(f"Looking for {args.name!r} ...")
            dev = await BleakScanner.find_device_by_name(args.name, timeout=15.0)
            if dev is None:
                emit("Not found. Is the FC powered and not already connected to the phone?")
                return
            address = dev.address
            emit(f"Found at {address}")
        await capture(address, args.password)
    finally:
        with open("fc_handshake.txt", "w", encoding="utf-8") as fh:
            fh.write("\n".join(out_lines) + "\n")
        print("\n(written to fc_handshake.txt)")


if __name__ == "__main__":
    asyncio.run(main())
