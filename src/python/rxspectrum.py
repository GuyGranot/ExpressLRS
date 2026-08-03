# Host-side driver and viewer for the receive-only spectrum analyzer built with
# -DRX_SPECTRUM_SCAN. Talks to the receiver's CRSF UART over BetaFlight serial
# passthrough: sends the scan trigger, decodes the CRSF_FRAMETYPE_ELRS_VENDOR
# frames that stream back and plots live + max-hold traces. Exit a scan by
# resetting the receiver. matplotlib is optional; without it the peak readout
# is printed as text.
#
# Levels are uncalibrated: they depend on the receiver's chain and antenna, so
# read them as relative -- comparable within one scan, not across receivers or
# against a lab instrument.
#
#     python rxspectrum.py --band both
#     python rxspectrum.py -p /dev/ttyACM0 --band 2g4 --log sweep.jsonl
#     python rxspectrum.py --selftest     # codec check, no hardware

import time
import argparse
from itertools import chain
import json
import sys
import bootloader

# pyserial and the passthrough helpers are imported where they are used, so
# --selftest and --replay run anywhere, including CI, with nothing installed.


def dbg_print(line=''):
    sys.stdout.write(line + '\n')
    sys.stdout.flush()


# CRSF wire constants, mirroring include/crsf_protocol.h
CRSF_SYNC_BYTE = 0xC8
CRSF_ADDRESS_CRSF_RECEIVER = 0xEC
CRSF_FRAMETYPE_ELRS_VENDOR = 0x83
CRSF_MAX_FRAME_LEN = 64

# Trigger sequence, in the form bootloader.py keeps the other non-CRSF commands
# in. Handled by RXEndpoint::handleRaw; appended bytes select band/rbw/compare.
SPECTRUM_SEQ = [0xEC, 0x04, 0x32, ord('s'), ord('p')]
PORT_900 = 0
PORT_2G4 = 1
PORT_BOTH = 2
BAND_NAMES = {"900": PORT_900, "2g4": PORT_2G4, "both": PORT_BOTH,
              "0": PORT_900, "1": PORT_2G4, "2": PORT_BOTH}

# Sensing bandwidth, resolved per band by the receiver: Wide is the band's own
# air-rate bandwidth, each step down halves it.
RBW_NAMES = {"wide": 0, "medium": 1, "narrow": 2}

# Spectrum sub-protocol, mirroring lib/SpectrumSweep/SpectrumProtocol.h
SUBTYPE = 0x01
SUBTYPE_STATUS = 0x02
PROTO_VERSION = 1
HEADER_BYTES = 15
MAX_BINS_PER_FRAME = 40
MAX_BINS = 80
RSSI_INVALID = -128
FLAG_TRACE_MASK = 0x03
FLAG_SWEEP_END = 0x04
FLAG_MODE_COMPARE = 0x08   # both radios on one band, one trace each
FLAG_RADIO_2 = 0x10        # compare mode: this trace is radio 2's
TRACE_LIVE = 0
TRACE_MAXHOLD = 1

STATUS_ACCEPTED = 0
STATUS_REFUSED_LINKED = 1
STATUS_RADIO_FAILED = 2
STATUS_TEXT = {
    STATUS_ACCEPTED: "accepted -- sweeping",
    STATUS_RADIO_FAILED: ("FAILED: the receiver could not initialise its radio, so "
                          "nothing was measured. It is rebooting."),
    STATUS_REFUSED_LINKED: ("REFUSED: the receiver has an RC link up. This is a bench "
                            "diagnostic and will not tear down a live link -- power the "
                            "transmitter off and retry."),
}

EXIT_REFUSED = 3  # the receiver answered the trigger with anything but ACCEPTED

# Bars are drawn from this baseline. SPECTRUM_RSSI_INVALID sits one below the
# -127 a real reading clamps to, so a floored bin shows as a hairline.
PLOT_FLOOR_DBM = -128


def build_trigger(band, rbw=0, compare=False):
    return bootloader.get_telemetry_seq(SPECTRUM_SEQ, [band, rbw, 1 if compare else 0])


# Port of SpectrumDecodeFrame. Returns a dict, or None if this is not a
# well-formed spectrum frame; a foreign sub-type is routine, not an error.
def decode_spectrum_payload(payload):
    if len(payload) < HEADER_BYTES:
        return None
    if payload[0] != SUBTYPE or payload[1] != PROTO_VERSION:
        return None
    bin_offset = payload[4]
    count = payload[5]
    total = payload[6]
    if count == 0 or count > MAX_BINS_PER_FRAME:
        return None
    if total == 0 or total > MAX_BINS:
        return None
    if bin_offset + count > total:
        return None
    if len(payload) != HEADER_BYTES + count:
        return None
    start_khz = (payload[7] << 24) | (payload[8] << 16) | (payload[9] << 8) | payload[10]
    step_khz = (payload[11] << 8) | payload[12]
    rbw_khz = (payload[13] << 8) | payload[14]

    def to_int8(v):
        return v - 256 if v >= 128 else v

    bins = [to_int8(payload[HEADER_BYTES + i]) for i in range(count)]
    return {
        "flags": payload[2],
        "trace": payload[2] & FLAG_TRACE_MASK,
        "sweep_end": bool(payload[2] & FLAG_SWEEP_END),
        "compare": bool(payload[2] & FLAG_MODE_COMPARE),
        "radio": 2 if (payload[2] & FLAG_RADIO_2) else 1,
        "sweepSeq": payload[3],
        "binOffset": bin_offset,
        "binCount": count,
        "totalBins": total,
        "startFreqKhz": start_khz,
        "stepKhz": step_khz,
        "rbwKhz": rbw_khz,
        "bins": bins,
    }


# Reassembles CRSF frames from a byte stream and yields validated ones.
class CRSFReader:
    def __init__(self):
        self.buf = bytearray()

    def feed(self, data):
        self.buf.extend(data)
        while True:
            # resync to a plausible sync byte
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
            if bootloader.calc_crc8(frame[2:total - 1]) != frame[total - 1]:
                continue
            yield frame[2], frame[2:total - 1]  # (type, type + payload)


# Extended frames carry dest and orig after the type: [type][dest][orig][payload]
def extract_vendor_payload(type_and_body):
    if len(type_and_body) < 4 or type_and_body[0] != CRSF_FRAMETYPE_ELRS_VENDOR:
        return None
    return type_and_body[3:]


# Per-source live and max-hold accumulation, keyed by the frame's own axis.
class TraceStore:
    def __init__(self):
        self.bands = {}

    @staticmethod
    def _source_id(dec):
        # comparing puts two traces on one axis, so the radio has to key it too
        band = "900MHz" if dec["startFreqKhz"] < 1_500_000 else "2.4GHz"
        return f"{band} ant {dec.get('radio', 1)}" if dec.get("compare") else band

    def update(self, dec):
        bid = self._source_id(dec)
        b = self.bands.get(bid)
        n = dec["totalBins"]
        if b is None or b["total"] != n or b["start"] != dec["startFreqKhz"]:
            start, step = dec["startFreqKhz"], dec["stepKhz"]
            b = {
                "total": n,
                "start": start,
                "step": step,
                "rbw": dec.get("rbwKhz", 0),
                "live": [None] * n,
                "max": [None] * n,
                # axis/bar geometry are functions of start/step/total alone, and
                # a change to any of them replaces this dict -- computed once
                "xs": [(start + i * step) / 1000.0 for i in range(n)],
                "width": (step / 1000.0) * 0.85,
            }
            self.bands[bid] = b
        maxhold = dec["trace"] == TRACE_MAXHOLD
        dst = b["max"] if maxhold else b["live"]
        off = dec["binOffset"]
        for i, v in enumerate(dec["bins"]):
            if v == RSSI_INVALID:
                continue
            idx = off + i
            if maxhold:
                dst[idx] = v if dst[idx] is None else max(dst[idx], v)
            else:
                dst[idx] = v

    def bin_freq_mhz(self, b, idx):
        return (b["start"] + idx * b["step"]) / 1000.0


# (index, dBm, live bin count) of the loudest live bin in band b, or None
def band_peak(b):
    vals = [(i, v) for i, v in enumerate(b["live"]) if v is not None]
    if not vals:
        return None
    pi, pv = max(vals, key=lambda t: t[1])
    return pi, pv, len(vals)


def peak_text(store):
    lines = []
    for bid, b in store.bands.items():
        pk = band_peak(b)
        if pk is None:
            continue
        pi, pv, n = pk
        lines.append(f"{bid}: peak {pv:>4} dBm @ {store.bin_freq_mhz(b, pi):8.1f} MHz "
                     f"({n}/{b['total']} bins, {b['rbw']}kHz RBW)")
    return "  |  ".join(lines) if lines else "(waiting for data...)"


# The two frames SpectrumEncodeFrame() emits for an 80-bin 2.4GHz sweep at Wide,
# captured from the firmware codec compiled for the host -- the decoder is
# checked against the real encoder, not a Python port of it. Regenerate whenever
# the wire format changes (see lua/mockup/README.md for the sibling recipe).
GOLDEN_FRAMES = [
    "010100070028500024a09003e8032ce2e1e0dfdedddcdbdad9d8d7d6d5d4d3d2d1d0"
    "cfcecdcccbcac9c8c7c6c5c4c3c2c1c0bfbebdbcbb",
    "010104072828500024a09003e8032cbab9b8b7b6b5b4b3b2b1e2e1e0dfdedddcdbda"
    "d9d8d7d6d5d4d3d2d1d0cfcecdcccbcac9c8c7c6c5",
]


def run_selftest():
    total = MAX_BINS
    bins = [(-30 - (i % 50)) for i in range(total)]
    frames = [bytes.fromhex(g) for g in GOLDEN_FRAMES]

    assert len(frames) == 2, f"expected 2 frames for {total} bins, got {len(frames)}"
    assert decode_spectrum_payload(frames[0])["sweep_end"] is False
    assert decode_spectrum_payload(frames[-1])["sweep_end"] is True

    rebuilt = [None] * total
    for p in frames:
        d = decode_spectrum_payload(p)
        assert d is not None, "decode failed"
        assert d["startFreqKhz"] == 2400400 and d["stepKhz"] == 1000
        assert d["rbwKhz"] == 812, "sensing bandwidth did not survive the round trip"
        for i, v in enumerate(d["bins"]):
            rebuilt[d["binOffset"] + i] = v
    assert rebuilt == bins, "round-trip mismatch"

    # a foreign sub-type is rejected, not crashed on
    bad = bytearray(frames[0])
    bad[0] = 0x99
    assert decode_spectrum_payload(bytes(bad)) is None

    # CRSF frame wrap, CRC and reader round-trip
    body = bytes([CRSF_FRAMETYPE_ELRS_VENDOR, CRSF_SYNC_BYTE,
                  CRSF_ADDRESS_CRSF_RECEIVER]) + frames[0]
    wire = bytes([CRSF_SYNC_BYTE, len(body) + 1]) + body + bytes([bootloader.calc_crc8(body)])
    got = list(CRSFReader().feed(b"\x00\x11" + wire))  # junk prefix must resync away
    assert len(got) == 1 and got[0][0] == CRSF_FRAMETYPE_ELRS_VENDOR
    vp = extract_vendor_payload(got[0][1])
    assert decode_spectrum_payload(vp)["totalBins"] == total

    trig = build_trigger(PORT_BOTH, RBW_NAMES["narrow"], compare=True)
    assert trig[0] == CRSF_ADDRESS_CRSF_RECEIVER and trig[3:8] == b"sp\x02\x02\x01"
    assert bootloader.calc_crc8(trig[2:-1]) == trig[-1]

    dbg_print("selftest OK")
    return 0


def open_rx_serial(args):
    import serial

    # Open with DTR and RTS already deasserted: pyserial raises both by default,
    # and on an STM32 FC those are reset lines -- the FC would reboot on open and
    # drop straight back out of serialpassthrough.
    s = serial.Serial()
    s.port = args.port
    s.baudrate = args.baud
    s.timeout = 0.05
    s.dtr = False
    s.rts = False
    s.open()
    s.reset_input_buffer()
    return s


def run_live(args):
    band = BAND_NAMES[args.band]
    rbw = RBW_NAMES[args.rbw]
    s = open_rx_serial(args)
    s.write(build_trigger(band, rbw, args.compare))
    s.flush()
    dbg_print("======== SPECTRUM SCAN ========")
    dbg_print("  Triggered band '%s', resolution '%s'%s on %s @ %s"
              % (args.band, args.rbw, ", comparing antennas" if args.compare else "",
                 args.port, args.baud))
    dbg_print("  Levels are uncalibrated (chain and antenna dependent) -- read them as relative")
    dbg_print("  Reset the receiver to exit the scan")

    store = TraceStore()
    reader = CRSFReader()

    plot = None
    if not args.no_plot:
        plot = try_make_plot(store)
        if plot is None:
            dbg_print("  matplotlib not available, falling back to text mode")

    log_f = None
    if args.log:
        log_f = open(args.log, "w")
        dbg_print("  Logging decoded frames to %s, replay with --replay" % args.log)

    t0 = time.time()
    last_txt = 0.0
    dirty = False
    try:
        while True:
            data = s.read(256)
            if data:
                for ftype, body in reader.feed(data):
                    if ftype != CRSF_FRAMETYPE_ELRS_VENDOR:
                        continue
                    vp = extract_vendor_payload(body)
                    if vp is None:
                        continue
                    if len(vp) >= 3 and vp[0] == SUBTYPE_STATUS:
                        code = vp[2]
                        dbg_print("  Receiver: %s" % STATUS_TEXT.get(code, "unknown status %s" % code))
                        if code != STATUS_ACCEPTED:
                            return EXIT_REFUSED
                        continue
                    dec = decode_spectrum_payload(vp)
                    if dec is not None:
                        store.update(dec)
                        dirty = dirty or dec["sweep_end"]
                        if log_f is not None:
                            log_f.write(json.dumps(
                                {"t": round(time.time() - t0, 4), **dec}) + "\n")
            now = time.time()
            # Redraw when a trace completed (SWEEP_END, ~every 200ms), plus once
            # per text tick so the canvas keeps servicing window events.
            if plot is not None and (dirty or now - last_txt >= 0.25):
                if not plot.pump():
                    break
                dirty = False
            if now - last_txt >= 0.25:
                txt = peak_text(store)
                if plot is not None:
                    dbg_print(txt)
                else:
                    sys.stdout.write("\r" + txt.ljust(100))
                    sys.stdout.flush()
                last_txt = now
    except KeyboardInterrupt:
        dbg_print("\ninterrupted")
    finally:
        s.close()
        if log_f is not None:
            log_f.close()
    return 0


# Replay a logged capture through the same store and plot, without hardware.
def run_replay(args):
    store = TraceStore()
    records = []
    with open(args.replay) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                records.append(json.loads(line))
            except ValueError:
                continue
    if not records:
        dbg_print("No decoded frames found in %s" % args.replay)
        return 1
    dbg_print("Replaying %s frames from %s" % (len(records), args.replay))

    plot = None if args.no_plot else try_make_plot(store)
    prev_t = records[0].get("t", 0.0)
    for rec in records:
        dec = {k: v for k, v in rec.items() if k != "t"}
        store.update(dec)
        if plot is not None:
            if not plot.pump():
                break
            # pace to the original timing, capped so idle gaps do not drag
            dt = min(max(rec.get("t", prev_t) - prev_t, 0.0), 0.2)
            prev_t = rec.get("t", prev_t)
            time.sleep(dt)
    dbg_print(peak_text(store))
    if plot is not None:
        plot.hold()
    return 0


class Plot:
    def __init__(self, plt, store):
        self.plt = plt
        self.store = store
        plt.ion()
        self.fig, self.ax = plt.subplots(1, 2, figsize=(12, 4))
        self.fig.suptitle("ELRS RX spectrum (live = bars, max-hold = markers)")
        self.lines = {}
        self.annots = {}
        # Two panels either way -- two bands, or two antennas when comparing --
        # but which pair only becomes clear from the first frames.
        self.order = []
        self.unplotted = set()
        for a in self.ax:
            a.set_xlabel("MHz")
            a.set_ylabel("dBm")
            a.grid(True, alpha=0.3)
        self.fig.tight_layout()

    def pump(self):
        store = self.store
        for name in store.bands:
            if name in self.order:
                continue
            if len(self.order) < len(self.ax):
                self.order.append(name)
                self.ax[len(self.order) - 1].set_title(name)
            elif name not in self.unplotted:
                # --band both --compare yields four sources for two panels
                self.unplotted.add(name)
                dbg_print(f"  no panel left for {name}; it is still in --log")
        for a, name in zip(self.ax, self.order):
            b = store.bands.get(name)
            if b is None:
                continue
            xs = b["xs"]
            live = [b["live"][i] if b["live"][i] is not None else float("nan")
                    for i in range(b["total"])]
            mx = [b["max"][i] if b["max"][i] is not None else float("nan")
                  for i in range(b["total"])]
            # bars and markers, not lines: each bin is an independent
            # measurement and nothing was sampled between them
            width = b["width"]
            heights = [(v - PLOT_FLOOR_DBM) if v == v else 0.0 for v in live]
            if name not in self.lines:
                bars = a.bar(xs, heights, width=width, bottom=PLOT_FLOOR_DBM,
                             align="center", color="tab:blue", label="live")
                (ml,) = a.plot(xs, mx, linestyle="none", marker="_",
                               ms=5, mew=1.2, color="tab:red", label="max-hold")
                a.legend(loc="upper right", fontsize=8)
                self.lines[name] = (bars, ml)
            else:
                bars, ml = self.lines[name]
                for rect, h in zip(bars, heights):
                    rect.set_height(h)
                ml.set_data(xs, mx)
            pk = band_peak(b)
            if pk is not None:
                pi, pv, _ = pk
                if name not in self.annots:
                    self.annots[name] = a.annotate(
                        "", xy=(0.02, 0.95), xycoords="axes fraction",
                        fontsize=9, va="top", ha="left",
                        bbox=dict(boxstyle="round", fc="w", alpha=0.7))
                self.annots[name].set_text(
                    f"peak {pv} dBm @ {store.bin_freq_mhz(b, pi):.1f} MHz"
                    f"\n{b['rbw']}kHz RBW")
            top = max((v for v in chain(live, mx) if v == v),
                      default=PLOT_FLOOR_DBM) + 6
            a.set_ylim(PLOT_FLOOR_DBM, max(top, PLOT_FLOOR_DBM + 12))
            a.set_xlim(xs[0] - width, xs[-1] + width)
        self.fig.canvas.draw_idle()
        self.fig.canvas.flush_events()
        return self.plt.fignum_exists(self.fig.number)

    def hold(self):
        self.plt.ioff()
        self.plt.show()  # block until the window is closed


def try_make_plot(store):
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        return None
    return Plot(plt, store)


def main(custom_args=None):
    parser = argparse.ArgumentParser(
        description="Initialize BetaFlight passthrough, trigger a receiver spectrum scan and plot it")
    parser.add_argument("-b", "--baud", type=int, default=420000,
        help="Baud rate for passthrough communication")
    parser.add_argument("-p", "--port", type=str,
        help="Override serial port autodetection and use PORT")
    parser.add_argument("--band", type=str, default="both", choices=list(BAND_NAMES.keys()),
        help="Antenna port to sweep: 900, 2g4 or both (default both)")
    parser.add_argument("--rbw", type=str, default="wide", choices=list(RBW_NAMES.keys()),
        help="Sensing bandwidth, resolved per band by the receiver (default wide)")
    parser.add_argument("--compare", action="store_true",
        help="Sweep both radios over one band, one trace per antenna (dual-radio receivers)")
    parser.add_argument("-np", "--no-passthrough", action="store_false",
        dest="passthrough", help="Do not initialize passthrough, the RX is already reachable on PORT")
    parser.add_argument("--no-plot", action="store_true",
        help="Print the peak readout as text instead of plotting")
    parser.add_argument("--log", type=str, metavar="FILE",
        help="Append each decoded frame to FILE as timestamped JSON lines")
    parser.add_argument("--replay", type=str, metavar="FILE",
        help="Replay a previously logged capture, no hardware and no trigger")
    parser.add_argument("--selftest", action="store_true",
        help="Run the codec round-trip test and exit, no hardware")
    args = parser.parse_args(custom_args)

    if args.selftest:
        return run_selftest()
    if args.replay:
        return run_replay(args)

    import serials_find
    from BFinitPassthrough import bf_passthrough_init, PassthroughEnabled

    if (args.port == None):
        args.port = serials_find.get_serial_port()

    if args.passthrough:
        try:
            bf_passthrough_init(args.port, args.baud)
        except PassthroughEnabled as err:
            dbg_print(str(err))

    return run_live(args)


if __name__ == '__main__':
    returncode = main()
    exit(returncode)
