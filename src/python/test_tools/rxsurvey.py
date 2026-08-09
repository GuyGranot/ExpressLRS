# Host-side watcher for the in-flight passive RF survey, built with
# -DDEBUG_RF_SURVEY. Talks to the receiver's CRSF UART over BetaFlight serial
# passthrough and decodes the survey's 3-byte transport riding the dead
# downlink bytes of the ordinary link-statistics frames -- exactly what
# Betaflight logs as debug[0..2] under debug_mode = CRSF_LINK_STATISTICS_DOWN.
# Unless --no-stream it also requests the full-fidelity 0x83 bench stream and
# checks every deduped sample appears identically on both transports.
#
# Arm the survey from the handset Lua "RF Survey" option (Off/Both/900/2.4),
# or remotely with --mode. The RC link must be UP: the survey samples the
# channels the live link visits, it is not the link-down spectrum sweep.
#
#     python rxsurvey.py                              # watch a Lua-armed receiver
#     python rxsurvey.py --mode both --log w.jsonl    # arm remotely, log samples
#     python rxsurvey.py --selftest                   # codec check, no hardware
#
# A healthy dual-band run looks like:
#
#     ======== RF SURVEY WATCH ========
#       30 s on COM7 @ 420000; the RC link must be up.
#       receiver: ARMED, mode Both, export interval 40 ms
#       1462 link-stats frames (48.7 Hz), 583 new samples (19.4 Hz)
#       clean-sample ratio 62% (no wanted packet in the sample's period)
#       band 900: 291 samples (9.7 Hz), 40/40 channels seen
#       band 2.4: 292 samples (9.7 Hz), 80/80 channels seen
#       0x83 stream: 585 samples; 583/583 debug samples matched it exactly
#       worst tock cost 30 us (budget: 200 us slack), coverage generation 4
#
# Levels are antenna-referred (power_lna_gain is already subtracted) but
# uncalibrated in absolute terms: they depend on the receiver's chain and
# antenna, so read them as relative -- comparable within one capture, not
# across receivers or against a lab instrument.

import argparse
import json
import os
import sys
import time

# bootloader.py lives in the parent directory (src/python) and is stdlib-only;
# pyserial and the passthrough helpers are imported where they are used, so
# --selftest runs anywhere, including CI, with nothing installed.
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import bootloader


def dbg_print(line=''):
    sys.stdout.write(line + '\n')
    sys.stdout.flush()


# CRSF wire constants, mirroring include/crsf_protocol.h
CRSF_SYNC_BYTE = 0xC8
CRSF_ADDRESS_CRSF_RECEIVER = 0xEC
CRSF_FRAMETYPE_ELRS_VENDOR = 0x83
CRSF_MAX_FRAME_LEN = 64


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


# The survey's bench command ('sf'), in the form bootloader.py keeps the other
# non-CRSF commands in. Handled by RXEndpoint::handleRaw; appended bytes are
# [stream on/off, optional mode]. Mode is 0=Off 1=Both 2=900 3=2.4, so a bench
# without a handset can arm remotely; omit it to leave the Lua-set mode alone.
SURVEY_SEQ = [0xEC, 0x04, 0x32, ord('s'), ord('f')]

# Survey sub-protocol, mirroring lib/RxSurvey/SurveyProtocol.h
SUBTYPE = 0x03
SUBTYPE_STATUS = 0x04
STATUS_PAYLOAD_BYTES = 7
PROTO_VERSION = 1
HEADER_BYTES = 25
SAMPLE_BYTES = 5
MAX_SAMPLES_PER_FRAME = 5
CHAN_INVALID = 0xFF
RSSI_INVALID = -128

FLAG_DUAL_RADIO = 0x01
FLAG_GATED_LINK = 0x02
FLAG_GATED_TIMER = 0x04
FLAG_GATED_RATE = 0x08
FLAG_GATED_BINDING = 0x10
FLAG_GATED_RADIO = 0x20
FLAG_GATED_LQ = 0x40  # link up but LQ under the sampling threshold

# Ordered most-explanatory first: GATED_RADIO causes GATED_LINK and GATED_TIMER,
# so reporting it first stops the reader chasing the transmitter.
GATE_TEXT = [
    (FLAG_GATED_RADIO, "the radio is not running -- receiver is in WiFi/BLE mode or a "
                       "spectrum sweep. Power-cycle it; the transmitter is not the problem"),
    (FLAG_GATED_LINK, "no RC link (the survey needs the transmitter ON -- this is not the sweep)"),
    (FLAG_GATED_TIMER, "receiver timer not locked yet"),
    (FLAG_GATED_RATE, "not a LoRa air rate; switch the handset off FLRC/GFSK"),
    (FLAG_GATED_BINDING, "receiver is in binding mode"),
    (FLAG_GATED_LQ, "uplink LQ under the survey's sampling threshold"),
]
ALL_GATES = (FLAG_GATED_LINK | FLAG_GATED_TIMER | FLAG_GATED_RATE | FLAG_GATED_BINDING |
             FLAG_GATED_RADIO | FLAG_GATED_LQ)

SFLAG_PACKET_ON_RADIO2 = 0x01
SFLAG_CLEAN = 0x02  # no wanted packet this period: free of the TX's own energy

# The receiver's ordinary link-statistics frame, which is already on the same
# UART. The survey's 3-byte transport rides its last three (downlink) bytes,
# dead on a stock receiver. Layout per crsf_protocol.h crsfLinkStatistics_t.
CRSF_FRAMETYPE_LINK_STATISTICS = 0x14
LINK_STATS_PAYLOAD_BYTES = 10


def decode_link_statistics(type_and_body):
    if len(type_and_body) != 1 + LINK_STATS_PAYLOAD_BYTES:
        return None
    if type_and_body[0] != CRSF_FRAMETYPE_LINK_STATISTICS:
        return None
    p = type_and_body[1:]
    # RSSI is sent positivized: -80 dBm goes out as 80. The last three bytes are
    # the downlink fields the survey rides -- unpack with unpack_debug.
    return {"uplinkRssi1": -p[0], "uplinkRssi2": -p[1], "uplinkLQ": p[2],
            "uplinkSnr": to_int8(p[3]), "antenna": p[4], "rfMode": p[5],
            "txPower": p[6], "debug": (p[7], p[8], p[9])}


EXIT_NO_SAMPLES = 4  # armed and reachable, but every gate blocked for the whole run


def to_int8(v):
    return v - 256 if v >= 128 else v


def build_command(stream_on, mode=None):
    extra = [1 if stream_on else 0]
    if mode is not None:
        extra.append(mode)
    return bootloader.get_telemetry_seq(SURVEY_SEQ, extra)


# Port of SurveyDecodeFrame. Returns a dict, or None if this is not a
# well-formed survey frame; a foreign sub-type is routine, not an error.
def decode_survey_payload(payload):
    if len(payload) < HEADER_BYTES:
        return None
    if payload[0] != SUBTYPE or payload[1] != PROTO_VERSION:
        return None
    count = payload[24]
    if count > MAX_SAMPLES_PER_FRAME:
        return None
    if len(payload) != HEADER_BYTES + SAMPLE_BYTES * count:
        return None

    samples = []
    for i in range(count):
        o = HEADER_BYTES + SAMPLE_BYTES * i
        samples.append({
            "chan1": payload[o],
            "chan2": payload[o + 1],
            "rssi1": to_int8(payload[o + 2]),
            "rssi2": to_int8(payload[o + 3]),
            "sflags": payload[o + 4],
        })

    return {
        "flags": payload[2],
        "dual": bool(payload[2] & FLAG_DUAL_RADIO),
        "seq": payload[3],
        "enumRate": payload[4],
        "bwKhz": (payload[5] << 8) | payload[6],
        "lnaGainDb": to_int8(payload[7]),
        "channelCount": payload[8],
        "startFreqKhz": ((payload[9] << 24) | (payload[10] << 16) |
                         (payload[11] << 8) | payload[12]),
        "stepKhz": (payload[13] << 8) | payload[14],
        # Second-band axis: a dual-band link hops two grids off one sequence
        # pointer, so chan2 joins against this axis when it is present (all
        # three fields are 0 on a single-band link).
        "startFreqKhz2": ((payload[15] << 24) | (payload[16] << 16) |
                          (payload[17] << 8) | payload[18]),
        "stepKhz2": (payload[19] << 8) | payload[20],
        "channelCount2": payload[21],
        "dropped": (payload[22] << 8) | payload[23],
        "samples": samples,
    }


# Status (sub-type 0x04): sent in answer to every bench command and
# periodically while streaming. Mode 0 means disarmed.
def decode_status_payload(payload):
    if len(payload) != STATUS_PAYLOAD_BYTES:
        return None
    if payload[0] != SUBTYPE_STATUS or payload[1] != PROTO_VERSION:
        return None
    return {
        "mode": payload[2],
        "armed": payload[2] != 0,
        "periodMs": payload[3],  # export interval, 0 while disarmed
        "worstTockUs": (payload[4] << 8) | payload[5],
        "coverageGen": payload[6],
    }


def chan_freq_khz(rec, chan):
    return rec["startFreqKhz"] + chan * rec["stepKhz"]


# Frequency of a radio-2 channel: the second band's axis when the frame carries
# one, the shared primary axis otherwise (diversity -- one band).
def chan2_freq_khz(rec, chan):
    if rec.get("startFreqKhz2"):
        return rec["startFreqKhz2"] + chan * rec["stepKhz2"]
    return chan_freq_khz(rec, chan)


# The 3-byte in-flight transport (SurveyPackDebug/SurveyUnpackDebug): the
# survey rides the three dead downlink bytes of the link-statistics frame,
# logged by Betaflight as debug[0..2]. Magnitudes are -dBm (127 = none);
# bit 7 is toggle / clean / assignment-or-band respectively.
DBG_MAG_INVALID = 127
DBG_CHAN_NONE = 127


def unpack_debug(b0, b1, b2):
    return {
        "magA": b0 & 0x7F, "toggle": bool(b0 & 0x80),
        "magB": b1 & 0x7F, "clean": bool(b1 & 0x80),
        "chan": b2 & 0x7F, "bit7": bool(b2 & 0x80),
    }


def debug_rssi(mag):
    return None if mag == DBG_MAG_INVALID else -mag


def gate_reasons(flags):
    return [text for bit, text in GATE_TEXT if flags & bit]


# One chunk of serial data -> the per-frame handlers. The decode order (status
# before survey: both ride the vendor frame type) lives here once.
def dispatch_frames(reader, data, on_link_stats, on_status, on_survey):
    for ftype, body in reader.feed(data):
        if ftype == CRSF_FRAMETYPE_LINK_STATISTICS:
            ls = decode_link_statistics(body)
            if ls is not None:
                on_link_stats(ls)
            continue
        if ftype != CRSF_FRAMETYPE_ELRS_VENDOR:
            continue
        vp = extract_vendor_payload(body)
        if vp is None:
            continue
        st = decode_status_payload(vp)
        if st is not None:
            on_status(st)
            continue
        rec = decode_survey_payload(vp)
        if rec is not None:
            on_survey(rec)


# ------------------------------------------------------------------- watch ----


MODE_NAMES = {0: "Off", 1: "Both", 2: "900", 3: "2.4"}
MODE_FROM_ARG = {"off": 0, "both": 1, "900": 2, "2.4": 3}

# enum_rate values of the dual-band rates: the transport's debug[2] bit 7 is a
# band bit on these, an assignment bit everywhere else. Mirrors RATE_LORA_DUAL_*
# in src/include/common.h (expresslrs_RFrates_e, pinned at 100) -- extend this
# set if the firmware ever adds a dual-band rate, or --no-stream watches will
# misread the band bit as an assignment bit.
DUAL_BAND_RF_MODES = {100, 101}


# Watch the survey's 3-byte transport live: decode the ordinary link-statistics
# frames, dedupe on the freshness toggle, unpack the debug bytes, and -- unless
# --no-stream -- also request the full-fidelity 0x83 stream and check every
# deduped sample appears identically on both transports. This validates on the
# bench exactly what Betaflight will log.
def run_watch(args):
    s = open_rx_serial(args)
    mode = MODE_FROM_ARG[args.mode] if args.mode else None
    stream_on = not args.no_stream
    log_f = open(args.log, "w") if args.log else None
    reader = CRSFReader()
    t0 = time.time()

    samples = []       # deduped debug-byte samples, the transport under test
    stream_recent = [] # recent 0x83 samples converted for the cross-check
    counts = {"frames": 0, "stream": 0, "matched": 0}
    state = {"toggle": None, "dual": None, "status": None, "gates": 0,
             "chanCount": None, "chanCount2": None}

    def note_link_stats(ls):
        now = time.time()
        counts["frames"] += 1
        d = unpack_debug(*ls["debug"])
        if state["dual"] is None:
            state["dual"] = ls["rfMode"] in DUAL_BAND_RF_MODES
        if d["toggle"] == state["toggle"]:
            return # the same staged sample re-sent; forced back-to-back sends exist
        first = state["toggle"] is None
        state["toggle"] = d["toggle"]
        if first or (d["chan"] == DBG_CHAN_NONE and d["magA"] == DBG_MAG_INVALID):
            return # "no sample yet", or an unknown-age sample at startup
        row = {"t": round(now - t0, 4), "magA": d["magA"], "magB": d["magB"],
               "chan": d["chan"], "clean": d["clean"], "bit7": d["bit7"],
               "rssiA": debug_rssi(d["magA"]), "rssiB": debug_rssi(d["magB"]),
               "dual": state["dual"]}
        if state["dual"]:
            row["band"] = "2.4" if d["bit7"] else "900"
        samples.append(row)
        if log_f is not None:
            log_f.write(json.dumps(row) + "\n")
        for i, cand in enumerate(stream_recent):
            if (d["magA"], d["magB"]) == cand["mags"] and d["chan"] in cand["chans"]:
                counts["matched"] += 1
                del stream_recent[:i + 1]
                break

    def note_status(st):
        if state["status"] is None:
            dbg_print("  receiver: %s, mode %s, export interval %d ms"
                      % ("ARMED" if st["armed"] else "disarmed",
                         MODE_NAMES.get(st["mode"], "?"),
                         st["periodMs"]))
            if not st["armed"] and mode is None:
                dbg_print("  (arm from the handset Lua 'RF Survey' option, or rerun with --mode)")
        state["status"] = st

    def note_survey(rec):
        state["gates"] |= rec["flags"] & ALL_GATES
        state["chanCount"] = rec["channelCount"]
        state["chanCount2"] = rec["channelCount2"]
        if rec["channelCount2"]:
            state["dual"] = True
        for smp in rec["samples"]:
            counts["stream"] += 1
            magA = (DBG_MAG_INVALID if smp["rssi1"] == RSSI_INVALID
                    else max(0, min(126, -smp["rssi1"])))
            magB = (DBG_MAG_INVALID if smp["rssi2"] == RSSI_INVALID
                    else max(0, min(126, -smp["rssi2"])))
            chans = {c for c in (smp["chan1"], smp["chan2"]) if c != CHAN_INVALID}
            stream_recent.append({"mags": (magA, magB), "chans": chans})
        del stream_recent[:-16]

    dbg_print("======== RF SURVEY WATCH ========")
    dbg_print("  %.0f s on %s @ %s; the RC link must be up."
              % (args.dwell, args.port, args.baud))
    try:
        s.write(build_command(stream_on, mode))
        s.flush()
        end = time.time() + args.dwell
        status_deadline = time.time() + 1.5
        while time.time() < end:
            data = s.read(256)
            if state["status"] is None and time.time() > status_deadline:
                dbg_print("  no answer -- is the receiver built with -DDEBUG_RF_SURVEY?")
                return 1
            if not data:
                continue
            dispatch_frames(reader, data, note_link_stats, note_status, note_survey)
    except KeyboardInterrupt:
        dbg_print("\ninterrupted")
    finally:
        try:
            if stream_on or mode is not None:
                # Leave nothing running: stream off, and disarm if we armed it.
                s.write(build_command(False, 0 if mode is not None else None))
                s.flush()
                time.sleep(0.2)
        finally:
            s.close()
        if log_f is not None:
            log_f.close()

    span = max(1e-9, time.time() - t0)
    dbg_print("  %d link-stats frames (%.1f Hz), %d new samples (%.1f Hz)"
              % (counts["frames"], counts["frames"] / span,
                 len(samples), len(samples) / span))
    if samples:
        clean = sum(1 for r in samples if r["clean"])
        dbg_print("  clean-sample ratio %.0f%% (no wanted packet in the sample's period)"
                  % (100.0 * clean / len(samples)))
        def cov_text(chans, total):
            return ("%d/%d" % (len(chans), total)) if total else "%d" % len(chans)

        if state["dual"]:
            for band, total in (("900", state["chanCount"]), ("2.4", state["chanCount2"])):
                rows = [r for r in samples if r.get("band") == band]
                dbg_print("  band %s: %d samples (%.1f Hz), %s channels seen"
                          % (band, len(rows), len(rows) / span,
                             cov_text({r["chan"] for r in rows}, total)))
        else:
            dbg_print("  %s distinct channels seen"
                      % cov_text({r["chan"] for r in samples}, state["chanCount"]))
    if counts["stream"]:
        dbg_print("  0x83 stream: %d samples; %d/%d debug samples matched it exactly"
                  % (counts["stream"], counts["matched"], len(samples)))
    st = state["status"]
    if st is not None:
        dbg_print("  worst tock cost %d us (budget: 200 us slack), coverage generation %d"
                  % (st["worstTockUs"], st["coverageGen"]))
    for reason in gate_reasons(state["gates"]):
        dbg_print("  gate blocked sampling: %s" % reason)
    return 0 if samples else EXIT_NO_SAMPLES


# --------------------------------------------------------------- selftest ----


# Frames SurveyEncodeFrame() emits, captured from the firmware codec compiled for
# the host -- the decoder is checked against the real encoder, not a Python port
# of it. Regenerate whenever the wire format changes; the recipe is in
# test/test_rxsurvey/README.
GOLDEN_FRAMES = [
    # a full 5-sample frame, 2.4GHz grid (no second band), dual radio
    "0301013204032c0c500024a09003e80000000000000000000500289ca10201299da2"
    "01022a9ea302032b9fa401042ca0a502",
    # the gate report: no samples, GATED_TIMER | GATED_RATE, dual-band grids
    # (40-channel 915 primary, 80-channel 2.4 second axis)
    "03010c330401f40c28000dc94c02580024a09003e850025800",
]

# Packed 3-byte transport samples from SurveyPackDebug(), same recipe: an
# ordinary sample (magA 100 + toggle, magB 95, chan 42 + bit7), then the
# sentinels (magA invalid, magB 0 + clean, chan none).
GOLDEN_DEBUG = ["e45faa", "7f807f"]


def run_selftest():
    frames = [bytes.fromhex(g) for g in GOLDEN_FRAMES]

    full = decode_survey_payload(frames[0])
    assert full is not None, "decode of the full frame failed"
    assert full["flags"] == FLAG_DUAL_RADIO and full["dual"], "frame flags lost"
    assert full["seq"] == 50, full["seq"]
    assert full["enumRate"] == 4, full["enumRate"]
    assert full["bwKhz"] == 812, full["bwKhz"]
    assert full["lnaGainDb"] == 12, full["lnaGainDb"]
    assert full["channelCount"] == 80, full["channelCount"]
    assert full["startFreqKhz"] == 2400400, full["startFreqKhz"]
    assert full["stepKhz"] == 1000, full["stepKhz"]
    # single-band frame: the second axis is absent and radio 2 shares the grid
    assert full["startFreqKhz2"] == 0 and full["stepKhz2"] == 0, full
    assert full["channelCount2"] == 0, full["channelCount2"]
    assert chan2_freq_khz(full, 40) == chan_freq_khz(full, 40)
    assert full["dropped"] == 0, full["dropped"]
    assert len(full["samples"]) == 5, len(full["samples"])

    # signed fields are what a byte-wise codec gets wrong silently
    for i, smp in enumerate(full["samples"]):
        assert smp["chan1"] == i and smp["chan2"] == i + 40, smp
        assert smp["rssi1"] == -100 + i and smp["rssi2"] == -95 + i, smp
        expected = SFLAG_CLEAN if i % 2 == 0 else SFLAG_PACKET_ON_RADIO2
        assert smp["sflags"] == expected, smp

    # the axis must reproduce the sweep's grid exactly or any join against a
    # link-down spectrum capture is meaningless
    assert chan_freq_khz(full, 0) == 2400400
    assert chan_freq_khz(full, 79) == 2479400

    gate = decode_survey_payload(frames[1])
    assert gate is not None, "decode of the gate report failed"
    assert gate["flags"] == (FLAG_GATED_TIMER | FLAG_GATED_RATE), gate["flags"]
    assert gate["seq"] == 51, gate["seq"]
    assert gate["enumRate"] == 4, gate["enumRate"]
    assert gate["bwKhz"] == 500, gate["bwKhz"]
    assert gate["lnaGainDb"] == 12, gate["lnaGainDb"]
    assert gate["channelCount"] == 40, gate["channelCount"]
    assert gate["startFreqKhz"] == 903500 and gate["stepKhz"] == 600, gate
    assert gate["samples"] == [], gate["samples"]
    assert gate["dropped"] == 600, gate["dropped"]
    assert set(gate_reasons(gate["flags"])) == {
        dict(GATE_TEXT)[FLAG_GATED_TIMER], dict(GATE_TEXT)[FLAG_GATED_RATE]}
    # GATED_RADIO must be reported before GATED_LINK: it causes it, and pointing
    # at the transmitter when the receiver's radio is switched off wastes a cycle
    both = gate_reasons(FLAG_GATED_RADIO | FLAG_GATED_LINK | FLAG_GATED_TIMER)
    assert both[0] == dict(GATE_TEXT)[FLAG_GATED_RADIO], both
    assert gate_reasons(0) == []
    assert chan_freq_khz(gate, 39) == 926900, chan_freq_khz(gate, 39)
    # the dual-band second axis: chan2 joins against the 2.4 grid, not the 915 one
    assert gate["startFreqKhz2"] == 2400400 and gate["stepKhz2"] == 1000, gate
    assert gate["channelCount2"] == 80, gate["channelCount2"]
    assert chan2_freq_khz(gate, 0) == 2400400
    assert chan2_freq_khz(gate, 79) == 2479400, chan2_freq_khz(gate, 79)

    # the 3-byte transport, against triples the firmware packer produced
    d1 = unpack_debug(*bytes.fromhex(GOLDEN_DEBUG[0]))
    assert d1 == {"magA": 100, "toggle": True, "magB": 95, "clean": False,
                  "chan": 42, "bit7": True}, d1
    assert debug_rssi(d1["magA"]) == -100 and debug_rssi(d1["magB"]) == -95
    d2 = unpack_debug(*bytes.fromhex(GOLDEN_DEBUG[1]))
    assert d2 == {"magA": DBG_MAG_INVALID, "toggle": False, "magB": 0,
                  "clean": True, "chan": DBG_CHAN_NONE, "bit7": False}, d2
    assert debug_rssi(d2["magA"]) is None and debug_rssi(d2["magB"]) == 0

    # a foreign sub-type is rejected, not crashed on: 0x83 is shared with the
    # spectrum sweep, which owns 0x01 and 0x02
    bad = bytearray(frames[0])
    bad[0] = 0x01
    assert decode_survey_payload(bytes(bad)) is None
    bad = bytearray(frames[0])
    bad[1] = PROTO_VERSION + 1
    assert decode_survey_payload(bytes(bad)) is None
    bad = bytearray(frames[0])
    bad[24] = MAX_SAMPLES_PER_FRAME + 1
    assert decode_survey_payload(bytes(bad)) is None
    assert decode_survey_payload(frames[0][:HEADER_BYTES - 1]) is None
    assert decode_survey_payload(frames[0][:-1]) is None

    # CRSF frame wrap, CRC and reader round-trip
    body = bytes([CRSF_FRAMETYPE_ELRS_VENDOR, CRSF_SYNC_BYTE,
                  CRSF_ADDRESS_CRSF_RECEIVER]) + frames[0]
    wire = bytes([CRSF_SYNC_BYTE, len(body) + 1]) + body + bytes([bootloader.calc_crc8(body)])
    got = list(CRSFReader().feed(b"\x00\x11" + wire))  # junk prefix must resync away
    assert len(got) == 1 and got[0][0] == CRSF_FRAMETYPE_ELRS_VENDOR
    assert decode_survey_payload(extract_vendor_payload(got[0][1]))["seq"] == 50

    # link statistics, the frame whose downlink bytes carry the transport. RSSI
    # is positivized on the wire and must come back negative, or a -80 dBm link
    # reads as +80.
    ls = decode_link_statistics(bytes([CRSF_FRAMETYPE_LINK_STATISTICS,
                                       80, 85, 99, 0xF6, 0, 6, 3, 0xE4, 0x5F, 0xAA]))
    assert ls == {"uplinkRssi1": -80, "uplinkRssi2": -85, "uplinkLQ": 99,
                  "uplinkSnr": -10, "antenna": 0, "rfMode": 6, "txPower": 3,
                  "debug": (0xE4, 0x5F, 0xAA)}, ls
    # ...and the debug bytes it carries unpack as the 3-byte transport
    assert unpack_debug(*ls["debug"]) == d1
    # a short or foreign frame is rejected rather than mis-parsed
    assert decode_link_statistics(bytes([CRSF_FRAMETYPE_LINK_STATISTICS, 1, 2])) is None
    assert decode_link_statistics(bytes([0x16] + [0] * 10)) is None

    # the bench command, byte for byte as RXEndpoint::handleRaw reads it
    cmd = build_command(True, 1)
    assert cmd[0] == CRSF_ADDRESS_CRSF_RECEIVER and cmd[1] == 0x06, cmd[:2]
    assert cmd[3:7] == b"sf\x01\x01", cmd[3:7]
    assert bootloader.calc_crc8(cmd[2:-1]) == cmd[-1]
    # without a mode byte the Lua-set mode is left alone
    cmd = build_command(False)
    assert cmd[1] == 0x05 and cmd[5] == 0, cmd

    # the status frame: mode 0 is disarmed, anything else armed
    st = decode_status_payload(bytes([SUBTYPE_STATUS, PROTO_VERSION, 1, 40, 0, 42, 3]))
    assert st["armed"] and st["mode"] == 1 and st["periodMs"] == 40, st
    assert st["worstTockUs"] == 42 and st["coverageGen"] == 3, st
    st = decode_status_payload(bytes([SUBTYPE_STATUS, PROTO_VERSION, 0, 0, 0, 50, 7]))
    assert st == {"mode": 0, "armed": False, "periodMs": 0,
                  "worstTockUs": 50, "coverageGen": 7}, st
    assert decode_status_payload(bytes([SUBTYPE_STATUS, PROTO_VERSION, 0, 0, 0])) is None
    assert decode_status_payload(bytes([SUBTYPE_STATUS, PROTO_VERSION + 1,
                                        0, 0, 0, 0, 0])) is None

    dbg_print("selftest OK")
    return 0


def main(custom_args=None):
    parser = argparse.ArgumentParser(
        description="Watch the in-flight RF survey (-DDEBUG_RF_SURVEY) on a "
                    "receiver: decode the 3-byte transport in the "
                    "link-statistics frames, cross-checked against the "
                    "full-fidelity 0x83 stream")
    parser.add_argument("-b", "--baud", type=int, default=420000,
        help="Baud rate for passthrough communication")
    parser.add_argument("-p", "--port", type=str,
        help="Override serial port autodetection and use PORT")
    parser.add_argument("--dwell", type=float, default=30.0,
        help="Seconds to watch (default 30)")
    parser.add_argument("--mode", type=str, choices=["off", "both", "900", "2.4"],
        help="Also set the survey mode remotely (otherwise arm from the "
             "handset Lua 'RF Survey' option)")
    parser.add_argument("--no-stream", action="store_true",
        help="Do not request the 0x83 stream; watch only what Betaflight "
             "would see in the link-statistics frames")
    parser.add_argument("--log", type=str, metavar="FILE",
        help="Write each deduped sample to FILE as JSON lines")
    parser.add_argument("--selftest", action="store_true",
        help="Run the codec round-trip test and exit, no hardware")
    args = parser.parse_args(custom_args)

    if args.selftest:
        return run_selftest()

    import serials_find
    from BFinitPassthrough import bf_passthrough_init, PassthroughEnabled

    if args.port is None:
        args.port = serials_find.get_serial_port()

    try:
        bf_passthrough_init(args.port, args.baud)
    except PassthroughEnabled as err:
        dbg_print(str(err))

    return run_watch(args)


if __name__ == '__main__':
    exit(main())
