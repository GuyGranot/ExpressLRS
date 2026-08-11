"""Per-band figures for the in-flight RF survey: condition ladder, per-channel
spectrum by condition, and noise floor vs altitude.

Inputs:
  flight_samples.jsonl   this directory, produced by test_tools/rxsurveylog.py
  the Blackbox logs      --bbl DIR (or $SURVEY_BBL_DIR); they hold the gyro,
                         eRPM and flightModeFlags used to classify each sample

The classification is what makes these figures readable: airborne vs ground
comes from sustained gyro rotation (a grounded airframe cannot rotate), not
from GPS altitude, and the VTX state comes from flightModeFlags bit 30
(BOXUSER1), which blackbox loads from rcModeActivationMask.
"""
import os, sys, json, bisect, argparse, statistics as st

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, 'test_tools'))
from rxsurveylog import repair_bbl
from orangebox import Parser
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.gridspec import GridSpec

ap = argparse.ArgumentParser(description=__doc__,
                             formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('--bbl', default=os.environ.get('SURVEY_BBL_DIR', '.'),
                help='directory holding btfl_003/004/005.bbl (default: $SURVEY_BBL_DIR or .)')
ap.add_argument('--samples', default=None, help='path to flight_samples.jsonl')
ap.add_argument('--out', default=None, help='directory to write the PNGs into')
args = ap.parse_args()

BBL = args.bbl
OUT = args.out or HERE
SAMPLES = args.samples or os.path.join(HERE, 'flight_samples.jsonl')

# --- palette (validated: categorical pair + 5-step ordinal blue ramp) -------
SURFACE   = '#fcfcfb'
INK       = '#0b0b0b'
INK2      = '#52514e'
MUTED     = '#898781'
GRID      = '#e1e0d9'
BASELINE  = '#c3c2b7'
RAMPS     = {'900': ['#86b6ef', '#5598e7', '#2a78d6', '#1c5cab', '#104281'],
             '2.4': ['#ee9a66', '#e07536', '#bd561f', '#96400f', '#6e2d0a']}
BAND_C    = {'900': '#2a78d6', '2.4': '#eb6834'}
WASH      = '#f0efec'

med = lambda x: st.median(x) if x else None

# --- load blackbox-derived per-sample features -----------------------------
ser = {}
for seg, fn in {3: 'btfl_003.bbl', 4: 'btfl_004.bbl', 5: 'btfl_005.bbl'}.items():
    p = Parser.load(repair_bbl(os.path.join(BBL, fn)))
    ix = {n: i for i, n in enumerate(p.field_names)}
    t0 = None; T = []; G = []; R = []
    for fr in p.frames():
        d = fr.data; t = d[ix['time']]
        if t0 is None: t0 = t
        T.append((t - t0) / 1e6)
        G.append(tuple(d[ix['gyroADC[%d]' % k]] for k in range(3)))
        R.append(max(d[ix['eRPM[%d]' % k]] for k in range(4)))
    ser[seg] = (T, G, R)

CACHE = {}
def feat(r):
    key = (r['segment'], round(r['t'], 3))
    if key in CACHE: return CACHE[key]
    if r['segment'] not in ser: CACHE[key] = None; return None
    T, G, R = ser[r['segment']]
    a = bisect.bisect_left(T, r['t'] - 0.25); b = bisect.bisect_right(T, r['t'] + 0.25)
    if b - a < 20: CACHE[key] = None; return None
    n = b - a
    rot = max(abs(sum(G[i][k] for i in range(a, b)) / n) for k in range(3))
    CACHE[key] = (rot, R[min(len(T)-1, bisect.bisect_left(T, r['t']))])
    return CACHE[key]

rows = [json.loads(l) for l in open(SAMPLES)]
AIR = 2.0

STATES = [
    ('ground, motors idle\nVTX off',   lambda r, f: r['segment'] == 4 and r['t'] > 5.19 and f[0] < AIR),
    ('ground, motors idle\nVTX full',  lambda r, f: ((r['segment'] == 4 and r['t'] < 5.19) or r['segment'] in (3,5)) and f[0] < AIR and f[1] < 600),
    ('hover < 10 m\nVTX full',         lambda r, f: r['segment'] in (3,5) and f[0] >= AIR and r['alt_m'] < 10),
    ('flight 30-120 m\nVTX full',      lambda r, f: r['segment'] in (3,5) and f[0] >= AIR and 30 <= r['alt_m'] < 120),
    ('flight > 250 m\nVTX full',       lambda r, f: r['segment'] in (3,5) and f[0] >= AIR and r['alt_m'] >= 250),
]
ALT_BINS = [('ground*', None), ('2-10 m', (2,10)), ('10-30 m', (10,30)), ('30-60 m', (30,60)),
            ('60-120 m', (60,120)), ('120-250 m', (120,250)), ('250-460 m', (250,9999))]

GRIDS = {'900': (903.5, 0.6, 40), '2.4': (2400.4, 1.0, 80)}
def fmhz(band, ch):
    s, step, _ = GRIDS[band]; return s + step * ch

def subset(band, sel):
    out = []
    for r in rows:
        if r['band'] != band or not (0 <= r['chan'] < GRIDS[band][2]): continue
        f = feat(r)
        if f is not None and sel(r, f): out.append(r)
    return out

def roll(vals, w=3):
    out = []
    for i in range(len(vals)):
        win = [v for v in vals[max(0, i-w//2): i+w//2+1] if v is not None]
        out.append(med(win) if win else None)
    return out

def style(ax):
    ax.set_facecolor(SURFACE)
    for s in ('top', 'right'): ax.spines[s].set_visible(False)
    for s in ('left', 'bottom'):
        ax.spines[s].set_color(BASELINE); ax.spines[s].set_linewidth(1.0)
    ax.tick_params(colors=MUTED, labelsize=9)
    for lb in ax.get_xticklabels() + ax.get_yticklabels(): lb.set_color(INK2)
    ax.yaxis.grid(True, color=GRID, linewidth=0.8); ax.set_axisbelow(True)

def make_figure(band, hotspot, hot_label, fname, headline):
    nch = GRIDS[band][2]
    fig = plt.figure(figsize=(13.2, 9.2), dpi=130, facecolor=SURFACE)
    gs = GridSpec(2, 2, figure=fig, height_ratios=[1, 1.05], width_ratios=[1, 1],
                  hspace=0.42, wspace=0.22, left=0.07, right=0.975, top=0.855, bottom=0.075)

    fig.text(0.07, 0.955, '%s  —  what raises the noise floor' % headline,
             color=INK, fontsize=17, fontweight='600', ha='left')
    fig.text(0.07, 0.918,
             'AOS55 quadcopter, ExpressLRS passive in-flight survey, 2 flights + 1 static ground segment.  '
             'Airborne vs ground classified by sustained gyro rotation, not altitude.',
             color=INK2, fontsize=9.5, ha='left')

    # ---------- Panel A: condition ladder ----------
    axA = fig.add_subplot(gs[0, 0]); style(axA)
    xs, ys, ns = [], [], []
    for i, (name, sel) in enumerate(STATES):
        v = [r['rssi'] for r in subset(band, sel)]
        xs.append(i); ys.append(med(v)); ns.append(len(v))
    axA.plot(xs, ys, '-', color=BAND_C[band], linewidth=2.0, zorder=3)
    axA.plot(xs, ys, 'o', color=BAND_C[band], markersize=9, zorder=4,
             markeredgecolor=SURFACE, markeredgewidth=2)
    for x, y in zip(xs, ys):
        axA.annotate('%.0f' % y, (x, y), textcoords='offset points', xytext=(0, 11),
                     ha='center', color=INK, fontsize=10, fontweight='600')
    for i in range(len(xs) - 1):
        d = ys[i+1] - ys[i]
        axA.annotate('%+.0f dB' % d, ((xs[i]+xs[i+1])/2, (ys[i]+ys[i+1])/2),
                     textcoords='offset points', xytext=(0, -17), ha='center',
                     color=INK2, fontsize=9.5)
    axA.set_xticks(xs)
    axA.set_xticklabels([s[0] for s in STATES], fontsize=8.5)
    axA.set_ylabel('median noise floor (dBm)', color=INK2, fontsize=10)
    axA.set_title('Condition ladder — each step changes one variable',
                  color=INK, fontsize=11.5, fontweight='600', loc='left', pad=10)
    rng = max(ys) - min(ys)
    axA.set_ylim(min(ys) - rng*0.30, max(ys) + rng*0.32)

    # ---------- Panel C: floor vs altitude ----------
    axC = fig.add_subplot(gs[0, 1]); style(axC)
    labels, vals = [], []
    for lab, rg in ALT_BINS:
        if rg is None:
            sel = STATES[1][1]
        else:
            lo, hi = rg
            sel = (lambda lo, hi: lambda r, f: r['segment'] in (3,5) and f[0] >= AIR and lo <= r['alt_m'] < hi)(lo, hi)
        v = [r['rssi'] for r in subset(band, sel)]
        if len(v) >= 25: labels.append(lab); vals.append(med(v))
    axC.plot(range(len(vals)), vals, '-', color=BAND_C[band], linewidth=2.0, zorder=3)
    axC.plot(range(len(vals)), vals, 'o', color=BAND_C[band], markersize=8, zorder=4,
             markeredgecolor=SURFACE, markeredgewidth=2)
    for i, v in enumerate(vals):
        axC.annotate('%.0f' % v, (i, v), textcoords='offset points', xytext=(0, 11),
                     ha='center', color=INK, fontsize=9.5, fontweight='600')
    axC.set_xticks(range(len(labels))); axC.set_xticklabels(labels, fontsize=9)
    axC.set_ylabel('median noise floor (dBm)', color=INK2, fontsize=10)
    span = max(vals) - min(vals)
    axC.set_title('Floor vs altitude  (VTX on throughout)   total swing %+.0f dB' % span,
                  color=INK, fontsize=11.5, fontweight='600', loc='left', pad=10)
    axC.set_ylim(min(vals) - max(span, 2)*0.30, max(vals) + max(span, 2)*0.34)
    axC.text(0.0, -0.20, '* ground point is motors-idle, VTX on', transform=axC.transAxes,
             color=MUTED, fontsize=8.5, ha='left', va='top')

    # ---------- Panel B: per-channel spectrum ----------
    axB = fig.add_subplot(gs[1, :]); style(axB)
    axB.xaxis.grid(True, color=GRID, linewidth=0.8)
    RAMP = RAMPS[band]
    if hotspot:
        axB.axvspan(hotspot[0], hotspot[1], color=WASH, zorder=0)
    freqs = [fmhz(band, c) for c in range(nch)]
    lo_v, hi_v = 1e9, -1e9
    for i, (name, sel) in enumerate(STATES):
        sub = subset(band, sel)
        by = {}
        for r in sub: by.setdefault(r['chan'], []).append(r['rssi'])
        per = roll([med(by.get(c)) for c in range(nch)], 3)
        pts = [(f, v) for f, v in zip(freqs, per) if v is not None]
        lo_v = min(lo_v, min(p[1] for p in pts)); hi_v = max(hi_v, max(p[1] for p in pts))
        axB.plot([p[0] for p in pts], [p[1] for p in pts], '-', color=RAMP[i],
                 linewidth=2.0, label='%s  (n=%d)' % (name.replace('\n', ', '), len(sub)), zorder=3)
    # headroom above for the hotspot label, below for the legend
    rng = hi_v - lo_v
    axB.set_ylim(lo_v - rng * 0.30, hi_v + rng * 0.22)
    if hotspot:
        axB.annotate(hot_label, ((hotspot[0]+hotspot[1])/2, axB.get_ylim()[1]), ha='center',
                     va='top', color=INK2, fontsize=9.5, fontweight='600',
                     textcoords='offset points', xytext=(0, -4))
    axB.set_xlabel('MHz', color=INK2, fontsize=10)
    axB.set_ylabel('median noise floor (dBm)', color=INK2, fontsize=10)
    axB.set_title('Spectrum by condition — per-channel medians, 3-channel rolling',
                  color=INK, fontsize=11.5, fontweight='600', loc='left', pad=10)
    leg = axB.legend(loc='lower left', frameon=False, fontsize=9, ncol=3,
                     labelcolor=INK2, handlelength=1.8, columnspacing=1.6,
                     borderaxespad=0.4)
    fig.savefig(os.path.join(OUT, fname), facecolor=SURFACE)
    print('wrote', fname)
    plt.close(fig)

print('\n=== SAMPLE COUNTS PER CLASSIFICATION (per band; per-ch = samples per channel) ===')
print('%-34s %22s %22s' % ('classification', '900 MHz (40 ch)', '2.4 GHz (80 ch)'))
def counts(tag, sel):
    line = '%-34s' % tag
    for band in ('900', '2.4'):
        n = len(subset(band, sel)); nch = GRIDS[band][2]
        line += ' %22s' % ('n=%5d   %5.1f/ch' % (n, n / nch))
    print(line)
for name, sel in STATES:
    counts(name.replace('\n', ', '), sel)
print('  -- altitude bins (airborne by gyro, VTX on) --')
for lab, rg in ALT_BINS:
    if rg is None: continue
    lo, hi = rg
    counts('  alt %s' % lab,
           (lambda lo, hi: lambda r, f: r['segment'] in (3,5) and f[0] >= AIR and lo <= r['alt_m'] < hi)(lo, hi))
counts('TOTAL usable (any classification)', lambda r, f: True)
print()

make_figure('900', (925.0, 927.0), 'US915 LoRaWAN\ndownlink',
            'band900_conditions.png', '900 MHz (FCC915)')
make_figure('2.4', (2473.5, 2480.5), 'quiet top of band',
            'band24_conditions.png', '2.4 GHz (ISM2G4)')

# ---------- cross-band comparison ----------
fig = plt.figure(figsize=(9.6, 6.0), dpi=130, facecolor=SURFACE)
ax = fig.add_subplot(111); style(ax)
fig.subplots_adjust(left=0.10, right=0.97, top=0.80, bottom=0.12)
fig.text(0.10, 0.935, '900 MHz vs 2.4 GHz — the bands cross over with altitude',
         color=INK, fontsize=15.5, fontweight='600', ha='left')
fig.text(0.10, 0.885,
         "900's floor is set by the aircraft and barely moves with height; 2.4's is set by the environment and climbs 15 dB.",
         color=INK2, fontsize=9.5, ha='left')
for band in ('900', '2.4'):
    labels, vals = [], []
    for lab, rg in ALT_BINS:
        if rg is None: sel = STATES[1][1]
        else:
            lo, hi = rg
            sel = (lambda lo, hi: lambda r, f: r['segment'] in (3,5) and f[0] >= AIR and lo <= r['alt_m'] < hi)(lo, hi)
        v = [r['rssi'] for r in subset(band, sel)]
        if len(v) >= 25: labels.append(lab); vals.append(med(v))
    ax.plot(range(len(vals)), vals, '-', color=BAND_C[band], linewidth=2.0, zorder=3,
            label='%s MHz' % band if band == '900' else '2.4 GHz')
    ax.plot(range(len(vals)), vals, 'o', color=BAND_C[band], markersize=8, zorder=4,
            markeredgecolor=SURFACE, markeredgewidth=2)
    # direct-label where the two series are far apart, not at the converged end
    li = 2
    ax.annotate('900 MHz' if band == '900' else '2.4 GHz', (li, vals[li]),
                textcoords='offset points', xytext=(18, 30 if band == '900' else -24),
                color=INK2, fontsize=10.5, fontweight='600', ha='left')
    for i, v in enumerate(vals):
        ax.annotate('%.0f' % v, (i, v), textcoords='offset points', xytext=(0, 10),
                    ha='center', color=INK, fontsize=9)
    ax.set_xticks(range(len(labels))); ax.set_xticklabels(labels, fontsize=9)
ax.set_xlim(-0.4, len(labels) - 0.6)
ax.set_ylabel('median noise floor (dBm)', color=INK2, fontsize=10)
ax.set_xlabel('altitude  (ground point = motors idle, VTX on)', color=INK2, fontsize=10)
ax.legend(loc='lower right', frameon=False, fontsize=10, labelcolor=INK2, handlelength=1.8)
fig.savefig(os.path.join(OUT, 'band_crossover.png'), facecolor=SURFACE)
print('wrote band_crossover.png')
