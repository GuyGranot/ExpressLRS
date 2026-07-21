# TX Spectrum Analyzer

Turns an ELRS transmitter into a **receive-only swept spectrum analyzer**, driven from the
handset's `elrs.lua`. It sweeps the FHSS channel grid measuring RSSI and streams live +
max-hold traces to the handset for a live plot — 2.4 GHz, sub-GHz, or both on a cross-band
module.

Build flag **`-DTX_SPECTRUM_SCAN`**, off by default. Flag-off builds are proven
byte-identical to stock (§2, R1.1).

| Target | Radio | Bands | Topology |
|---|---|---|---|
| RadioMaster Nomad | dual LR1121 | 900 + 2.4 | cross-band — one radio per band |
| BETAFPV SuperG Nano | dual SX1280 (Gemini) | 2.4 | same-band split — 2 bins/dwell |
| RadioMaster Boxer (internal) | single SX1280 | 2.4 | single-radio |

Between them they exercise every sweep topology the code supports. Validation status per
target is in §7.

**Non-goals:** no config persistence, no sub-band selection, no automatic action on the
data, no calibrated absolute accuracy (§3.6), no in-flight operation.

---

## 1. Safety model

Three constraints drive every design decision below. This feature **stops the RC link** —
that is deliberate, and it is what makes it cheap to reason about. The alternative,
sampling in the gaps of a live link, would put measurement code on the flight path.

### R1 — Co-existence

| Rule | Mechanism |
|---|---|
| R1.1 | Entire feature behind `-DTX_SPECTRUM_SCAN`, off by default. Flag-off image byte-identical to stock |
| R1.2 | **Additive-only diff.** No deletions or edits to existing logic except the enum (R1.3) and two switch sites (§3.7) |
| R1.3 | Scan is a **mode state**, not a hook in the packet loop. Nothing runs during flight |
| R1.4 | **Refuse to enter while armed**; abort on a disarm→arm transition |
| R1.5 | **Exit by reboot.** No restore path to get wrong |

R1.3 is what makes this cheap to qualify. Code that steals time inside a live packet loop
must prove it did no harm — a statistical A/B plus an ISR timing budget. Here the link is
*intentionally* dead and the aircraft is on the ground, so that entire class of gate does
not apply. **If a future change puts any part of the sweep inside the packet loop, that is
no longer true.**

R1.5 over a clean teardown: a botched restore could leave the radio on the wrong frequency
or SF while still *looking* connected — and you would fly it. A reboot guarantees a
known-good state. WiFi and BLE already exit this way.

### R2 — Rollback must be a non-event

**Do not add config fields. Do not bump `TX_CONFIG_VERSION`.** This is not stylistic —
`lib/CONFIG/config.cpp:194`:

```c
if (version < 5 || version > TX_CONFIG_VERSION)
{
    SetDefaults(true);
    return;
}
```

`TX_CONFIG_VERSION` is `8U` on both 4.0.1 and 4.1.0, which is what makes rollback
version-agnostic here. Bump it to `9`, and when the user later reflashes stock firmware the
stored `9 > 8` **silently wipes the entire TX config** — model matches, rates, power, VTX,
bind phrase. Catastrophic for a debug feature.

So all scan parameters live in **RAM only**. Nobody needs their sweep step size to survive a
power cycle.

- **R2.2** — `elrs.lua` is an SD-card file and *will* outlive a firmware rollback. Params
  are discovered, not hardcoded, so the Spectrum folder simply isn't enumerated by a stock
  module.
- **R2.3** — the `connectionState_e` insertion (§3.1) never crosses a wire: not serialised
  to the backpack, the RX, or NVS, so renumbering cannot desync a non-updated peer.

### R3 — Never key the PA

**The scan path is receive-only and never enables the transmit front-end.** It reaches only
STANDBY / config / frequency / RX / RSSI opcodes. The three TX openers — `TXnb()`,
`startCWTest()`, `SetMode(TX)` — are never on the path.

> ⚠ **R3.1 vs R3.1a — do not confuse these.** Entry drops power via
> `POWERMGNT::setPower(MinPower)`, matching the WiFi/BLE idiom. **That is a gesture, not the
> interlock.** `SetOutputPower` only queues `pwrPending`; the register write happens in
> `CommitOutputPower()`, reached from the TX-*done* ISR — so a stray TX would go out at the
> **pre-scan** power. `devWIFI` survives the same idiom only because `Radio.End()` follows
> immediately, and we keep the radio alive.
>
> **R3.1a is the real interlock: `hwTimer` stays stopped.**
> `timerCallback()` → `SendRCdataToRF()` → `Radio.TXnb()` has no `connectionState` guard, so
> a stopped timer is the *only* thing suppressing TX in any mode state. Three
> `hwTimer::resume()` sites are reachable during a scan and are guarded under the flag:
> `EnterBindingModeSafely()`, `OnPowerSetCalibration()`, `UARTconnected()`.

- **R3.2** (test method, not an invariant) — the paired RX must show **zero** received
  packets and go to failsafe for the whole scan. A TX leak shows up as the RX staying linked.
- **R3.3** — receive-only means **no antenna requirement**; bring-up carries no VSWR risk.
  This also makes antenna-detached scanning a valid diagnostic, which is how external RF was
  ruled out in §3.3.3.

### Invariant quick reference

**Any change to the sweep must be checked against all of these.** The three most often got
wrong are marked ⚠.

| # | Invariant | Enforced by |
|---|---|---|
| R1.1 | Flag-off image byte-identical to stock | everything under `#if defined(TX_SPECTRUM_SCAN)` |
| ⚠ R1.2 | Additive-only — no deletions from flight code | review; `LR1121.cpp` byte-identical to stock |
| R1.3 | Scan is a mode state, not a packet-loop hook | `connectionState == spectrumScan` |
| R1.4 | Never scan while armed; abort if armed mid-scan | event **and** level check in `timeout()` |
| R1.5 | Exit by reboot; no teardown path | `scheduleRebootTime()` |
| R2.1 | No config fields, no version bump | scan state is file-static RAM |
| R2.3 | Enum insertion never crosses a wire | not serialised to backpack, RX or NVS |
| R3 | **Receive-only — the PA is never keyed** | no TX-class opcode on any sweep path |
| R3.1 | Power dropped at entry | ⚠ **gesture, NOT the interlock** |
| ⚠ R3.1a | **`hwTimer` stays stopped** — the real interlock | three guarded `resume()` sites |
| S1 | Blocking **budgeted in time, not counted in bins** | `TX_SPECTRUM_CHUNK_BUDGET_US` = 1300 µs |
| ⚠ M1 | **Per-bin AGC reset and the settle move together** | `SweepChunk` + `RssiSettleUs` |

---

## 2. Architecture

### 2.1 Mode state

One value is added to `connectionState_e` (`src/include/common.h`). **The enum is ordered
and the ordering is load-bearing** — `MODE_STATES`, `NO_CONFIG_SAVE_STATES` and
`FAILURE_STATES` are sentinels, and every `<`/`>` comparison in the tree derives meaning
from position. This is the single highest-risk line in the feature.

`spectrumScan` must be `> MODE_STATES` (halt normal RF), `> NO_CONFIG_SAVE_STATES` (block
config commits), `< FAILURE_STATES` (not a hardware failure):

```c
    NO_CONFIG_SAVE_STATES,
    wifiUpdate,
    serialUpdate,
#if defined(TX_SPECTRUM_SCAN)
    spectrumScan,           // <-- here
#endif
    FAILURE_STATES,
```

This yields semantics identical to `wifiUpdate` while renumbering the fewest existing
values. Note the `#if` *inside* the enum: with the flag off the enum is numerically
identical to stock, which R1.1 depends on.

### 2.2 The seam — where the sweep runs

`tx_main.cpp` hard-returns when `connectionState > MODE_STATES`. The obvious template for
pushing data to the handset, `checkSendLinkStatsToHandset()`, is called **below that
return** — copying its call site would produce code that never runs.

What still runs *above* the return is the seam: `HandleUARTout()`, `devicesUpdate(now)`,
`executeDeferredFunction()`, `HandleUARTin()`. CRSF to and from the handset keeps flowing
while the RC link is dead.

### 2.3 The measurement engine

The bin grid comes free from the FHSS config: **a bin *is* an FHSS hop channel**, placed by
the same expression `FHSS.h` uses. That is deliberate — the plot's x-axis is already the
axis a future sub-band picker would select over, so no resampling ever has to be reconciled.
It also bounds the problem: the widest band that exists is 2.4 GHz's 80 channels, hence
`TX_SPECTRUM_MAX_BINS` = 80.

**Per-bin cycle:**

```
  [LR1121 only]  SpectrumResetRx(radio)                 -> STDBY_RC   (§2.3.2)
                 SetFrequencyReg(freq, radio, doRx=true)              (§2.3.1)
                 delayMicroseconds(settleUs)                          (§2.3.3)
  [LR1121 only]  StartRssiInst(radio)
                 rssi = ReadRssiInst(radio)
                 StoreBin(bin, rssi)      // LNA gain backed out, max-hold folded in
```

**Blocking is budgeted, not counted (S1).** `positionsPerCall` derives from
`TX_SPECTRUM_CHUNK_BUDGET_US` (1300 µs) rather than a fixed bin count, because `settleUs`
spans 80 µs to 1000 µs across families — a fixed count would block anywhere from 0.6 ms to
3.8 ms. Budgeting time keeps every `timeout()` call flat, which is what let the LR1121
settle rise 4× with no other constant re-tuned. Blocking is normal for this framework
(`devBackpack.cpp` does `delay(100)`) and safe here: `hwTimer` is stopped and only
`HandleUARTin()` is waiting, accruing ~8 bytes in 1.3 ms against a 256-byte ring.

#### 2.3.1 `doRx=true` is mandatory

The abandoned upstream branch `adds-noisefloor-measure-feature` ends on the commit *"works
on 900 but not on 2.4"*. Its bug: it hoisted `SetMode(RX)` out of the loop and wrote only
`SET_RFFREQUENCY` inside it. On a command/state-machine radio a frequency write takes effect
only when the receiver next enters FS/RX, so its RSSI stayed pinned to the first frequency.
SX127x retunes on a direct register write — hence "works on 900."

`SetFrequencyReg(..., doRx=true)` writes the frequency then re-enters RX. On SX1280 this
works **only** because the early-return guard in `SetMode` is commented out:

```c
// if (OPmode == currOpmode)
// {
//    return;
// }
```

> ⚠ **If anyone "optimises" that guard back in, FHSS hopping and this sweep both break — and
> the sweep breaks *silently*, reporting bin 0's RSSI across every bin.** The distinct
> per-bin RSSI check (§7) is the regression test for exactly this.

#### 2.3.2 AGC gain carryover — why there is a per-bin reset

**The defect.** Left alone the sweep enters RX continuous and never leaves it, retuning per
bin only. **RX_CONT does not re-run gain acquisition on a bare retune**, so a gain step the
AGC picks for one bin persists into every bin measured after it. Once it steps down, every
subsequent bin reads that step's gain-limited floor.

Observed on the Nomad as the entire 915 trace lifting ~20 dB to a flat, peakless
**−83…−85 dBm** for seconds at a time, then recovering. Evidence for that diagnosis over
"there is real energy there":

- **The level is a fingerprint.** LR1121 UM §7.2.15's per-gain-step table places the top
  gain steps at **−81…−87 dBm**, under 1.5 dB apart. A stepped-down AGC's floor lands
  exactly in the observed band; broadband noise has no preferred level.
- **The sibling part has it as published errata.** RadioLib documents SX126x reporting RSSI
  up to 35 dB high, self-recovering, cured by restarting the AGC. (No LR11xx errata document
  exists either way — analogy, not citation.)
- **Elimination.** External RF was ruled out by scanning with the antenna detached, which
  R3.3 makes safe: the lift *and the whole floor* were unchanged. That refuted the entire
  external-RF family in one shot and reframed the problem as internal to the chip.

> ⚠ **M1 — the reset and the settle are a matched pair.** `SpectrumResetRx()` forces
> `STDBY_RC` so the following RX entry re-acquires gain. That entry now costs a PLL relock,
> so `RssiSettleUs()` returns a flat **1000 µs** for LR1121. **Shortening the settle without
> removing the reset reintroduces the lift; removing the reset without shortening the settle
> merely wastes time.** Hardware-confirmed on the Nomad.

Applied on **both** bands, not just the one that showed the symptom — gain carryover is a
measurement error wherever it occurs; 2.4 GHz simply never made it visible. SX1280 paths are
left exactly as validated (§9 explains why that is the right call rather than timidity).

#### 2.3.3 Settling time

Reading before the receiver has settled returns garbage, so this is not optional.

| Family | Settle | Provenance |
|---|---|---|
| SX1280 | SF5→100 µs, SF6→141, SF7→218, else 480; FLRC 80 | LBT bench data |
| SX127x | 240 µs | inherited, **unmeasured** |
| **LR1121** | **1000 µs flat** | Semtech `SWSD003` `spectral_scan` |

> ⚠ **The LR1121 value deliberately ignores SF and modulation. Do not replace it with a
> generic settle table.** Generic tables answer "how long until RSSI is valid on an
> *already-running* receiver" — 22 µs, 240 µs, that order. This sweep asks a strictly larger
> question, because it drops to `STDBY_RC` and re-enters RX on *every* bin, so the delay must
> also cover PLL relock and full AGC re-acquisition. Shortening it while the per-bin reset
> stays brings the floor lift straight back (M1). `TxSpectrum.cpp` repeats this warning at
> the function itself.

**Sensing config, not link config.** The scan need not use the configured air rate;
configuring for the shortest settle is legitimate, and since we reboot to exit, clobbering
the radio config is free. `Config()` is called per band on cross-band devices because
crossing 900↔2.4 on an LR1121 selects the PA path and RF switch — it is not a bare frequency
write. Sync words are irrelevant; we read instantaneous RSSI and never demodulate.

### 2.4 Sweep topologies

| Topology | Condition | Behaviour |
|---|---|---|
| **Single-radio** | one radio, or a cross-band device | `positions == totalBins`; the band-matched radio sweeps |
| **Same-band split** (Gemini) | `isDualRadio() && !hasSecondBand` | radio 1 takes bin `i`, radio 2 bin `i + n/2` → **2× sweep rate** |
| **Cross-band** (Nomad) | `isDualRadio() && hasSecondBand` | one band-matched radio per band; the other idle. **Not** a split |

The split falls out of a quirk: `RXnb()` takes no radio argument — it forces **both** radios
into RX. That looks like a limitation and is an advantage, and it is structurally identical
to what Gemini already does in flight.

The cross-band case is why `activeRadio` exists and why `scanBand` selects the band
*explicitly* rather than reading the link's active-band globals: the page button must be able
to scan a band the link is not using.

> **Antenna-compare is designed but NOT built.** Putting both radios on the *same* bin would
> show a per-antenna delta — an instant feedline/antenna fault check. It is the same sweep
> with one `if` changed, and `TX_SPECTRUM_FLAG_MODE_COMPARE` / `_RADIO_2` are reserved for
> it. **The flags existing is not evidence the mode does.** Nothing sets them today.

**One screen per band. Do not build a combined plot.** On a cross-band target it is tempting
to render 900 and 2.4 on one axis, and it produces a plot that looks right and reads wrong:
the two bands are ~1.5 GHz apart with nothing between them, so a shared x-axis is either
mostly empty or silently non-linear, and a cursor readout means two different things
depending which half it is in. The page button flips bands instead. This also keeps Lua heap
flat — one band's tables at a time, max 2×80 — which matters given §4. The unviewed band's
max-hold starts cold on return, which is the honest behaviour: nothing was being measured.

**Sweep rate**, which the AGC reset changed materially:

| Target | Bins | Per bin | Sweep | Rate |
|---|---|---|---|---|
| SuperG (split, SF5) | 80 ÷ 2 radios | ~160 µs | ~6.5 ms | ~150 Hz |
| Nomad 900 | 40 | ~1 ms + relock | ~50 ms | ~20 Hz |
| Nomad 2.4 | 80 | ~1 ms + relock | ~100 ms | ~10 Hz |

Every one still outruns its own display update — 5 Hz for an 80-bin band, 10 Hz for a
40-bin one (§4) — so the live trace stays fed.

### 2.5 Max-hold is mandatory, not a nice-to-have

A swept single-bin receiver mostly *misses* bursty traffic. A single sweep of a busy band
looks empty; WiFi beacons are ~102 ms apart. **Ship max-hold from day one or the feature
will look broken while working perfectly.**

**It is accumulated on the TX and must stay there.** It looks like removable wire traffic —
the handset could derive it from the live trace and halve both the frames and its own GC
load — but the handset only sees a trace at 5–10 Hz while the TX sweeps faster. A
handset-side max-hold would miss the sweeps in between, failing at exactly the bursty-traffic
job it exists for. The AGC reset narrowed this margin (from ~30× on the SuperG to ~2× on the
Nomad); the argument still holds but is no longer overwhelming.

### 2.6 Accuracy — what the numbers are and are not

**Amplitude.** ELRS carries a per-target receive-path gain figure
(`power_lna_gain`, a dB value, 12 dB on both the Gemini layout and the Nomad), so reporting
`rssi - power_lna_gain` gives roughly antenna-referred dBm. Where absent, `lnaGainDb == 0`
and the axis is raw front-end dBm.

Accuracy is ±several dB — RSSI is uncalibrated and RBW-dependent. This is a decent
*relative* instrument with a plausible absolute axis, not a lab analyzer:

- **The idle floor is receiver-noise-limited, not ambient.** With the antenna detached the
  whole floor was unchanged, so the sloped top-of-band rise is the receiver's own frequency
  response rather than signals.
- **`SET_RSSI_CALIBRATION` (0x0229) is never called anywhere in ELRS.** The factory default
  is calibrated for 868–915, so sub-GHz is roughly right and 2.4 GHz carries a static offset.
  First thing to pull on if absolute accuracy is ever wanted.
- **`CalibImage` spans only the primary band's range**, so a radio retuned to the other band
  may read slightly hot or cold.

**Frequency** is a separate axis with its own budget, coarser than the readout's one decimal
suggests. Resolution *is* the FHSS channel step: 600 kHz on FCC915, 1 MHz on ISM2G4. There is
nothing between bins, so a signal anywhere inside one is reported at that bin's centre —
worst-case peak error **±300 kHz / ±500 kHz**, inherent to sampling the hop grid.

No shipping grid lands on an integer MHz (FCC915 is `903.5 + 0.6n`, ISM2G4 `2400.4 + 1.0n`),
which is why the cursor prints **one decimal**. An integer label misstates *every* bin on
both grids — truncating understates by up to 0.9 MHz on FCC915, more than that band's own bin
step, so two adjacent bins could print the same number.

> **The decimal identifies the bin. It does not claim 100 kHz accuracy.** `914.9MHz` means
> "the bin centred at 914.9", i.e. energy somewhere in 914.6–915.2.

**RBW is not the bin step.** Sensing bandwidth comes from the air-rate config and is
generally wider, so one strong carrier lights neighbouring bins too. That skirt is filter
shape, not a family of real signals — and it is why the CW regression check asserts a tone
lands on the *right* bin, not on *only* one.

### 2.7 Backpack and LED states

Both the SuperG and the Nomad set `use_backpack: true` — an ESP8285 running WiFi/ESPNOW **on
2.4 GHz, centimetres from the radio we are measuring with**. Left running, we would measure
our own backpack. `devBackpack.cpp` disables it via an **equality** check, which a new state
is not covered by:

```c
disabled = disabled || connectionState == spectrumScan;   // added by this feature
```

This is a **measurement-correctness requirement**, not a nicety, and is one of only two edits
to existing logic (the other being the enum). `devRGB.cpp` and `devLED.cpp` each gain a
`case` so it is unmistakable that the link is intentionally down.

### 2.8 Intervention points — the audit surface

Everywhere this feature reaches into radio state. **If you are reviewing radio-facing risk,
this table is complete.**

| # | Point | Call | Purpose | Family |
|---|---|---|---|---|
| I1 | scan entry | `hwTimer::stop()` | R3.1a — the TX interlock | all |
| I2 | scan entry | `POWERMGNT::setPower(MinPower)` | gesture only — **not** what enforces R3 | all |
| I3 | scan entry | `detachInterrupt(DIO1[, DIO1_2])` | sweep takes exclusive SPI ownership | LR1121 |
| I4 | band select | `Radio.Config(..., activeRadio)` | cross-band retune: PA path + RF switch | LR1121 |
| I5 | per bin | `SpectrumResetRx()` → `SetMode(STDBY_RC)` | AGC re-acquisition (§2.3.2) | LR1121 |
| I6 | per bin | `SetFrequencyReg(f, radio, doRx=true)` | retune + RX entry (§2.3.1) | all |
| I7 | per bin | `delayMicroseconds(settleUs)` | PLL relock + settle (§2.3.3) | all |
| I8 | per bin | `StartRssiInst()` / `GetRssiInst()` | the measurement | all |

**On I3** — this closes a real unsynchronised-SPI race (an async RX-done ISR transacting on
the radio the sweep is mid-read on), but it **fixed no observed symptom**. It was written for
a diagnosis of the 915 floor lift that testing refuted; the cause was §2.3.2. Kept on its own
merits. **Do not cite it as the lift fix** — it was already in place while the lift was still
occurring.

---

## 3. Control and transport

**Lua parameters.** A `Spectrum` folder with Start, band select (cross-band targets only),
max-hold reset, and step/dwell controls. Headroom is fine — `MAX_CRSF_PARAMETERS` is 64. The
scan-entry command reuses `handleWifiBle`'s existing "enter a mode that drops the link"
machinery (function pointer + target state + confirm/running text) rather than duplicating
its state machine.

**Wire format: CRSF frame type `0x30`**, TX module → handset, extended header. Full layout
and codec are in [`TxSpectrumProtocol.h`](./TxSpectrumProtocol.h), which is dependency-free
and natively unit-tested. Key properties:

- **Big-endian**, matching CRSF convention and the sibling `0x2E`. Not cosmetic: the decoder
  is `elrs.lua`, which already ships a big-endian reader (`fieldGetValue()`), so BE lets it
  reuse that helper instead of a hand-rolled decode that could silently disagree.
- **The frequency axis is repeated in every frame**, so a decoder joining mid-sweep never
  guesses and a dropped frame cannot shift the axis.
- **`SWEEP_END` is set by the encoder** from the data, not by the caller, so it cannot
  disagree with the bins it describes.
- 40 bins/frame: an 80-bin 2.4 GHz sweep is exactly two frames, a 40-channel FCC915 sweep
  exactly one. Payload 52 B against a 58 B budget.

**Pacing, not bursting.** One frame per 50 ms, traces alternating, so a full live + max-hold
update lands at **10 Hz for 40 bins, 5 Hz for 80**. This is required, not tuned: on B&W
handsets the Lua telemetry queue is a single 255-byte FIFO shared with ELRS's own
`0x29`/`0x2B`/`0x2D`/`0x2E` traffic — about four of our frames. It is also **lazily
allocated** (NULL until Lua's first `crossfireTelemetryPop()`), so frames sent before the
script's first poll are silently dropped.

EdgeTX forwards unknown extended frame types to Lua on every version from 2.8.4 to current
(known types get a `case` and are consumed; unknown ones fall to `default:`). `0x30` is
unclaimed in all of them. **Hazard direction:** if it were ever claimed upstream it would
become consumed and hidden from Lua, and these frames would silently stop arriving.

---

## 4. Handset Lua

`elrs.lua` gains the Spectrum view: live + max-hold plot, a movable cursor reading bin centre
and level, and page-button band flip on cross-band targets. The folder only appears when the
module advertises the parameter, so stock modules show nothing new.

**The cost is not flag-gated.** A Lua script cannot be compiled out, so the script grows for
every handset that loads it. Measured on a real Boxer (EdgeTX 2.12.1) via
`lua/mockup/heapchk.lua`: **+6.9 KB loaded, +16% Lua heap** (source grew +38%, but comments
are stripped by the lexer). It loads and runs.

**Simulation harness.** `TxSpectrumDecodeFrame()` being unit-tested says nothing about the
handset: `parseSpectrumMessage()` is a hand-rolled second decoder with no coverage of its
own. `lua/mockup/simcheck.py` drives the *real* `elrs.lua` in a real Lua VM against an EdgeTX
stub, fed golden vectors generated from the firmware encoder
(`gen_spectrumgolden.cpp` → `spectrumgolden.lua`). It catches the whole silent-mismatch class
— endianness, bin offsets, index base, the EXIT lockout — in about a second:

```
cd src/lua && python mockup/simcheck.py
```

---

## 5. Building and flashing

Set the flag and domain in `src/super_defines.txt` (gitignored, read after
`user_defines.txt`, so it overrides and survives pulls):

```
-DTX_SPECTRUM_SCAN
-DRegulatory_Domain_ISM_2400      # or FCC_915 etc. for sub-GHz
```

| Target | Env | Pick at the menu |
|---|---|---|
| SuperG | `Unified_ESP32_2400_TX_via_ETX` | `BETAFPV SuperG 2.4GHz Gemini TX` |
| Boxer | `Unified_ESP32_2400_TX_via_ETX` | `RadioMaster Boxer Internal 2.4GHz TX` |
| Nomad | `Unified_ESP32_LR1121_TX_via_ETX` | `RadioMaster Nomad 2.4/900 TX` |

```
cd src && pio run -e <ENV>
```

The `_via_ETX` / `_via_UART` / `_via_WIFI` suffixes all produce the same `firmware.bin`; they
differ only in what `-t upload` does. Flash the SuperG and Nomad over WiFi (module's own AP);
the Boxer's internal module needs stock flashed via Configurator/EdgeTX passthrough first,
then WiFi OTA. **Copy `src/lua/elrs.lua` to the SD card at `\SCRIPTS\TOOLS\`.**

> **Two benign failures you will see.** A `FAILED` at the post-link
> `UnifiedConfiguration.appendConfiguration` step means it wanted an interactive product
> choice; it runs *after* `firmware.bin` is written and does not touch it. Pipe
> `echo "" | pio run` to leave the image bare, or patch it afterwards with
> `python python/binary_configurator.py --tx --target <t> --auto-wifi=-1 <bin>`. A headless
> build always hits this.
>
> **A bare image bricks nothing but wastes a cycle** — it boots with no hardware layout. Check
> before flashing: the embedded JSON should carry the expected `radio_busy` pin.

---

## 6. Test status

| Target | Topology | Feature validated | AGC fix |
|---|---|---|---|
| RadioMaster Nomad | cross-band | ✅ full ladder on 4.1.0 | ✅ confirmed |
| BETAFPV SuperG | Gemini split | ✅ full ladder on 4.0.1; CW re-check on this branch | n/a — LR1121 only |
| RadioMaster Boxer internal | single-radio | ✅ bring-up on 4.1.0; CW re-check on this branch | n/a — LR1121 only |

A CW carrier peaks on the **2440.4 MHz** bin on both SX128x targets — the correct bin, since
ISM2G4 is `2400.4 + 1.0n`. **That the two topologies agree is the substantive part:** the
split sweep interleaves two bins per dwell across two radios, and an off-by-one there would
shift the peak on the SuperG while leaving the single-radio Boxer correct.

Off-hardware gates: `pio test -e native` (includes 14 `test_txspectrum` cases) and
`lua/mockup/simcheck.py`. CI builds the feature for one target per radio driver so it cannot
bit-rot.

**Regression checks a measurement change must re-run:**

| Check | What it catches |
|---|---|
| **CW spike on the right bin, both bands** | The important one — a gain or settle change can fix the floor while blunting real-signal response |
| **Distinct per-bin RSSI** | The silent `SetMode` early-return failure (§2.3.1) |
| Band flip (cross-band targets) | `activeRadio` / `Config()` routing |
| Arm guard both ways | R1.4 |
| Flag-off byte-identity | R1.1 |

> **Reading the CW result.** `freq_center` need not land on a bin. A 915.000 MHz tone lands on
> bin 19, centred at **914.9 MHz**; 2440.000 lands on bin 40 at **2440.4 MHz**. Both correct —
> a tone one bin off-centre is the grid, not an error. Treat a *whole* number as suspect.

---

## 7. Known debt

**Not debt — a deliberate value.** `RssiSettleUs()`'s LR1121 branch is scan-specific and must
not be collapsed into a generic table; see the warning in §2.3.3. An earlier review
recommended exactly that, and acting on it reintroduces a hardware-confirmed bug.

**Remaining — all blocked on the same trade.** Each requires editing a flight-critical shared
header (`common.h` or `FHSS.h`) and rewriting existing flight call sites. That is what R1.2
forbids on this branch, and it would void the byte-identity property the qualification rests
on. **Recommendation: do these as a separate upstream-facing branch.** They are improvements
to *ELRS*, not to this feature — the feature merely declines to make them worse.

- **`ReadRssiInst()` is a local per-family dispatch shim.** Pure dispatch with no policy, so
  unlike `RssiSettleUs` it *is* safely shareable; the right home is beside `isDualRadio()` in
  `common.h`. **If you add a radio family, this shim needs a branch.**
- **`BinToRadioFreq()` is a tenth open-coding** of `freq_start + spread * ch / SCALE`.
  `FHSS.h` spells it out in eight places and offers no channel→frequency helper. The copies
  are not uniform — some subtract `FreqCorrection`, some `FreqCorrection_2`, some neither —
  which is exactly the axis `BeginScan()` latches explicitly to avoid.
- **No `FREQ_REG_VAL_TO_HZ`.** Everything converts Hz→reg; `ComputeAxis()` is the first
  reverse conversion in the tree and re-derives per-family unit knowledge in its own `#ifdef`.
- **`TxSpectrumHasSecondBand()` is a third copy** of the two-band predicate, byte-identical to
  the band-selector gate in `TXModuleParameters.cpp` and to `has_low_band`/`has_high_band` in
  `devWIFI.cpp`. **They must stay in agreement.**

---

## 8. Portability to other radio families

The measurement cycle is family-agnostic; the *state management* around it is not. Only the
LR1121 needed I3 and I5.

| | **SX127x** | **SX1280** | **LR1121** |
|---|---|---|---|
| RSSI call | `GetCurrRSSI` | `GetRssiInst` | `StartRssiInst` + `GetRssiInst` |
| Settle | 240 µs *(unmeasured)* | 100/141/218/480 by SF; 80 FLRC | **1000 µs flat** |
| Per-bin AGC reset (I5) | no | no | **yes** |
| ISR detach (I3) | no | no | yes |
| `SetMode` visibility | **public** | private | private |
| AGC carryover observed? | **untested** | **not observed** | **confirmed, fixed** |

**Do not read "not observed" as "not present."** The SX1280 targets are 2.4-only, and 2.4 is
where the symptom was hardest to see even on the affected part. Gain carryover is a plausible
property of any AGC-equipped transceiver held in continuous RX across retunes; only the
LR1121 has been *shown* to exhibit it. The SX1280 path was left exactly as validated because
changing a validated path to chase an unobserved defect trades certain risk for uncertain
benefit — not because the risk is known absent.

**If you suspect carryover on another family**, the discriminating test needs no new silicon:
force `STDBY`→RX per bin with a generous settle and see whether an intermittent uniform floor
lift disappears. A *fixed* bias is not this bug — carryover is intermittent and dwells for
seconds.

**Porting I5 — checklist:**

1. **Does the family re-acquire gain on a retune in continuous RX?** If yes, stop; I5 is pure
   cost.
2. **Reach a standby mode.** For a private `SetMode`, add a gated inline wrapper next to the
   RSSI calls (see `LR1121.h::SpectrumResetRx`) — **not** a new public driver API, which would
   widen the surface and weaken R1.1.
3. **Raise the settle** to cover PLL relock + gain acquisition, and record where the number
   came from. Honour M1: the two move together.
4. **Confirm S1 still holds** — the chunk budget should absorb it with no other change.
5. **Re-run the CW test.** A per-bin reset is exactly the change that could fix a floor
   artifact while blunting response to real signals. This is the check that matters.

---

## 9. Upstreaming notes and known limits

The radio-facing footprint is deliberately minimal, which is what makes this cheap to review
and cheap to rebase:

- **One driver file is touched:** `LR1121.h`, +8 lines, entirely inside the flag guard.
  `LR1121.cpp` is byte-identical to stock; the SX1280 and SX127x drivers are untouched.
- **The wrapper is an inline over an existing private method.** It adds no new capability and
  is unreachable with the flag off.
- **No new public driver API, no config fields, no wire changes.**

Expect these discussion points:

1. **The private-method wrapper.** A maintainer may prefer a driver-owned
   `RestartRx()`/`ResetAgc()` primitive — a reasonable outcome that would let the `#if`
   disappear. Making `SetMode` public is the worse alternative for R1.1.
2. **Whether the AGC fix belongs to the analyzer at all.** Arguably any continuous-RX
   measurement loop in ELRS has this latent. It is scoped to the sweep because that is where
   it was observed and validated; a driver-level fix would need its own qualification.
3. **The flat 1 ms settle** reads as unsophisticated next to the SF-aware tables. §2.3.3
   explains why — keep that reasoning attached to the constant if it moves.
4. **`0x30`** must be settled before the wire format is frozen.

**Known limits:**

- **Absolute accuracy is not established** (§2.6): the idle floor is receiver-noise-limited
  rather than ambient, and `SET_RSSI_CALIBRATION` is never called.
- **`lnaGainDb` back-out assumes `power_lna_gain` is set**; where absent the axis is raw
  front-end dBm.
- **Never reached, therefore untested, as noise sources:** the LR1121 DC-DC regulator
  (`radio_dcdc: true`), the idle second radio's synthesiser on cross-band targets, and USB
  charger noise. The AGC fix landed before these were investigated — nothing implicates them,
  and nothing clears them.
