# TX Spectrum Analyzer — Status

_Last updated: 2026-07-18_

High-level status and decision history for the experimental **TX-side** spectrum
analyzer. For the design and its rationale see [`DESIGN.md`](./DESIGN.md); this file
is the orientation doc — read it first.

## What this is

An optional ELRS **transmitter** mode (`-DTX_SPECTRUM_SCAN`) driven from the handset's
`elrs.lua`. It halts the link, sweeps the FHSS channel grid receive-only measuring
RSSI, and streams the trace to the handset as CRSF frame `0x30` for a live plot.
Exits by reboot. Feeds a **future** FHSS sub-band picker (explicitly out of scope).

Target: **BETAFPV SuperG Nano** (SX1280 Gemini dual-radio, ESP32). Handset:
**RadioMaster Boxer** (128x64 mono, EdgeTX 2.12.1).

## Branches

| Branch | Base | Purpose |
|--------|------|---------|
| `tx-spectrum-nomad-4.1.0` | tag `4.1.0` (`a9d4a9cb`) | **Current. What we flash.** TX+RX suite migrated to 4.1.0. |
| `tx-spectrum-nomad-4.0.1` | `spectrum-scan-4.0.1-stable` (`b5a5b5fd`) | Preserved 4.0.1 fallback (SuperG P0-P5 + Nomad desk-complete). |
| `tx-spectrum-superg-4.0.1` | `spectrum-scan-4.0.1-stable` (tag `4.0.1`, `b5a5b5fd`) | Original single-band SuperG line. |

Pushed to the `skyscanner` fork (`github.com/GuyGranot/skyscanner`).
`origin` is the public ExpressLRS upstream — do not push there.

## Port to ELRS 4.1.0 (2026-07-18)

The TX analyzer was re-integrated onto the **4.1.0** release base (`a9d4a9cb`, tagged
2026-07-14) on branch `tx-spectrum-nomad-4.1.0`. Done as a **clean re-integration**, not a
rebase — the self-contained modules (`lib/TxSpectrum`, tests, mockup) carry over untouched;
the shared-file hooks were re-applied by hand against 4.1.0's refactored code.

**Upstream structural changes handled:**
- `crsf_protocol.h` was **moved** to `src/include/` (commit `815ade62`), not deleted — the
  `0x30` frame-type enum was re-homed there.
- `elrs.lua`: **3-way merged** with upstream's folder-title / `handsetId` `0xEF`→`0xEA`
  changes. Clean, no conflicts; the band-switch uses the `handsetId` variable so it inherits
  the new address.
- `elrsmock.lua`: spectrum mock fields renumbered to id 27/28 to clear 4.1.0's new
  "Nested Test" (ids 20-26); `SCAN_FIELD_ID` in `spectrummock.lua` matched.

**Three API breaks our safety/scan code depended on — found by the build, fixed:**
1. `LR1121::Config()`'s `bool setFSKModulation` param **and** the `RADIO_TYPE_*` enum both
   became `RadioBandMod::Combined` (band+modulation, radio-agnostic). `mp->radio_type` is
   already that type, so `Config()` takes it directly; both `RssiSettleUs` copies updated to
   `RadioBandMod::Combined::*`.
2. `Handset::IsArmed()` → **global `isArmed`** (`OTA.h`). The arm guard reads it now. Verified
   it still updates **mid-scan**: the CRSF channel handler (`TXModuleEndpoint.cpp:151`) sets it
   on every incoming channel frame — independent of OTA generation, which is halted during a
   scan — and still fires `EVENT_ARM_FLAG_CHANGED` with the AUX1/CH5 convention. P4 preserved.
3. `rebootTime` global → **`scheduleRebootTime()`** (`rxtx_intf.h`). The arm-abort uses it.

**Safety re-audit vs 4.1.0 line numbers — all held:** R3 receive-only (the LR1121 scan-path
functions `SetFrequencyReg`/`StartRssiInst`/`GetRssiInst`/`SetPaConfig` are **unchanged**; the
only `LR1121.cpp` edits were the `Config` signature + `useFSK`→`modulation` rename, and the
three TX openers stay off-path); `SX1280::SetMode` guard still commented out (upstream added a
comment explaining why — SuperG mechanism safe); FHSS dual-band model, `isDualRadio()`,
`POWER_OUTPUT_VALUES_*` predicates all intact.

**Desk validation on 4.1.0:** LR1121 TX builds (RAM 21.9% / Flash 86.0%), LR1121 RX builds
(RAM 21.6% / Flash 63.8%), native `test_txspectrum` 14/14 + `test_spectrum` 10/10, `simcheck`
18/18 incl. T7 band-flip. **N0 flag-off byte-identical proof: ✅ PASSED** — both LR1121 TX
flag-off images 1,694,848 B, only the two SHA-256 blocks differ (app_elf_sha256 + trailing
image hash); masked, byte-identical. Flag-off ships stock 4.1.0. Same method/caveat as the
4.0.1 N0 (see the 4.0.1 section below).

**Nomad hardware bring-up on 4.1.0:**
- ✅ **N2 PASSED (2026-07-18):** trace varies on **both** bands (Config/SetMode trap cleared on
  each -> band-matched-radio routing confirmed, r1=sub-GHz / r2=2.4); paired RX **failsafes on
  both bands** (R3.2 = receive-only proven empirically, doubled for the 900 + 2.4 PAs). Page
  button flips 900 <-> 2.4. First flash of the 4.1.0 image via the WiFi web-UI path.
- ✅ **N3 PASSED (2026-07-18):** CW emitter (RX in `/cw` mode) at band center lands on the
  right bin on both bands -- 915.0 MHz mid-`SPEC 900`, 2440.0 MHz mid-`SPEC 2.4`. Axis mapping
  correct per band, sub-GHz settle adequate (clean spike over a real noise floor), CalibImage
  accuracy fine on both. (CW freq is fixed at `freq_center`, not arbitrary.)
- ✅ **N4 PASSED (2026-07-18):** arm guard works both ways -- Start Scan refuses while armed
  ("Disarm first"), and arming mid-scan aborts (reboot back to a working link). Confirms the
  `handset->IsArmed()` -> global `isArmed` migration: the global is driven mid-scan by the CRSF
  channel handler + `EVENT_ARM_FLAG_CHANGED`, exactly as the SuperG behaved.
- ✅ **N5 PASSED (2026-07-18):** flashed stock (flag-off) 4.1.0 -> boots clean, links, config
  intact, Spectrum folder absent (on-device confirmation of the N0 byte-proof). R2 rollback safe.

**🎉 The RadioMaster Nomad cross-band port on ELRS 4.1.0 is HARDWARE-VALIDATED end to end
(N0-N5, 2026-07-18).** Both bands scan receive-only, page button flips 900<->2.4, arm guard
holds, rollback is clean. Branch `tx-spectrum-nomad-4.1.0` on the `skyscanner` fork.

Note: many `DESIGN.md` line-number citations were written against 4.0.1 and are now approximate.

## 915 idle-floor lift — root-caused & FIXED (2026-07-20)

> The **architecture** of the fix — intervention points, the reset/settle invariant (M1),
> cross-family portability and upstreaming notes — lives in [`DESIGN.md`](./DESIGN.md)
> §3.3.3, §3.9 and §11. This section is the evidence trail.


**Symptom (field-reported):** on 915 the whole trace intermittently lifted ~20 dB to a
uniform **-83..-85 dBm** for several seconds, then dropped back to the correct *sloped*
floor (~-100 dBm across the span, ~-85 at the top). Uniform + band-wide + peakless +
multi-second dwell ⇒ not a real per-bin signal and not a fixed settle bias.

**Root cause: AGC gain-state carryover across bins.** The sweep enters RX_CONT and never
leaves it, retuning per bin only. RX_CONT does **not** re-run gain acquisition on a bare
retune, so a gain step the AGC picks for one bin persists into every bin after it — and
once it steps down, the whole trace reads that step's gain-limited floor. Three
corroborations:

- **Level fingerprint:** LR1121 UM §7.2.15's per-gain-step table puts the top gain steps at
  **-81..-87 dBm**, <1.5 dB apart. The lift sat at **-83..-85 dBm** — the floor of a
  stepped-down AGC, not broadband noise.
- **Sibling-part errata:** RadioLib documents SX126x reporting RSSI **up to 35 dB high**,
  self-recovering, cured by *restarting the AGC*. (No LR11xx errata exists either way.)
- **Explains every failed fix:** the ISR detach was irrelevant (state is in the chip);
  longer settle only half-helped (dwell aids convergence *within* a bin; staying in RX never
  resets gain).

Suspected trigger: false preamble detect (LoRa CRC is off, so noise can raise detection;
Semtech's gateway HAL exposes `AGC_FREEZE_ON_DETECT`, default on). Inference, not documented
for LR11xx — and not needed for the fix.

**Fix (hardware-confirmed on the Nomad, 2026-07-20 — lift gone):**
- `LR1121Driver::SpectrumResetRx()` — `TX_SPECTRUM_SCAN`-gated public wrapper over the
  private `SetMode(LR1121_MODE_STDBY_RC, …)` (`LR1121.h`).
- `SweepChunk()` calls it under `#if defined(RADIO_LR1121)` before each bin's retune, so RX
  entry re-acquires gain. Applied on **both** bands: gain carryover is a measurement error
  wherever it happens, 2.4 simply never made it visible.
- `RssiSettleUs()`'s LR1121 branch is a flat **1000 µs**, replacing the LBT-derived
  22/40/240 µs. Those only covered RSSI-valid time on an already-running receiver; a per-bin
  STDBY→RX cycle must also cover PLL relock + AGC re-acquisition. 1 ms is Semtech's own
  `DELAY_BETWEEN_SET_RX_AND_VALID_RSSI_MS` (`SWSD003` LR11xx `spectral_scan`).
  **The reset and the settle are a pair — shortening one without removing the other
  reintroduces the lift.**
- SX1280 (SuperG/Boxer) paths untouched: P0-P5 validated, symptom never observed.

**Known cost:** sweep rate drops (≈20 Hz for a 40-bin sub-GHz sweep, ≈10 Hz for 80-bin 2.4,
vs ~40-50 Hz before). Both still at/above the 10 Hz emit rate so the live trace stays fed,
but TX-side max-hold now integrates fewer sweeps per second — the argument for keeping
max-hold on the TX (DESIGN.md 3.5) is weakened proportionally, not reversed.

### How it was found — and what was wrong on the way

Recorded because each wrong turn cost a flash cycle, and the eliminations are what made the
answer findable. The **2026-07-19 "OTA RX ISR race" diagnosis was published in this file as
root-caused & fixed, and was wrong.**

**What was eliminated, in order** — each row cost a flash cycle:

| Hypothesis | Test | Result |
|---|---|---|
| OTA RX ISR race | DIO1 detached | lift unchanged — **refuted** |
| Wrong regulatory domain | rebuilt `domain: 1` | fixed the span; lift unaffected |
| Module's own WiFi | `wifi-on-interval: -1` build | lift unchanged — **refuted** |
| Backpack ESP8285 | already `EN` low in `spectrumScan` | not a factor |
| Fixed settle bias | 3 ms override | "behaves better", lift persists — partial |
| External RF blocker (cellular etc.) | phone airplane mode | lift unchanged — **refuted** |
| Any antenna-borne RF | sub-GHz antenna detached | lift unchanged, **whole floor unchanged** — **refuted** |
| AGC gain carryover | STDBY_RC per bin + 1 ms settle | **lift gone — CONFIRMED** |

The antenna test was the turning point: it put the artifact **inside the chip** and killed
the entire external-emitter family at once. It also surfaced a finding independent of this
bug — with no antenna the *whole floor* was unchanged, so the idle trace is
**receiver-noise-limited, not ambient**: the sloped top-of-band rise is our own frequency
response, not signals. That is an absolute-accuracy question for the analyzer (note that
`SET_RSSI_CALIBRATION`, 0x0229, is never called anywhere in ELRS), tracked separately.

Because the confirming test came before them, the DC-DC (`radio_dcdc: true`), idle-2.4-radio,
and USB-charger suspects were **never reached and remain untested**. Nothing implicates them.

**Ruled out as aggressors by inspection:** RGB LEDs (the `spectrumScan` blink is an 800 ms
period — too fast for a multi-second dwell) and the fan (gated on
`currPower >= PowerFanThreshold`; the scan drops to minimum power on entry, so it spins down
rather than cycles).

**Residue from the wrong turn, deliberately kept:** `SweepIsolateRadio()` (the DIO1 detach)
stays in `TxSpectrum.cpp`. It closes a real unsynchronised-SPI race — an async ISR
transacting on a radio the sweep is mid-read on — but it fixed **no observed symptom**, and
its comment says so. Everything else from that investigation (the `TxSpectrumRxDoneIsrCount`
counter in `LR1121.cpp`, `SweepDebugTick()`, `-DTX_SPECTRUM_DEBUG_KEEP_RX_ISR`,
`-DTX_SPECTRUM_SETTLE_US_OVERRIDE`) has been removed; `LR1121.cpp` is byte-identical to
stock again, so the only driver change the feature carries is the gated `SpectrumResetRx`
wrapper in `LR1121.h`.

**Post-fix regression, on the cleaned build (`nomad-915-FIXED-clean.bin`):**

- ✅ **Lift gone** — confirmed again on the cleaned build, so removing the scaffolding did
  not disturb the fix.
- ✅ **N3 RE-PASSED (2026-07-20), both bands.** CW landed at **2440 MHz** on 2.4 and
  **914 MHz** on 900. Both correct: `freq_center` is 915.000 MHz exactly, but the FCC915 grid
  is `903.5 + 0.6n`, so **no bin sits on 915** — the nearest is bin 19, centred at
  **914.9 MHz** (100 kHz off, well inside one 600 kHz bin). The integer labels seen during
  this test were an artefact of the axis label, since fixed (below). The 2.4 case is the same
  shape: nearest bin is 40 at 2440.4 MHz. **This is the check that mattered** — a per-bin AGC
  reset could have fixed the floor while blunting real-signal response, and it did not.

**Still owed:** N2 (per-bin variation both bands), band flip, N4 arm guard, and the N0
flag-off byte-identity proof.

**Axis label — now one decimal (2026-07-20).** Fixed in two steps, and the first step was
not enough.

The label used `floor()`, which understated every bin: no shipping grid lands on an integer
MHz (FCC915 `903.5 + 0.6n`, ISM2G4 `2400.4 + 1.0n`), so the error was a constant 0.4 MHz on
2.4 and up to 0.9 MHz on FCC915 — more than that band's own 0.6 MHz bin step, meaning two
adjacent bins could print the same number. Switching to rounding made the N3 tone read
`915`/`2440`, which *looked* right but was equally wrong: **it claimed a whole-MHz centre no
bin actually has.**

The readout now prints the bin centre to **one decimal** — `914.9MHz`, `2440.4MHz` — via
`string.format` rather than `drawNumber(..., PREC1)`, because the format path is already
proven in this script whereas an unregistered `PREC1` would silently render `9149`. What the
decimal does and does not claim is documented in DESIGN.md §3.6.1: it identifies the *bin*,
not the frequency of a signal within it (±300 kHz on FCC915, ±500 kHz on ISM2G4).

The golden oracles became **strings** rather than numbers, since the value is now drawn as
text: `bin1Label`, `binLastLabel`, `notchLabel` in both `expect` and `expect900`. That also
keeps THE endianness check working — it asserts the rendered label, which is what a byte-order
error would corrupt.

**Ruled out as aggressors by inspection:** RGB LEDs (scan blink is an 800 ms period — too
fast for a multi-second dwell) and the fan (gated on `currPower >= PowerFanThreshold`; the
scan drops to minimum power on entry, so it spins down rather than cycles).

## Qualification progress

| Phase | Status | Evidence |
|-------|--------|----------|
| **P0** Wire codec | ✅ **14/14** native, on Windows | `pio test -e native -f test_txspectrum` |
| **P1** Build matrix | ✅ A/B/C green | build logs |
| **P1** Zero-footprint | ✅ flag-off **byte-identical** to stock 4.0.1 | see below |
| **Audit** Safety + integration | ✅ **done 2026-07-16** | this file, DESIGN.md 9.4/9.5/10.1 |
| **L1** Golden vectors | ✅ generated from the real encoder | `lua/mockup/spectrumgolden.lua` |
| **L2** Lua decode + render | ✅ **15/15** headless | `lua/mockup/simcheck.py` |
| **L2.5** Lua heap on real radio | ✅ **+6.9 KB (+16%), loads fine** — see below | `lua/mockup/heapchk.lua` |
| **P2** Bench bring-up | ✅ **PASSED 2026-07-17.** Plot renders; **trace varies** (the `SetMode` trap did not bite); **R3.2** RX shows zero packets + failsafe for the whole scan; reboot-exit re-links in ~2-4s; **S1** Bind refused mid-scan (trace unbroken, with a positive control). | SuperG + Boxer |
| **P3** Measurement sanity | ✅ **PASSED 2026-07-17** — max-hold fills in over ~5s where a single sweep looks empty. | SuperG + Boxer |
| **P4** Arm guard | ✅ **PASSED 2026-07-17** — arm *during* a scan aborts cleanly (reboots straight back into a working link); refusal-while-armed was also exercised at length (arming note below). | TX + handset |
| **P5** Rollback | ✅ **PASSED 2026-07-17** — flashed B, set a distinctive config, scanned, reflashed **stock 4.0.1**, config intact. The `TX_CONFIG_VERSION` discipline (R2) held. | TX |
| **P6** Gemini split / antenna-compare | ⏳ not started | TX |

The single-band 2.4GHz feature on the SuperG is **complete and hardware-validated (P0–P5)**.
The **RadioMaster Nomad (cross-band LR1121)** port is underway on `tx-spectrum-nomad-4.0.1`
— see the Nomad section below and `DESIGN.md §10`.

**Inertness is proven byte-identical, not merely symbol-free.** Building HEAD as a
control and diffing section *contents* (not just sizes):

| Build | text | data | bss | total |
|---|---|---|---|---|
| stock 4.0.1 / **flag off** | 1199300 | 448596 | 40897 | **1688793** |
| **B** (flag on) | 1200832 | 448792 | 41105 | 1690729 |

Both flag-off images hash identically (`249c2004…`). Re-run this after **every**
change that touches a file compiled into the flight image — `nm` showing no symbols
does not prove it. Method: stash, build, `objcopy -O binary`, compare.

## Build configs

Set flags in the gitignored `src/super_defines.txt`, or uncomment in `user_defines.txt`.

| ID | Flags | Use |
|----|-------|-----|
| A | *(none)* | control / baseline |
| **B** | `-DTX_SPECTRUM_SCAN` | **operational** |
| C | B + `-DDEBUG_LOG` | bench logging |

PIO env: `Unified_ESP32_2400_TX_via_UART`. The post-link step prompts for a product
config — pipe an empty line (`echo "" | pio run …`) to leave it bare, or it EOFs.

## The audit (2026-07-16)

The three base assumptions were re-verified against the tree rather than trusted.
**Two of three held cleanly; the hardware claim had two false sub-claims, both fixed.**

### Fixed

- 🔴 **"The scan never transmits" was false.** A stopped `hwTimer` is the *only* thing
  suppressing TX in any mode state (`timerCallback()` → `SendRCdataToRF()` → `TXnb()`
  has no `connectionState` guard). Three `hwTimer::resume()` sites were reachable
  during a scan and are now guarded under `TX_SPECTRUM_SCAN`:
  `EnterBindingModeSafely()` (`tx_main.cpp:1066`), `OnPowerSetCalibration()` (`:1009`),
  `UARTconnected()` (`:751` — the `resume()` sits *outside* the `if`). Bind was the
  concrete path: `devTXLUA.cpp:7` keeps Lua live below `FAILURE_STATES`, and a scan is
  deliberately there so lcsCancel can exit — live Lua means a live Bind.
- 🔴 **"Even a latent bug transmits at min power" was false.** `setPower()` only queues
  `pwrPending`; `CommitOutputPower()` runs from `TXnbISR` — the TX-**done** ISR — so the
  change lands *after* the next packet. `devWIFI` survives the same idiom only because
  `Radio.End()` follows. Comment corrected; the real interlock is the timer.
- 🔴 **`elrs.lua` documented the axis as little-endian.** It is big-endian, and the code
  three lines down said so. Leftover from the original LE design. Comment fixed.
- 🟡 Stale "four frames per 100ms" comment (it is 200ms at a 50ms interval); the
  axis re-latch now tests the axis, not just `total`.

### Closed by research, no test needed

- **Endianness.** `CRSFEndpoint.cpp:408` `htobe32(0x454C5253)` ↔ `elrs.lua:406`
  `fieldGetValue(...) == 0x454C5253` is a shipping closed loop across the same two
  layers. `'E'`=0x45 first only equals that under a BE reader.
- **The `+3` index base.** `crsf_ext_header_t` → `data[1]`=dest, `data[2]`=orig,
  `data[3]`=payload[0]; matches `parseDeviceInfoMessage` (`elrs.lua:397-398`) exactly.
- **`lcd.drawPoint` exists on B&W** — `api_stdlcd.cpp:631`, flags optional, zero
  `COLORLCD` guards in that file.
- **`0xEA` is always routable** — registered at `CRSFHandset.cpp:53-54`, and
  `CRSFConnector::devices` is insert-only. So `deliverMessageTo`'s missing broadcast
  fallback (unlike `deliverMessage`, which the 0x2E precedent uses) is a latent
  fragility, not a bug.

### Accepted, documented, not fixed

- 🟡 **Cross-core SPI race.** `Button_device` (core 0) can start WiFi → `Radio.End()`
  while core 1 is mid-`SweepChunk` (~1.3ms of blocking SPI). Upstream has no equivalent:
  core-1 radio SPI normally comes from the hwTimer ISR, which `hwTimer::stop()` ends
  deterministically before `Radio.End()`. A loop-core sweep has no such handshake. Not
  damaging — WiFi does not need the radio.
- ESP8285 + the flag is a raw compile error, not a clean `#error`.
- `device.cpp:33` `deviceTimeout[16]`; TX ESP32 count is **15** with the flag on.

## Test ladder

Each rung's honest scope. **The middle is only testable on hardware**: the simulator
has no firmware (so no router/FIFO/UART), and the native tests never run the Lua decoder.

| Rung | Runs on | Proves | Cannot prove |
|---|---|---|---|
| **L0** ✅ | `pio test -e native` | C encode/decode round-trip | anything about the Lua decoder |
| **L1** ✅ | g++ → Lua | encoder ↔ Lua decoder agreement | rendering, firmware, wire |
| **L2** ✅ | `mockup/simcheck.py` | Lua decode, render, EXIT lockout | **memory**, firmware, wire |
| **L2.5** ⏳ | **real Boxer, SD card only** | **Lua heap**, real primitives, R2.2 | firmware, wire |
| **L3** ⏳ | TX + handset | firmware → router → FIFO → UART | — |

`TxSpectrumDecodeFrame()` is unit tested but **the handset never runs it** —
`parseSpectrumMessage()` is a hand-rolled second decoder. L1+L2 exist because nothing
else compares the two. Golden frames come from the *real* encoder
(`mockup/gen_spectrumgolden.cpp`); hand-written frames would encode someone's reading
of the format, which is the thing under test.

**Green checks were mutation-tested, not trusted.** Flipping the C encoder to LE is
caught by `test_golden_vector_frame_is_pinned`; flipping `fieldGetValue` is caught
end-to-end by `simcheck.py` (`expected 2400, drew 2426414`); deleting `elrs.lua`'s
`fieldPopup` entry guard throws and kills the script.

## P2 bring-up notes (2026-07-17)

**The core gate passed on the first flash: the trace varies across bins.** So
`SetFrequencyReg(..., doRx=true)` really does re-enter RX and make the frequency
write take effect — the mechanism DESIGN.md 3.3 bet on, and the one upstream's
noise-floor branch gets wrong. **It works only because `SX1280::SetMode`'s
early-return guard is commented out (`SX1280.cpp:209-216`); if anyone restores it,
the symptom is a flat trace, which reads as a quiet band rather than a fault.**
That is the regression test, and it is now known to be discriminating.

**R3.2 passed: the paired RX shows zero packets and goes to failsafe for the whole
scan.** That is the empirical proof of R3 — no carrier, no PA keying — and it is the
claim the audit found was only *conditionally* true. Note what it does and does not
cover: it proves the **sweep itself** never transmits. It does **not** exercise the
three `hwTimer::resume()` holes the audit found (Bind / power-cali / UART-reconnect);
only the S1 Bind-refusal test does that.

**Reboot-exit takes ~2-4s to re-link, and that is expected**: `rebootTime = millis()
+ 400`, then an ESP32 cold boot (~1-2s), then FHSS resync (~1s). It is the price of
R1.5 (exit by reboot) and the trade is deliberate — a clean teardown would return
faster but could leave the radio on the wrong frequency while still *looking*
connected, which is the failure you would fly.

**S1 passed, and the LED cannot be the indicator.** Bind mid-scan is refused: the
trace stays unbroken. That is the *only* valid read — `EnterBindingMode()` calls
`SetRFLinkRate(RATE_BINDING)` (50Hz + InvertIQ), which reprograms the SX1280 out
from under `SweepChunk` on the loop core, so a fired Bind cannot hide. The LED is
structurally useless here: `connectionState` stays `spectrumScan` either way
(`EnterBindingMode` never calls `setConnectionState`), and the TX has no bind LED at
all — `devRGB.cpp:408`'s binding block is `#if defined(TARGET_RX)`. What looks like
"bind mode" on a TX is just `disconnected` showing because the link dropped. **Run
the positive control first** (button binds outside a scan), or "nothing happened" is
indistinguishable from an unmapped button.

**P3 passed**: max-hold fills in over ~5s where a single sweep looks nearly empty —
i.e. 3.5's claim holds in practice, and shipping max-hold from day one was load
bearing rather than a nicety.

### Two upgrade traps found the hard way (neither is this feature's fault)

- 🔴 **A "bare" Unified build has no pin layout and will not boot.** The
  `UnifiedConfiguration.py` prompt embeds the *hardware layout* as well as the
  product name — not just the binding phrase. `options_init()` returns
  `hasHardware` (`options.cpp:293`), so a bare image makes it return false, and the
  module boots WiFi-only into `hardwareUndefined` with the web root redirected to
  `/index.html#hardware` (`tx_main.cpp:1388`, `devWIFI.cpp:163`). That rescue path
  is deliberate and it works — but **always flash a `binary_configurator`-patched
  image**, and verify it first:
  ```
  grep -a -o '"radio_busy":[0-9-]*' fw.bin   # layout present?
  grep -a -o '{"uid[^}]*}'          fw.bin   # UID present?
  grep -a    "BFPV SuperG 2G4"      fw.bin   # product config present?
  ```
- 🔴 **ELRS's arm convention is hardcoded, and an "arm at low" setup inverts every
  arm-driven behaviour on the TX.** `CRSF_to_BIT` (`crsf_protocol.h:476`) is
  `return (val > CRSF_CHANNEL_VALUE_MID) ? 1 : 0` — **CH5 above centre (992, ~1500us)
  *is* armed**, on the hardcoded channel `AUX1 = 4` (`common.h:287`). There is no
  option, no inversion flag, and no way to select a different channel; 3.3.1 was
  identical (`CRSF.h`: `IsArmed() { return CRSF_to_BIT(ChannelData[4]); }`).
  Betaflight, by contrast, lets you put the ARM range anywhere — including low.

  **Seen 2026-07-17:** `[ ! Armed ! ]` with the arm switch in the safe position,
  Betaflight arming correctly, scan refused. **Cause: the setup armed at CH5 *low*.**
  EdgeTX's ordinary convention gives -100% (~1000us) for a switch pushed away, and
  Betaflight's ARM range had been set down there to match. So armed == CH5 low ==
  ELRS "disarmed", and vice versa. **Both systems were correct, on opposite
  conventions.** Fix: invert the mix (`MDL -> MIXES -> CH5 -> Weight: -100`) so armed
  drives CH5 **high**, then move Betaflight's ARM range to ~1700-2100. Verify on
  `MDL -> Channels`, which is ground truth. Props off while changing it — between
  the two edits, the *safe* switch position arms the quad.

  **The dangerous half is in flight, not on the bench.** With the inversion, ELRS
  believes you are **disarmed while flying**, so ~15 call sites misbehave silently:
  `ota->rc.isArmed` **sent over the air** (`OTA.cpp:124,228`), the module buttons
  staying **live in flight** (`devButton.cpp:88` disables them when armed — inverted
  off), dynamic power (`dynpower.cpp:108`), the telemetry ratio
  (`tx_main.cpp:557`), and VTX gating (`devVTX.cpp:100`). All predate this feature.
  For us it means **R1.4 is defeated in the worst direction** — a scan would start,
  and kill the link, while the aircraft is armed. The refusal on the bench is the
  *safe* half of the same inversion.

  Two earlier drafts of this note were wrong and are kept visible rather than
  quietly rewritten: first blaming a stale `crsfArmingMode` bit from an EdgeTX 2.8
  model migration, then blaming "Arm using: Switch" with a low trigger. Both were
  theories built from source reading; the model was already set to **CH5**, which
  means EdgeTX sends the 24-byte frame with **no status byte** (`crossfire.cpp:106`,
  `lenAdjust = 0`) and none of that machinery was ever in play. **The measurement
  that settled it took two numbers off the Channels page.** Read the channel before
  theorising about the protocol.

  Also worth remembering: the Lua script is a **pure messenger** — `elrs.lua:511`
  renders a string the *module* composed and never reads a channel — so
  `[ ! Armed ! ]` is never the script's opinion.

## Known constraints & gotchas

- 🟡 **`elrs.lua` has no build flag and cannot have one.** It is an SD-card file: it
  ships to every handset regardless of firmware and **outlives a firmware rollback**,
  so it is the one component whose cost lands on users who never enabled the flag.
  **Measured on a real Boxer 2026-07-17: +6.9 KB loaded (+16%), and it loads** — so
  this is no longer a blocker, and a pre-generated `.luac` is not needed. Source grew
  +38% but heap only +16%: comments are free. Detail, including the SRAM-spill
  inference, in DESIGN.md 9.4. **Re-measure with `mockup/heapchk.lua` before growing
  this file** — the simulator cannot (it never compiles the CCM allocator) and B&W
  targets have no soft cap (`LUA_MEM_MAX` is 0), so the failure would destabilise the
  radio rather than fail cleanly.
- **Deploying `elrs.lua` to a card: delete `elrs.luac` too.** EdgeTX precompiles on
  first run, and the load mode is `"bt"` — *newer wins, binary preferred on a tie* — so
  a leftover `.luac` can silently win over an updated `.lua` and you run stale code
  with no sign. (`heapchk.lua` forces `"T"` so it always measures the source.)
- **Serial is not a constraint, and never will be.** `EmitNextFrame()` sends one frame
  per 50ms and never bursts: 1.16 KB/s regardless of bins, radios or traces, against
  ~10 KB/s at 115200 worst case. **The pacing wall is EdgeTX's Lua GC** (each frame
  mints a ~54-entry table) — which is why the interval went 25 → 50ms. Do not optimise
  the wire format for bandwidth; that is not where the budget goes. DESIGN.md 10.1.
- **One screen per band** for dual-radio; do not build a combined plot. It keeps
  `TX_SPECTRUM_MAX_BINS` at 80, keeps the axis a single `(start, step)`, and adds no
  Lua heap. DESIGN.md 10.1.
- **Colour handsets cannot render this view by construction** — `setLCDvar` leaves
  `SPEC_Y1` nil and `parseSpectrumMessage` refuses to enter. Bench on mono only.
- **Native tests here run on Windows**: `test_txspectrum`
  deliberately does not include `crsf_protocol.h`, so it never drags in
  `CRSFEndpoint.cpp` and its `strlcpy`/`stpcpy` gap. The *other* native suites still
  fail on Windows for that reason — pre-existing, not ours. Keep it that way.
- **`lib/TxSpectrum` is in the native env's `lib_ignore`.** Required: PIO's chain LDF
  scans `#include` lines without evaluating `#ifdef`. Re-run `pio test -e native`
  after adding any `.cpp` here.
- **`include/hardware.h` has no include guard.** Do not include it directly.
- **P2's first failure mode looks like success**: a flat trace across all 80 bins means
  the `SetMode` guard trap, not a quiet band. DESIGN.md 3.3.

---

## Nomad port (cross-band LR1121) — desk-complete 2026-07-17

Branch `tx-spectrum-nomad-4.0.1` (off the validated SuperG branch). Scans **both bands,
one at a time**, each on its own page; the **page button** flips bands. User decision:
retune-on-flip (one band-matched radio at a time, `+0 B` RAM) rather than sweeping both
concurrently — the unviewed band's max-hold goes cold on a flip, the accepted cost.

**Design, maximally reused (no per-target fork):**
- One `elrs.lua`, one `TxSpectrum.cpp`, one flag. The sweep engine already spoke LR1121;
  the only single-band assumption (`BeginScan` picking the band from the link's active-band
  global) was generalised to an explicit `scanBand` + band-matched radio (r1=sub-GHz,
  r2=2.4).
- Band flip = a private lcs step (`TX_SPECTRUM_LCS_NEXT_BAND = 8`) the plot pushes at its
  **own** field id → `TxSpectrumSwitchBand()` re-runs `BeginScan` for the other band. No
  second command field, no sibling-id lookup. On a single-band device it is a no-op, so
  the SuperG path is unchanged.
- Crossing 900↔2.4 on an LR1121 needs a `Config()` (not a bare freq write) to select the
  band's PA/RF-switch; done once per flip in `BeginScan`.
- Wire format **unchanged (proto v1)**: the per-frame axis already identifies the band, and
  `elrs.lua` re-latches on a `startKhz` change, so 900 vs 2.4 render on correct axes for free.

**Safety re-verified for the LR1121 + sub-GHz PA (the point of this port):**
- 🟢 **R3 — the scan path is provably receive-only on the LR1121.** `Config` /
  `SetFrequencyReg(doRx)` / `StartRssiInst` / `GetRssiInst` reach only STANDBY/config/freq/
  RX/RSSI opcodes. `Config` ends in `STDBY_RC`; `SetPaConfig` only *selects* a PA, never
  keys it. The three TX openers (`TXnb`, `startCWTest`, `SetMode(TX)`) are never on the
  path, and `hwTimer` stays stopped. Same standard as the SX1280 proof. R3.2 (zero packets +
  failsafe on **both** bands) is the bench confirmation.
- All firmware edits are inside `#if defined(TX_SPECTRUM_SCAN)`, so **flag-off is
  byte-identical to stock by construction** — and now **empirically proven** on the LR1121
  target (N0, see below).

**Verified on the desk:**
- ✅ LR1121 TX firmware builds (`Unified_ESP32_LR1121_TX_via_ETX`, BUILD-B): SUCCESS,
  RAM 22.0% / Flash 86.6%, no warnings on the changed files.
- ✅ `pio test -e native`: 14/14 (codec unchanged).
- ✅ `simcheck.py`: 18/18 incl. new **T7 band-flip** — page event → axis re-latches to the
  900 golden set (bin 1 reads 903 MHz), title flips to `SPEC 900`, notch on the new axis,
  flips back. **Mutation-proven**: reintroducing the fieldPopup guard on the re-latch freezes
  the plot on the old band and T7 fails as predicted.
- ✅ New sub-GHz golden set (`frames900`/`expect900`) from the **real** encoder (FCC915:
  903.5–926.9 MHz, 40 ch, 600 kHz step). The 2.4 golden bytes are byte-identical.

**A bug caught before hardware:** the plot's `fieldPopup==nil` entry guard also blocked the
band re-latch (fieldPopup is nil during an open scan), which would have frozen the view on
the first band. Split into initial-entry (needs the popup) vs band-flip (keeps the stashed
id) — see `parseSpectrumMessage`. Exactly the class of silent seam the sim ladder exists for.

**N0 — flag-off byte-identical proof on the LR1121 target: ✅ PASSED (2026-07-18).**
Method (reusable for any target, incl. the future Boxer-internal 2.4): build the
`Unified_ESP32_LR1121_TX_via_ETX` image twice at **HEAD** — once with the feature files
present but the flag off (`super_defines.txt` reduced to just `-DRegulatory_Domain_FCC_915`),
once with the 9 TX-compiled shared files reverted to stock `b5a5b5fd` — then `cmp` the two
`firmware.bin`s. Both builds at HEAD (identical git-version string) with the random
`flash-discriminator` pinned (`build_flags.py:133`), so the only variable is the feature edits.
**Result: across the 1.7 MB image the only differing bytes are two SHA-256 blocks** — the
`app_elf_sha256` in the app descriptor (offset 176–207, magic `0xABCD5432` confirmed at 0x20)
and esptool's trailing whole-image hash (last 32 B). **Mask those two and the images are
byte-identical.** No code or data byte differs. The ELF hash differs only because the ELF
carries symbol/debug entries for the feature's source files that are **stripped from the
flashed `.bin`** — so a full-image `cmp` will always show these ~65 bytes even on a genuine
pass; the meaningful comparison is the image with the two hash regions masked.
Caveats worth carrying forward: (a) a bare `pio run` on the `_ETX` env reports `FAILED` at the
`UnifiedConfiguration.appendConfiguration` post-action (needs the `hardware/` layout repo) —
this runs *after* `firmware.bin` is written and does not touch it; the image is complete and
valid (magic `e9`, 6 segments, trailing hash). (b) the proof reverts only the TX-compiled
shared files. Proof script kept in the session scratchpad.

**Page-event delivery on the Boxer: ✅ CONFIRMED (2026-07-18).** The `pgprobe.lua` no-flash
Tools probe (`src/lua/mockup/pgprobe.lua`) showed the Boxer emits **both** `EVT_VIRTUAL_NEXT_PAGE`
and `EVT_VIRTUAL_PREV_PAGE` to Lua. `runSpectrumPage` (`elrs.lua:1021`) already maps both to
the band-flip step, so the page buttons drive the band switch as designed — **no long-press-ENTER
fallback needed, the Lua is final.** This was the only unverified UI assumption in the port.

**Pending as of 2026-07-17 — all since closed, kept for the record:**
- ~~N2–N5 on hardware~~ **all PASSED 2026-07-18** (see the 4.1.0 section at the top):
  per-bin variation on both bands, R3.2 on both, band-matched-radio routing, CalibImage
  accuracy, arm guard, and rollback to stock **4.1.0**.
- ~~the "unmeasured 240 µs" sub-GHz settle~~ **closed 2026-07-20** — it is now a measured-
  intent 1000 µs paired with the per-bin AGC reset (DESIGN.md §3.3.3, invariant M1).
