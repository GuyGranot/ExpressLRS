#!/usr/bin/env python3
"""Analyse in-flight RF survey samples from a Betaflight Blackbox log.

The DEBUG_RF_SURVEY feature exports one noise-floor sample per CRSF
link-statistics frame through the three downlink debug bytes, which Betaflight
records as debug[0..2] under `set debug_mode = CRSF_LINK_STATISTICS_DOWN`.
This tool recovers those samples from .bbl logs (deduped on the freshness
toggle), joins them with GPS position, altitude, throttle and current from the
same log, and reports per-channel spectra and load correlations per flight.

Requires the pure-Python `orangebox` Blackbox parser. Its PyPI wheel trips an
entry-point bug in recent pip; install with
    pip download orangebox --no-deps -d /tmp/obx
    python -m zipfile -e /tmp/obx/orangebox-*.whl <somewhere on sys.path>
or run with PYTHONPATH pointing at an extracted copy.

Levels carry the survey's standing caveat: antenna-referred via
power_lna_gain, uncalibrated in absolute terms.
"""
import argparse
import collections
import json
import math
import sys

try:
    from orangebox import Parser
except ImportError:
    print("error: the 'orangebox' package is required -- see the module docstring",
          file=sys.stderr)
    sys.exit(2)

from rxsurvey import unpack_debug, DBG_MAG_INVALID, DBG_CHAN_NONE

# Channel->frequency grids, matching lib/FHSS/FHSS.cpp domains[] (sub-GHz) and
# domainsDualBand[0] (the 2.4 grid every dual-band link uses). MHz.
SUBGHZ_GRIDS = {
    "au_915":  (915.5, 0.6, 20),
    "fcc_915": (903.5, 0.6, 40),
    "eu_868":  (863.275, 0.525, 13),
    "in_866":  (865.375, 0.525, 4),
}
GRID_2G4 = (2400.4, 1.0, 80)


def _repair_name_list(names, want):
    """Fix a 'Field I name' header holding a duplicated splice (seen in the
    wild: a flash-write glitch repeats a run of entries, leaving a mangled
    name like 'axisF[0]]' at the seam and more names than encodings). Drop
    the surplus window starting at the first malformed name."""
    import re
    ok = re.compile(r"\w+(\[\d+\])?$")
    drop = len(names) - want
    bad = next((i for i, n in enumerate(names) if not ok.fullmatch(n)), None)
    if drop <= 0 or bad is None:
        return None
    fixed = names[:bad] + names[bad + drop:]
    if len(fixed) == want and all(ok.fullmatch(n) for n in fixed):
        return fixed
    return None


def repair_bbl(path):
    """Return a path whose header field lists are self-consistent: the file
    itself when healthy, else a repaired temp copy. Binary-safe: split/join
    on newlines is byte-exact and only recognized header lines are edited."""
    data = open(path, "rb").read()
    lines = data.split(b"\n")
    changed = False
    for k, line in enumerate(lines):
        if not line.startswith(b"H Field I name:"):
            continue
        names = line.decode("ascii", "replace").split(":", 2)[-1].split(",")
        want = None
        for j in range(k + 1, min(k + 6, len(lines))):
            if lines[j].startswith(b"H Field I signed:"):
                want = len(lines[j].split(b":", 2)[-1].split(b","))
                break
        if want and len(names) > want:
            fixed = _repair_name_list(names, want)
            if fixed:
                lines[k] = b"H Field I name:" + ",".join(fixed).encode()
                changed = True
            else:
                print("warning: %s has a damaged field header this tool cannot repair"
                      % path, file=sys.stderr)
    if not changed:
        return path
    import os
    import tempfile
    fd, tmp = tempfile.mkstemp(suffix=".bbl")
    with os.fdopen(fd, "wb") as f:
        f.write(b"\n".join(lines))
    return tmp


def scan_segments(paths):
    """Yield (path, log_index, Parser) for every log session in every file."""
    for path in paths:
        readable = repair_bbl(path)
        first = Parser.load(readable)
        count = first.reader.log_count
        yield path, 1, first
        for i in range(2, count + 1):
            yield path, i, Parser.load(readable, i)


def haversine_m(lat1, lon1, lat2, lon2):
    r = 6371000.0
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp, dl = p2 - p1, math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * r * math.asin(math.sqrt(a))


def alt_scale(headers, raw_span):
    """Betaflight's writeGPSFrame logs gpsSol.llh.altCm / 10, i.e. decimeters;
    keep a span heuristic only for non-Betaflight logs."""
    if "Betaflight" in str(headers.get("Firmware revision", "")):
        return 0.1
    if raw_span > 50000:
        return 0.01
    if raw_span > 5000:
        return 0.1
    return 1.0


def extract(parser, single_band=False):
    """Decode one log session into deduped survey samples with flight context.

    Returns (samples, meta). Each sample: dict with t (s from log start), band
    ('900'/'2.4', or 'A'/'B' assignment on single-band links), chan, rssi
    (-dBm), clean, throttle, amps, lat, lon, alt_raw.
    """
    names = parser.field_names
    idx = {n: i for i, n in enumerate(names)}
    need = ["time", "debug[0]", "debug[1]", "debug[2]"]
    for n in need:
        if n not in idx:
            raise SystemExit("field '%s' missing from log -- wrong debug_mode?" % n)
    t_i, d0, d1, d2 = (idx[n] for n in need)
    thr_i = idx.get("rcCommand[3]")
    amp_i = idx.get("amperageLatest")
    lat_i, lon_i = idx.get("GPS_coord[0]"), idx.get("GPS_coord[1]")
    alt_i = idx.get("GPS_altitude")

    def num(v):
        return v if isinstance(v, (int, float)) else None

    samples = []
    toggle = None
    t0 = None
    frames = 0
    for fr in parser.frames():
        v = fr.data
        frames += 1
        t = v[t_i]
        if t0 is None:
            t0 = t
        u = unpack_debug(v[d0] & 0xFF, v[d1] & 0xFF, v[d2] & 0xFF)
        if u["toggle"] == toggle:
            continue
        first = toggle is None
        toggle = u["toggle"]
        if first or (u["chan"] == DBG_CHAN_NONE and u["magA"] == DBG_MAG_INVALID):
            continue
        if single_band:
            band = "B" if u["bit7"] else "A"
            mag = u["magA"] if u["magA"] != DBG_MAG_INVALID else u["magB"]
        else:
            band = "2.4" if u["bit7"] else "900"
            mag = u["magA"] if band == "900" else u["magB"]
        if mag == DBG_MAG_INVALID:
            continue
        samples.append({
            "t": (t - t0) / 1e6,
            "band": band,
            "chan": u["chan"],
            "rssi": -mag,
            "clean": u["clean"],
            "throttle": num(v[thr_i]) if thr_i is not None else None,
            "amps": num(v[amp_i]) / 100.0 if amp_i is not None and num(v[amp_i]) is not None else None,
            "lat": num(v[lat_i]) / 1e7 if lat_i is not None and num(v[lat_i]) else None,
            "lon": num(v[lon_i]) / 1e7 if lon_i is not None and num(v[lon_i]) else None,
            "alt_raw": num(v[alt_i]) if alt_i is not None else None,
        })
    meta = {
        "frames": frames,
        "duration": (0 if not samples else samples[-1]["t"]),
        "datetime": parser.headers.get("Log start datetime", "?"),
        "craft": parser.headers.get("Craft name", "?"),
    }
    return samples, meta


def add_derived(samples, headers):
    """Home distance and altitude in meters, relative to the first GPS fix."""
    fixes = [s for s in samples if s["lat"]]
    if not fixes:
        return
    home = fixes[0]
    alts = [s["alt_raw"] for s in fixes if s["alt_raw"] is not None]
    scale = alt_scale(headers, max(alts) - min(alts)) if alts else 1.0
    alt0 = alts[0] * scale if alts else 0.0
    for s in samples:
        if s["lat"]:
            s["home_m"] = haversine_m(home["lat"], home["lon"], s["lat"], s["lon"])
            if s["alt_raw"] is not None:
                s["alt_m"] = s["alt_raw"] * scale - alt0


def pct(vals, p):
    vals = sorted(vals)
    return vals[min(len(vals) - 1, int(p * len(vals)))]


def freq_mhz(band, chan, grid900):
    start, step, _ = GRID_2G4 if band == "2.4" else grid900
    return start + chan * step


def band_rows(samples, band):
    return [s for s in samples if s["band"] == band]


def channel_table(rows):
    by_chan = collections.defaultdict(list)
    for s in rows:
        by_chan[s["chan"]].append(s["rssi"])
    return by_chan


def report_segment(samples, meta, grid900, min_samples, out):
    p = out.append
    p("  frames %d, %.0f s, %d survey samples (%.1f Hz staging)"
      % (meta["frames"], meta["duration"], len(samples),
         len(samples) / meta["duration"] if meta["duration"] else 0))
    gps = [s for s in samples if s.get("home_m") is not None]
    if gps:
        p("  GPS: %d fixed samples, max home distance %.0f m, max rel altitude %.0f m"
          % (len(gps), max(s["home_m"] for s in gps),
             max((s.get("alt_m", 0) or 0) for s in gps)))
    else:
        p("  GPS: no fix")
    for band in sorted({s["band"] for s in samples}):
        rows = band_rows(samples, band)
        by_chan = channel_table(rows)
        floors = [s["rssi"] for s in rows]
        p("  band %s: %d samples, %d channels, floor median %d / p90 %d / max %d dBm"
          % (band, len(rows), len(by_chan), pct(floors, 0.5), pct(floors, 0.9),
             max(floors)))
        hot = sorted(((pct(v, 0.5), c) for c, v in by_chan.items()
                      if len(v) >= min_samples), reverse=True)[:5]
        p("    hottest medians: " + ", ".join(
            "%.1f MHz %d dBm (n=%d)" % (freq_mhz(band, c, grid900), m, len(by_chan[c]))
            for m, c in hot))
        clean = [s["rssi"] for s in rows if s["clean"]]
        dirty = [s["rssi"] for s in rows if not s["clean"]]
        if len(clean) >= min_samples and len(dirty) >= min_samples:
            p("    AGC self-check: clean median %d vs post-packet %d dBm (%d clean samples)"
              % (pct(clean, 0.5), pct(dirty, 0.5), len(clean)))
        else:
            p("    AGC self-check: %d clean samples -- too few to compare" % len(clean))
        alt_rows = [s for s in rows if s.get("alt_m") is not None]
        if alt_rows and max(s["alt_m"] for s in alt_rows) > 20:
            bins = ((0, 10), (10, 50), (50, 150), (150, 10000))
            line = []
            for lo, hi in bins:
                part = [s["rssi"] for s in alt_rows if lo <= s["alt_m"] < hi]
                if len(part) >= min_samples:
                    line.append("%d-%dm: %d (n=%d)" % (lo, hi, pct(part, 0.5), len(part)))
            p("    floor median by altitude: %s dBm" % "; ".join(line))
        for label, key, unit in (("throttle", "throttle", ""), ("current", "amps", " A")):
            vals = sorted({s[key] for s in rows if s[key] is not None})
            if len(vals) < 4:
                continue
            loaded = [s for s in rows if s[key] is not None]
            loaded.sort(key=lambda s: s[key])
            q = len(loaded) // 4
            line = []
            for i in range(4):
                part = loaded[i * q:(i + 1) * q] if i < 3 else loaded[3 * q:]
                lo, hi = part[0][key], part[-1][key]
                line.append("%g-%g%s: %d" % (lo, hi, unit,
                                             pct([s["rssi"] for s in part], 0.5)))
            p("    floor median by %s quartile: %s dBm" % (label, "; ".join(line)))


def plot_segment(samples, meta, grid900, tag, plot_dir, min_samples):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    bands = sorted({s["band"] for s in samples})
    fig, axs = plt.subplots(len(bands), 1, figsize=(11, 4 * len(bands)),
                            squeeze=False)
    for ax, band in zip((a for row in axs for a in row), bands):
        by_chan = channel_table(band_rows(samples, band))
        chans = sorted(c for c, v in by_chan.items() if len(v) >= min_samples)
        f = [freq_mhz(band, c, grid900) for c in chans]
        ax.fill_between(f, [pct(by_chan[c], 0.1) for c in chans],
                        [pct(by_chan[c], 0.9) for c in chans], alpha=0.25)
        ax.plot(f, [pct(by_chan[c], 0.5) for c in chans], marker=".", lw=1.2)
        ax.plot(f, [max(by_chan[c]) for c in chans], lw=0.6, alpha=0.5,
                color="crimson", label="max")
        n = sum(len(by_chan[c]) for c in chans)
        ax.set_title("%s -- band %s, %d samples, median + p10-p90 + max" % (tag, band, n))
        ax.set_xlabel("MHz")
        ax.set_ylabel("dBm (antenna-referred, uncalibrated)")
        ax.grid(True, alpha=0.3)
        ax.legend(loc="upper right", fontsize=8)
    fig.tight_layout()
    out = "%s/%s.png" % (plot_dir, tag)
    fig.savefig(out, dpi=130)
    plt.close(fig)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("logs", nargs="+", help=".bbl file(s); multi-session files are expanded")
    ap.add_argument("--list", action="store_true", help="list log sessions and exit")
    ap.add_argument("--segment", type=int, action="append",
                    help="analyse only these sessions (numbers from --list); repeatable")
    ap.add_argument("--domain", default="fcc_915", choices=sorted(SUBGHZ_GRIDS),
                    help="sub-GHz FHSS domain for the channel axis (default fcc_915)")
    ap.add_argument("--single-band", action="store_true",
                    help="single-band link: bit 7 is the antenna/Gemini assignment, not a band bit")
    ap.add_argument("--min-samples", type=int, default=3,
                    help="minimum samples per channel for tables/plots (default 3)")
    ap.add_argument("--plot-dir", metavar="DIR", help="write a spectrum PNG per session")
    ap.add_argument("--export", metavar="JSONL",
                    help="write all deduped samples (with context) to one JSONL")
    args = ap.parse_args()
    grid900 = SUBGHZ_GRIDS[args.domain]

    exported = open(args.export, "w") if args.export else None
    seg_no = 0
    for path, log_idx, parser in scan_segments(args.logs):
        seg_no += 1
        if args.list:
            samples, meta = extract(parser, args.single_band)
            print("segment %d: %s log %d | %s | %.0f s | %d samples"
                  % (seg_no, path, log_idx, meta["datetime"], meta["duration"],
                     len(samples)))
            continue
        if args.segment and seg_no not in args.segment:
            continue
        samples, meta = extract(parser, args.single_band)
        add_derived(samples, parser.headers)
        tag = "seg%d" % seg_no
        print("== segment %d: %s log %d | %s ==" % (seg_no, path, log_idx, meta["datetime"]))
        if not samples:
            print("  no survey samples (survey disarmed, or wrong debug_mode)")
            continue
        out = []
        report_segment(samples, meta, grid900, args.min_samples, out)
        print("\n".join(out))
        if args.plot_dir:
            print("  plot: " + plot_segment(samples, meta, grid900, tag,
                                            args.plot_dir, args.min_samples))
        if exported:
            for s in samples:
                s["segment"] = seg_no
                exported.write(json.dumps(s) + "\n")
    if exported:
        exported.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
