# TX Spectrum Analyzer — Design

Turn an ELRS transmitter into a receive-only swept spectrum analyzer, driven from the
handset's `elrs.lua`. This is the single design document: goals and risk rules (§1–2),
architecture including the measurement engine (§3), control/transport/UI (§4–6), process
and history (§7–9), and portability, compatibility and upstreaming (§10–11).

**Status:** see [`STATUS.md`](./STATUS.md) — the orientation doc and the record of what has
actually been proven on hardware. **Build & flash runbook:** [`BUILDING.md`](./BUILDING.md).

**Base:** ELRS `4.1.0` (`a9d4a9cb`), branch `tx-spectrum-nomad-4.1.0`. Originally authored
2026-07-16 against `4.0.1` (`b5a5b5fd`) for the SuperG; the 4.1.0 port and the Nomad
cross-band bring-up followed. **Sections written before that port cite 4.0.1 line numbers
and are approximate on 4.1.0** — the reasoning is unaffected, the line numbers drift.

**Build flag:** `-DTX_SPECTRUM_SCAN` (off by default; flag-off builds are byte-identical to
stock — R1.1).

**Targets — all three hardware-validated:**

| Target | Radio | Bands | Sweep topology (§3.4) |
|---|---|---|---|
| **RadioMaster Nomad** | dual LR1121 | 900 + 2.4 | cross-band — one radio per band |
| **BETAFPV SuperG Nano** | dual SX1280 (Gemini) | 2.4 | same-band split — 2 bins/dwell |
| **RadioMaster Boxer** (internal) | single SX1280 | 2.4 | single-radio |

Between them they exercise every sweep topology the code supports. Per-target build
commands and layout picks are in `BUILDING.md`; validation evidence is in `STATUS.md`.
**All three are now validated against `tx-spectrum-pr` itself** — the branch that ships, not an
ancestor of it. See §11.2 for what was proven where.

## 1. Goal

Turn the TX module into a receive-only swept spectrum analyzer, driven from the
ELRS Lua script, rendering a live spectrum on the handset — 2.4 GHz, sub-GHz, or both
on a cross-band module. Purpose: see the local RF environment (WiFi APs, other links,
noise) before flying, and produce the data a future FHSS sub-band-selection feature
would consume.

**Still non-goals:** no config persistence (R2.1), no sub-band selection, no automatic
action on the data, no calibrated absolute accuracy (§3.6), no in-flight operation
(R1.4).

> **Shipped since v1 was scoped.** The original non-goal list also said *"no Nomad/LR1121
> support"* and the goal named 2.4 GHz only — both written 2026-07-16 when the SuperG was
> the sole target. The Nomad cross-band port is now hardware-validated (N0–N5) and is the
> primary platform, so those two are struck. The remaining non-goals above are unchanged
> and still binding — in particular **absolute accuracy is still not claimed**, and §3.6
> now has hardware evidence for why.

---

## 2. The three risk constraints, as testable design rules

These are the load-bearing requirements. Everything in §3 follows from them.

### R1 — Co-existence: near-zero risk to the firmware you fly

| Rule | Mechanism | Gate |
|---|---|---|
| R1.1 | Entire feature behind `-DTX_SPECTRUM_SCAN`, **off by default** | `nm` symbol count 0 in flag-off ELF; strengthened to a byte-identity proof (§9.1) |
| R1.2 | **Additive-only diff.** No deletions, no edits to existing logic except the enum (R1.3) and two switch/condition sites (§3.6) | `git diff --stat` shows ~0 deletions |
| R1.3 | Scan is a **mode state**, not a hook in the packet loop. Normal RF path is untouched — nothing runs during flight | code review + P1 |
| R1.4 | **Refuse to enter while armed**; abort on disarm→arm transition | P4 |
| R1.5 | **Exit by reboot.** No restore path to get wrong | P2 |

R1.3 is what makes this feature cheap to qualify. Anything that steals time inside a live
packet loop on a flying aircraft has to prove it did no harm — a statistical A/B over a
large sample, plus an ISR timing budget. Here the link is *intentionally* dead and the
aircraft is on the ground, so that entire class of gate does not apply. **There is no
in-flight do-no-harm phase in this plan**, and if a future change puts any part of the
sweep inside the packet loop, that is no longer true.

R1.5 deserves defending, since a clean teardown looks more "correct": a botched
restore could leave the radio on the wrong frequency or SF while still *looking*
connected — and you would fly it. A reboot guarantees a known-good state. WiFi
and BLE modes already exit this way (`TXModuleParameters.cpp:522`). v1 therefore
has **no restore code at all**, which is both less work and strictly safer.

### R2 — Rollback: reflashing the stock base release must be a non-event

**The rule: do not add config fields. Do not bump `TX_CONFIG_VERSION`.**

This is not stylistic. `src/lib/CONFIG/config.cpp:159-164`:

```c
// Can't upgrade from version <5, or when flashing a previous version, just use defaults.
if (version < 5 || version > TX_CONFIG_VERSION)
{
    SetDefaults(true);
    return;
}
```

`TX_CONFIG_VERSION` is `8U` (`config.h`), stored in NVS under key `tx_version`. **Re-checked
on 4.1.0 (2026-07-20): still `8U`, and the guard above is unchanged.** That is what makes
rollback version-agnostic here — 4.0.1 and 4.1.0 share a config version, so a config written
under either is readable by the other, in both directions.
If this feature bumps it to `9` and the user later reflashes stock firmware, the
stored `9 > 8` → **`SetDefaults(true)` silently wipes the entire TX config**:
model matches, packet rates, power levels, VTX settings, bind phrase. That is a
catastrophic rollback experience for a debug feature.

So: **all scan parameters live in RAM only**, initialised to defaults each boot.
Nobody needs their sweep step size to survive a power cycle. With zero config
touched, the NVS layout is byte-identical to stock, `tx_version` stays `8`, and
rollback is a pure reflash with no migration and no data loss — in **either**
direction, repeatedly.

Corollaries:
- **R2.1** — no new fields in `TxConfig` / `tx_config_t`.
- **R2.2** — the modified `elrs.lua` must degrade gracefully on stock firmware.
  It is an SD-card file and *will* outlive a firmware rollback. The spectrum
  folder simply won't be enumerated by a stock module (params are discovered, not
  hardcoded), so this should be free — but it is an explicit P6 test, not an
  assumption.
- **R2.3** — `connectionState_e` insertion (§3.1) never crosses a wire.
  Verified: `connectionState` appears in `devBackpack.cpp` only as local
  comparisons (`:446`, `:474`); it is not serialised to the backpack, the RX, or
  NVS. So renumbering states cannot desync a non-updated backpack or receiver.

### R3 — Hardware: never key the PA

The SuperG is a PA-equipped 1 W-class module. The dominant hardware risk for any
TX module is transmitting into a bad load or keying the PA unintentionally.

**Invariant: the scan path is receive-only and never enables the transmit
front-end.** `RFAMP.TXenable()` has exactly two call sites in the SX1280 driver:

- `SX1280.cpp:142` — `startCWTest()`
- `SX1280.cpp:490` — `TXnb()`

The scan path must never reach either. It uses only `SetFrequencyReg(..., doRx=true)`
→ `RXnb()` → `RFAMP.RXenable()` (`SX1280.cpp:533`). This is mechanically
checkable, not a matter of care:

- **R3.1** — enter scan mode via `POWERMGNT::setPower(MinPower)` first, matching
  the WiFi/BLE teardown idiom (`devWIFI.cpp:967`). **Corrected 2026-07-16:** this
  rule originally claimed "so even a latent bug transmits at minimum power". It
  does not. `SetOutputPower` only queues `pwrPending` (`SX1280.cpp:185-194`); the
  register write is `CommitOutputPower()`, reached from `TXnbISR` — the TX-*done*
  ISR (`SX1280.cpp:441`). The queued change lands *after* the next packet, so a
  stray TX would go out at the **pre-scan** power. `devWIFI` survives the same
  idiom only because `Radio.End()` follows immediately; we keep the radio alive
  (§3.3), so we inherit the idiom without its guard. `CommitOutputPower()` is
  private in all three drivers, so there is no local fix — and none is needed,
  because **R3.1 is not what enforces R3**. That is R3.1a:
- **R3.1a** — **`hwTimer` stays stopped.** This is the actual TX interlock.
  `timerCallback()` → `SendRCdataToRF()` → `Radio.TXnb()` has no
  `connectionState` guard (`tx_main.cpp:672`), so a stopped timer is the *only*
  thing suppressing TX in any mode state — including `wifiUpdate`. Three
  `hwTimer::resume()` sites are reachable during a scan and are guarded under
  `TX_SPECTRUM_SCAN`: `EnterBindingModeSafely()` (`tx_main.cpp:1066`),
  `OnPowerSetCalibration()` (`:1009`), `UARTconnected()` (`:751`, where the
  `resume()` sits *outside* the `if`). Bind is the concrete one: `devTXLUA.cpp:7`
  keeps Lua live whenever `connectionState < FAILURE_STATES`, which includes a
  scan **by design** (lcsCancel must work to exit) — so Bind is live too.
- **R3.2** — P2 asserts, on an SDR-less bench, that no carrier appears: the paired
  RX must show **zero** received packets and go to failsafe for the whole scan.
  A TX leak would show up as the RX staying linked.
- **R3.3** — receive-only means **no antenna requirement**. Scanning without an
  antenna is harmless (unlike TX), so bring-up carries no VSWR risk.
- **R3.4** — no config writes (R2) ⇒ no NVS/flash wear from repeated scans.

### Invariant quick reference

Re-verified against the tree on 2026-07-20. **Any change to the sweep must be checked
against all of these.** The three most often got wrong are marked ⚠.

| # | Invariant | Enforced by | Status |
|---|---|---|---|
| R1.1 | Flag-off image byte-identical to stock | everything under `#if defined(TX_SPECTRUM_SCAN)` | ✅ N0 / P1 |
| ⚠ R1.2 | Additive-only diff — no deletions from flight code | review; `LR1121.cpp` is byte-identical to stock | ✅ §9.2 |
| R1.3 | Scan is a mode state, not a packet-loop hook | `connectionState == spectrumScan` | ✅ §3.1 |
| R1.4 | Never scan while armed; abort if armed mid-scan | event **and** level check in `timeout()` | ✅ N4 / P4 |
| R1.5 | Exit by reboot; no teardown path | `scheduleRebootTime()` | ✅ N5 |
| R2.1 | No config fields, no `TX_CONFIG_VERSION` bump | scan state is file-static RAM | ✅ P5 |
| R2.2 | `elrs.lua` degrades gracefully on stock firmware | params are discovered, not hardcoded | ✅ N5 / P5 |
| R2.3 | `connectionState_e` insertion never crosses a wire | not serialised to backpack, RX or NVS | ✅ audit |
| R3 | **Receive-only — the PA is never keyed** | no TX-class opcode on any sweep path | ✅ N2 / P2 |
| R3.1 | Power dropped to minimum at entry | `POWERMGNT::setPower(MinPower)` | ⚠ **gesture, NOT the interlock** |
| ⚠ R3.1a | **`hwTimer` stays stopped** — the real TX interlock | three guarded `resume()` sites | ✅ §9.5 |
| R3.3 | No antenna required ⇒ antenna-off is a valid diagnostic | receive-only | ✅ used to disprove external RF |
| R3.4 | No config writes ⇒ no NVS wear | RAM-only | ✅ follows R2.1 |
| S1 | Blocking is **budgeted in time, not counted in bins** | `TX_SPECTRUM_CHUNK_BUDGET_US` = 1300 µs | ✅ absorbed a 4× settle rise |
| ⚠ M1 | **Per-bin AGC reset and the settle move together** | `SweepChunk` + `RssiSettleUs` | ✅ 2026-07-20 |

**R3.2 is deliberately absent** — it is a *test method*, not an invariant: it is how R3 is
demonstrated (a paired RX must show zero packets and failsafe for the whole scan).

S1 and M1 are measurement-engine invariants rather than risk rules, hence the separate
lettering. R1–R3 are the risk argument; these two are what keep the measurement honest.

---

## 3. Architecture

### 3.1 Mode state

Add one value to `connectionState_e` (`src/include/common.h:36-53`). The enum is
**ordered and the ordering is load-bearing** — `MODE_STATES`,
`NO_CONFIG_SAVE_STATES`, `FAILURE_STATES` are sentinels, and every `<`/`>`
comparison in the tree derives meaning from position. This is the single
highest-risk line in the feature.

Required semantics for `spectrumScan`: `> MODE_STATES` (halt normal RF),
`> NO_CONFIG_SAVE_STATES` (block config commits during scan — conservative, and
we save nothing anyway), `< FAILURE_STATES` (not a hardware failure).

**Insert immediately after `serialUpdate`, before `FAILURE_STATES`:**

```c
    NO_CONFIG_SAVE_STATES,
    wifiUpdate,
    serialUpdate,
#if defined(TX_SPECTRUM_SCAN)
    spectrumScan,           // <-- here
#endif
    // Failure states go below here to display immediately
    FAILURE_STATES,
```

This position yields semantics identical to `wifiUpdate` while renumbering the
fewest existing values (only `FAILURE_STATES`, `radioFailed`,
`hardwareUndefined` shift by 1, and per R2.3 none of them cross a wire).

> Note the `#if` inside the enum: with the flag off, the enum is numerically
> identical to stock. This matters for R1.1's zero-footprint proof.

### 3.2 The seam — where the sweep runs

`tx_main.cpp:1526` hard-returns when `connectionState > MODE_STATES`. The obvious
template for pushing unsolicited data to the handset —
`checkSendLinkStatsToHandset()` (`tx_main.cpp:1391-1403`) — is called at `:1535`,
**below that return**. Copying its call site would produce code that never runs.

What still runs *above* the return: `HandleUARTout()` (`:1497`),
`devicesUpdate(now)` (`:1512`), `executeDeferredFunction()` (`:1522`),
`HandleUARTin()` (`:1524`). **That is the seam.** CRSF to/from the handset keeps
flowing while the RC link is dead.

So the sweep lives in a new `device_t` whose `timeout` returns a real tick
interval, driven by `devicesUpdate()` — modelled on `devBLE.cpp:83-90`, not on
`devTXLUA.cpp` (which returns `DURATION_NEVER` and is purely event-driven).

### 3.3 The measurement engine

The bin grid comes free from the FHSS config (`FHSS.cpp`): `freq_start` / `freq_stop` /
`freq_count` / `freq_center` are already per-domain. **The bin grid *is* the FHSS channel
grid** — bin *n* is `freq_start + spread * n / FREQ_SPREAD_SCALE`, the same expression
`FHSS.h` uses to place hop channels. That is deliberate: the plot's x-axis is already the
axis a future sub-band picker selects over, so scan and select share a coordinate system
and no resampling ever has to be reconciled. It also bounds the problem — the widest single
band that exists is 2.4 GHz's 80 channels, which is why `TX_SPECTRUM_MAX_BINS` is 80 and
why per-band screens are the right decomposition (§10.1).

#### 3.3.1 The per-bin cycle

```
  [LR1121 only]  SpectrumResetRx(radio)                 -> STDBY_RC   (3.3.3)
                 SetFrequencyReg(freq, radio, doRx=true)               (3.3.2)
                 delayMicroseconds(settleUs)                           (3.3.4)
  [LR1121 only]  StartRssiInst(radio)
                 rssi = ReadRssiInst(radio)
                 StoreBin(bin, rssi)      // LNA gain backed out, max-hold folded in
```

`ReadRssiInst` is the family shim — SX1280 `GetRssiInst`, SX127x `GetCurrRSSI`, LR1121 a
two-step `StartRssiInst` first.

**Blocking is budgeted, not counted (S1).** `positionsPerCall` derives from
`TX_SPECTRUM_CHUNK_BUDGET_US` (1300 µs) rather than being a fixed bin count, because
`settleUs` spans 80 µs to 1000 µs across families — a fixed count would block anywhere from
0.6 ms to 3.8 ms. Budgeting time keeps every `timeout()` call flat regardless of settle,
which is what let the LR1121 settle rise 4× (§3.3.3) with no other constant re-tuned.
Blocking here is normal for this framework (`devBackpack.cpp` does `delay(100)`) and safe:
`hwTimer` is stopped and only `HandleUARTin()` is waiting, accruing ~8 bytes in 1.3 ms
against a 256-byte ISR-filled ring.

#### 3.3.2 `doRx=true` is mandatory — and the trap it avoids

The abandoned upstream branch `adds-noisefloor-measure-feature` ends on the commit *"works
on 900 but not on 2.4"*. Its bug: it hoisted `SetMode(RX)` out of the loop and wrote only
`SET_RFFREQUENCY` inside it. On a command/state-machine radio a frequency write takes
effect only when the receiver next enters FS/RX, so its RSSI stayed pinned to the first
frequency. SX127x retunes on a direct register write — hence "works on 900."

`SetFrequencyReg(..., doRx=true)` writes the frequency then re-enters RX, re-issuing
`SET_RX`. On SX1280 this works **only** because the early-return guard in `SetMode` is
commented out (`SX1280.cpp:209-216`):

```c
// if (OPmode == currOpmode)
// {
//    return;
// }
```

> ⚠ **If anyone ever "optimises" that guard back in, FHSS hopping and this sweep both
> break — and the sweep breaks *silently*, reporting bin 0's RSSI across every bin.** The
> "distinct per-bin RSSI" check (N2/P2) is the regression test for exactly this, and the
> sweep site carries a comment pointing at those lines.

#### 3.3.3 AGC gain carryover — why there is a per-bin reset

**The defect.** Left alone, the sweep enters RX continuous and never leaves it, retuning
per bin only. **RX_CONT does not re-run gain acquisition on a bare retune**, so a gain step
the AGC picks for one bin persists into every bin measured after it. Once it steps down,
every subsequent bin reads that step's gain-limited floor.

Observed on the Nomad as the entire 915 trace lifting ~20 dB to a flat, peakless
**-83…-85 dBm** for seconds at a time, then recovering. Evidence for that diagnosis over
"there is real energy there":

- **The level is a fingerprint.** LR1121 UM §7.2.15's per-gain-step table places the top
  gain steps at **-81…-87 dBm**, under 1.5 dB apart. A stepped-down AGC's floor lands
  exactly in the observed band; broadband noise has no preferred level.
- **The sibling part has it as published errata.** RadioLib documents SX126x reporting RSSI
  **up to 35 dB high**, self-recovering, cured by restarting the AGC. (No LR11xx errata
  document exists either way — this is analogy, not citation.)
- **Elimination.** External RF was ruled out by scanning with the antenna detached, which
  R3.3 makes safe: the lift *and the whole floor* were unchanged. Full table in `STATUS.md`.

Suspected trigger is a false preamble detect (LoRa CRC is off, so noise can raise
detection; Semtech's gateway HAL exposes `AGC_FREEZE_ON_DETECT`, default on). **Inference,
and not needed** — the fix addresses carryover regardless of what starts it.

> ⚠ **M1 — the reset and the settle are a matched pair.** `SpectrumResetRx()` forces
> `STDBY_RC` so the following RX entry re-acquires gain. That entry now costs a PLL relock,
> so `RssiSettleUs()` returns a flat **1000 µs** for LR1121. **Shortening the settle without
> removing the reset reintroduces the lift; removing the reset without shortening the settle
> merely wastes time.** Hardware-confirmed on the Nomad 2026-07-20.

Applied on **both** bands, not just the one that showed the symptom: gain carryover is a
measurement error wherever it occurs; 2.4 GHz simply never made it visible. SX1280 paths are
left exactly as validated (§11.1 explains why that is the right call rather than timidity).

#### 3.3.4 Settling time

Reading before the receiver has settled returns garbage, so this is not optional.

| Family | Settle | Provenance |
|---|---|---|
| SX1280 | SF5→100 µs, SF6→141, SF7→218, else 480; FLRC 80 | LBT bench data (`SpreadingFactorToRSSIvalidDelayUs`) |
| SX127x | 240 µs | inherited, **unmeasured** |
| **LR1121** | **1000 µs flat** | Semtech `SWSD003` `spectral_scan` |

The LR1121 value deliberately ignores SF and modulation. The inherited LBT figures measure
only *RSSI-valid time on an already-running receiver* — a strictly smaller quantity than
this cycle needs, since §3.3.3's per-bin STDBY→RX must also cover PLL relock plus gain
re-acquisition. No characterised number exists for that on this part, so the reference
implementation's figure is used rather than a guess dressed up as SF-aware:
`DELAY_BETWEEN_SET_RX_AND_VALID_RSSI_MS = 1` in Semtech's own LR11xx spectral-scan example
is as close to authoritative as this gets.

**Sensing config, not link config.** The scan need not use the configured air rate —
configuring for the shortest settle is legitimate, and since we reboot to exit, clobbering
the radio config is free. `Config()` is called per band on cross-band devices because
crossing 900↔2.4 on an LR1121 is not a bare frequency write: it selects the PA path and RF
switch. Sync words are irrelevant — we read instantaneous RSSI and never demodulate.

### 3.4 Sweep topologies

| Topology | Condition | Behaviour |
|---|---|---|
| **Single-radio** | one radio, or a cross-band device | `positions == totalBins`; the band-matched radio sweeps |
| **Same-band split** (Gemini) | `isDualRadio() && !hasSecondBand` | radio 1 takes bin `i`, radio 2 bin `i + n/2` → **2× sweep rate** |
| **Cross-band** (Nomad) | `isDualRadio() && hasSecondBand` | one band-matched radio per band; the other is idle. **Not** a split |

The split falls out of a quirk: `RXnb()` takes no radio argument — it forces **both** radios
into RX. That looks like a limitation and is an advantage, and it is structurally identical
to what Gemini already does in flight.

> **Antenna-compare is designed but NOT built.** Putting both radios on the *same* bin would
> show a per-antenna delta — an instant feedline/antenna fault check, arguably more useful
> day-to-day than the spectrum view itself — and it is the same sweep with one `if` changed.
> `TX_SPECTRUM_FLAG_MODE_COMPARE` / `_RADIO_2` are reserved in the protocol for it, and
> `TxSpectrumProtocol.h` carries the same warning: **the flags existing is not evidence the
> mode does.** Nothing sets them today. It applies only to same-band Gemini, not to
> cross-band targets, whose radios cannot see the same frequency.

The cross-band case is why `activeRadio` exists and why `scanBand` selects the band
*explicitly* rather than reading the link's active-band globals: the page button must be able
to scan a band the link is not using.

**Sweep rate**, which the AGC reset changed materially:

| Target | Bins | Per bin | Sweep | Rate |
|---|---|---|---|---|
| SuperG (split, SF5) | 80 ÷ 2 radios | ~160 µs | ~6.5 ms | ~150 Hz |
| Nomad 900 | 40 | ~1 ms + relock | ~50 ms | **~20 Hz** |
| Nomad 2.4 | 80 | ~1 ms + relock | ~100 ms | **~10 Hz** |

Every one of these still outruns its own display update — 5 Hz for an 80-bin band, 10 Hz
for a 40-bin one (§5) — so the live trace stays fed. The real cost is that TX-side max-hold
integrates fewer sweeps per update, which weakens — but does not reverse — §3.5.

### 3.5 Traces — max-hold is mandatory, not a nice-to-have

A swept single-bin receiver mostly *misses* bursty traffic. A single sweep of a busy band
looks empty; WiFi beacons are ~102 ms apart. **Ship max-hold from day one or the feature
will look broken while working perfectly.**

**Max-hold is accumulated on the TX and must stay there.** It looks like removable wire
traffic — the handset could derive it from the live trace and halve both the frames and its
own GC load — but the handset only sees a trace at 5–10 Hz (§5) while the TX sweeps faster.
A handset-side max-hold would miss the sweeps in between and so fail at exactly the
bursty-traffic job it exists for. Note §3.4 narrowed this margin substantially on LR1121
(~30× on the SuperG, exactly ~2× on the Nomad on both bands); the argument still holds but is no longer
overwhelming.

### 3.6 Calibration — better than "relative only", short of absolute

ELRS already carries a per-target receive-path gain figure, mandatory on every SX1280
target:

```c
#define LBT_RSSI_THRESHOLD_OFFSET_DB hardware_int(HARDWARE_power_lna_gain)
```

`power_lna_gain` is a **dB value, not a GPIO** (12 dB on both the Gemini layout and the
Nomad). Reporting `rssi - power_lna_gain` gives roughly antenna-referred dBm rather than
raw front-end numbers. Where it is absent, `lnaGainDb == 0` and the axis is raw front-end
dBm.

**What the numbers are not.** Accuracy is ±several dB — RSSI is uncalibrated and
RBW-dependent — so this is a decent *relative* instrument with a plausible absolute axis,
not a lab spectrum analyzer, and the UI should say so. Three specific limits, the first two
surfaced during Nomad bring-up:

- **The idle floor is receiver-noise-limited, not ambient.** With the antenna detached the
  whole floor was unchanged, so the sloped top-of-band rise is the receiver's own frequency
  response rather than signals. Real emitters land on the right bin at the right *relative*
  level; treat absolute dBm as uncalibrated.
- **`SET_RSSI_CALIBRATION` (0x0229) is never called anywhere in ELRS.** Per the LR1121 UM
  the factory default is calibrated for 868–915, so the sub-GHz path is roughly right and
  2.4 GHz carries a static offset. This is the first thing to pull on if absolute accuracy
  is ever wanted.
- **`CalibImage` spans only the primary band's range**, so a radio retuned to the other band
  may read slightly hot or cold. Correctness, not safety.

#### 3.6.1 Frequency accuracy — a separate axis from amplitude

Everything above is about the **y** axis. The **x** axis has its own error budget, and it is
coarser than the readout's one decimal place suggests.

**Resolution is the FHSS channel step, by construction** (§3.3): 600 kHz on FCC915, 1 MHz on
ISM2G4. There is nothing between bins — a signal anywhere inside a bin is reported *at that
bin's centre*. So the worst-case frequency error for a peak is **±300 kHz on FCC915 and
±500 kHz on ISM2G4**, and that is inherent to sampling the hop grid rather than a defect.

**No shipping grid lands on an integer MHz.** FCC915 bins are `903.5 + 0.6n`, ISM2G4 bins are
`2400.4 + 1.0n`. Every bin centre therefore has a fractional part, which is why the cursor
readout carries **one decimal** (`914.9MHz`, not `915MHz`). An integer label is wrong for
*every* bin on both grids: truncating understates by up to 0.9 MHz on FCC915 — more than that
band's own bin step, so two adjacent bins could print the same number — and rounding merely
relocates the error.

> **The decimal identifies the bin. It does not claim 100 kHz accuracy.** Reading `914.9MHz`
> means "the bin centred at 914.9", i.e. a signal somewhere in 914.6–915.2. Do not read the
> decimal as measurement precision — it is exact about *which* bin and coarse about *where in
> it* the energy sits.

**RBW is not the bin step.** The sensing bandwidth comes from the air-rate config, is
generally wider than the step, and is not swept in sympathy with it — so a single strong
carrier lights up neighbouring bins too. That skirt is the receiver's filter shape, not a
family of real signals, and it is the reason N3/P3 checks that a CW tone lands on the right
bin rather than that it lands on *only* one.

**Consequence for the CW test:** `freq_center` need not be a bin centre, and on FCC915 it is
not — 915.000 MHz sits 100 kHz from the 914.9 bin. A tone reading one bin off-centre is the
grid doing its job. See §11.2.

---

### 3.7 The backpack will pollute the measurement

Both the SuperG and the Nomad set `use_backpack: true` — an ESP8285 running WiFi/ESPNOW
**on 2.4 GHz, centimetres from the radio we are measuring with**. Left running, we would
measure our own backpack.

`devBackpack.cpp` disables the backpack via an **equality** check, which a new state is not
covered by (`> MODE_STATES` does not reach it):

```c
disabled = disabled || connectionState == spectrumScan;   // <-- added by this feature
```

**Done** — this is one of only two edits to existing logic (the other being the enum), and
it is a **measurement-correctness requirement**, not a nicety. Confirmed on the Nomad: the
backpack `EN` pin is pulled low for the duration of a scan.

### 3.8 Two existing switch statements

Mode states are consumed by UI devices. Adding a `case` is additive; omitting it
means the UI silently does nothing:

- `devRGB.cpp:445-457` (`case wifiUpdate/serialUpdate/bleJoystick/noCrossfire`)
- `devLED.cpp:175-209`

Give `spectrumScan` its own LED pattern so it is unmistakable that the link is
intentionally down.

### 3.9 Intervention points — the audit surface

Everywhere this feature reaches into radio state. If you are reviewing the radio-facing
risk, this table is complete.

| # | Point | Call | Purpose | Family |
|---|---|---|---|---|
| **I1** | scan entry | `hwTimer::stop()` | R3.1a — the TX interlock | all |
| **I2** | scan entry | `POWERMGNT::setPower(MinPower)` | gesture only — **not** what enforces R3 | all |
| **I3** | scan entry | `detachInterrupt(DIO1[, DIO1_2])` | sweep takes exclusive SPI ownership | LR1121 |
| **I4** | band select | `Radio.Config(..., activeRadio)` | cross-band retune: PA path + RF switch; leaves `STDBY_RC` | LR1121 |
| **I5** | per bin | `SpectrumResetRx()` → `SetMode(STDBY_RC)` | AGC gain re-acquisition (§3.3.3) | LR1121 |
| **I6** | per bin | `SetFrequencyReg(f, radio, doRx=true)` | retune + RX entry, one opcode (§3.3.2) | all |
| **I7** | per bin | `delayMicroseconds(settleUs)` | PLL relock + settle (§3.3.4) | all |
| **I8** | per bin | `StartRssiInst()` / `GetRssiInst()` | the measurement | all |

**On I2** — see R3.1: dropping power is a gesture, not an interlock, because the register
write happens in the TX-*done* ISR. R3.1a is the real guarantee. Do not mistake I2 for
protection.

**On I3** — this closes a real unsynchronised-SPI race (an async RX-done ISR transacting on
the radio the sweep is mid-read on) but it **fixed no observed symptom**. It was written for
a 2026-07-19 diagnosis of the 915 floor lift that testing refuted; the cause was §3.3.3. It
is kept on its own merits. **Do not cite it as the lift fix** — `STATUS.md` records the full
elimination trail.

---

## 4. Lua parameters (TX → handset control)

Headroom is fine: `MAX_CRSF_PARAMETERS` is 64 (`CRSFEndpoint.h:6`), TX currently
registers 36.

Add one folder + params in `TXModuleParameters.cpp`, registered in
`registerParameters()` (`:799`). All RAM-only (R2.1):

| Param | Type | Notes |
|---|---|---|
| `Spectrum` | `folderParameter` | |
| `Start Scan` | `commandParameter` | reuses `handleWifiBle`, already parameterized for "enter a mode that drops the link" |

Scan entry is not a new state machine: `handleWifiBle` was *already* parameterized
by `(setTargetState, targetState, textConfirm, textRunning)` and selected on item
identity, so the spectrum mode is a third branch plus one `requireDisarmed` axis.
Subscribe to `EVENT_ARM_FLAG_CHANGED` (`TXModuleEndpoint.cpp:151`, ESP32-only —
fine, SuperG is ESP32) to abort a running scan.

> **There is deliberately no `Reset Max-Hold` parameter.** An earlier draft
> registered one. It cannot work: it is unreachable from the field list *while
> scanning* (the plot owns the screen, and leaving it reboots the TX), and a
> **no-op when not scanning**, because `BeginScan()` clears max-hold
> unconditionally anyway. Useless in both states. Instead, re-invoking the
> running `Start Scan` means "restart the measurement" — which *is* a max-hold
> reset — and `TxSpectrumStart()` handles that with an early return. The handset
> binds it to ENTER using the field id it already stashed for EXIT, so there is
> no name lookup and no second wire contract.

> **Refusing while armed uses `lcsExecuting`, not `lcsCancel`, and §4 of an
> earlier draft was wrong about why.** `elrs.lua`'s `runPopupPage` renders only
> popup statuses 0/2/3; an `lcsCancel` (5) response matches no branch, draws
> nothing, and leaves a frozen screen until RTN. `lcsExecuting` is the only status
> that shows the reason *and* offers "Press [RTN] to exit", and the `lcsQuery`
> echo repeats it until the user does. RTN then lands in `lcsCancel` → `lcsIdle`
> → popup closes, and cannot reboot because `connectionState` never became
> `spectrumScan`.
>
> The earlier plan — "surface the reason via `LUA_FLAG_ISARMED`" — is **not
> implementable**. That flag is recomputed from `handset->IsArmed()` on every
> status send (`TXModuleParameters.cpp:411`); it is derived state, not a settable
> event channel, so writing it from a refusal path is a no-op. It is also already
> redundant: armed already raises `elrsFlags` bit 3, which blinks
> `"[ ! Armed ! ]"` in the Lua title bar for free. The scan screen renders that
> warning too (§6.0) — which matters, because arming aborts the scan by reboot.

---

## 5. Transport: TX → handset

**The param system cannot do this.** Every `CRSFEndpoint` send is a response to
an inbound frame; there is no unsolicited param push. So: a custom frame type.

Use **`0x30`** — free in the ELRS enum, inside the extended-header range 0x28–0x96, and
adjacent to the existing ELRS block.

**EdgeTX forwarding: verified, not assumed.** Checked against EdgeTX source at
tag **v2.12.1** — the version on the target handset (RadioMaster Boxer, B&W).
The six load-bearing files are byte-identical across `v2.12.1`, `v2.12.2` and
branch `2.12`, so this covers the whole 2.12 line.

There is **no whitelist** — the semantics are the inverse. `crossfire.cpp`
switches on `id = rxBuffer[2]` and gives a `case` only to types it knows, each of
which `break`s **without** reaching Lua. Everything else falls to `default:`
(`crossfire.cpp:414-441`, v2.12.1):

```c
#if defined(LUA)
    default:
      if (id == DEVICE_INFO_ID && rxBuffer[4] == MODULE_ADDRESS) {
        ...   // DEVICE_INFO parsed inline, then STILL falls through to the push
      }

      // destination address and CRC are skipped
      pushTelemetryDataToQueues(rxBuffer + 1, rxBufferCount - 2);
      break;
#endif
```

So *known* types are consumed and hidden from Lua; *unknown* types are
forwarded. The 16 consumed types in v2.12.1 are `0x02, 0x03, 0x07, 0x08, 0x09,
0x0A, 0x0C, 0x0D, 0x0E, 0x14, 0x16, 0x1C, 0x1D, 0x1E, 0x21, 0x3A`.

**`0x30` and `0x31` are unclaimed** — grep across v2.12.1 `crossfire.cpp` +
`crossfire.h` returns zero hits for either. The nearest neighbours are
`REQUEST_SETTINGS_ID 0x2A` and `COMMAND_ID 0x32`; 0x30/0x31 sit in the gap.

**This is not version-fragile, and the trend is favourable.** EdgeTX has been
steadily claiming newly-standardised types — 2.11 added `0x0A, 0x0C, 0x0D, 0x0E,
0x16`; main added `0x03` — but every addition landed *below* `0x14`. Nothing has
encroached on the `0x2E`-`0x32` region, and the `default:` block is byte-identical
between 2.11 and v2.12.1. Note the direction of the hazard: a type gaining a
`case` means it becomes **consumed and hidden from Lua**, so if `0x30` were ever
claimed upstream, our frames would silently stop arriving. That is the failure
mode to watch for, and it argues for settling §8.1 before the format is frozen.

Address filtering applies to byte[0] only (`_validHdr`, `pulses/crossfire.cpp:235-238`,
must be `0xEA` or `0xC8`). The extended-header dest/orig (`rxBuffer[3]`/`[4]`)
are read **only** inside the `DEVICE_INFO_ID` branch — for `0x30` they are never
inspected. Our `0xEA`/`0xEE` passes. Gate order: `_validHdr` → `_lenIsSane` →
`_checkFrameCRC` → dispatch. (Mechanism change from 2.8.4, same outcome: 2.8.4
*rejected* a bad first byte; v2.12.1 *resyncs* by scanning forward. More
forgiving, not stricter.)

Length: `_lenIsSane` requires `len > 2 && len < 127` and
`TELEMETRY_RX_PACKET_SIZE` is 128. Our len field is 56 — nothing truncates it.
The ceiling is ~121 payload bytes after the 5-byte ext header, so there is room
to grow.

> ⚠️ Still open: absence from the *ELRS* enum ≠ unclaimed in the wider TBS/CRSF
> ecosystem. EdgeTX will forward `0x30` regardless, but **check the TBS CRSF
> spec before freezing the wire format** so we don't collide with a future TBS
> allocation (§8.1).

```
CRSF_FRAMETYPE_ELRS_TX_SPECTRUM = 0x30
  ext header (dest=0xEA RADIO_TRANSMITTER, orig=0xEE CRSF_TRANSMITTER)
  u8  version
  u8  flags            // trace id + SWEEP_END; compare-mode bits reserved (§3.4)
  u8  sweepSeq         // wraps; constant across all frames of one trace's sweep
  u8  binOffset        // absolute index of the first bin in this frame
  u8  binCount         // bins in this frame
  u8  totalBins        // full sweep: 80 on 2.4, 40 on FCC915
  u32 startFreqKhz     // BIG ENDIAN -- centre frequency of bin 0
  u16 stepKhz          // BIG ENDIAN -- spacing between bin centres
  i8  rssi[binCount]   // dBm, antenna-referred (LNA gain subtracted)
```

12-byte header + up to 40 bins = 52 payload bytes, inside the 58-byte ceiling
(`CRSF_MAX_PACKET_LEN` 64 − 5 ext header − 1 CRC); the CRSF `len` field reads 56.
**The frequency axis is repeated in every frame** — deliberate, so a decoder joining
mid-sweep is never left guessing and a dropped frame cannot silently shift the axis.
40 bins/frame makes an 80-bin 2.4 sweep exactly two frames and a 40-channel FCC915 sweep
exactly one. Emit via
`SetExtendedHeaderAndCrc()` + `deliverMessageTo(CRSF_ADDRESS_RADIO_TRANSMITTER, ...)`
(`CRSFRouter.h:77,104`), called **from the scan `device_t`** (above the `:1526`
return, per §3.2) — *not* from `checkSendLinkStatsToHandset`'s call site.

**Pace one frame per ~50 ms. Do not burst a sweep.** This is a hard constraint,
not tuning. EdgeTX's Lua telemetry queue is **256 bytes, 255 usable**
(`LUA_TELEMETRY_INPUT_FIFO_SIZE`, `telemetry/telemetry.h:237-238` @ v2.12.1 —
unchanged since 2.8.4). `hasSpace(n)` is `N > (size() + n)`, so our 56-byte
frame is accepted while the queue holds <=199 bytes: **~4 frames of headroom.**

**On the Boxer this queue is shared, which is what makes pacing load-bearing.**
B&W targets use a single global `luaInputTelemetryFifo` that ELRS's own
`0x29`/`0x2B`/`0x2D`/`0x2E` traffic also lands in. (COLORLCD is the lucky case:
from 2.11 a tools script runs under `LuaScriptManager` and gets a *private*
per-script queue, so it never contends — but that does not help us here.)
Emitting a full update as a burst (live + max-hold = 4 frames = 224 bytes) would
nearly fill the shared queue in one go and risk starving ELRS's param traffic.

There is a second, tighter reason the FIFO argument misses: **every frame we send
makes EdgeTX mint a fresh ~54-entry Lua table** (`luaCrossfireTelemetryPop` does
`lua_newtable` plus a `settable` per byte). That is ~570-1080 bytes of garbage per
frame, on a script that already forces a full `collectgarbage("collect")` on every
string parse (`elrs.lua:168`). At 25 ms that was ~23-43 KB/s sustained; 50 ms
halves it.

Paced at one frame per ~50 ms, at most one frame is ever in flight,
and a full live+max-hold update lands at **10 Hz for a 40-bin band (2 frames) or 5 Hz for
an 80-bin band (4 frames)** — at or under what the Lua tick rate (~20-30 ms) can consume
anyway. (§10.1's table is the authority on this; an earlier draft of this paragraph said
"10 Hz" for the 80-bin case, which is wrong by a factor of two.) The sweep itself continues
far faster underneath (§3.4); max-hold is what carries the information between updates
(§3.5), so a slow transport costs nothing.

Overflow behaviour is benign: `hasSpace(n)` is checked before any push, so a
full FIFO drops **whole frames atomically** rather than truncating one. No
stream desync is possible. Combined with `sweepSeq` + `binOffset` + the axis
repeated in every frame (§5 payload), a dropped frame is both detectable and
self-healing — the next frame reconstructs its own position with no state
carried across the gap.

> **Queue arming — still true on v2.12.1.** The queue is lazily allocated and
> stays `NULL` until Lua's **first** `crossfireTelemetryPop()` call
> (`getTelemetryQueue()`, `api_general.cpp:894-910`), and `pushDataToQueue`
> guards `if (queue && queue->hasSpace(length))` (`telemetry.cpp:470-477`), so
> frames arriving before that first pop are **silently dropped**. This was not
> refactored away in 2.11/2.12 — both the COLORLCD and B&W branches allocate on
> first pop. Harmless for us: `elrs.lua` pops from its very first `run()` tick
> (`:529-550`), long before a scan can be started. But **the TX must never be
> the first mover**, and any standalone test script must pop once before
> expecting data.

Make the codec header-only `static inline` with no Arduino, driver or CRSF deps. That is
what makes it natively unit-testable — and what let P0 pass before any `.cpp` existed.

---

## 6. Handset Lua

This was the **first custom full-screen renderer in `elrs.lua`** — there was no graph mode
and no bind/wifi custom screen to copy, since command params delegate to the firmware's
`popupConfirmation`. (The script was ~958 lines when this was written and is larger now;
§9.4's heap figures are the ones that matter, not the line count.)

Constraints, all load-bearing:

- **Redraw is event-gated.** `run()` only redraws on `event ~= 0 or forceRedraw or edit`.
  An animating graph **cannot** rely on `forceRedraw`; it needs its own redraw path.
- **Memory is tight.** There is a `collectgarbage("collect")` smuggled into a
  return tuple in the string parser (`elrs.lua:168`), and dead functions are
  nil'd out to reclaim space (`:837-840`, `:936-937`). 80 bins as a flat table is
  fine; the drawing code must be lean.
- **No framebuffer.** No `luaLcdBuffer` anywhere in the repo — immediate-mode only.
- **Screen detection is a feature probe**, not resolution: `lcd.RGB ~= nil`
  (`:829`, `:849`). Geometry then derives from `LCD_W`/`LCD_H`; the script already
  distinguishes 128x64 / 212x64 / 128x96 mono and colour.
- Pop `0x30` in the existing drain loop (`:529-550`), alongside `0x2E`. Keep the
  `repeat ... until command == nil` shape — with only ~4 frames of FIFO (§5),
  draining aggressively every tick is what prevents overflow.
- **Payload offsets, verified against EdgeTX v2.12.1** (`api_general.cpp:1153-1176`):
  EdgeTX strips byte[0] (address) and the CRC
  (`pushTelemetryDataToQueues(rxBuffer + 1, rxBufferCount - 2)`), so Lua sees

  | Lua | contents |
  |---|---|
  | `command` | `0x30` |
  | `data[1]` | `0xEA` — extended dest |
  | `data[2]` | `0xEE` — extended orig (= `deviceId`, matches `elrs.lua:10`) |
  | `data[3..54]` | the 52 payload bytes of §5 |

  So `TxSpectrumProtocol.h`'s `payload[0]` (version) is Lua's `data[3]`, and the
  bins begin at `data[3 + TX_SPECTRUM_HEADER_BYTES]` = `data[15]`. This mirrors
  the convention the script already uses — `parseElrsInfoMessage` checks
  `data[2] ~= deviceId` (`elrs.lua:497`).

### 6.0 Implemented 2026-07-16

`elrs.lua` (r16) now carries the view. ~95 lines added, all inert unless a `0x30`
frame arrives — so the script still works unchanged against stock firmware
(R2.2). Additions: `spectrum` state + `specY()` + `parseSpectrumMessage()` before
`refreshNext()`, a `0x30` arm in the existing pop loop, `drawSpectrum()` +
`runSpectrumPage()` before `run()`, and one branch in `run()`.

**Entry is implicit.** There is no "open the spectrum view" action: the first
`0x30` frame *is* the trigger. `parseSpectrumMessage` allocates the state, stashes
`fieldPopup.id` (the Start Scan command) so EXIT can cancel it, then clears
`fieldPopup` — taking the screen over from the "Scanning..." popup that the
command already put up. Nothing else needed wiring.

**Keys:** PREV/NEXT move the cursor (wrapping), ENTER resets max-hold by
re-invoking Start Scan, EXIT sends `lcsCancel` → the TX reboots → link returns.
`spectrum = nil` on exit drops both 80-entry tables (~2.5-4.8 KB), matching the
script's existing memory discipline.

**Both keys reuse the one field id the view already stashed.** ENTER pushes
`lcsClick` at `s.fieldId` — the Start Scan command — and `TxSpectrumStart()`
early-returns into `TxSpectrumResetMaxHold()` when already scanning. No name
lookup, no second field, no extra wire contract. (An earlier draft registered a
separate `Reset Max-Hold` param and hunted for it in `fields` by name; see §4 for
why that param could never work.)

**The scan screen renders the armed warning.** `drawSpectrum`'s title uses
`titleShowWarn and elrsFlagsInfo or "SPECTRUM"`, mirroring `lcd_title_bw`. This is
load-bearing: arming aborts a scan by rebooting the TX (R1.4), so without it the
plot would simply freeze with no explanation. The C side already computes and
ships the flag for free (`TXModuleParameters.cpp:411`), and the script already
polls it at 1 Hz — so this costs one `and/or` and no protocol.

**Mono only, by construction.** `setLCDvar()`'s B&W branch sets `SPEC_Y0/Y1/SPAN`;
the colour branch leaves them nil, and `parseSpectrumMessage` refuses to enter
when `SPEC_Y1 == nil`. A colour user's "Scanning..." popup simply stays up
offering [RTN], which cancels the scan normally — R2.2's graceful-degradation
shape, for free. This is a safety gate, not tidiness: the API audit below was
mono-only, and a Lua error would kill the script *while the TX is in scan mode
with the link down and RTN unreachable* — the feature's worst failure mode.
Deriving `SPEC_Y1 = LCD_H - textSize - 3` also makes the 128x96 mono panels work
(the plot filled 2/3 of the screen when it was hardcoded to 53) while staying
bit-identical on 64px panels.

**Redraw is per-trace, not per-frame.** `parseSpectrumMessage` returns true only on
`SWEEP_END`, and `run()`'s existing `forceRedraw` plumbing carries it. Four frames
arrive per 100ms update; redrawing all 80 bins on each would be pure waste. Cursor
movement redraws via `event ~= 0`.

**Verified without hardware:** 13 native codec tests, including one that pins the
axis byte order (a roundtrip test cannot — encode/decode agree under either
endianness, and the Lua depends on BE via `fieldGetValue`). The file parses
(`luaparser`); every constant used
(`SOLID`, `DOTTED`, `GREY_DEFAULT`, `INVERS`, `RIGHT`) and every API called
(`drawPoint`, `drawLine`, `drawNumber`, `getLastPos`, `drawFilledRectangle`) is
confirmed registered for **monochrome** targets in EdgeTX v2.12.1 — note
`lcd.sizeText` is *not*, which is why the colour path is the only one that calls
it. The C encoder's byte layout was replayed against the Lua `data[]` offsets:
80 bins round-trip exactly and `SWEEP_END` lands only on frame 2. The derived
geometry was re-checked across every mono panel `setLCDvar` handles: 128x64 and
212x64 give y 11..53 at 1.90 dB/px, 128x96 gives y 11..85 at 1.08 dB/px, and no
row overlaps on any of them.

### 6.1 Target: RadioMaster Boxer (128x64 mono)

The handset is fixed for v1, which pins the rendering decisions:

- **80 bins into 128 px is very nearly 1:1** — one pixel per bin with 48 px spare
  for margins and labels. A happy accident of the 2.4GHz FHSS grid being 80
  channels wide.
- **No colour to separate the traces.** The 1-bit idiom: filled bars from the
  baseline for the live trace, plus one dot per column marking max-hold. That is
  what a real swept analyzer looks like, so it reads as intended rather than as a
  compromise. `elrs.lua` does not currently use `lcd.drawPoint`, but EdgeTX
  exposes it; the script's existing primitives are `drawLine` / `drawRectangle` /
  `drawFilledRectangle`.
- **~56 px of vertical budget** after a title bar. Over a plausible -110..-30 dBm
  span that is ~1.4 dB/px — coarse, but well matched to the SX1280's real
  accuracy, so the display will not imply precision the instrument lacks.
- **Key-driven only.** `elrs.lua` sets `touch2evt = nil` on mono (`:836`), so
  there are no touch affordances to design around.
- Overlay the FHSS grid and WiFi channel centres; those are the reference marks
  that make the plot actionable.

Colour targets are not a v1 goal, but nothing in the wire format precludes them.

---

## 7. Build matrix

> Operational build/flash instructions live in [`BUILDING.md`](./BUILDING.md), which is
> current for all three targets. This section is the *rationale* for the build split.

Envs: `Unified_ESP32_2400_TX_via_UART` / `_via_ETX` / `_via_WIFI` for SX1280 targets;
`Unified_ESP32_LR1121_TX_via_ETX` for the Nomad (`targets/unified.ini`).

| Build | Flags | Purpose |
|---|---|---|
| **A** | *(none)* | stock control — inertness proof (R1.1) |
| **B** | `-DTX_SPECTRUM_SCAN` | the feature |
| **C** | `-DTX_SPECTRUM_SCAN -DDEBUG_LOG` | bring-up logging |

**The regulatory domain is per target, not global.** SX1280 targets (SuperG, Boxer) take
`-DRegulatory_Domain_ISM_2400`. The Nomad is cross-band and needs a **sub-GHz** domain —
`-DRegulatory_Domain_FCC_915` or regional equivalent — with the 2.4 half implicit; passing
`ISM_2400` to the LR1121 build is wrong. Note the domain is also a *runtime* option baked
into the image, so a bin patched with the wrong `--domain` sweeps the wrong span while
looking healthy (this cost a bring-up cycle — see `STATUS.md`).

> **Note.** BUILD-C is `-DDEBUG_LOG`. There is no per-bin settling harness on the TX; if
> one is ever wanted, it has to be built, not merely enabled by a flag.

**Logging advantage over the RX targets.** The base Gemini layout puts CRSF on
`serial_rx:3 / serial_tx:1` (UART0), but the `superg` overlay moves it to
`serial_rx:21 / serial_tx:21` — half-duplex on one pin — freeing GPIO21 via
`radio_rst_2:-1`. So **UART0 is not consumed by CRSF**, and the operational build
and the bench-log build need not be mutually exclusive. A backpack debug path also exists
(`debug_backpack_tx:5 / rx:18 @ 460800`).

*Open: verify the module physically exposes UART0 (USB port or pads). Pin map ≠ PCB.* (§8)

Build-workflow gotchas carry over from the RX work: `printf '0\n' | pio run -e ENV`
to bypass the interactive product picker; flags in gitignored `super_defines.txt`;
never interrupt a backgrounded `pio` build (orphaned grandchildren).

---

## 8. Open questions

1. **Is `0x30` free in the TBS CRSF spec?** EdgeTX forwards it regardless (§5), so nothing
   is blocked — but it must be settled **before the wire format is frozen**, to avoid
   colliding with a future TBS allocation. Note the failure mode is silent and inverted: a
   frame type that *gains* an EdgeTX `case` becomes consumed and hidden from Lua, so our
   frames would simply stop arriving. **Open.**
2. **Is gain carryover (§3.3.3) present on SX1280?** Untested, not cleared — §11.1 has the
   discriminating test. Deliberately not chased: changing a P0–P5 validated path to hunt an
   unobserved defect is a bad trade. **Open.**
3. **Is antenna-compare (§3.4) worth building at all?** The protocol flags are reserved and
   nothing else blocks it; it applies to same-band Gemini only, never to cross-band targets.
   **Open, unscheduled.**
4. **Keep the unviewed band's max-hold warm?** (§10.1 item 2.) Costs +160 B bss; §3.4's
   slower LR1121 sweep makes the cold fill more noticeable than when this was first raised,
   which strengthens the case. **Open.**

**Redacted — settled, and the useful form lives elsewhere:**

- ~~Does EdgeTX's `crossfireTelemetryPop()` forward `0x30` to Lua?~~ **Yes.** Verified
  against v2.12.1 and cross-checked 2.8.4 → main: there is no whitelist, unknown types are
  forwarded by the `default:` case, and neither `0x30` nor `0x31` has ever been claimed. The
  mechanism, the queue-pacing constraint it uncovered and the byte offsets are all in §5 —
  which is the form worth keeping. The question no longer needs a slot here.
- ~~Does the SuperG expose UART0, and is BUILD-C therefore usable?~~ **Moot in practice.**
  The Nomad is the primary platform, and every target has been brought up and logged over
  the WiFi web-UI path (`BUILDING.md` §5). No bring-up has needed UART0 logging.
- ~~Split vs antenna-compare as the default mode?~~ **Moot as posed** — compare mode was
  never built, so split is the only same-band Gemini mode. Restated as item 3 above.

---

## 9. Phases

Deliberately shorter than the RX plan: no in-flight phase, no FC/Betaflight
dependency, no do-no-harm A/B (R1.3), no offline analysis loop — you can *see*
whether it works.

| Phase | Gate | Needs HW |
|---|---|---|
| **P0** ✅ | **DONE 2026-07-16.** `TxSpectrumProtocol.h` + `test/test_txspectrum/` — **14/14 native tests pass on 4.0.1 locally**. §8.3 resolved (EdgeTX forwards `0x30`). §8.1 still open but no longer blocking. Note: the zero-dependency rule paid off immediately — the codec builds natively on 4.0.1 where `test_spectrum` cannot, because it never pulls in `CRSFEndpoint.cpp` and its `strlcpy`/`stpcpy` gap. | no |
| **P1** ✅ | **DONE 2026-07-16.** All three builds green on `Unified_ESP32_2400_TX_via_UART`. **Inertness proven, stronger than planned** (§9.1). R1.2 holds — every deletion is a rewritten line, no code removed. | no |
| **Audit** ✅ | **DONE 2026-07-16.** The three risk constraints (§2) re-verified against the tree rather than trusted. R1/R2 held; **R3's stated mechanism did not** — two false sub-claims found and fixed (§9.5). Endianness and the Lua index base closed by research, not testing. | no |
| **L1/L2** ✅ | **DONE 2026-07-16.** Golden vectors from the real encoder + a headless Lua harness — the only things that compare the firmware encoder against `elrs.lua`'s hand-rolled decoder (§9.5). | no |
| **L2.5** | **Lua heap on the real Boxer** (`lua/mockup/heapchk.lua`). Needs **no flash and no TX** — it is a script-load cost, measured against stock firmware. Closes the largest open risk (§9.4) and doubles as R2.2. **Do this before P2.** | handset only |
| **P2** | Bring-up: enter scan from Lua, sweep runs, RSSI is not constant across bins (**the "not on 2.4" regression test** — a flat trace = the `SetMode` guard bug, §3.3). **R3.2: paired RX shows zero packets + failsafe for the whole scan.** Reboot-exit returns to a normal link. Also: **Bind during a scan must be refused** (§9.5 V1 regression). | TX + RX |
| **P3** | Measurement sanity: a known WiFi AP appears at the right bin; max-hold fills in over ~5 s where a single sweep looks empty (§3.5). Backpack disabled → floor drops (§3.7). | TX |
| **P4** | Arm guard: refuse to start while armed; abort a running scan on arm. (R1.4) | TX + handset |
| **P5** | **Rollback test.** Flash B → set a distinctive config (model match, rate, power) → run a scan → reflash **stock** (4.0.1 when this ran; 4.1.0 on the current branch) → **config must be intact** (R2). Then stock → B → stock again. Also: modified `elrs.lua` against stock firmware must not error (R2.2). | TX |
| **P6** | Gemini: split mode 2×-rate confirmed; antenna-compare shows a delta when one antenna is detuned/removed. | TX |

P5 is the phase that would catch an accidental `TX_CONFIG_VERSION` bump, and it
is the one most likely to be skipped. Don't.

### 9.1 P1 results (2026-07-16)

**Inertness: proven byte-identical, not merely symbol-free.** The plan only
asked for `nm` to show zero TxSpectrum symbols with the flag off. That proves
the *code* is absent, not that the *diff* is inert — so the feature was stashed
(`git stash -u`) and pristine 4.0.1 built for comparison. Every section matches
exactly:

| Build | text | data | bss | total |
|---|---|---|---|---|
| pristine 4.0.1 | 1199300 | 448596 | 40897 | **1688793** |
| **A** (this tree, flag off) | 1199300 | 448596 | 40897 | **1688793** |
| **B** (flag on) | 1200972 | 448928 | 41185 | 1691085 |

So with `TX_SPECTRUM_SCAN` undefined, this branch compiles to a bit-for-bit
copy of stock 4.0.1. That is the strongest form of R1.1 and it should be
re-run whenever the feature touches a new file.

BUILD-B costs **+2156 bytes** total (+1640 text, +308 data, +208 bss). The bss
figure is `liveBins[80] + maxHoldBins[80] + ~48` bytes of latched scan state,
i.e. no hidden allocation. Flash 82.8%, RAM 21.8%.

> Both figures re-verified after the 2026-07-16 cleanup pass (§9.2): pristine vs
> flag-off stayed byte-identical, and BUILD-B shrank by 136 bytes. The
> byte-identical result is what proves the cleanup's merge into `handleWifiBle`
> — a function compiled into *every* build — did not leak into the flight image.

`nm` on BUILD-B, for the record (most statics inline away into `event`/`timeout`):

```
400e1694 T _Z15TxSpectrumStartv
400e1678 T _Z22TxSpectrumResetMaxHoldv
3ffc6af1 b _ZL11maxHoldBins
3ffc6b71 b _ZL8liveBins
400e1244 t _ZL8StoreBinha
3ffbe444 D TxSpectrum_device
```

**Gotcha found during P1: `include/hardware.h` has no include guard** — no
`#pragma once`, no `#ifndef`. Nothing else in the tree includes it directly;
everyone reaches it via `targets.h`, so the hazard had never been exposed.
`TxSpectrum.cpp`'s direct `#include "hardware.h"` (for `hardware_int()`) was the
first, and it fails with `conflicting declaration 'typedef enum nameType'`. Fixed
on our side by dropping the include and relying on `common.h` -> `targets.h`,
rather than adding a guard to a shared header for our convenience. If you add a
file here, do not include `hardware.h`.

**Not yet done** *(as of this P1 snapshot — superseded)***:** the Lua script side.
`elrs.lua` was untouched at this point, so BUILD-B exposed the Spectrum folder and streamed
`0x30` with nothing rendering it. Shipped later the same day — see §6.0.

### 9.2 Known debt — reviewed 2026-07-20

Surfaced by a reuse/efficiency review on 2026-07-16, re-checked against the tree after the
4.1.0 port and the AGC fix. **Two items resolved themselves, one turned out not to be debt
at all, and four remain.** Recorded so they are not rediscovered as findings.

#### Resolved

- ~~**`extern unsigned long rebootTime;` is the fourth copy.**~~ **Fixed upstream in 4.1.0.**
  The global was replaced by `scheduleRebootTime()` behind `rxtx_intf.h`; this feature calls
  that API through a proper include and replicates nothing. One stray `extern` survives at
  `lib/SCREEN/menu.cpp:31`, which is upstream's to clean up, not ours.
- ~~**The `isFsk` test in `BeginScan()`'s `Config()` is a fifth copy.**~~ **Dissolved by the
  4.1.0 port.** `Config()` now takes `RadioBandMod::Combined` (band + modulation) directly,
  so `mp->radio_type` is passed straight through and there is no modulation test to
  duplicate.

#### Not debt — a deliberate value, do not "tidy" it

- **`RssiSettleUs()`'s LR1121 branch is scan-specific and must not be replaced by a generic
  table.** A 2026-07-16 review entry called this function duplicated and recommended
  collapsing it against a generic settle table. **Acting on that now reintroduces a
  hardware-confirmed bug.** Generic tables answer "how long until RSSI is valid on an
  already-running receiver" — 22 µs, 240 µs, that order. This sweep asks a strictly larger
  question, because it drops to `STDBY_RC` and re-enters RX on *every* bin (§3.3.1), so the
  delay must also cover a PLL relock and a full AGC re-acquisition. That is the flat
  1000 µs, and shortening it while the per-bin reset stays brings the 915 floor lift
  straight back (§3.3.3, invariant M1). `TxSpectrum.cpp` carries this warning at the
  function itself, because that is where someone about to "simplify" it will be looking.

#### Remaining — all four blocked on the same trade

Each requires editing a **flight-critical shared header** (`common.h` or `FHSS.h`) and, for
the collapse to be worth anything, rewriting existing flight call sites. That is precisely
what **R1.2 (additive-only)** forbids on this branch, and it would void the property the
whole qualification rests on: the flag-off image is byte-identical to stock (R1.1, N0), so
today the feature provably cannot affect the firmware you fly. Clearing this debt trades
that guarantee for tidiness and forces a re-run of the ladder.

**Recommendation: do it as a separate upstream-facing branch, not here.** These are real
improvements to *ELRS*, not to this feature — the feature merely declines to make them worse.

- **`ReadRssiInst()` is a local per-family dispatch shim** rather than a shared one. It is
  pure dispatch with no policy, so unlike `RssiSettleUs` it *is* safely shareable; the right
  home is beside `isDualRadio()` in `common.h`. Left local because that is a flight header.
  **If you add a radio family, this shim needs a branch** — it is the one place the sweep
  knows which RSSI call a chip family wants.
- **`BinToRadioFreq()` is a tenth open-coding** of `freq_start + spread * ch / SCALE`.
  `FHSS.h` spells it out in eight places plus `test_fhss.cpp`, and offers no
  channel→frequency helper. `FHSSfreqForChannel(cfg, spread, channel)` would collapse all
  of them — worthwhile, and the most flight-critical of the four. The copies are not
  uniform (some subtract `FreqCorrection`, some `FreqCorrection_2`, some neither), which is
  exactly the axis `BeginScan()` latches explicitly to avoid.
- **No `FREQ_REG_VAL_TO_HZ`.** Everything converts Hz→reg; `ComputeAxis()` is the first
  reverse conversion in the tree and re-derives the per-family unit knowledge in its own
  `#ifdef`. The inverse belongs beside the forward macro in `FHSS.h`.
- **`TxSpectrumHasSecondBand()` is the third copy of the two-band predicate**, byte-identical
  to the band-selector gate in `TXModuleParameters.cpp` and to `has_low_band`/`has_high_band`
  in `devWIFI.cpp`. No shared `hasDualBand()` exists; the right home is `common.h`. **They
  must stay in agreement** — the page-button flip is only meaningful where the band selector
  exists. Note the selector gates the second *radio* separately on `GPIO_PIN_NSS_2`, which
  `BeginScan()` mirrors.

### 9.3 Two traps for anyone extending this

- **Do not `#include <crsf_protocol.h>` from `test_txspectrum`.** It drags
  `lib/CrsfProtocol` into the native link, and 4.0.1's `CRSFEndpoint.cpp` uses
  `strlcpy`/`stpcpy`, which mingw lacks (master added `include/native.h` after
  4.0.1 was cut). The size budget is asserted by a `static_assert` in
  `TxSpectrum.cpp` instead — which is stronger anyway, since it fires on every
  firmware build rather than only when someone runs the native suite.
- **`lib/TxSpectrum` is in the native env's `lib_ignore`** (`platformio.ini:15`),
  with `-Ilib/TxSpectrum` added so the header stays reachable. This is required,
  not cosmetic: PIO's chain LDF scans `#include` lines **without evaluating
  `#ifdef`**, so leaving the lib in would compile `TxSpectrum.cpp`'s includes
  into the native build even though the whole file is `#if defined(TX_SPECTRUM_SCAN)`.
  Symptom is the `stpcpy` error above. P0 passed before `TxSpectrum.cpp` existed;
  adding it is what broke native, so **re-run `pio test -e native` after adding
  any .cpp here.**

### 9.4 The `elrs.lua` heap — MEASURED 2026-07-17, not a blocker

> **Result, on a real Boxer (EdgeTX 2.12.1), via `lua/mockup/heapchk.lua`:**
>
> | | compile | loaded | runtime state |
> |---|---|---|---|
> | stock `elrs.lua` | 38.8 KB | 43.1 KB | +4.3 |
> | **with the spectrum view** | 45.2 KB | **50.0 KB** | +4.8 |
> | **delta** | +6.4 | **+6.9 KB (+16%)** | +0.5 |
>
> **It loads, so the blocker is gone** — the parser's transient peak fits, which is
> the only thing a pre-generated `.luac` would have bought. **`.luac` is therefore
> not needed**; EdgeTX generates one on first run anyway.
>
> **6.4 of the 6.9 KB is bytecode and constants**, only 0.5 KB is runtime state —
> proportional to ~185 lines of added *code*, with no hidden allocation. Note the
> source grew **+38%** but the heap only **+16%**: comments are stripped by the lexer,
> so commenting this file is free. A desktop Lua 5.5 run predicted +11.5 KB, i.e.
> ~2x the radio's figure — `heapchk` on hardware is the only number to trust, but
> the desktop proxy is usable for *estimating* future growth.
>
> **Not included: the bin tables.** `s.bins`/`s.hold` are allocated when a scan
> starts and nil'd on exit, so 50 KB is the idle-loaded cost; a running scan adds
> ~1.5-2 KB for 2x80 Lua numbers (peak ~52 KB). **That is the figure a combined
> dual-band plot or antenna-compare would double** — see 10.1 for why per-band
> screens avoid it.
>
> **Inference, not measurement: it is almost certainly spilling into SRAM, and
> always has.** Total VM with the script loaded is ~56 KB (50 + a 5.9 KB baseline).
> The CCM pool is 64 KB *minus* `.data`, `.bss` and task stacks, so ~56 KB cannot
> fit there — it is using the `ccm_malloc` -> system `malloc` fallback below. But
> **stock already sits at ~49 KB and spills too.** Spilling is the normal shipping
> condition for `elrs.lua` on a Boxer; this feature made an already-spilling script
> 16% heavier against a 128 KB SRAM heap. Confirming this properly means reading
> `_eccm` / `_main_stack_start` out of a linked EdgeTX `.map`.

The original analysis follows, because the *reasoning* still governs anyone who grows
this file further.



**`elrs.lua` is the one component that cannot be put behind the build flag.** It
is an SD-card file, so it ships to every handset regardless of firmware and
**outlives a firmware rollback**. The spectrum view grew it **+31% (30,064 →
39,492 bytes)** and +12 top-level locals (86 → 98), in a script whose own code
already calls `collectgarbage("collect")` (`:168`) and whose EXIT handler warns it
"runs near the Lua heap limit". Every other R1.1 guarantee — flag-off builds
byte-identical to the stock base release — says nothing about this file.

So the exposure is: **a user who never enabled `TX_SPECTRUM_SCAN`, running stock
firmware, could lose the whole ELRS config UI.** That is a worse failure than
anything the feature itself can do.

Three facts make it harder to dismiss than it first looks (EdgeTX v2.12.1):

- **There is no soft cap on B&W targets.** `targets/taranis/board.h:51` defines
  `LUA_MEM_MAX (0)` — "0 means unlimited" — so `checkLuaMemoryUsage()`
  (`lua/interface.cpp:1310-1325`) is compiled out entirely. Colour radios get a
  6 MB cap; the Boxer gets none. The real ceiling is physical: a CCM allocator
  over 64K shared with `.data`/`.bss`/task stacks. The exact figure is
  build-dependent and only knowable from the linked `.map` (`_eccm` →
  `_main_stack_start`) — **any fixed constant quoted for this is a guess.**
- **The failure is not clean.** `ccm_malloc` falls back to system `malloc()` when
  CCM is exhausted (`ccm_allocator.cpp:154-157`), spilling into the 128K SRAM
  heap — so true exhaustion means CCM *and* SRAM are gone, destabilising the
  radio (audio, mixer) rather than politely killing the script. There is no
  "script too big" message; the user sees **"Script panic"** (`TR_SCRIPT_PANIC`)
  via `LUA_ERRMEM`.
- **The simulator cannot detect it.** `radio/src/CMakeLists.txt:573-575` returns
  for `NATIVE_BUILD` *before* `:618` adds `-DUSE_CUSTOM_ALLOCATOR`, so the
  simulator never compiles `custom_allocator.cpp` and Lua falls through to
  `luaL_newstate()` — plain desktop `malloc`, gigabytes. No CCM, no 64K ceiling.
  Task stacks compound it (`std::thread` defaults vs the Boxer's 8000-byte CCM
  menus stack). **The simulator validates logic and drawing, never footprint.**

**Measure it on the radio, and note the obvious readout is unavailable here:**
EdgeTX's built-in `"GV Use: <n>b"` display (`lua/interface.cpp:1205-1212`) is
wrapped in `#if defined(KEYS_GPIO_REG_MENU)`, and the Boxer has no MENU key — it
is compiled out. Use `collectgarbage("count")` instead: `lua/mockup/heapchk.lua`
does exactly this, comparing stock `elrs.lua` against ours. **It needs no flash
and no TX** — this is a script-load cost, so it runs against stock firmware. It
also doubles as the R2.2 test.

**If it is tight:** EdgeTX precompiles `.lua` → `.luac` on-radio (`LUA_COMPILER`
is on by default), and the *parser* builds prototypes on the Lua heap — so
shipping a pre-generated `.luac` materially cuts peak load memory. Note the
simulator diverges here too: load mode is `"bt"` on radio (binary preferred) vs
`"T"` on SIMU (`lua/lua_api.h:36-44`).

### 9.5 The audit, and why the test ladder is shaped like this (2026-07-16)

Status and findings live in [`STATUS.md`](./STATUS.md). What belongs *here* is the
design consequence.

**R3's stated mechanism was wrong, and the invariant now rests elsewhere.** The
original R3.1 claimed `setPower(MinPower)` meant "even a latent bug transmits at
minimum power". It does not — the commit runs from the TX-*done* ISR. And "the scan
never transmits" was conditionally false: three `hwTimer::resume()` sites were
reachable during a scan. Both are fixed, but the design lesson is **R3.1a**: a
stopped `hwTimer` is the *only* thing standing between any mode state and a
transmission. That is true for `wifiUpdate` too; upstream is merely covered by
`Radio.End()`, which this feature cannot call because the radio is the instrument.
**If you add a path that resumes the timer, this feature is where it breaks first.**

**Guard the mechanism, not the well-named wrapper (P2, 2026-07-17).** The Bind guard
was first placed in `EnterBindingModeSafely()` — the obvious seam, since every
Lua/CRSF/menu path routes through it and its name advertises the contract. It was
the wrong altitude, and the bench proved it: **you cannot reach Bind from the
handset during a scan at all, because the plot owns the screen.** The audit's
reasoning ("Lua stays live below `FAILURE_STATES`, so Bind is live") was true about
the *code* and false about the *user*. Meanwhile `tx_main.cpp` registers the button
against **`EnterBindingMode` directly** (`registerButtonFunction(ACTION_BIND,
EnterBindingMode)`) while `rx_main.cpp` registers the `Safely` variant — an upstream
asymmetry. So the guard covered only the unreachable paths and missed the one a user
can actually hit. It now sits in `EnterBindingMode()`, where `hwTimer::resume()`
lives: **put the check next to the hazard, not next to the API whose name promises
safety.** Every caller inherits it, including ones added later.

**The structural gap the ladder exists to close.** `TxSpectrumDecodeFrame()` is unit
tested, but **the handset never runs it** — `parseSpectrumMessage()` in `elrs.lua` is
a hand-rolled *second* decoder. The 14 native tests prove nothing about the wire path
that actually ships. So:

| Rung | Proves | Cannot prove |
|---|---|---|
| **L0** native | C encode/decode round-trip | anything about the Lua decoder |
| **L1** golden vectors | encoder ↔ Lua decoder agreement | rendering, firmware, wire |
| **L2** `simcheck.py` | Lua decode, render, EXIT lockout | **memory**, firmware, wire |
| **L2.5** real handset | **Lua heap**, real primitives | firmware, wire |
| **L3** hardware | firmware → router → FIFO → UART | — |

**The middle is only testable on hardware** — the simulator has no firmware, the
native tests never run Lua. L1 is what bridges the two halves, and its value comes
entirely from provenance: the golden frames are emitted by the *real*
`TxSpectrumEncodeFrame()` (`lua/mockup/gen_spectrumgolden.cpp`). Hand-written frames
would encode someone's *reading* of the format — the exact thing under test.

**Endianness and the index base were closed by research, not testing**, and that is
the cheaper move whenever it is available: `htobe32(0x454C5253)` ↔
`fieldGetValue(...) == 0x454C5253` (`CRSFEndpoint.cpp:408` ↔ `elrs.lua:406`) is a
shipping closed loop across the same two layers this feature spans. `'E'`=0x45 first
only equals that under a big-endian reader. The index base falls out of
`parseDeviceInfoMessage` the same way. **Look for a working parallel before writing a
test.**

**Green checks here were mutation-tested.** A test that cannot fail is worse than no
test, because it reads as coverage. Flipping the C encoder to LE must fail
`test_golden_vector_frame_is_pinned`; flipping `fieldGetValue` must fail
`simcheck.py`; deleting `elrs.lua`'s `fieldPopup` entry guard must throw. All three
verified. That last one also corrected the guard's own comment: an in-flight frame
does not merely re-open the view, it kills the script.

---

## 10. Cross-band: Nomad (LR1121 dual-band)

> **Written as a follow-on plan; now shipped.** The Nomad is hardware-validated end to end
> (N0–N5, 2026-07-18) and is the primary platform. The section is kept because §10.1's
> decision still governs anyone adding a view, and the predictions below are worth reading
> against what actually happened.

Mostly additive once the SuperG works: radio 1 on sub-GHz + radio 2 on 2.4, using the same
engine with a different radio assignment. But it is a genuine second bring-up, not a free
ride — different `RssiSettleUs` branch, and LR1121 needs `StartRssiInst()` before
`GetRssiInst()`.

**What the plan underestimated:** the LR1121 needed more than a settle branch and an extra
SPI step. Cross-band routing required a per-band `Config()` (PA path + RF switch, I4), and
the sub-GHz path turned out to carry an AGC gain-carryover defect that took a per-bin
receiver restart to fix (§3.3.3) — a class of problem the 2.4-only targets never exhibited.
"Mostly additive" held for the *structure*; it did not hold for the *measurement*.

### 10.1 One screen per band. Do not build a combined plot.

**Decision (2026-07-16): each band gets its own screen, and only the displayed
band is streamed.** Generalised: *stream only what's displayed* makes refresh
independent of how many bands or radios exist — frames per cycle is bounded by
the widest single view, not by the total.

| View | Bins | Frames/cycle | Cycle @50ms |
|---|---|---|---|
| sub-GHz screen (FCC915) | 40 | 2 | **100 ms** — faster than the 2.4 view |
| 2.4 screen | 80 | 4 | **200 ms** — same as today |
| ~~combined~~ | ~~120~~ | ~~6~~ | ~~300 ms~~ |

This is not a latency optimisation — it is what keeps the design *simple*. A
combined 120-bin plot costs four things that per-band screens cost zero:

- **`TX_SPECTRUM_MAX_BINS` stays 80.** You never hold 120 bins at once, and 80 is
  the widest single band that exists. No bss growth, and no silent truncation at
  `BeginScan()`'s clamp.
- **The axis stays one `(start, step)` pair.** `TxSpectrumBinFreqKhz` is a single
  linear formula; sub-GHz and 2.4 are disjoint axes (FCC915 = 903500 kHz / 600 kHz
  step; ISM2G4 = 2400400 kHz / 1000 kHz step). One band per screen means the Lua's
  existing latch works unchanged.
- **No band/frame-straddle rule.** A combined plot only works if a band never
  spans a frame. FCC915 (40) + 2.4 (80) is frame-aligned at 40 by luck; **EU868
  (13)** + 2.4 would put the boundary mid-frame, where one frame needs two axes.
  Per-band screens make every domain work.
- **No new Lua heap.** One band's tables at a time — max 2×80, identical to today.
  A combined view (or antenna-compare) would need 2×120 or 4×80, on the handset
  heap that is already this feature's largest open risk (§9.4).

**Serial is not a constraint and was never going to be.** `EmitNextFrame()` sends
exactly one frame per `TX_SPECTRUM_EMIT_INTERVAL_MS` and never bursts, so
bandwidth is **58 B / 50 ms = 1.16 KB/s regardless of bins, radios or traces** —
adding traces costs refresh rate, not bytes. Against ~32 KB/s at 400k/250Hz and
~10 KB/s at 115200/250Hz, that is 9–27× headroom. **The real pacing wall is
EdgeTX's Lua GC** (the reason the interval went 25 → 50 ms): each frame mints a
~54-entry Lua table. Serial has ~9× headroom; the GC has none. Do not "optimise"
the wire format for bandwidth — it is not where the budget goes.

`CRSF_MAX_PACKET_LEN 64` caps bins/frame at 50 (62 payload − 12 header), so a
120-bin view would need ≥3 frames however you slice it. You cannot buy refresh
back with bigger frames.

**Open at Nomad bring-up — both now settled:**
1. ~~**Band switching.**~~ **RESOLVED.** Shipped as the in-plot **page-button flip**, the
   option this section listed as "wanted later" rather than the two-command-field version it
   recommended as cheapest. `TxSpectrumSwitchBand()` flips `scanBand` and re-runs
   `BeginScan()` in place, so the plot keeps the screen and no second command field or
   protocol change was needed. Validated N2.
2. **Sweep both, stream one?** **Still open, still not done.** Costs `liveBins`/`maxHoldBins`
   at 160 (+160 B bss) but keeps the unviewed band's max-hold **warm**, so switching shows
   history instead of a cold fill. §3.4's slower LR1121 sweep makes the cold fill *more*
   noticeable than when this was written, which strengthens the case — against the same bss
   cost this approach otherwise avoids.

The same principle covers Gemini antenna-compare (§3.4): one screen per radio,
stream the displayed one → 4 frames/cycle, not 8.

**Regulatory note for the *future* sub-band feature (not this one):** FCC 15.247
sets a minimum channel count for 2.4 GHz frequency hoppers; ELRS's 80 channels
sits comfortably inside it. Restricting to a quiet sub-band trades interference
avoidance against both frequency diversity and possibly compliance. This scanner
only listens and has no such exposure — but the feature it feeds does.

---

## 11. Portability, compatibility and upstreaming

### 11.1 Generalization across radio families

The measurement cycle (§3.3.1) is family-agnostic; the *state management* around it is not.
Only the LR1121 needed I3 and I5.

| | **SX127x** | **SX1280** | **LR1121** |
|---|---|---|---|
| RSSI call | `GetCurrRSSI` | `GetRssiInst` | `StartRssiInst` + `GetRssiInst` |
| Settle | 240 µs *(unmeasured)* | 100/141/218/480 by SF; 80 FLRC | **1000 µs flat** |
| Settle provenance | inherited | LBT bench data | Semtech `SWSD003` |
| Per-bin AGC reset (I5) | no | no | **yes** |
| ISR detach (I3) | no | no | yes |
| `SetMode` visibility | **public** | private | private |
| Wrapper needed to port I5 | none | **yes** | added (`SpectrumResetRx`) |
| AGC carryover observed? | **untested** | **not observed** (P0–P5) | **confirmed, fixed** |

**Do not read "not observed" as "not present."** The SX1280 targets are 2.4-only, and 2.4 is
where the symptom was hardest to see even on the affected part. The honest position: gain
carryover is a plausible property of any AGC-equipped transceiver held in continuous RX
across retunes, and only the LR1121 has been *shown* to exhibit it. The SX1280 path was left
exactly as hardware-validated because changing a validated path to chase an unobserved
defect trades certain risk for uncertain benefit — not because the risk is known absent.

**If you suspect carryover on another family**, the discriminating test needs no new
silicon: force `STDBY`→RX per bin with a generous settle and see whether an intermittent
uniform floor lift disappears. A *fixed* bias is not this bug — carryover is intermittent
and dwells for seconds.

**Porting I5 to a new family — checklist:**

1. **Does the family re-acquire gain on a retune in continuous RX?** If yes, stop; I5 is
   pure cost.
2. **Reach a standby mode.** `SetMode` is public on SX127x, private on SX1280 and LR1121.
   For a private one add a gated inline wrapper next to the RSSI calls (see
   `LR1121.h::SpectrumResetRx`) — **not** a new public driver API, which would widen the
   surface and weaken R1.1.
3. **Raise the settle** to cover PLL relock + gain acquisition, and record where the number
   came from. Honour M1: the two move together.
4. **Confirm S1 still holds** — the chunk budget should absorb it with no other change.
5. **Re-run the CW test** (N3/P3). A per-bin reset is exactly the change that could fix a
   floor artifact while blunting response to real signals. This is the check that matters.

### 11.2 Compatibility and test status

| Target | Radio | Bands | Topology | Feature validated | AGC fix validated |
|---|---|---|---|---|---|
| **RadioMaster Nomad** | dual LR1121 | 900 + 2.4 | cross-band | ✅ N0–N5, 4.1.0 | ✅ 2026-07-20 |
| **BETAFPV SuperG** | dual SX1280 | 2.4 | Gemini split | ✅ P0–P5 **4.0.1**; ✅ CW re-check on **`tx-spectrum-pr`** 2026-07-21 | n/a — LR1121 only |
| **RadioMaster Boxer** internal | single SX1280 | 2.4 | single-radio | ✅ brought up **4.1.0**; ✅ re-verified on **`tx-spectrum-pr`** 2026-07-21 | n/a — LR1121 only |

> **Provenance, corrected 2026-07-20.** An earlier version of this table credited the Boxer
> internal with "P0–P5, 4.0.1". That was a misreading of STATUS.md's phase table, whose third
> column is *hardware used*: "SuperG + Boxer" there means the SuperG module **in** the Boxer
> handset, and the P0–P5 record is the SuperG's alone. The Boxer's internal module was brought up
> separately on **4.1.0** (~2026-07-19; the two-step bootstrap in `BUILDING.md` §5b comes from that
> session). The scope of that bring-up was not recorded phase-by-phase, so it is stated here as
> bring-up rather than as a P0–P5 equivalent.
>
> **PR-branch verification, 2026-07-21 — complete.** Both SX128x targets have been re-checked
> against `tx-spectrum-pr` itself. A CW carrier peaks on the **2440.4 MHz** bin on the Boxer
> internal (single-radio) *and* on the SuperG (Gemini split), which is the correct bin: the ISM2G4
> grid is `2400.4 + 1.0n`, so a 2440.000 MHz tone lands on bin 40 at 2440.4 (§3.6.1).
>
> **That the two topologies agree is the substantive part.** The split sweep measures two bins per
> dwell across two radios and interleaves them into one trace; an off-by-one in that interleave, or
> a radio/bin mis-mapping, would shift the peak by a bin on the SuperG while leaving the
> single-radio Boxer correct. Both landing on the same bin is direct evidence the interleave is
> right — which a single-target test could not have shown.

Off-hardware gates: `pio test -e native -f test_txspectrum -f test_spectrum` (24/24) and
`lua/mockup/simcheck.py`.

**Regression checks a measurement change must re-run:**

| Check | What it catches |
|---|---|
| **CW spike on the right bin, both bands** (N3/P3) | The important one — a gain or settle change can fix the floor while blunting real-signal response |
| **Distinct per-bin RSSI** (N2/P2) | The silent `SetMode` early-return failure (§3.3.2) |
| Band flip (cross-band targets) | `activeRadio` / `Config()` routing |
| Arm guard both ways (N4/P4) | R1.4 |
| Flag-off byte-identity (N0/P1) | R1.1 |

> **Reading the CW result.** `freq_center` need not land on a bin. FCC915's grid is
> `903.5 + 0.6n`, so a 915.000 MHz tone lands on bin 19, whose centre is **914.9 MHz** —
> 100 kHz off, well inside one 600 kHz bin. ISM2G4 is `2400.4 + 1.0n`, so 2440.000 lands on
> bin 40 at **2440.4 MHz**. Both are correct: a tone landing one bin off-centre is the grid,
> not an error (§3.6.1). The readout prints the bin centre to one decimal, so this is now
> directly visible rather than hidden by an integer label — expect `914.9MHz` and
> `2440.4MHz`, and treat a *whole* number on either band as suspect.

### 11.3 Upstreaming

The radio-facing footprint is deliberately minimal, because that is what makes the feature
cheap to review and cheap to rebase:

- **One driver file is touched:** `LR1121.h`, +8 lines, entirely inside
  `#if defined(TX_SPECTRUM_SCAN)`. `LR1121.cpp` is byte-identical to stock; the SX1280 and
  SX127x drivers are untouched.
- **The wrapper is an inline over an existing private method.** It adds no new capability
  and is unreachable with the flag off.
- **No new public driver API, no config fields, no wire changes** (R2.1/R2.3).

Expect these discussion points, in this order:

1. **The private-method wrapper.** The alternative — making `SetMode` public — is a larger
   surface change and worse for R1.1. A maintainer may prefer a driver-owned
   `RestartRx()`/`ResetAgc()` primitive; that is a reasonable outcome and would let the
   `#if` disappear.
2. **Whether the AGC fix belongs to the analyzer at all.** Arguably any continuous-RX
   measurement loop in ELRS has this latent, including RSSI/noise-floor work outside this
   feature. It is scoped to the sweep because that is where it was observed and validated;
   a driver-level fix would need its own qualification.
3. **The flat 1 ms settle** reads as unsophisticated next to the SF-aware tables. §3.3.4
   explains why — keep that reasoning attached to the constant if it moves.
4. **`ReadRssiInst` belongs in `common.h`** beside `isDualRadio()` (§9.2). Upstream will
   likely want it shared; that is the right call there and the wrong call on this branch,
   where R1.2 forbids editing flight headers. **`RssiSettleUs` is the opposite** — its
   LR1121 value is scan-specific (M1) and must not be folded into a generic table.
5. **`0x30` vs the TBS CRSF allocation** (§8.1) must be settled before the wire format is
   frozen.

### 11.4 Known limits

- **Absolute accuracy is not established** (§3.6): the idle floor is receiver-noise-limited
  rather than ambient, and `SET_RSSI_CALIBRATION` is never called.
- **`lnaGainDb` back-out assumes `power_lna_gain` is set**; where absent the axis is raw
  front-end dBm.
- **Never reached, therefore untested, as noise sources:** the LR1121 DC-DC regulator
  (`radio_dcdc: true`), the idle second radio's synthesizer on cross-band targets, and USB
  charger noise. The AGC fix landed before these were investigated — nothing implicates
  them, and nothing clears them.
