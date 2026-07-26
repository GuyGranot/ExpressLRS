# RX Spectrum Analyzer

A receive-only swept spectrum analyzer that runs **on an ELRS receiver**. It turns
the RX into a bench instrument: it drops the RC link, sweeps its radio across the
FHSS channel grid measuring instantaneous RSSI per bin, and streams the trace out
the serial (flight-controller / host) UART as CRSF `ELRS_VENDOR` (`0x30`) frames.

This is a **port of the TX-side analyzer** (`../TxSpectrum`). The measurement
engine, wire protocol, and safety model are shared; this document covers only the
**RX-specific deltas**. For the underlying reasoning — why max-hold is mandatory,
why the per-bin `STDBY_RC` + 1000 µs settle is a matched pair, what the dBm numbers
are and are not, the frame layout — read **`../TxSpectrum/DESIGN.md`** first. Where
this doc cites an invariant (R1/R2/R3, I3, M1, S1), it means the one defined there.

Flag: **`-DRX_SPECTRUM_SCAN`** (off by default; a flag-off build is byte-identical
to stock — mirror of R1.1). Files: `RxSpectrum.{h,cpp}`, `RxSpectrumProtocol.h`
(a one-line reuse of the TX codec).

Validated 2026-07-27 on the **BetaFPV SuperX Nano** (ESP32-C3, dual cross-band
LR1121): a live scan cross-correlated against the proven TX analyzer — the spectral
shapes match.

---

## 1. How it differs from the TX analyzer

| | TX analyzer | RX analyzer (this) |
|---|---|---|
| Trigger | handset Lua parameter | CRSF command over the FC/host UART (Betaflight passthrough) |
| Transport direction | TX module → handset | RX module → flight controller / host |
| Band/port selection | page button | trigger argument (`0`=900, `1`=2.4, `2`=both) |
| Exit | reboot | reboot (identical — R1.5, no restore path) |
| Host UI | `elrs.lua` on EdgeTX | `python/rxspectrum.py` (see `UTILITIES.md`) |

Everything else — the `spectrumScan` mode state, the `device_t` sweep engine, the
per-bin measurement cycle, max-hold, the `0x30` frame codec — is the TX design.

---

## 2. Safety model (RX mirror + one addition)

The three TX rules hold, re-established at RX call sites:

- **R3 — never key the PA.** The sweep is receive-only. The only radio calls on the
  path are `SetFrequencyReg(..., doRx=true)` (→ `RFAMP.RXenable()`), the per-bin
  RSSI reads, and the LR1121 `SpectrumResetRx` (`STDBY_RC`). The three PA openers
  (`TXnb()`, `startCWTest()`, `SetMode(TX)`) are never reached. The telemetry-uplink
  TX is additionally suppressed because **`hwTimer` stays stopped** (R3.1a) and the
  **RXdone DIO1 ISR is detached** (I3) so nothing re-arms the tock timer.
- **R2 — no config, no version bump.** All scan state is file-static RAM. Nothing is
  written to NVS, so a later downgrade to stock firmware can't be silently wiped
  (the `RX_CONFIG_VERSION` landmine in `config.cpp`).
- **R1 — co-existence / rollback.** Additive, flag-gated, exit-by-reboot.

**New for the RX: the ESP WiFi AP is a transmitter too.** On the RX the same MCU
runs a WiFi access point for updates. Opening the RX front-end while the AP is
transmitting would violate R3's spirit, so scan entry explicitly tears the AP down
(see §4). This is enforced independent of device-dispatch order.

**Trigger is refused while a link is up.** `RxSpectrumStart()` returns immediately
if `connectionState == connected`, so a scan can only begin on a bench / passthrough
session with the link down. Entering `spectrumScan` itself drops the link, so no
RC/arm can occur mid-scan; the over-the-air trigger route is self-defeating (a frame
can only arrive from the TX while connected, which the guard refuses). No RX
`isArmed` global is needed.

---

## 3. Architecture

### 3.1 Mode state and the seam
`spectrumScan` is a `connectionState_e` value (`include/common.h`), ordered **after
`serialUpdate` and below `FAILURE_STATES`** — identical semantics to `wifiUpdate`
(`> MODE_STATES` halts RF, `> NO_CONFIG_SAVE_STATES` blocks config commits, staying
after `wifiUpdate` preserves the `connectionState < wifiUpdate` auto-WiFi test). The
ordering is load-bearing; do not renumber.

`rx_main.cpp` hard-returns from its packet loop at `connectionState > MODE_STATES`.
Everything the scan needs runs **above** that seam and still executes:
`devicesUpdate(now)` (drives the `RxSpectrum_device` timeout), `handleSerialIO()`
(drains our frames to the UART and still parses the trigger), `executeDeferredFunction()`.

`RxSpectrum_device` is registered in `ui_devices[]` **after `WIFI_device`** — which
matters for the AP teardown (§4), though §4 no longer relies on it.

### 3.2 Cross-band port selection — port == band == radio
On a cross-band dual-radio LR1121 (SuperX Nano) each chip is hard-wired to its own
band-matched front end and antenna connector; there is no independent antenna GPIO.
So the band the host selects **is** the antenna port **is** the radio:

- `RX_SPECTRUM_PORT_900` → primary `FHSSconfig` / `freq_spread` on `SX12XX_Radio_1`.
- `RX_SPECTRUM_PORT_2G4` → `FHSSconfigDualBand` / `freq_spread_DualBand` on
  `SX12XX_Radio_2` (`activeRadio = isDualRadio() ? Radio_2 : Radio_1`).

`SelectBand()` latches the FHSS config and `activeRadio`; `Config()` routes the chip
to its sub-GHz vs 2.4 PA/RF-switch path by `regfreq < 1 GHz`. The emitted frequency
axis (`ComputeAxis`) always reports the true band, so the host disambiguates 900 vs
2.4 from the frame itself — no side channel.

On a **single-band** RX (`RxSpectrumHasSecondBand()` false) the 900/2.4 selector
collapses to the one configured band. On a **same-band Gemini** dual-radio RX
(`scanDual`) the one band is split across both radios (radio 1 the low half, radio 2
the high half) for 2× sweep rate — this is orthogonal to cross-band.

### 3.3 `RX_SPECTRUM_PORT_BOTH` — outer dwell alternation
"Both" sweeps one port fully, emits its traces, then flips `wantSubGHz`, re-runs
`BeginScan()` for the other port, and repeats. `RX_SPECTRUM_BAND_DWELL_MS` (500 ms)
per port is long enough to emit several complete live+max-hold traces before
switching, so the host never sees a trace torn across bands. Frames are
self-describing (per-frame frequency axis), so the host demuxes the two ports with
no extra channel.

### 3.4 Per-bin measurement cycle (`SweepChunk`)
Time-budgeted, not bin-counted (S1): `positionsPerCall = CHUNK_BUDGET_US /
settleUs`, clamped `[1, 16]`, with `CHUNK_BUDGET_US = 1300`. Per bin (LR1121):

```
SpectrumResetRx(radio)            -> STDBY_RC  (per-bin AGC re-acquire; I5/M1)
SetFrequencyReg(freq, radio, doRx=true)        (retune + RX entry; doRx mandatory)
delayMicroseconds(settleUs)                    (LR1121 flat 1000 us; PLL relock + AGC)
StartRssiInst(radio); GetRssiInst(radio)
StoreBin(bin, rssi)                            (LNA gain backed out; max-hold updated)
```

`settleUs` comes from `RssiSettleUs()` — the LR1121 branch is a flat 1000 µs and
**must not** be collapsed to a generic per-SF table (M1); the per-bin `STDBY_RC`
drop and the 1000 µs settle are a matched pair, and dropping either brings back the
~20 dB 915 AGC-carryover floor lift. `doRx=true` is mandatory on a
command/state-machine radio and only works because the `SetMode` early-return guard
is commented out in the driver (re-adding it reports bin 0's RSSI for every bin).

### 3.5 Max-hold and accuracy
Max-hold is accumulated in `StoreBin` on the device, not host-side (the sweep runs
faster than the emit rate — DESIGN.md 2.5). `StoreBin` backs out the front-end LNA
gain (`hardware_int(HARDWARE_power_lna_gain)`) so values are antenna-referred.
`BinToRadioFreq` uses the true FHSS grid and deliberately does **not** apply
`FreqCorrection`. See DESIGN.md 2.6 for what the dBm figures are and are not.

---

## 4. Scan-entry sequence — the hybrid reset

Entering `spectrumScan` runs a **one-shot** hardware init in the device `event()`
(`if (!running)`), in this exact order:

1. **Tear down the WiFi AP** — `WiFi.disconnect(true); WiFi.mode(WIFI_OFF)`
   (`+ forceSleepBegin()` on ESP8266). Enforces the no-transmit invariant (§2)
   explicitly, *before* any radio access. The `WIFI_device` also does this on the
   `connectionState` change, but that relies on device-dispatch order and the state
   enum staying below `FAILURE_STATES`; the explicit call makes it hold regardless.
   Idempotent — the `WIFI_device` reconciles its own `wifiStarted` flag on the same
   event, so nothing is left stale.
2. **`hwTimer::stop()`** (R3.1a) — before bringing the radio up, so nothing can key
   the PA during init.
3. **Full `Radio.Begin(FHSSgetMinimumFreq(), FHSSgetMaximumFreq())` — once.** This is
   the hybrid's core. `Radio.End()` (what the WiFi AP calls when it starts) leaves the
   LR1121 in **SLEEP with `hal.end()` having torn down SPI** — a state the lightweight
   `Config()` reconfigure cannot recover from. Only a full `Begin()` (HAL re-init +
   `hal.reset()` + `CalibImage`) brings it back. Entry is the one place the prior
   radio state is unknown (dead after the AP, or a half-configured link), so pay the
   reset/calibration cost **here and only here**. This call is character-identical to
   the boot path (`rx_main.cpp`) and the WiFi-return path (`devWIFI.cpp`).
4. **`SweepIsolateRadio()` — after `Begin()`.** `Begin()` re-inits the HAL (which
   re-attaches the MCU RXdone ISR) and reprograms the chip DIO/IRQ mapping, so the
   DIO1 detach (I3) must come *last* to keep exclusive, receive-only ownership.
5. **`BeginScan()`** — select band, `Config()` the radio, compute the axis, size the
   sweep, clear the bin buffers.

### Why "once at entry", not "always"
The in-sweep `BeginScan()` calls — the `RX_SPECTRUM_PORT_BOTH` band flip and a
re-trigger restart — deliberately **stay on the lightweight `Config()` reconfigure**,
*not* a full `Begin()`. A full re-init on every band flip would (a) inject
reset+`CalibImage` dead-time into the stream on each 900↔2.4 flip, dropping the
both-mode refresh cadence, and (b) re-run image calibration each time, drifting the
absolute-dBm baseline between sweeps and vs the TX analyzer. `Config()` is a
`STDBY_RC` + register-write reconfigure — fast and baseline-stable. So: full reset
where the prior state is unknown (entry), lightweight reconfigure where it isn't
(in-sweep).

**Consequence:** a scan can now be triggered **directly from WiFi AP mode** — entry
tears down the AP and fully re-inits the radio. (Before the hybrid, you had to
trigger within the ~60 s window before `wifi-on-interval` started the AP.)

---

## 5. Control and transport

**Trigger** — `RXEndpoint::handleRaw` (`lib/rx-crsf/RXEndpoint.cpp`) matches a CRSF
`COMMAND` frame with `sync_byte == CRSF_ADDRESS_CRSF_RECEIVER` and payload bytes
`'s','p',<band>` — the same local-command channel as the existing `'b','l'`
(reboot-to-bootloader), `'b','d'` (bind), `'m','m'` (model-match). It calls
`RxSpectrumStart(payload[2])`. Betaflight's normal traffic never emits this triplet.

**Emit** — `EmitNextFrame()` encodes past the CRSF header with
`TxSpectrumEncodeFrame`, sets an extended `ELRS_VENDOR` header (dest
`CRSF_ADDRESS_FLIGHT_CONTROLLER` `0xC8`, orig `CRSF_ADDRESS_CRSF_RECEIVER` `0xEC`),
and routes it with `crsfRouter.deliverMessage(&otaConnector, ...)` — which forwards
to every *other* connector, i.e. the serial connector to the FC/host, mirroring how
the RX already emits link statistics. `handleSerialIO()` drains it to the UART →
Betaflight passthrough → host. Pacing: one frame per `EMIT_INTERVAL_MS` (50 ms),
alternating live / max-hold, ~1.2 KB/s against a 420000-baud UART (headroom; the
pacing just keeps the 256-byte serial FIFO from bursting).

**Protocol** — `RxSpectrumProtocol.h` is a one-line `#include` of
`../TxSpectrum/TxSpectrumProtocol.h`; the wire format is identical (frame `0x30`,
sub-type `0x01`, big-endian 13-byte header + int8 bins, 40 bins/frame). Only the
address pair and direction differ.

---

## 6. Building, flashing, host tool

See **`UTILITIES.md`** in this directory for the full toolchain:
- `build_superx_nano.sh` — build + bake the board layout into a ready-to-flash image.
- `python/rxspectrum.py` — trigger a scan, decode, live/max-hold plot, `--log`/`--replay`.
- `python/bf_passthrough_min.py`, `python/elrs_cfg_tool.py` — passthrough + image tools.

Quick reference: build env `Unified_ESP32C3_LR1121_RX_via_UART`, target
`betafpv.rx_dual.superx-nano`, sub-GHz domain `FCC_915`. Flag-off builds are stock.

---

## 7. Known limits

- **`FreqCorrection` is not applied** (§3.5) — the axis is the nominal grid, not the
  crystal-corrected one. Intentional; the grid is what a channel plan is built on.
- **Absolute dBm** is antenna-referred (LNA backed out) but uncalibrated; treat it as
  relative / self-consistent, same caveat as the TX analyzer (DESIGN.md 2.6).
- **Single peak-frequency validation so far** — cross-correlation of trace shape
  against the TX analyzer is done; a formal per-bin accuracy / CW-injection sweep on
  the RX is not yet itemized.
- **Portability:** the SX127x `RssiSettleUs` branch is unmeasured (240 µs placeholder);
  a single-band or same-band-Gemini RX works but only the cross-band path is
  hardware-proven.
