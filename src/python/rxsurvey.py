# Host-side driver for the Phase 0 in-flight-survey experiment, built with
# -DRX_SURVEY_PHASE0. Talks to the receiver's CRSF UART over BetaFlight serial
# passthrough, the same rail rxspectrum.py uses.
#
# THE QUESTION THIS ANSWERS
#
# An in-flight passive survey would read instantaneous RSSI on the channels the
# live link already visits. lib/SpectrumSweep measures the same channels with the
# link DOWN, and it only gets a clean floor by dropping to STDBY_RC and settling
# 0.5-4 ms per bin -- neither of which a live link can do. So: is the live read
# biased by the AGC state the wanted packet just left behind, and if so, by how
# much and from what offset does it stop mattering?
#
# The experiment is a paired comparison. Sweep the sample offset, capture live;
# then power the transmitter off and capture a reference sweep with rxspectrum.py
# over the same channels from the same position; then join per channel.
#
#     # 1. link UP, transmitter on, FC in passthrough
#     python rxsurvey.py --offsets 50,100,200,400 --dwell 30 --log phase0.jsonl
#
#     # 2. link DOWN, transmitter off, receiver in the same place
#     python rxspectrum.py --band 2g4 --no-plot --log reference.jsonl
#
#     # 3. join them
#     python rxsurvey.py --compare phase0.jsonl --reference reference.jsonl
#
#     python rxsurvey.py --disarm      # leave the receiver alone again
#     python rxsurvey.py --selftest    # codec check, no hardware
#
# Levels are uncalibrated on both sides, and deliberately so: the comparison is
# between two readings from the SAME receive chain, where the calibration term
# cancels. Do not read the absolute numbers as dBm against an instrument.

import argparse
import json
import statistics
import sys
import time

import bootloader
from rxspectrum import (CRSFReader, CRSF_ADDRESS_CRSF_RECEIVER, CRSF_FRAMETYPE_ELRS_VENDOR,
                        CRSF_SYNC_BYTE, RSSI_INVALID as SPECTRUM_RSSI_INVALID,
                        TRACE_LIVE, TRACE_MAXHOLD, dbg_print, extract_vendor_payload,
                        open_rx_serial)

# Arm/disarm command, in the form bootloader.py keeps the other non-CRSF
# commands in. Handled by RXEndpoint::handleRaw; appended bytes are
# [on, offset us/4, period ms].
SURVEY_SEQ = [0xEC, 0x04, 0x32, ord('s'), ord('v')]

# The in-flight survey's bench command ('sf'): [stream on/off, optional mode].
# Mode is FLIGHT_SURVEY_* (0=Off 1=Both 2=900 3=2.4), so a bench without a
# handset can arm remotely; omit it to leave the Lua-set mode alone.
FLIGHT_SEQ = [0xEC, 0x04, 0x32, ord('s'), ord('f')]

# Survey sub-protocol, mirroring lib/RxSurvey/SurveyProtocol.h
SUBTYPE = 0x03
SUBTYPE_STATUS = 0x04
PROTO_VERSION = 2
HEADER_BYTES = 26
SAMPLE_BYTES = 7
MAX_SAMPLES_PER_FRAME = 4
CHAN_INVALID = 0xFF
RSSI_INVALID = -128
OFFSET_QUANTUM_US = 4
OFFSET_MAX_US = 1020

FLAG_ARMED = 0x01
FLAG_DUAL_RADIO = 0x02
FLAG_GATED_LINK = 0x04
FLAG_GATED_TIMER = 0x08
FLAG_GATED_RATE = 0x10
FLAG_GATED_BINDING = 0x20
FLAG_GATED_RADIO = 0x40
FLAG_GATED_LQ = 0x80  # flight survey only: link up but LQ under the threshold

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
SFLAG_GEMINI = 0x02
SFLAG_CLEAN = 0x04  # no wanted packet this period: free of the TX's own energy

# The receiver's ordinary link-statistics frame, which is already on the same
# UART. Decoded here so the cost of the ISR busy-wait is MEASURED rather than
# asserted: at a deep offset the sample gives up the packet behind it, and uplink
# LQ is where that shows. Layout per crsf_protocol.h crsfLinkStatistics_t.
CRSF_FRAMETYPE_LINK_STATISTICS = 0x14
LINK_STATS_PAYLOAD_BYTES = 10


def decode_link_statistics(type_and_body):
    if len(type_and_body) != 1 + LINK_STATS_PAYLOAD_BYTES:
        return None
    if type_and_body[0] != CRSF_FRAMETYPE_LINK_STATISTICS:
        return None
    p = type_and_body[1:]
    # RSSI is sent positivized: -80 dBm goes out as 80. The last three bytes are
    # the downlink fields, dead on a stock receiver -- the in-flight survey's
    # 3-byte transport rides exactly there (unpack with unpack_debug).
    return {"uplinkRssi1": -p[0], "uplinkRssi2": -p[1], "uplinkLQ": p[2],
            "uplinkSnr": to_int8(p[3]), "antenna": p[4], "rfMode": p[5],
            "txPower": p[6], "debug": (p[7], p[8], p[9])}

STATUS_ARMED = 0
STATUS_DISARMED = 1

EXIT_NO_SAMPLES = 4  # armed and reachable, but every gate blocked for the whole run


def to_int8(v):
    return v - 256 if v >= 128 else v


def build_command(on, offset_us=200, period_ms=100):
    q = min(max(int(offset_us), 0), OFFSET_MAX_US) // OFFSET_QUANTUM_US
    return bootloader.get_telemetry_seq(SURVEY_SEQ, [1 if on else 0, q, period_ms])


def build_flight_command(stream_on, mode=None):
    extra = [1 if stream_on else 0]
    if mode is not None:
        extra.append(mode)
    return bootloader.get_telemetry_seq(FLIGHT_SEQ, extra)


# Port of SurveyDecodeFrame. Returns a dict, or None if this is not a
# well-formed survey frame; a foreign sub-type is routine, not an error.
def decode_survey_payload(payload):
    if len(payload) < HEADER_BYTES:
        return None
    if payload[0] != SUBTYPE or payload[1] != PROTO_VERSION:
        return None
    count = payload[25]
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
            "offsetUs": payload[o + 4] * OFFSET_QUANTUM_US,
            "packetRssi": to_int8(payload[o + 5]),
            "sflags": payload[o + 6],
        })

    return {
        "flags": payload[2],
        "armed": bool(payload[2] & FLAG_ARMED),
        "dual": bool(payload[2] & FLAG_DUAL_RADIO),
        "seq": payload[3],
        "reqOffsetUs": payload[4] * OFFSET_QUANTUM_US,
        "enumRate": payload[5],
        "bwKhz": (payload[6] << 8) | payload[7],
        "lnaGainDb": to_int8(payload[8]),
        "channelCount": payload[9],
        "startFreqKhz": ((payload[10] << 24) | (payload[11] << 16) |
                         (payload[12] << 8) | payload[13]),
        "stepKhz": (payload[14] << 8) | payload[15],
        # Second-band axis: a dual-band link hops two grids off one sequence
        # pointer, so chan2 joins against this axis when it is present (all
        # three fields are 0 on a single-band link).
        "startFreqKhz2": ((payload[16] << 24) | (payload[17] << 16) |
                          (payload[18] << 8) | payload[19]),
        "stepKhz2": (payload[20] << 8) | payload[21],
        "channelCount2": payload[22],
        "dropped": (payload[23] << 8) | payload[24],
        "samples": samples,
    }


def decode_status_payload(payload):
    if len(payload) < 5 or payload[0] != SUBTYPE_STATUS or payload[1] != PROTO_VERSION:
        return None
    st = {
        "armed": payload[2] == STATUS_ARMED,
        "offsetUs": payload[3] * OFFSET_QUANTUM_US,
        "periodMs": payload[4],
    }
    # The flight survey's extended form; length-tolerant on purpose.
    if len(payload) >= 9:
        st["worstTockUs"] = (payload[5] << 8) | payload[6]
        st["mode"] = payload[7]
        st["coverageGen"] = payload[8]
    return st


def chan_freq_khz(rec, chan):
    return rec["startFreqKhz"] + chan * rec["stepKhz"]


# Frequency of a radio-2 channel: the second band's axis when the frame carries
# one, the shared primary axis otherwise (diversity/Gemini -- one band).
def chan2_freq_khz(rec, chan):
    if rec.get("startFreqKhz2"):
        return rec["startFreqKhz2"] + chan * rec["stepKhz2"]
    return chan_freq_khz(rec, chan)


# The 3-byte in-flight transport (SurveyPackDebug/SurveyUnpackDebug): the
# shipping survey rides the three dead downlink bytes of the link-statistics
# frame, logged by Betaflight as debug[0..2]. Magnitudes are -dBm (127 = none);
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


# ---------------------------------------------------------------- capture ----


# Flatten a decoded frame into one record per (sample, radio). A Gemini pair
# reads two DIFFERENT channels in the same instant, so the two radios cannot
# share a row without losing which frequency each level belongs to.
def flatten(rec, t):
    out = []
    for s in rec["samples"]:
        for radio, chan, rssi in ((1, s["chan1"], s["rssi1"]), (2, s["chan2"], s["rssi2"])):
            if chan == CHAN_INVALID or rssi == RSSI_INVALID:
                continue
            out.append({
                "t": round(t, 4),
                "radio": radio,
                "chan": chan,
                "freqKhz": chan_freq_khz(rec, chan) if radio == 1 else chan2_freq_khz(rec, chan),
                "rssi": rssi,
                "offsetUs": s["offsetUs"],
                "reqOffsetUs": rec["reqOffsetUs"],
                "packetRssi": s["packetRssi"],
                "packetOnThisRadio": bool(s["sflags"] & SFLAG_PACKET_ON_RADIO2) == (radio == 2),
                "gemini": bool(s["sflags"] & SFLAG_GEMINI),
                "enumRate": rec["enumRate"],
                "bwKhz": rec["bwKhz"],
                "lnaGainDb": rec["lnaGainDb"],
                "channelCount": rec["channelCount"],
                "startFreqKhz": rec["startFreqKhz"],
                "stepKhz": rec["stepKhz"],
            })
    return out


# One chunk of serial data -> the per-frame handlers. The decode order (status
# before survey: both ride the vendor frame type) lives here once, shared by the
# Phase 0 capture and the flight-survey watch.
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


class Capture:
    def __init__(self, serial_port, log_file):
        self.s = serial_port
        self.log = log_file
        self.reader = CRSFReader()
        self.records = []
        self.dropped = 0
        self.gaps = 0
        self.last_seq = None
        self.gates_seen = 0
        self.frames = 0
        self.link = []
        self.status = None
        self.t0 = time.time()

    # Uplink LQ over the capture. The busy-wait costs at most one packet per
    # sample period by construction; this is what confirms it did.
    def link_summary(self):
        if not self.link:
            return None
        lq = [s["uplinkLQ"] for s in self.link]
        rssi = [s["uplinkRssi1"] for s in self.link]
        return {"n": len(lq), "lq_min": min(lq), "lq_med": statistics.median(lq),
                "rssi_med": statistics.median(rssi),
                "tx_power": self.link[-1]["txPower"], "rf_mode": self.link[-1]["rfMode"]}

    def _note_status(self, st):
        self.status = st

    def pump(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            data = self.s.read(256)
            if not data:
                continue
            dispatch_frames(self.reader, data,
                            self.link.append, self._note_status, self._take)

    def _take(self, rec):
        if self.last_seq is not None:
            expected = (self.last_seq + 1) & 0xFF
            if rec["seq"] != expected:
                self.gaps += (rec["seq"] - expected) & 0xFF
        self.last_seq = rec["seq"]
        self.dropped += rec["dropped"]
        self.gates_seen |= rec["flags"] & ALL_GATES
        self.frames += 1
        rows = flatten(rec, time.time() - self.t0)
        self.records.extend(rows)
        if self.log is not None:
            for r in rows:
                self.log.write(json.dumps(r) + "\n")


def run_capture(args):
    s = open_rx_serial(args)
    offsets = [int(x) for x in args.offsets.split(",") if x.strip()]

    dbg_print("======== PHASE 0 SURVEY ========")
    dbg_print("  Offsets %s us, %.0f s each, sample period %d ms, on %s @ %s"
              % (offsets, args.dwell, args.period, args.port, args.baud))
    dbg_print("  The RC LINK MUST BE UP -- transmitter on, receiver connected.")
    dbg_print("  Levels are uncalibrated; the comparison, not the absolute value, is the result.")

    log_f = open(args.log, "w") if args.log else None
    if log_f is not None:
        dbg_print("  Logging samples to %s" % args.log)

    total = 0
    try:
        for off in offsets:
            s.write(build_command(True, off, args.period))
            s.flush()
            cap = Capture(s, log_f)
            # Let the command land and the first frames arrive before counting
            # the dwell, so a slow passthrough does not eat the shortest offset.
            cap.pump(0.5)
            if cap.status is None:
                dbg_print("  no answer from the receiver -- is it built with -DRX_SURVEY_PHASE0?")
                return EXIT_NO_SAMPLES
            cap.pump(args.dwell)

            n = len(cap.records)
            total += n
            achieved = sorted({r["offsetUs"] for r in cap.records})
            dbg_print("  offset %4d us requested -> %5d readings, achieved %s us%s%s"
                      % (off, n,
                         ("%d..%d" % (achieved[0], achieved[-1])) if achieved else "n/a",
                         ", %d dropped" % cap.dropped if cap.dropped else "",
                         ", %d frames lost" % cap.gaps if cap.gaps else ""))
            ls = cap.link_summary()
            if ls is not None:
                dbg_print("      link: uplink LQ median %d, min %d | RSSI median %d dBm | "
                          "rf_mode %d, tx_power idx %d"
                          % (ls["lq_med"], ls["lq_min"], ls["rssi_med"],
                             ls["rf_mode"], ls["tx_power"]))
            for reason in gate_reasons(cap.gates_seen):
                dbg_print("      gate blocked sampling: %s" % reason)
            # Every reason the receiver can refuse to sample sets a gate, so no
            # samples AND no gate means it was willing and no packets arrived --
            # a different fault, and one the receiver cannot see from its side.
            if n == 0 and not cap.gates_seen:
                dbg_print("      no gate was blocking and still nothing was sampled: the "
                          "receiver reports a locked LoRa link but no packets reached the "
                          "sample hook (%d frames seen)" % cap.frames)
    except KeyboardInterrupt:
        dbg_print("\ninterrupted")
    finally:
        # Always leave the receiver disarmed: the hook busy-waits in the RXdone
        # ISR, so an armed receiver that is forgotten about is a receiver quietly
        # giving up a packet every sample period.
        try:
            s.write(build_command(False))
            s.flush()
            time.sleep(0.2)
        finally:
            s.close()
        if log_f is not None:
            log_f.close()

    dbg_print("  disarmed. %d readings total." % total)
    if total == 0:
        return EXIT_NO_SAMPLES
    dbg_print("  Now power the transmitter OFF, leave the receiver where it is, and run:")
    dbg_print("    python rxspectrum.py --band 2g4 --no-plot --log reference.jsonl")
    dbg_print("    python rxsurvey.py --compare %s --reference reference.jsonl"
              % (args.log or "<survey.jsonl>"))
    return 0


def run_disarm(args):
    s = open_rx_serial(args)
    try:
        s.write(build_command(False))
        s.flush()
        cap = Capture(s, None)
        cap.pump(1.0)
        if cap.status is None:
            dbg_print("no answer from the receiver")
            return 1
        dbg_print("receiver reports %s" % ("ARMED" if cap.status["armed"] else "disarmed"))
    finally:
        s.close()
    return 0


# ------------------------------------------------------------------- watch ----


MODE_NAMES = {0: "Off", 1: "Both", 2: "900", 3: "2.4"}
MODE_FROM_ARG = {"off": 0, "both": 1, "900": 2, "2.4": 3}

# enum_rate values of the dual-band rates: the transport's debug[2] bit 7 is a
# band bit on these, an assignment bit everywhere else. Mirrors RATE_LORA_DUAL_*
# in src/include/common.h (expresslrs_RFrates_e, pinned at 100) -- extend this
# set if the firmware ever adds a dual-band rate, or --no-stream watches will
# misread the band bit as an assignment bit.
DUAL_BAND_RF_MODES = {100, 101}


# Watch the in-flight survey's 3-byte transport live (RX_FLIGHT_SURVEY): decode
# the ordinary link-statistics frames, dedupe on the freshness toggle, unpack
# the debug bytes, and -- unless --no-stream -- also request the full-fidelity
# 0x83 stream and check every deduped sample appears identically on both
# transports. This validates on the bench exactly what Betaflight will log.
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
                         MODE_NAMES.get(st.get("mode", -1), "?"),
                         st["periodMs"]))
            if not st["armed"] and mode is None:
                dbg_print("  (arm from the handset Lua, or rerun with --mode)")
        state["status"] = st

    def note_survey(rec):
        state["gates"] |= rec["flags"] & ALL_GATES
        note_stream(rec)

    def note_stream(rec):
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

    dbg_print("======== FLIGHT SURVEY WATCH ========")
    dbg_print("  %.0f s on %s @ %s; the RC link must be up."
              % (args.dwell, args.port, args.baud))
    try:
        s.write(build_flight_command(stream_on, mode))
        s.flush()
        end = time.time() + args.dwell
        status_deadline = time.time() + 1.5
        while time.time() < end:
            data = s.read(256)
            if state["status"] is None and time.time() > status_deadline:
                dbg_print("  no answer -- is the receiver built with -DRX_FLIGHT_SURVEY?")
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
                s.write(build_flight_command(False, 0 if mode is not None else None))
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
    if st is not None and "worstTockUs" in st:
        dbg_print("  worst tock cost %d us (budget: 200 us slack), coverage generation %d"
                  % (st["worstTockUs"], st["coverageGen"]))
    for reason in gate_reasons(state["gates"]):
        dbg_print("  gate blocked sampling: %s" % reason)
    return 0 if samples else EXIT_NO_SAMPLES


# ---------------------------------------------------------------- analysis ----


def load_jsonl(path):
    out = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                out.append(json.loads(line))
            except ValueError:
                continue
    return out


# Reduce an rxspectrum.py capture to one median level per channel frequency.
#
# Live traces only: max-hold is a running maximum over every sweep since the scan
# started, which is not an estimate of anything the survey measures.
#
# radio filters to one chain. That matters on a true-diversity receiver, where the
# survey reads BOTH radios on the same frequency at the same instant: pooling two
# antennas into one reference median, then differencing a single antenna against
# it, manufactures a fixed offset out of nothing but the antennas differing -- and
# a fixed offset is exactly what this experiment is trying to attribute. A sweep
# taken without rxspectrum.py --compare has only radio 1 and is tagged as such.
def reference_by_freq(records, radio=None):
    per_freq = {}
    for rec in records:
        if rec.get("trace") != TRACE_LIVE:
            continue
        if radio is not None and rec.get("radio", 1) != radio:
            continue
        start = rec["startFreqKhz"]
        step = rec["stepKhz"]
        for i, v in enumerate(rec["bins"]):
            if v == SPECTRUM_RSSI_INVALID:
                continue
            per_freq.setdefault(start + (rec["binOffset"] + i) * step, []).append(v)
    return {f: statistics.median(v) for f, v in per_freq.items()}


def summarise(deltas):
    if not deltas:
        return None
    deltas = sorted(deltas)
    return {
        "n": len(deltas),
        "median": statistics.median(deltas),
        "p10": deltas[max(0, int(0.10 * (len(deltas) - 1)))],
        "p90": deltas[int(0.90 * (len(deltas) - 1))],
    }


# Bucket achieved offsets so jitter around a requested value does not shatter
# the table into dozens of one-sample rows.
def offset_bucket(us, width):
    return int(round(us / float(width)) * width)


def run_compare(args):
    survey = load_jsonl(args.compare)
    if not survey:
        dbg_print("No samples found in %s" % args.compare)
        return 1
    ref_records = load_jsonl(args.reference)

    only = None if args.radio == "both" else int(args.radio)
    if only is not None:
        survey = [r for r in survey if r.get("radio", 1) == only]
        if not survey:
            dbg_print("No readings from radio %d in %s" % (only, args.compare))
            return 1
    reference = reference_by_freq(ref_records, only)
    if not reference:
        dbg_print("No live-trace bins%s found in %s"
                  % ("" if only is None else " for radio %d" % only, args.reference))
        if only is not None:
            dbg_print("  A sweep taken without rxspectrum.py --compare carries radio 1 only.")
        return 1

    dbg_print("======== PHASE 0 COMPARISON ========")
    dbg_print("  survey    : %s, %d readings%s"
              % (args.compare, len(survey),
                 "" if only is None else " (radio %d only)" % only))
    dbg_print("  reference : %s, %d channels" % (args.reference, len(reference)))

    # On a true-diversity receiver both radios read the same frequency in the same
    # instant, so pooling them hides an antenna difference inside what is supposed
    # to be an AGC measurement. Say how far apart they are before pooling them.
    if only is None:
        per_radio = {}
        for r in survey:
            per_radio.setdefault(r.get("radio", 1), []).append(r["rssi"])
        if len(per_radio) > 1:
            meds = {k: statistics.median(v) for k, v in per_radio.items()}
            spread = max(meds.values()) - min(meds.values())
            dbg_print("  radios    : %s"
                      % ", ".join("r%d n=%d median %.1f dBm" % (k, len(per_radio[k]), meds[k])
                                  for k in sorted(per_radio)))
            if spread >= args.radio_spread:
                dbg_print("  WARNING: the two chains differ by %.1f dB. They are being pooled,"
                          % spread)
                dbg_print("           which widens the spread below and can look like offset")
                dbg_print("           jitter. Re-run with --radio 1 and --radio 2 separately,")
                dbg_print("           against a reference captured with rxspectrum.py --compare.")

    bw = {r.get("bwKhz") for r in survey}
    dbg_print("  link sensing bandwidth: %s kHz" % ", ".join(str(b) for b in sorted(bw)))
    if len(bw) > 1:
        dbg_print("  WARNING: the bandwidth changed mid-capture. Readings taken through")
        dbg_print("           different filters are not comparable; split the capture.")

    # Per (offset bucket, channel) median of the survey, joined to the reference
    # by FREQUENCY, not channel index -- the two tools must land on the same
    # grid, and joining on kHz proves it instead of assuming it.
    by_bucket = {}
    for r in survey:
        key = (offset_bucket(r["offsetUs"], args.bucket), r["freqKhz"])
        by_bucket.setdefault(key, []).append(r["rssi"])

    unmatched = {f for (_, f) in by_bucket if f not in reference}
    if unmatched:
        dbg_print("  WARNING: %d survey frequencies have no reference bin -- the two captures"
                  % len(unmatched))
        dbg_print("           are not on the same band or grid. First: %d kHz"
                  % sorted(unmatched)[0])

    dbg_print("")
    dbg_print("  Delta = survey - reference, in dB. Positive means the live read sits ABOVE")
    dbg_print("  the link-down floor, which is what AGC carry-over from the wanted packet")
    dbg_print("  would look like. Convergence toward 0 as the offset grows is the pass.")
    dbg_print("")
    dbg_print("  %8s %6s %8s %8s %8s" % ("offset", "chans", "p10", "median", "p90"))
    dbg_print("  %8s %6s %8s %8s %8s" % ("-" * 8, "-" * 6, "-" * 8, "-" * 8, "-" * 8))

    buckets = sorted({b for (b, _) in by_bucket})
    rows = []
    for b in buckets:
        deltas = []
        for (bb, freq), vals in by_bucket.items():
            if bb != b or freq not in reference:
                continue
            if len(vals) < args.min_samples:
                continue
            deltas.append(statistics.median(vals) - reference[freq])
        s = summarise(deltas)
        if s is None:
            continue
        rows.append((b, s))
        dbg_print("  %6d us %6d %8.1f %8.1f %8.1f"
                  % (b, s["n"], s["p10"], s["median"], s["p90"]))

    if not rows:
        dbg_print("  (no channel reached --min-samples %d in any offset bucket)" % args.min_samples)
        return 1

    # The discriminator. AGC carry-over is set by the wanted signal, so its
    # contribution must scale with how strong that signal was. A delta that does
    # not move with packet RSSI is a fixed calibration difference between the two
    # measurement paths -- a different bug, and not one more settling time fixes.
    #
    # A matrix rather than one column: at an offset where the bias has already
    # decayed the strength breakdown is flat by construction and says nothing, so
    # the interesting cells are the shallow offsets where a bias still exists.
    dbg_print("")
    dbg_print("  Median delta by wanted-signal strength (rows) and offset (columns).")
    dbg_print("  AGC carry-over falls off DOWN a column and ACROSS a row. A row that is")
    dbg_print("  flat across strength is the two measurement paths disagreeing, not the AGC.")
    dbg_print("")
    cells = {}
    for r in survey:
        if r["freqKhz"] not in reference:
            continue
        band = int(r["packetRssi"] // 10) * 10  # 10 dB bins
        key = (band, offset_bucket(r["offsetUs"], args.bucket))
        cells.setdefault(key, []).append(r["rssi"] - reference[r["freqKhz"]])

    dbg_print("  %12s %s" % ("packet RSSI", "".join("%9s" % ("%dus" % b) for b in buckets)))
    dbg_print("  %12s %s" % ("-" * 12, "".join("%9s" % ("-" * 7) for _ in buckets)))
    for band in sorted({b for (b, _) in cells}, reverse=True):
        row = ""
        for b in buckets:
            vals = cells.get((band, b))
            row += "%9s" % ("%.1f" % statistics.median(vals)
                            if vals and len(vals) >= args.min_samples else "-")
        dbg_print("  %5d..%-5d %s" % (band, band + 9, row))
    dbg_print("  (cells with fewer than --min-samples %d readings are blank)" % args.min_samples)

    dbg_print("")
    dbg_print("  Read this against the reference being a different measurement, not a truth:")
    dbg_print("  the sweep resets the radio and settles 0.5-4 ms per bin, the survey does")
    dbg_print("  neither. A residual offset that is flat across packet strength is the two")
    dbg_print("  paths disagreeing, not the AGC.")
    return 0


# --------------------------------------------------------------- selftest ----


# Frames SurveyEncodeFrame() emits, captured from the firmware codec compiled for
# the host -- the decoder is checked against the real encoder, not a Python port
# of it. Regenerate whenever the wire format changes; the recipe is in
# test/test_rxsurvey/README.
GOLDEN_FRAMES = [
    # a full 4-sample frame, 2.4GHz grid (no second band), dual radio, Gemini
    "030203323204032c0c500024a09003e80000000000000000000400289ca132d80201"
    "299da232d703022a9ea332d602032b9fa432d503",
    # the gate report: no samples, GATED_TIMER | GATED_RATE, dual-band grids
    # (40-channel 915 primary, 80-channel 2.4 second axis)
    "03021933320401f40c28000dc94c02580024a09003e850025800",
]

# Packed 3-byte transport samples from SurveyPackDebug(), same recipe: an
# ordinary sample (magA 100 + toggle, magB 95, chan 42 + bit7), then the
# sentinels (magA invalid, magB 0 + clean, chan none).
GOLDEN_DEBUG = ["e45faa", "7f807f"]


def run_selftest():
    frames = [bytes.fromhex(g) for g in GOLDEN_FRAMES]

    full = decode_survey_payload(frames[0])
    assert full is not None, "decode of the full frame failed"
    assert full["armed"] and full["dual"], "frame flags lost"
    assert full["seq"] == 50, full["seq"]
    assert full["reqOffsetUs"] == 200, full["reqOffsetUs"]
    assert full["bwKhz"] == 812, full["bwKhz"]
    assert full["lnaGainDb"] == 12, full["lnaGainDb"]
    assert full["channelCount"] == 80, full["channelCount"]
    assert full["startFreqKhz"] == 2400400, full["startFreqKhz"]
    assert full["stepKhz"] == 1000, full["stepKhz"]
    # single-band frame: the second axis is absent and radio 2 shares the grid
    assert full["startFreqKhz2"] == 0 and full["channelCount2"] == 0, full
    assert chan2_freq_khz(full, 40) == chan_freq_khz(full, 40)
    assert len(full["samples"]) == 4, len(full["samples"])

    # signed fields are what a byte-wise codec gets wrong silently
    s0 = full["samples"][0]
    assert s0["chan1"] == 0 and s0["chan2"] == 40, s0
    assert s0["rssi1"] == -100 and s0["rssi2"] == -95, s0
    assert s0["packetRssi"] == -40, s0
    assert s0["offsetUs"] == 200, s0
    assert full["samples"][1]["sflags"] & SFLAG_PACKET_ON_RADIO2

    # the axis must reproduce the sweep's grid exactly or the join is meaningless
    assert chan_freq_khz(full, 0) == 2400400
    assert chan_freq_khz(full, 79) == 2479400

    # Gemini reads two different channels at one instant; flatten must keep both
    rows = flatten(full, 0.0)
    assert len(rows) == 8, len(rows)
    assert {r["chan"] for r in rows[:2]} == {0, 40}, rows[:2]

    gate = decode_survey_payload(frames[1])
    assert gate is not None, "decode of the gate report failed"
    assert gate["samples"] == [], gate["samples"]
    assert gate["dropped"] == 0x0258, gate["dropped"]
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
    assert flatten(gate, 0.0) == []

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
    bad[25] = MAX_SAMPLES_PER_FRAME + 1
    assert decode_survey_payload(bytes(bad)) is None
    assert decode_survey_payload(frames[0][:HEADER_BYTES - 1]) is None

    # CRSF frame wrap, CRC and reader round-trip
    body = bytes([CRSF_FRAMETYPE_ELRS_VENDOR, CRSF_SYNC_BYTE,
                  CRSF_ADDRESS_CRSF_RECEIVER]) + frames[0]
    wire = bytes([CRSF_SYNC_BYTE, len(body) + 1]) + body + bytes([bootloader.calc_crc8(body)])
    got = list(CRSFReader().feed(b"\x00\x11" + wire))  # junk prefix must resync away
    assert len(got) == 1 and got[0][0] == CRSF_FRAMETYPE_ELRS_VENDOR
    assert decode_survey_payload(extract_vendor_payload(got[0][1]))["seq"] == 50

    # the arm command, byte for byte as RXEndpoint::handleRaw reads it
    cmd = build_command(True, 200, 100)
    assert cmd[0] == CRSF_ADDRESS_CRSF_RECEIVER and cmd[1] == 0x07, cmd[:2]
    assert cmd[3:8] == b"sv\x01\x32\x64", cmd[3:8]
    assert bootloader.calc_crc8(cmd[2:-1]) == cmd[-1]
    # out-of-range offsets are clamped rather than wrapping into a small one:
    # payload is [3]='s' [4]='v' [5]=on [6]=offset us/4 [7]=period ms
    assert build_command(True, 99999)[6] == OFFSET_MAX_US // OFFSET_QUANTUM_US
    assert build_command(False)[5] == 0

    # link statistics, the yardstick for what the ISR busy-wait costs. RSSI is
    # positivized on the wire and must come back negative, or a -80 dBm link
    # reads as +80 and every cost estimate is nonsense.
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

    # the flight survey's bench command and its extended status frame
    fc = build_flight_command(True, 1)
    assert fc[3:7] == b"sf\x01\x01", fc[3:7]
    assert bootloader.calc_crc8(fc[2:-1]) == fc[-1]
    assert build_flight_command(False)[5] == 0
    est = decode_status_payload(bytes([SUBTYPE_STATUS, PROTO_VERSION, 0, 0, 40, 0, 42, 1, 3]))
    assert est["armed"] and est["periodMs"] == 40, est
    assert est["worstTockUs"] == 42 and est["mode"] == 1 and est["coverageGen"] == 3, est
    short = decode_status_payload(bytes([SUBTYPE_STATUS, PROTO_VERSION, 1, 50, 100]))
    assert short == {"armed": False, "offsetUs": 200, "periodMs": 100}, short

    # the analysis reduces a reference sweep by live trace only
    sweep = [
        {"trace": TRACE_LIVE, "radio": 1, "startFreqKhz": 2400400, "stepKhz": 1000,
         "binOffset": 0, "bins": [-100, -98, SPECTRUM_RSSI_INVALID]},
        {"trace": TRACE_MAXHOLD, "radio": 1, "startFreqKhz": 2400400, "stepKhz": 1000,
         "binOffset": 0, "bins": [-10, -10, -10]},
        {"trace": TRACE_LIVE, "radio": 2, "startFreqKhz": 2400400, "stepKhz": 1000,
         "binOffset": 0, "bins": [-94, -92, SPECTRUM_RSSI_INVALID]},
    ]
    assert reference_by_freq(sweep, 1) == {2400400: -100, 2401400: -98}
    assert reference_by_freq(sweep, 2) == {2400400: -94, 2401400: -92}
    # pooling two chains that differ by 6 dB lands between them -- which is why
    # --radio exists, and why a per-antenna comparison must filter both sides
    assert reference_by_freq(sweep) == {2400400: -97, 2401400: -95}
    # a sweep taken without --compare has no radio id and reads as radio 1
    assert reference_by_freq([{k: v for k, v in sweep[0].items() if k != "radio"}], 1)

    dbg_print("selftest OK")
    return 0


def main(custom_args=None):
    parser = argparse.ArgumentParser(
        description="Drive the Phase 0 in-flight-survey experiment on a receiver "
                    "built with -DRX_SURVEY_PHASE0, compare the result against "
                    "a link-down rxspectrum.py sweep, or watch the shipping "
                    "survey's 3-byte transport live (--watch, -DRX_FLIGHT_SURVEY)")
    parser.add_argument("-b", "--baud", type=int, default=420000,
        help="Baud rate for passthrough communication")
    parser.add_argument("-p", "--port", type=str,
        help="Override serial port autodetection and use PORT")
    parser.add_argument("--offsets", type=str, default="50,100,200,400",
        help="Comma-separated sample offsets from packet end, in us (default 50,100,200,400). "
             "Offsets past the point where the next packet starts on air will cost that "
             "packet -- that is bounded by --period and shows up as LQ in the same capture")
    parser.add_argument("--dwell", type=float, default=30.0,
        help="Seconds to capture at each offset (default 30)")
    parser.add_argument("--period", type=int, default=100,
        help="Minimum ms between samples, which bounds the disturbed-packet rate (default 100)")
    parser.add_argument("-np", "--no-passthrough", action="store_false",
        dest="passthrough", help="Do not initialize passthrough, the RX is already reachable on PORT")
    parser.add_argument("--log", type=str, metavar="FILE",
        help="Write each reading to FILE as JSON lines")
    parser.add_argument("--disarm", action="store_true",
        help="Disarm the receiver and exit")
    parser.add_argument("--watch", action="store_true",
        help="Watch the in-flight survey (RX_FLIGHT_SURVEY): decode the 3-byte "
             "transport in the link-statistics frames for --dwell seconds, "
             "cross-checked against the full-fidelity 0x83 stream")
    parser.add_argument("--mode", type=str, choices=["off", "both", "900", "2.4"],
        help="With --watch: also set the survey mode remotely (otherwise arm "
             "from the handset Lua)")
    parser.add_argument("--no-stream", action="store_true",
        help="With --watch: do not request the 0x83 stream; watch only what "
             "Betaflight would see in the link-statistics frames")
    parser.add_argument("--compare", type=str, metavar="SURVEY_JSONL",
        help="Analyse a captured survey against --reference, no hardware")
    parser.add_argument("--reference", type=str, metavar="SWEEP_JSONL",
        help="A link-down rxspectrum.py --log capture to compare against")
    parser.add_argument("--bucket", type=int, default=25,
        help="Width in us of the achieved-offset buckets in --compare (default 25)")
    parser.add_argument("--min-samples", type=int, default=5,
        help="Readings a channel needs in a bucket before it enters the statistics (default 5)")
    parser.add_argument("--radio", type=str, default="both", choices=["1", "2", "both"],
        help="Restrict --compare to one receive chain (default both). On a true-diversity "
             "receiver both radios read the same frequency at the same instant, so a "
             "per-antenna comparison needs this on BOTH sides -- capture the reference with "
             "rxspectrum.py --compare so its traces carry a radio id too")
    parser.add_argument("--radio-spread", type=float, default=2.0, metavar="DB",
        help="Warn when the two chains' median levels differ by more than this (default 2)")
    parser.add_argument("--selftest", action="store_true",
        help="Run the codec round-trip test and exit, no hardware")
    args = parser.parse_args(custom_args)

    if args.selftest:
        return run_selftest()
    if args.compare:
        if not args.reference:
            parser.error("--compare needs --reference, a link-down rxspectrum.py capture")
        return run_compare(args)

    import serials_find
    from BFinitPassthrough import bf_passthrough_init, PassthroughEnabled

    if args.port is None:
        args.port = serials_find.get_serial_port()

    if args.passthrough:
        try:
            bf_passthrough_init(args.port, args.baud)
        except PassthroughEnabled as err:
            dbg_print(str(err))

    if args.disarm:
        return run_disarm(args)
    if args.watch:
        return run_watch(args)
    return run_capture(args)


if __name__ == '__main__':
    exit(main())
