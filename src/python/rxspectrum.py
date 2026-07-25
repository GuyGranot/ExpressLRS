#!/usr/bin/env python3
"""
Host-side driver + viewer for the ELRS RX spectrum analyzer (RX_SPECTRUM_SCAN).

Runs against a receiver flashed with -DRX_SPECTRUM_SCAN, reached over the flight
controller's Betaflight serial passthrough (so the host talks straight to the
RX's CRSF UART, default 420000 baud).

It sends a CRSF command frame to trigger a scan, then decodes the streamed
CRSF_FRAMETYPE_ELRS_VENDOR (0x30) frames -- the exact same wire format as the
TX-side analyzer (see src/lib/TxSpectrum/TxSpectrumProtocol.h) -- and shows a
live + max-hold plot. Bands are demultiplexed by each frame's own frequency
axis, so a "both" scan renders 900 MHz and 2.4 GHz on separate panels.

Exit the scan by resetting the receiver (power cycle) -- there is no stop command.

Usage:
    python rxspectrum.py --port COM7 --band both
    python rxspectrum.py --port /dev/ttyACM0 --baud 420000 --band 2g4
    python rxspectrum.py --selftest        # codec round-trip, no hardware

Requires pyserial for live capture (`pip install pyserial`) and, optionally,
matplotlib for the plot (`pip install matplotlib`); without matplotlib it prints
a text peak readout instead.
"""

import argparse
import sys
import time

# --- CRSF / ELRS_VENDOR wire constants (mirror src/include/crsf_protocol.h) ---
CRSF_SYNC_BYTE = 0xC8
CRSF_ADDRESS_FLIGHT_CONTROLLER = 0xC8
CRSF_ADDRESS_CRSF_RECEIVER = 0xEC
CRSF_FRAMETYPE_COMMAND = 0x32
CRSF_FRAMETYPE_ELRS_VENDOR = 0x30
CRSF_CRC_POLY = 0xD5  # CRC-8/DVB-S2
CRSF_MAX_FRAME_LEN = 64

# --- ELRS spectrum sub-protocol (mirror TxSpectrumProtocol.h) ---
SUBTYPE = 0x01
PROTO_VERSION = 2
HEADER_BYTES = 13
MAX_BINS_PER_FRAME = 40
RSSI_INVALID = -128
FLAG_TRACE_MASK = 0x03
FLAG_SWEEP_END = 0x04
TRACE_LIVE = 0
TRACE_MAXHOLD = 1

# --- trigger command (mirror RXEndpoint::handleRaw 's','p',band) ---
PORT_900 = 0
PORT_2G4 = 1
PORT_BOTH = 2
BAND_NAMES = {"900": PORT_900, "2g4": PORT_2G4, "both": PORT_BOTH,
              "0": PORT_900, "1": PORT_2G4, "2": PORT_BOTH}


def crc8(data, poly=CRSF_CRC_POLY):
    """CRC-8/DVB-S2 over `data`, matching ELRS GENERIC_CRC8(CRSF_CRC_POLY)."""
    crc = 0
    for b in data:
        crc ^= b & 0xFF
        for _ in range(8):
            crc = ((crc << 1) ^ poly) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc


def build_command_frame(dest, payload):
    """Build a CRSF COMMAND frame: [dest][len][type][payload...][crc]."""
    body = bytes([CRSF_FRAMETYPE_COMMAND]) + bytes(payload)
    frame_len = len(body) + 1  # + crc
    header = bytes([dest, frame_len])
    return header + body + bytes([crc8(body)])


def build_trigger(band):
    """Trigger frame the RX's handleRaw expects: 's','p',band to the receiver."""
    return build_command_frame(CRSF_ADDRESS_CRSF_RECEIVER, b"sp" + bytes([band]))


def encode_spectrum_payload(info, bins):
    """Encode one ELRS_VENDOR spectrum payload (port of TxSpectrumEncodeFrame).

    Provided mainly so --selftest can round-trip against the decoder; the
    firmware is the authoritative encoder. `info` is a dict with keys flags,
    sweepSeq, binOffset, totalBins, startFreqKhz, stepKhz.
    """
    total = info["totalBins"]
    off = info["binOffset"]
    if total == 0 or total > 80 or off >= total:
        return None
    count = min(total - off, MAX_BINS_PER_FRAME)
    flags = info["flags"] & ~FLAG_SWEEP_END
    if off + count >= total:
        flags |= FLAG_SWEEP_END
    p = bytearray(HEADER_BYTES + count)
    p[0] = SUBTYPE
    p[1] = PROTO_VERSION
    p[2] = flags
    p[3] = info["sweepSeq"] & 0xFF
    p[4] = off
    p[5] = count
    p[6] = total
    sf = info["startFreqKhz"] & 0xFFFFFFFF
    p[7] = (sf >> 24) & 0xFF
    p[8] = (sf >> 16) & 0xFF
    p[9] = (sf >> 8) & 0xFF
    p[10] = sf & 0xFF
    st = info["stepKhz"] & 0xFFFF
    p[11] = (st >> 8) & 0xFF
    p[12] = st & 0xFF
    for i in range(count):
        p[HEADER_BYTES + i] = bins[off + i] & 0xFF
    return bytes(p)


def decode_spectrum_payload(payload):
    """Decode an ELRS_VENDOR payload (port of TxSpectrumDecodeFrame).

    Returns a dict, or None if the payload is not a well-formed spectrum frame
    (a foreign sub-type is a routine occurrence, not an error).
    """
    if len(payload) < HEADER_BYTES:
        return None
    if payload[0] != SUBTYPE or payload[1] != PROTO_VERSION:
        return None
    bin_offset = payload[4]
    count = payload[5]
    total = payload[6]
    if count == 0 or count > MAX_BINS_PER_FRAME:
        return None
    if total == 0 or total > 80:
        return None
    if bin_offset + count > total:
        return None
    if len(payload) != HEADER_BYTES + count:
        return None
    start_khz = (payload[7] << 24) | (payload[8] << 16) | (payload[9] << 8) | payload[10]
    step_khz = (payload[11] << 8) | payload[12]

    def to_int8(v):
        return v - 256 if v >= 128 else v

    bins = [to_int8(payload[HEADER_BYTES + i]) for i in range(count)]
    return {
        "flags": payload[2],
        "trace": payload[2] & FLAG_TRACE_MASK,
        "sweep_end": bool(payload[2] & FLAG_SWEEP_END),
        "sweepSeq": payload[3],
        "binOffset": bin_offset,
        "binCount": count,
        "totalBins": total,
        "startFreqKhz": start_khz,
        "stepKhz": step_khz,
        "bins": bins,
    }


class CRSFReader:
    """Reassembles CRSF frames from a byte stream and yields validated ones."""

    def __init__(self):
        self.buf = bytearray()

    def feed(self, data):
        self.buf.extend(data)
        while True:
            # Resync to a plausible sync byte.
            while self.buf and self.buf[0] not in (CRSF_SYNC_BYTE, CRSF_ADDRESS_CRSF_RECEIVER):
                del self.buf[0]
            if len(self.buf) < 2:
                return
            length = self.buf[1]  # bytes after len: type..payload..crc
            if length < 2 or length > CRSF_MAX_FRAME_LEN:
                del self.buf[0]
                continue
            total = 2 + length
            if len(self.buf) < total:
                return
            frame = bytes(self.buf[:total])
            del self.buf[:total]
            # CRC covers type..payload (frame[2 : total-1]); crc is frame[total-1].
            if crc8(frame[2:total - 1]) != frame[total - 1]:
                continue
            frame_type = frame[2]
            yield frame_type, frame[2:total - 1]  # (type, type+payload, no crc)


def extract_vendor_payload(type_and_body):
    """From a type+body slice of an ELRS_VENDOR frame, return the spectrum payload.

    Extended frames carry dest+orig after the type, so the spectrum payload
    (starting at the sub-type byte) begins 3 bytes in: [type][dest][orig][payload].
    """
    if len(type_and_body) < 4 or type_and_body[0] != CRSF_FRAMETYPE_ELRS_VENDOR:
        return None
    return type_and_body[3:]


class TraceStore:
    """Per-band live + max-hold accumulation, keyed by the frame's start freq."""

    def __init__(self):
        self.bands = {}  # band_id -> dict

    @staticmethod
    def _band_id(start_khz):
        return "900MHz" if start_khz < 1_500_000 else "2.4GHz"

    def update(self, dec):
        bid = self._band_id(dec["startFreqKhz"])
        b = self.bands.get(bid)
        n = dec["totalBins"]
        if b is None or b["total"] != n or b["start"] != dec["startFreqKhz"]:
            b = {
                "total": n,
                "start": dec["startFreqKhz"],
                "step": dec["stepKhz"],
                "live": [None] * n,
                "max": [None] * n,
            }
            self.bands[bid] = b
        dst = b["max"] if dec["trace"] == TRACE_MAXHOLD else b["live"]
        off = dec["binOffset"]
        for i, v in enumerate(dec["bins"]):
            if v == RSSI_INVALID:
                continue
            idx = off + i
            if dec["trace"] == TRACE_MAXHOLD:
                dst[idx] = v if dst[idx] is None else max(dst[idx], v)
            else:
                dst[idx] = v
        return bid, b

    def bin_freq_mhz(self, b, idx):
        return (b["start"] + idx * b["step"]) / 1000.0


# ---------------------------------------------------------------------------

def run_selftest():
    """Round-trip the Python codec and check field/endianness fidelity."""
    total = 80
    bins = [(-30 - (i % 50)) for i in range(total)]  # arbitrary, in-range
    info = {"flags": TRACE_LIVE, "sweepSeq": 7, "binOffset": 0,
            "totalBins": total, "startFreqKhz": 2400400, "stepKhz": 1000}
    frames = []
    off = 0
    while off < total:
        info["binOffset"] = off
        p = encode_spectrum_payload(info, bins)
        assert p is not None, "encode failed"
        frames.append(p)
        off += len(p) - HEADER_BYTES

    assert len(frames) == 2, f"expected 2 frames for 80 bins, got {len(frames)}"
    assert decode_spectrum_payload(frames[0])["sweep_end"] is False
    assert decode_spectrum_payload(frames[-1])["sweep_end"] is True

    rebuilt = [None] * total
    for p in frames:
        d = decode_spectrum_payload(p)
        assert d is not None, "decode failed"
        assert d["startFreqKhz"] == 2400400 and d["stepKhz"] == 1000
        for i, v in enumerate(d["bins"]):
            rebuilt[d["binOffset"] + i] = v
    assert rebuilt == bins, "round-trip mismatch"

    # foreign sub-type is rejected as routine, not crashed on
    bad = bytearray(frames[0])
    bad[0] = 0x99
    assert decode_spectrum_payload(bytes(bad)) is None

    # CRSF frame wrap + CRC + reader round-trip
    body = bytes([CRSF_FRAMETYPE_ELRS_VENDOR, CRSF_ADDRESS_FLIGHT_CONTROLLER,
                  CRSF_ADDRESS_CRSF_RECEIVER]) + frames[0]
    wire = bytes([CRSF_SYNC_BYTE, len(body) + 1]) + body + bytes([crc8(body)])
    rd = CRSFReader()
    got = list(rd.feed(b"\x00\x11" + wire))  # junk prefix should be resynced away
    assert len(got) == 1 and got[0][0] == CRSF_FRAMETYPE_ELRS_VENDOR
    vp = extract_vendor_payload(got[0][1])
    assert decode_spectrum_payload(vp)["totalBins"] == total

    # trigger frame sanity
    trig = build_trigger(PORT_BOTH)
    assert trig[0] == CRSF_ADDRESS_CRSF_RECEIVER and trig[2] == CRSF_FRAMETYPE_COMMAND
    assert trig[3:6] == b"sp\x02"
    assert crc8(trig[2:-1]) == trig[-1]

    print("selftest OK")
    return 0


def peak_text(store):
    lines = []
    for bid, b in store.bands.items():
        vals = [(i, v) for i, v in enumerate(b["live"]) if v is not None]
        if not vals:
            continue
        pi, pv = max(vals, key=lambda t: t[1])
        lines.append(f"{bid}: peak {pv:>4} dBm @ {store.bin_freq_mhz(b, pi):8.1f} MHz "
                     f"({len(vals)}/{b['total']} bins)")
    return "  |  ".join(lines) if lines else "(waiting for data...)"


def run_live(args):
    try:
        import serial
    except ImportError:
        print("pyserial is required for live capture: pip install pyserial", file=sys.stderr)
        return 2

    band = BAND_NAMES[args.band]
    ser = serial.Serial(args.port, args.baud, timeout=0.05)
    ser.reset_input_buffer()
    ser.write(build_trigger(band))
    ser.flush()
    print(f"triggered scan (band={args.band}) on {args.port} @ {args.baud} baud")
    print("reset the receiver to exit the scan.")

    store = TraceStore()
    reader = CRSFReader()

    plot = None
    if not args.no_plot:
        plot = _try_make_plot(store)
        if plot is None:
            print("matplotlib not available; falling back to text mode "
                  "(pip install matplotlib for a plot).")

    last_txt = 0.0
    try:
        while True:
            data = ser.read(256)
            if data:
                for ftype, body in reader.feed(data):
                    if ftype != CRSF_FRAMETYPE_ELRS_VENDOR:
                        continue
                    vp = extract_vendor_payload(body)
                    if vp is None:
                        continue
                    dec = decode_spectrum_payload(vp)
                    if dec is not None:
                        store.update(dec)
            if plot is not None:
                if not plot.pump():
                    break
            else:
                now = time.time()
                if now - last_txt >= 0.25:
                    print("\r" + peak_text(store).ljust(100), end="", flush=True)
                    last_txt = now
    except KeyboardInterrupt:
        print("\ninterrupted.")
    finally:
        ser.close()
    return 0


def _try_make_plot(store):
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        return None

    class Plot:
        def __init__(self):
            plt.ion()
            self.fig, self.ax = plt.subplots(1, 2, figsize=(12, 4))
            self.fig.suptitle("ELRS RX spectrum (live = solid, max-hold = dashed)")
            self.lines = {}
            self.order = ["900MHz", "2.4GHz"]
            for a, name in zip(self.ax, self.order):
                a.set_title(name)
                a.set_xlabel("MHz")
                a.set_ylabel("dBm")
                a.grid(True, alpha=0.3)
            self.fig.tight_layout()

        def pump(self):
            for a, name in zip(self.ax, self.order):
                b = store.bands.get(name)
                if b is None:
                    continue
                xs = [store.bin_freq_mhz(b, i) for i in range(b["total"])]
                live = [b["live"][i] if b["live"][i] is not None else float("nan")
                        for i in range(b["total"])]
                mx = [b["max"][i] if b["max"][i] is not None else float("nan")
                      for i in range(b["total"])]
                if name not in self.lines:
                    (ll,) = a.plot(xs, live, "-", lw=1.2, label="live")
                    (ml,) = a.plot(xs, mx, "--", lw=0.9, label="max-hold")
                    a.legend(loc="upper right", fontsize=8)
                    self.lines[name] = (ll, ml)
                else:
                    ll, ml = self.lines[name]
                    ll.set_data(xs, live)
                    ml.set_data(xs, mx)
                a.relim()
                a.autoscale_view()
            self.fig.canvas.draw_idle()
            self.fig.canvas.flush_events()
            return plt.fignum_exists(self.fig.number)

    return Plot()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (e.g. COM7, /dev/ttyACM0)")
    ap.add_argument("--baud", type=int, default=420000, help="baud (default 420000)")
    ap.add_argument("--band", default="both", choices=list(BAND_NAMES.keys()),
                    help="antenna port / band: 900, 2g4, both (default both)")
    ap.add_argument("--no-plot", action="store_true", help="text peak readout only")
    ap.add_argument("--selftest", action="store_true",
                    help="run the codec round-trip test and exit (no hardware)")
    args = ap.parse_args(argv)

    if args.selftest:
        return run_selftest()
    if not args.port:
        ap.error("--port is required (or use --selftest)")
    return run_live(args)


if __name__ == "__main__":
    sys.exit(main())
