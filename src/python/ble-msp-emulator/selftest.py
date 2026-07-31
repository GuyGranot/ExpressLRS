"""Self-test for the Phase 0 emulator: no BLE, no hardware, no sleeps > ~12s.

  1. Handshake responder vs the byte sequences the reference client
     (../external/speedybee_ble_bridge, from HCI snoops) sends/expects.
  2. MSP stream parser: v1, v1-jumbo, v2, fragmentation, raw CLI bytes.
  3. ShapedPipe schedule math (no sleeping -- inspects queued due-times).
  4. End-to-end async session: scripted "app" runs a config-read against a
     fake FC through the LinkSimulator at 625 B/s with 2% injected drops
     and one retry per request -- the go/no-go traffic model.

Run:  .venv\\Scripts\\python selftest.py
"""

from __future__ import annotations

import asyncio
import logging
import sys
import time

import sb_protocol as sbp
from link_sim import LinkConfig, LinkSimulator, ShapedPipe

PASS = 0
FAIL = 0


def check(name: str, cond: bool, detail: str = ""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  ok    {name}")
    else:
        FAIL += 1
        print(f"  FAIL  {name}  {detail}")


# ------------------------------------------------------------------ handshake

def test_handshake():
    print("[1] handshake responder")
    hs = sbp.HandshakeResponder()

    # init: app writes 02 00 08 03, expects 03 00 02 08 03
    resp = hs.handle_write(bytes.fromhex("02000803"))
    check("init ack bytes", resp == [bytes.fromhex("0300020803")],
          f"got {[r.hex() for r in resp]}")

    # device info: app writes 0e 00 08 0d 12 0a <10-char serial>
    pkt = bytes([0x0E, 0x00]) + sbp.encode_fields(
        (1, "varint", 13), (2, "bytes", b"abcdef0123"))
    resp = hs.handle_write(pkt)
    check("device info cmd", len(resp) == 1 and resp[0][0] == 0xF6)
    if resp:
        r = resp[0]
        plen = (r[1] << 8) | r[2]
        check("device info len16 correct", plen == len(r) - 3)
        fields = sbp.decode_fields(r[3:])
        blob = fields.get(3, b"")
        check("device info blob >= 128B (3-byte proto header)", len(blob) >= 128,
              f"len={len(blob)}")
        check("device info strings present", b"ELRS" in blob and b"\x00" in blob)
        # the reference client skips 6 bytes then splits on NUL:
        strings = [s for s in r[6:].split(b"\x00") if len(s) > 2]
        check("client-side parse finds strings", len(strings) >= 2,
              f"{strings[:3]}")

    # session key: app writes 02 00 08 2d, expects cmd 0x26
    resp = hs.handle_write(bytes.fromhex("0200082d"))
    check("session key cmd 0x26", len(resp) == 1 and resp[0][0] == 0x26)
    check("session ready", hs.session_ready)

    # password board flavor
    hs2 = sbp.HandshakeResponder(password="1234")
    resp = hs2.handle_write(bytes.fromhex("02000803"))
    check("pw-required init bytes", resp == [bytes.fromhex("070006080310021a00")],
          f"got {[r.hex() for r in resp]}")
    pw_pkt = bytes([0x08, 0x00]) + sbp.encode_fields(
        (1, "varint", 4), (2, "bytes", b"1234"))
    resp = hs2.handle_write(pw_pkt)
    check("pw accept bytes", resp == [bytes.fromhex("050004 0804 1a00".replace(" ", ""))],
          f"got {[r.hex() for r in resp]}")

    # unknown cmd -> no crash, no response
    check("unknown cmd tolerated", hs.handle_write(b"\x7f\x00\x08\x01") == [])


# ------------------------------------------------------------------ MSP parse

msp_v1 = sbp.build_msp_v1
msp_v2 = sbp.build_msp_v2


def test_msp_parser():
    print("[2] MSP stream parser")
    p = sbp.MspStreamParser()

    f = msp_v1("<", 2)  # API_VERSION request, the reference 'kick': 244d3c000202
    check("v1 kick bytes match reference", f.hex() == "244d3c000202")
    ev = p.feed(f)
    check("v1 request parsed", len(ev) == 1 and ev[0][0] == "msp"
          and ev[0][1].cmd == 2 and ev[0][1].is_request)

    ev = p.feed(msp_v1(">", 101, bytes(20)))
    check("v1 response parsed", len(ev) == 1 and ev[0][1].payload_len == 20)

    big = msp_v1(">", 4, b"")  # build jumbo manually: len byte 0xFF + u16 len
    payload = bytes(300)
    csum = 0xFF ^ 4
    for b in bytes([300 & 0xFF, 300 >> 8]) + payload:
        csum ^= b
    jumbo = bytes([0x24, 0x4D, 0x3E, 0xFF, 4, 300 & 0xFF, 300 >> 8]) + payload + bytes([csum])
    ev = p.feed(jumbo)
    check("v1 jumbo parsed", len(ev) == 1 and ev[0][1].payload_len == 300)

    ev = p.feed(msp_v2("<", 0x2000, b"hi"))
    check("v2 parsed", len(ev) == 1 and ev[0][1].version == 2
          and ev[0][1].cmd == 0x2000)

    # fragmentation: one frame in 1-byte drips
    frame = msp_v1(">", 42, b"xyz")
    got = []
    for i in range(len(frame)):
        got += p.feed(frame[i:i + 1])
    check("fragmented reassembly", len(got) == 1 and got[0][1].cmd == 42)

    # raw CLI traffic
    ev = p.feed(b"#\r\nstatus\r\n")
    check("raw CLI emitted as raw", ev and all(k == "raw" for k, _ in ev))

    # '$' inside raw garbage that is not MSP
    ev = p.feed(b"abc$Qdef" + msp_v1("<", 1))
    kinds = [k for k, _ in ev]
    check("garbage $ skipped, frame still found", kinds.count("msp") == 1)


# ------------------------------------------------------------- pipe schedule

def test_pipe_math():
    print("[3] shaped pipe schedule math")

    async def run():
        sink_times = []

        async def sink(piece):
            sink_times.append((time.monotonic(), len(piece)))

        pipe = ShapedPipe("t", rate_bps=625, latency_s=0.05, sink=sink)
        # do not start dispatcher; inspect the schedule directly
        t0 = time.monotonic()
        pipe.write(bytes(625))  # exactly 1 second of serialization
        dues = []
        while not pipe._queue.empty():
            due, piece = pipe._queue.get_nowait()
            dues.append((due, len(piece)))
        total = sum(n for _, n in dues)
        last_due = dues[-1][0] - t0
        check("all bytes scheduled", total == 625)
        check("625B @625B/s +50ms lat ~= 1.05s", 1.0 <= last_due <= 1.1,
              f"{last_due:.3f}s")
        check("ordering monotonic", all(dues[i][0] <= dues[i + 1][0]
                                        for i in range(len(dues) - 1)))

    asyncio.run(run())


# ------------------------------------------------------- end-to-end session

class FakeFc:
    """Answers MSP v1 requests with canned response sizes (config-read mix)."""

    SIZES = {100: 22, 101: 30, 102: 40, 111: 20, 112: 90, 114: 30,
             116: 300, 117: 250, 4: 60, 2: 3, 3: 6, 10: 24}

    def __init__(self, linksim):
        self.parser = sbp.MspStreamParser("fc")
        self.linksim = linksim

    async def on_bytes(self, data: bytes):
        for kind, item in self.parser.feed(data):
            if kind != "msp" or not item.is_request:
                continue
            size = self.SIZES.get(item.cmd, 16)
            self.linksim.fc_to_app(msp_v1(">", item.cmd, bytes(size)))


def test_end_to_end():
    print("[4] end-to-end 625 B/s, 2% drops, 1 retry, config-read model")

    async def run():
        app_rx = asyncio.Queue()
        fc_holder = {}

        async def to_fc(piece):
            await fc_holder["fc"].on_bytes(piece)

        async def to_app(piece):
            await app_rx.put(piece)

        sim = LinkSimulator(
            LinkConfig(rate_bps=625, latency_s=0.1, drop_down=0.02, seed=7),
            to_fc_sink=to_fc, to_app_sink=to_app)
        fc_holder["fc"] = FakeFc(sim)
        sim.start()

        app_parser = sbp.MspStreamParser("app")
        responses = asyncio.Queue()

        async def app_reader():
            while True:
                data = await app_rx.get()
                for kind, item in app_parser.feed(data):
                    if kind == "msp":
                        await responses.put(item)

        reader = asyncio.get_running_loop().create_task(app_reader())

        # config-read: every canned cmd once, sequential, 1.5s timeout, 1 retry
        cmds = list(FakeFc.SIZES) * 2      # 24 requests, ~2.3KB of responses
        t0 = time.monotonic()
        completed, retries, failures = 0, 0, 0
        for cmd in cmds:
            for attempt in (1, 2):
                sim.app_to_fc(msp_v1("<", cmd))
                try:
                    while True:
                        resp = await asyncio.wait_for(responses.get(), timeout=1.5)
                        if resp.cmd == cmd:
                            break
                    completed += 1
                    break
                except asyncio.TimeoutError:
                    if attempt == 1:
                        retries += 1
                    else:
                        failures += 1
        elapsed = time.monotonic() - t0
        reader.cancel()
        sim.stop()

        print(f"      {completed}/{len(cmds)} ok, {retries} retries, "
              f"{failures} failures in {elapsed:.1f}s")
        print(f"      stats: {sim.stats.summary()}")
        check("all requests completed (with retry)", failures == 0,
              f"{failures} failed")
        check("retry count sane vs 2% drop", retries <= 5, f"{retries}")
        check("elapsed plausible for ~2.7KB @625B/s", 4.0 <= elapsed <= 15.0,
              f"{elapsed:.1f}s")
        check("pipeline depth tracked", sim.stats.max_outstanding >= 1)

    asyncio.run(run())


def main():
    logging.basicConfig(level=logging.ERROR)  # keep injected-drop noise down
    test_handshake()
    test_msp_parser()
    test_pipe_math()
    test_end_to_end()
    print(f"\n{PASS} passed, {FAIL} failed")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
