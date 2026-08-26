# Action Camera Bridge — Product Requirements Specification v0.8

**Status:** Draft
**Supersedes:** v0.7
**Minimum supported:** Betaflight **2025.12.5**, INAV **8.0.1** — also the behavioural baseline for every citation (§0)
**Target family:** Betaflight 2025.12.5+, INAV 8.0.1+ within 8.x, subject to forward-compatibility validation (§0)
**Initial camera families:** GoPro (V1), DJI Osmo Nano (V1.1, gated on §27.4)
**Architecture:** Standalone BLE-to-FC bridge over one spare UART
**Future architecture:** Optional ELRS-integrated implementation sharing the camera/control/OSD core
**Distribution:** Personal / open-source

---

## What changed from v0.7

The architecture is **retained unchanged**, as it was in v0.7: the bridge observes configured
RC intent, never interprets FC armed state, defaults Record to a level control for
recoverability, and lets Betaflight and INAV use deliberately different RX-loss mechanisms.
Nothing in this revision reopens those decisions.

v0.8 closes one genuine control-path hole that v0.7 created, specifies the INAV half of a
requirement v0.7 tested but never defined, and repairs a test that could not pass.

| Change | Rationale | Section |
| --- | --- | --- |
| **Betaflight startup qualification no longer qualifies on a clear guard alone.** Qualification now requires either an observed assert→clear edge (condition A) or a grace interval derived from `MSP_FAILSAFE_CONFIG` (condition B) | v0.7 §12.4.3 proved the guard reads clear during the boot window and then defined qualification as "one guarded snapshot" — which that window satisfies. The section refuted itself, and the baseline it would capture is `midrc` | §12.4.3, §21.1, §24, §27.1 |
| **INAV startup qualification specified.** Qualify on `ARMING_DISABLED_RC_LINK` clear **while `ARMED` is not set**; no timer and no config read are needed | INAV initialises every channel to `PWM_RANGE_MIDDLE` and has the same boot exposure. v0.7 made startup qualification `Required` on both platforms in §24.1 but specified it only for Betaflight | §12.4.4, §5.2, §24, §27.1 |
| **`REC STOPPED` test no longer accepts camera power-off as a stop.** Power-off is a separate `CAM LOST` row | A powered-off camera cannot confirm anything. The criterion as written pressured the implementation into synthesizing a confirmed stop from an absence of evidence — the exact fabrication §7 forbids | §18, §24.2, §27.1 |
| **ELRS integration keeps startup qualification.** Only the MSP *mechanism* disappears; first-valid-sample-is-a-baseline-not-an-edge is common `ControlPolicy` semantics | An integrated receiver still boots before its first valid RC frame. v0.7 removed the requirement along with the transport | §25 |
| **Version contract made explicit** — minimum supported vs target family | The header said `2025.12+` / `8.x` while §0 called those same versions the minimum. Three places disagreed | header, §0, §9, §27.1 |
| **OSD persistence restated as a telemetry-truth issue, on both platforms**, with owned slots initialised to non-authoritative content at startup | "Harmless" was true of flight control and false of camera-state truth: a stale `REC 04:22` survives a reboot. Betaflight is exposed identically to INAV — `pilotConfig_t` is a parameter group — and v0.7 documented the corollary only for INAV | §9, §10, §24.1 |

### Carried forward from v0.7

| Change | Rationale | Section |
| --- | --- | --- |
| **Startup qualification replaces startup assertion.** Bridge input is `UNQUALIFIED` until it observes a guarded valid RC snapshot; the first qualified sample may reconcile a level control but shall never emit an edge event | v0.6 claimed the guard is asserted at FC init. It is not. It is asserted on first valid data, or after `failsafe_delay` — **stock default 1.5 s** — of none. That is a wide, routinely-hit window in which the guard reads clear while `rcData[]` holds nothing but init values | §12.4, §22, §24, §27.1 |
| **Guarded commit for Betaflight RC samples.** `MSP_STATUS` and `MSP_RC` are separate transactions and are not one FC instant; a transition-bearing sample must be bracketed by a trailing status read before it is committed | `rcData[]` is written *before* the failsafe flag is updated, so a status read taken before the RC read can report valid while the RC reply already carries substituted values | §12.4, §21.1, §24.2, §27.1 |
| **Recovery delay corrected to 500 ms, and then de-hardcoded** | v0.6's "default 1.0 s" was copied from a stale comment in Betaflight's own source, which the same file contradicts. The bridge never needs the number: it waits for the flag | §12.3, §12.4, §24.2, §27.1 |
| **`Combi` given explicit arbitration** | v0.6 described it in one sentence of prose. Two conforming implementations could behave completely differently | §12.1 |
| **`REC STOPPED` no longer suppressed during a guard freeze** | v0.6 contradicted itself: §12.4 freezes the *last accepted intent*, so intent is known. A camera that stops for card-full during RX loss is exactly when the warning matters | §18 |
| **Hysteresis given a testable value** — ±10 µs, with a minimum configurable range width | "Reasonable hysteresis" is not a requirement. ±10 µs is under half of Betaflight's 25 µs mode-range grid, so it cannot swallow an adjacent boundary | §12.2 |
| **RF gate given a calibrated method** | Passive survey detects interference but does not measure sensitivity in dBm. "No measurable degradation" is undefined without the rig's uncertainty | §27.2 |
| **All citations re-pinned to the supported baseline**, and three platform-availability errors fixed | See §0 | throughout |

## 0. Source baseline and citation rule

This specification's **behavioural baseline is the minimum supported release**, not `master`:

```
Betaflight   2025.12.5      ← all [BF] citations
INAV         8.0.1          ← all [INAV] citations
```

Citations marked **[BF]** are `betaflight/betaflight` at tag `2025.12.5`; **[INAV]** are
`iNavFlight/inav` at tag `8.0.1`. Line numbers were **read from those tags on 2026-08-25**,
and the §12.4 control-path citations re-read on **2026-08-26**. Forward compatibility against
`master` is a separate audit and its findings shall be recorded as version notes, never by
silently re-pointing a citation.

**Version contract.** The baseline and the support floor are the same versions, and the
document shall not describe them in two ways:

```
Minimum supported:   Betaflight 2025.12.5      INAV 8.0.1
Target family:       Betaflight 2025.12.5+     INAV 8.0.1+ within 8.x
```

Earlier patch releases in those lines — 2025.12.0 through .4, or 8.0.0 — are **not**
supported, and shall not be described as such by writing `2025.12+` or `8.x`. Newer releases
are targeted but require forward-compatibility validation before support is claimed
(§27.1). v0.7 stated `2025.12+` / `8.x` in its header and `2025.12.5` / `8.0.1` in this
section; the loose form is what allowed a graceful-degrade test to be written against "a
Betaflight build older than 2025.12", which is a different question from the one this
product needs answered.

**Why the rule exists.** v0.6 cited `master` line numbers under 8.0.1 and 2025.12+ labels.
Most of those numbers were individually correct *for master* and wrong for the release the
product actually supports — which is worse than an obvious error, because they survive
spot-checking. Re-pinning surfaced three claims that were not line-number drift at all but
**genuine platform-availability errors**:

| v0.6 claim | Reality on the supported baseline | Fixed in |
| --- | --- | --- |
| `MSP_MODE_RANGES_EXTRA` (238) usable on both platforms | Betaflight-only ([BF] `msp/msp_protocol.h:263`). INAV 8.0.1 has only `MSP_MODE_RANGES` (34) at [INAV] `msp/msp_protocol.h:127` | §12.6, §21.1 |
| `MSP2_INAV_LOGIC_CONDITIONS_CONFIGURED` (0x203C) available for the INAV trigger | **Does not exist in INAV 8.0.1** — absent from both `msp_protocol_v2_inav.h` and `fc_msp.c`. It is a later addition | §12.7 |
| INAV "gates only `MSP_REBOOT` on `ARMING_FLAG(ARMED)`" | False. 8.0.1 has armed guards at [INAV] `fc/fc_msp.c:1434,1894,2617,2628,2635,2643,2651,3316,3324`. The *conclusion* survives — the custom-element setter at `:3519` carries no armed guard — but the supporting claim did not | §10 |

### Citation corrections applied

| v0.6 cited (master) | Corrected to (2025.12.5 / 8.0.1) | For |
| --- | --- | --- |
| [BF] `msp/msp.c:1396-1400` | `msp/msp.c:1317-1321` | `MSP_RC` serialization |
| [BF] `msp/msp.c:1200-1203` | `msp/msp.c:1128` (case at `:1094-1095`) | `armingDisableFlags` in `MSP_STATUS` |
| [BF] `msp/msp.c:4386,4419` | `msp/msp.c:2647,4052` | `mspProcessInCommand`, `MSP2_SET_TEXT` |
| [BF] `msp/msp.c:2277` | `msp/msp.c:2179` | `MSP_RTC` |
| [BF] `msp_protocol_v2_betaflight.h:52` | `msp_protocol_v2_betaflight.h:43` | `MSP2TEXT_CUSTOM_MSG_0` |
| [BF] `rx/rx.c:676-710`, `781,787,801`, `819` | `rx/rx.c:619`, `724,730,744`, `762` | failsafe substitution |
| [BF] `flight/failsafe.c:188-195` | `flight/failsafe.c:176-183` | assertion on first valid data |
| [BF] `flight/failsafe.c:198-205` | `flight/failsafe.c:186-192` | delayed clear |
| [BF] `flight/failsafe.c:208-222` (set at `:214`) | `flight/failsafe.c:196-209` (set at `:202`) | `failsafeOnValidDataFailed` |
| [BF] `flight/failsafe.c:183` | `flight/failsafe.c:339` | `BOXFAILSAFE` zero recovery preset — wrong function on both baselines |
| [BF] `flight/failsafe.c:110-113,120` | `flight/failsafe.c:101-105,111` | recovery-period computation |
| [BF] `fc/runtime_config.c:74-77` | `fc/runtime_config.c:73-76` | `setArmingDisabled` |
| [INAV] `fc/fc_msp.c:673-677` | `fc/fc_msp.c:621-625` | `MSP_RC` serialization |
| [INAV] `fc/fc_msp.c:599-603`, `:627-631` | `fc/fc_msp.c:570`, `:575` | LC status, GVAR status |
| [INAV] `fc/fc_msp.c:604` | **command absent in 8.0.1** | `..._LOGIC_CONDITIONS_CONFIGURED` |
| [INAV] `fc/fc_msp.c:1826-1832` | `fc/fc_msp.c:1732-1737` | custom-element capability reply |
| [INAV] `fc/fc_msp.c:3783`, `:3785` | `fc/fc_msp.c:3519`, `:3521` | custom-element setter, size check |
| [INAV] `fc/fc_msp.c:4715` | `fc/fc_msp.c:1434-1435` | `MSP_REBOOT` armed guard |
| [INAV] `fc/fc_msp.c:1521` | `fc/fc_msp.c:1459` | `MSP_RTC` |
| [INAV] `custom_elements.c:217` | `custom_elements.c:215` | `sl_toupper` |
| [INAV] `msp_protocol_v2_inav.h:115`, `:117` | `msp_protocol_v2_inav.h:112`, `:114` | custom-element commands |
| [INAV] `target/common.h:173` | `target/common.h:172` | `USE_PROGRAMMING_FRAMEWORK` |

Citations verified **unchanged** and correct on the pinned baseline: [BF] `rx/rx.c:128`,
`:158`; `msp/msp_protocol.h:107`, `:176`; `msp_protocol_v2_betaflight.h:28`; `pg/pilot.h:26,27,32`.
[INAV] `rx/rx.c:489`, `:500-506`, `:657`; `custom_elements.h:22,23,24`.

### Added in v0.8

Read from the same tags on **2026-08-26**, supporting §12.4.3, §12.4.4, §9 and §10:

| Citation | What it establishes |
| --- | --- |
| [BF] `msp/msp.c:1657` | `MSP_FAILSAFE_CONFIG` writes `failsafe_delay` as its first byte, U8 in tenths of a second |
| [BF] `flight/failsafe.c:96-100` | `rxDataFailurePeriod = failsafe_delay × 100 ms`, floored at `PERIOD_RXDATA_RECOVERY` (100 ms) — the floor v0.7's arithmetic would have missed |
| [BF] `flight/failsafe.c:106`, `:215` | `validRxDataReceivedAt = 0` at reset, compared against raw `millis()` — so the FC's no-RX deadline runs from MCU boot, which is what makes §12.4.3 condition B safe by construction |
| [BF] `pg/pilot.h:29-35` | `pilotConfig_t` carries `message[][]` and is a parameter group — Betaflight's custom messages persist on save exactly as INAV's custom elements do |
| [INAV] `rx/rx.c:271-272`, `:279-289` | every channel initialised to `PWM_RANGE_MIDDLE`, ARM AUX specially initialised OFF — the same safe-by-luck asymmetry as Betaflight |
| [INAV] `fc/fc_core.c:200-225` | `ARMING_DISABLED_RC_LINK` is driven from `failsafeIsReceivingRxData()`, **inside the disarmed branch** — the fact that makes it a startup-only signal |
| [INAV] `fc/runtime_config.h:38` | `ARMING_DISABLED_RC_LINK` is bit 18 |
| [INAV] `flight/failsafe.c:165`, `:289` | `rxLinkState` starts `DOWN` and `failsafeIsReceivingRxData()` is `rxLinkState == UP` — no timeout path, so on INAV a clear flag really does prove data arrived |
| [INAV] `fc/fc_msp.c:459` | `MSP2_INAV_STATUS` writes the full U32 `armingFlags` |

One correction of v0.7's own additions: `failsafeCheckDataFailurePeriod()` spans
[BF] `flight/failsafe.c:212-221`, not `:212-222`.

**Standing rule.** Line numbers are the part of this document most likely to rot, and the
part most likely to be *regenerated from memory rather than read*. Re-verification before
each release means opening the file at the pinned tag, not recalling it — and a citation
that is right on `master` is still wrong here.

---

## 1. Product Goal

Provide reliable in-flight control and telemetry for supported action cameras without
modifying flight-controller firmware or inserting the device into any flight-critical
control path.

The bridge shall:

- control supported cameras over Bluetooth;
- read the camera's actual reported state;
- display camera state and telemetry through the flight controller's native OSD;
- support Betaflight 2025.12.5+ and INAV 8.0.1+ (§0);
- work independently of the video system;
- require only power and one bidirectional FC UART;
- continue to have no effect on flight if the bridge, Bluetooth connection, or camera fails;
- not measurably degrade the aircraft's control link.

The primary value is **confirmed camera state**, rather than inferred state from the
pilot's switch position.

## 2. Core User Experience

After initial installation and pairing:

1. Aircraft powers up.
2. Bridge boots and identifies the FC.
3. Bridge automatically reconnects to the paired camera.
4. Camera telemetry appears in the FC OSD.
5. Configured RC switches or buttons control recording according to bridge configuration.
6. Camera responses update the OSD.
7. Loss of camera communication is explicitly indicated.

No phone, camera app, or bridge configuration interface is required for normal use.
Normal operation shall require no interaction with the bridge itself.

## 3. Safety Architecture

The bridge is a **passenger device**. It is isolated from flight control in three
independent dimensions: logical, electrical, and radio-frequency.

It shall not participate in:

- RC reception;
- motor control;
- servo output;
- PID processing;
- navigation;
- failsafe;
- arming authorization;
- receiver-to-FC signal transport.

```
RC receiver ───────────────► Flight Controller ───► flight control
                                    ▲
                                    │ MSP (read + non-flight-critical writes)
                                    │
                        Camera ◄──── BLE ───── Camera Bridge
```

### 3.1 Logical isolation

The bridge may read FC state and write explicitly supported non-flight-critical
display data. It shall never use:

- RC override or RC injection — `MSP_SET_RAW_RC`, `MSP2_INAV_SET_AUX_RC`, MSP RC
  override/overlay commands, or equivalent;
- FC mode-range, USER-mode, PINIO or PINIO_BOX **configuration writes** (notably
  `MSP_SET_MODE_RANGE`, 35 — [BF] `msp/msp_protocol.h:107`);
- Programming Framework writes — on INAV, `MSP2_INAV_SET_LOGIC_CONDITIONS` (0x2023) or
  `MSP2_INAV_SET_GVAR` (0x2214). These are not RC state and are not covered by the RC
  override prohibition above; a GVAR write is the obvious "helpful" shortcut and would
  silently reach into the pilot's flight programming;
- motor commands;
- servo commands;
- arming commands;
- flight-mode commands;
- PID/configuration writes;
- any command that triggers a flash/EEPROM write (notably `MSP_EEPROM_WRITE`);
- `MSP_REBOOT`.

Read-only queries against mode and Programming Framework configuration are permitted and
are used for the setup-time advisories of §12.6 and §12.7. The distinction this section
draws is **write versus read**, not subsystem — which is why `MSP_FAILSAFE_CONFIG`
(§12.4.3) and INAV's `armingFlags` (§12.4.4) are permitted despite naming subsystems this
document otherwise stays clear of. Reading a pilot's failsafe timing to avoid depending on
its default value is the opposite of interfering with it.

### 3.2 Electrical isolation

A bridge crash, reset, watchdog event, brownout, UART failure or camera failure shall
leave flight-controller behavior unchanged. The FC shall continue operating normally with
the bridge electrically connected but completely non-functional, including with the
bridge's TX line held at either rail.

### 3.3 RF coexistence

This is the only path by which a correctly-isolated passenger device can still endanger
a flight, and it is the dimension v0.1 omitted.

The standalone bridge contains its own ESP32-C3 2.4 GHz BLE transmitter. A 2.4 GHz ELRS
receiver on the same aircraft is a **separate radio**, but the two antennas may be only
centimetres apart. BLE transmission or scanning can therefore create in-band desense even
though the bridge is logically and electrically outside the control path.

The adverse case is camera loss: a naive implementation may respond by scanning more
aggressively exactly when the bridge is already degraded. **Degraded bridge operation must
not become degraded control-link operation.**

The RF policy is **deliberately independent of FC arm state**. The bridge is not required
to know whether the FC is armed, and RF safety shall not depend on that information being
available or correct.

Requirements:

1. The bridge shall bound BLE transmit and scan duty cycle (§21.3).
2. Runtime reconnect to a paired camera shall be **directed** and shall back off rather than
   scan continuously (§15, §21.3).
3. Broad/discovery scanning shall occur **only after an explicit user Pair/Setup action**. It
   shall never be entered autonomously because a paired camera is absent or disconnected.
4. BLE transmit power shall be treated as a coexistence parameter and minimized to the
   lowest validated level that maintains reliable camera communication (§21.3, §27.2).
5. The bridge shall be shipped only after its effect on control-link sensitivity/link
   margin has been **measured**, not asserted (§24.2, §27.2).

Requirement 5 is a release gate. Compliance is demonstrated by the §27.2 spike.

The future ELRS-integrated architecture has an additional shared-MCU/scheduler/RF timing
problem and is treated separately in §25.

## 4. Hardware Interface

### 4.1 Flight Controller

Four-wire interface:

```
Bridge        FC
5 V    ───── 5 V
GND    ───── GND
TX     ───── RX
RX     ───── TX
```

Requirements:

- one free full-duplex UART, configured for MSP;
- no additional FC wiring;
- no connection to RC receiver, VTX, or camera (other than Bluetooth).

Default MSP baud rate: 115200. Higher rates may be supported where validated.

### 4.2 Power

The bridge shall document its **peak** current draw, not only its average. BLE transmit
bursts on ESP32-C3 draw several hundred milliamps for short intervals; the installation
guidance shall state the peak figure and the required local bulk capacitance, because the
FC 5 V rail is frequently shared with the camera itself.

The bridge shall tolerate brownout and reset cleanly (§22).

## 5. FC Platform Detection

At startup the bridge shall identify the FC using standard MSP identification queries.

| Purpose | Command |
| --- | --- |
| MSP/API version | `MSP_API_VERSION` |
| FC family | `MSP_FC_VARIANT` → `"BTFL"` / `"INAV"` |
| FC firmware version | `MSP_FC_VERSION` |
| INAV OSD capability probe | `MSP2_INAV_CUSTOM_OSD_ELEMENTS` (0x2100) |

Supported variants:

```
BTFL → Betaflight backend
INAV → INAV backend
```

### 5.1 Capability probing

The bridge shall not infer OSD capability from the version string alone.

- **INAV:** issue `MSP2_INAV_CUSTOM_OSD_ELEMENTS` (0x2100). The FC replies with
  `MAX_CUSTOM_ELEMENTS`, `OSD_CUSTOM_ELEMENT_TEXT_SIZE - 1`, and `CUSTOM_ELEMENTS_PARTS`
  ([INAV] `fc/fc_msp.c:1732-1737`). A missing or error reply means the target was built
  without `USE_PROGRAMMING_FRAMEWORK` and has no custom elements at all — this is the
  correct trigger for telemetry-disabled compatibility mode. `USE_PROGRAMMING_FRAMEWORK`
  is enabled by default ([INAV] `target/common.h:172`) but small flash targets
  may undefine it.
- **Betaflight:** custom messages require 2025.12 or later. Below that, enter
  telemetry-disabled compatibility mode.

Unsupported FC variants shall enter telemetry-disabled compatibility mode and shall not
send platform-specific OSD writes.

The bridge shall track FC family, FC firmware version, MSP/API version and available OSD
capabilities.

### 5.2 FC operational state is deliberately not a control input

The bridge shall **not** query or track actual FC armed/disarmed state for camera control,
BLE reconnect policy, setup availability or maintenance interlocks.

This is a deliberate product boundary:

- camera controls represent **configured RC intent**, not whether the FC accepted that
  intent;
- arming authorization can be denied for reasons unrelated to the camera (prearm, throttle,
  GPS, calibration, failsafe, configuration, etc.);
- coupling camera behaviour to those decisions adds platform-specific semantics without
  improving camera-state truth;
- a passenger device should not need to interpret the aircraft's operational state.

Accordingly, no `MSP_BOXIDS` ARM lookup is part of the V1 runtime contract, and no camera
behaviour anywhere in this document differs according to whether the aircraft is armed.

**One narrow exception, and why it is not one.** INAV startup qualification (§12.4.4) reads
the `ARMED` bit of `armingFlags` — not to decide anything about the camera, but to decide
whether a *different* flag is currently being maintained. `ARMING_DISABLED_RC_LINK` lives
inside the `else` branch of `if (ARMING_FLAG(ARMED))` in `updateArmingStatus()` ([INAV]
`fc/fc_core.c:200-225`), so while armed it is frozen at whatever value it held at the arming
instant. Reading `ARMED` is therefore a **staleness qualifier on another reading**, in the
same category as a timestamp, and the test that keeps it honest is this one:

```
if the camera would behave differently armed vs disarmed → this is a control input, forbidden
if the camera behaves identically and only a flag's freshness is judged → permitted
```

The rule holds. An INAV bridge that has already qualified never re-reads `ARMED` for any
purpose, and an unqualified bridge does nothing either way.

"Arm Record" (§12.1) means that the Record AUX control is configured to the same radio
channel/range normally used by the pilot's arm switch. If the switch enters that range but
the FC remains disarmed, the camera still follows the configured Record control. This is
intentional.

The only FC status metadata used by the control path is a Betaflight-specific RX-loss guard
needed to reject failsafe-substituted AUX values (§12.4). **That guard is about input
validity, not arming state** — but note that Betaflight implements it *as* an arming
permission flag, which is why §12.4 must specify its edges rather than treat it as a clean
boolean.

## 6. Camera Abstraction

Camera-specific protocols shall not leak into the FC interface.

```
CameraService
 ├── GoProDriver          (V1)   — Open GoPro BLE
 ├── OsmoNanoDriver       (V1.1) — DUML over BLE, gated on §27.4
 └── future drivers
```

Common camera state:

```
connected, mode, recording, recordingTime, batteryPercent,
recordTimeRemaining, storageRemaining, resolution, frameRate, errorState
```

Optional capabilities, each behind an explicit capability flag:

```
photoCapture, modeSwitch, presetSwitch, highlight, cameraPower,
clockSet, gpsPush, remainingStorage, temperature, other camera-specific telemetry
```

Unsupported information shall remain **unknown**, not fabricated. Capability flags shall
gate both behaviour and UI: GoPro-specific functionality shall not appear for DJI cameras
or vice versa.

## 7. Camera State Truth Model

OSD information shall represent camera-confirmed state.

```
Pilot requests RECORD → Bridge sends BLE command → Camera reports recording → OSD shows REC
```

The bridge shall not display `REC` solely because a control input was issued.

States:

```
CAM OFF, CONNECTING, READY, PHOTO, VIDEO, REC, CAM STALE, CAM LOST, CAM ERR
```

`CAM STALE` covers the interval between "last update is too old to trust" and "the link is
gone", which v0.1 left undefined. Thresholds are in §17.

## 8. OSD Requirements

Field content is constrained by verified per-platform limits (§9, §10). Because INAV
upper-cases all custom element text at render time ([INAV]
`io/osd/custom_elements.c:215`), the bridge shall compose all OSD strings in
uppercase on every platform, so Betaflight and INAV render identically.

Minimum common dataset — four fields, each ≤ 15 characters so one layout satisfies both
platforms:

| # | Field | Examples | Max used |
| --- | --- | --- | --- |
| 1 | Camera state + timer | `REC 01:23`, `VID READY`, `CAM LOST`, `CAM STALE` | 12 |
| 2 | Camera battery | `CAM BAT 72%` | 11 |
| 3 | Remaining record capacity | `REC LEFT 38M` | 12 |
| 4 | Capture configuration | `4K100`, `1080P240`, `5.3K25` | 8 |

If a camera does not report a value, that field shall be blank or omitted. No estimated
value may be presented as confirmed camera telemetry.

Where a platform offers more slots than the minimum (INAV, §10), additional optional fields
may be presented: camera temperature, storage remaining, active preset, warnings.

## 9. Betaflight OSD Backend

**Minimum version:** Betaflight **2025.12.5** (§0). The custom-message feature merged
2024-12-22 to the 2025.12 milestone; the supported floor is the .5 patch release, which is
also the citation baseline.

Transport:

```
MSP2_SET_TEXT (0x3007) → subtype MSP2TEXT_CUSTOM_MSG_0..+3 → pilotConfig.message[0..3] → OSD
```

Verified constants:

| Item | Value | Source |
| --- | --- | --- |
| Command | `MSP2_SET_TEXT = 0x3007` | [BF] `msp/msp_protocol_v2_betaflight.h:28` |
| Subtype base | `MSP2TEXT_CUSTOM_MSG_0 = 7` (so 7, 8, 9, 10) | [BF] `msp_protocol_v2_betaflight.h:43` |
| Slot count | `OSD_CUSTOM_MSG_COUNT = 4` | [BF] `pg/pilot.h:27` |
| Chars per slot | `MAX_NAME_LENGTH = 16` | [BF] `pg/pilot.h:26,32` |
| Handler | `mspProcessInCommand` (`msp.c:2647`), `case MSP2_SET_TEXT` | [BF] `msp/msp.c:4052` |

Payload: `[subtype u8][length u8][chars...]`.

**Armed behaviour:** the handler carries no `ARMING_FLAG(ARMED)` guard; the armed checks in
`msp.c` apply to unrelated commands. Writes are accepted while armed. (Confirm on hardware
per §27.1 before relying on this.)

**Persistence:** writes land in the `pilotConfig` parameter group in RAM. No flash write
occurs unless something separately issues `MSP_EEPROM_WRITE`, which the bridge shall never
send (§3.1).

**But the bridge is not the only client.** `pilotConfig_t` carries
`char message[OSD_CUSTOM_MSG_COUNT][MAX_NAME_LENGTH + 1]` and is a parameter group ([BF]
`pg/pilot.h:29-35`), so a Configurator or CLI `save` performed by the user persists whatever
the bridge last wrote — exactly as on INAV (§10). Betaflight is **not** the safer platform
here; v0.7 documented the corollary only for INAV. See §10 for the shared requirement.

The user shall enable and position OSD elements `Custom Message 1..4`. No custom Betaflight
build shall be required.

### 9.1 Slot ownership

The four custom-message slots are a shared global resource. Another peripheral (a lap
timer, a radar, a rangefinder) writing the same slot will silently fight the bridge for it.

The bridge shall therefore:

- expose which slot indices it owns as configuration (default: all four);
- write only owned slots;
- leave unowned slots untouched.

### 9.2 Update policy

| Data | Rate |
| --- | --- |
| State changes | Immediate |
| Recording timer | 1 Hz maximum |
| Battery / storage | On change, or low-rate periodic refresh (≤ 1 Hz) |
| Resolution / FPS | On change |

The bridge shall not rewrite a slot whose rendered content is unchanged.

## 10. INAV 8 OSD Backend

**Minimum version:** INAV **8.0.1** (§0), within the 8.x family. INAV support is a
release-blocking requirement, not an optional compatibility mode.

**Gate status: RESOLVED.** v0.1 treated runtime injection into INAV Custom OSD Elements as
an unknown that could sink the backend, and hedged with an `MSP_SET_NAME` → Craft Name
fallback. The mechanism exists, is reachable while armed, and does not touch flash. The
fallback is removed from this specification.

Transport:

```
MSP2_INAV_SET_CUSTOM_OSD_ELEMENTS (0x2102) → osdCustomElements[n] → Custom OSD Element n
```

Verified constants:

| Item | Value | Source |
| --- | --- | --- |
| Command | `MSP2_INAV_SET_CUSTOM_OSD_ELEMENTS = 0x2102` | [INAV] `msp/msp_protocol_v2_inav.h:114` |
| Capability query | `MSP2_INAV_CUSTOM_OSD_ELEMENTS = 0x2100` | [INAV] `msp_protocol_v2_inav.h:112` |
| Element count | `MAX_CUSTOM_ELEMENTS = 8` | [INAV] `io/osd/custom_elements.h:24` |
| Parts per element | `CUSTOM_ELEMENTS_PARTS = 3` | [INAV] `custom_elements.h:23` |
| Text size | `OSD_CUSTOM_ELEMENT_TEXT_SIZE = 16` (15 usable + NUL) | [INAV] `custom_elements.h:22` |
| Handler | `mspFcProcessInCommand` | [INAV] `fc/fc_msp.c:3519` |
| Build gate | `USE_PROGRAMMING_FRAMEWORK` (default on) | [INAV] `target/common.h:172` |

### 10.1 Payload format

Exactly **28 bytes**. The handler enforces
`dataSize == (OSD_CUSTOM_ELEMENT_TEXT_SIZE - 1) + (CUSTOM_ELEMENTS_PARTS * 3) + 4`
([INAV] `fc_msp.c:3521`) — short writes are rejected with `MSP_RESULT_ERROR`.

```
[element index      u8 ]   < MAX_CUSTOM_ELEMENTS
[part0.type         u8 ]   [part0.value u16]
[part1.type         u8 ]   [part1.value u16]
[part2.type         u8 ]   [part2.value u16]
[visibility.type    u8 ]   [visibility.value u16]
[text              15 B]   NUL is appended by the FC
```

For a plain text field the bridge sets:

```
part0.type = CUSTOM_ELEMENT_TYPE_TEXT     (1)   part0.value = 0
part1.type = CUSTOM_ELEMENT_TYPE_NONE     (0)   part1.value = 0
part2.type = CUSTOM_ELEMENT_TYPE_NONE     (0)   part2.value = 0
visibility.type = CUSTOM_ELEMENT_VISIBILITY_ALWAYS (0)   visibility.value = 0
text = 15 uppercase characters, space padded
```

(Type and visibility enums: [INAV] `custom_elements.h:26-63`; `CUSTOM_ELEMENT_TYPE_TEXT`
at `:28`, `CUSTOM_ELEMENT_VISIBILITY_ALWAYS` at `:60`.)

**Whole-element rewrite.** Every SET replaces parts, visibility and text together. The
bridge shall cache complete element state and resend it intact on each update; it cannot
patch one field.

**Armed behaviour:** the custom-element setter carries **no** `ARMING_FLAG(ARMED)` guard —
[INAV] `fc/fc_msp.c:3519-3540` — and is therefore reachable while armed. (Confirm on
hardware per §27.1.)

*Correction against v0.6, which claimed INAV "gates only `MSP_REBOOT` on
`ARMING_FLAG(ARMED)`". That is false on 8.0.1: armed guards appear at [INAV]
`fc/fc_msp.c:1434,1894,2617,2628,2635,2643,2651,3316,3324`. The conclusion is unaffected
because none of them covers `MSP2_INAV_SET_CUSTOM_OSD_ELEMENTS`, but the claim shall be
stated as "this handler is unguarded", never as "INAV guards only reboot" — the latter
would license writes that are in fact blocked.*

**Persistence:** the handler writes `osdCustomElementsMutable(n)` — the parameter group in
RAM. No flash write.

**Persisted camera text is a telemetry-truth problem, not a flight-safety one.** If the user
saves configuration from the Configurator, whatever the bridge last wrote becomes persisted.
v0.7 called that harmless. It is harmless to flight control and it is **not** harmless to the
§7 truth model:

```
runtime          bridge writes  REC 04:22
user saves FC configuration
power cycle      FC loads       REC 04:22        ← no camera, no bridge, no recording
bridge dead / absent / slow
                 OSD still says REC 04:22        ← indefinitely
```

That is the OSD asserting a camera-confirmed state that nothing confirmed. The same exposure
exists on Betaflight (§9).

**Requirement, both platforms.** As soon as the FC backend is up — and **before** any camera
connection is attempted — the bridge shall write every owned slot (§9.1, §10.2) to
non-authoritative content: blank, or a connection-state string such as `CONNECTING`. This
runs on every bridge start, whether or not a camera is expected, and it is the only write the
bridge makes before it has camera state to report.

A bridge that never comes up cannot clear anything, so the residual case — persisted text
plus a dead bridge — remains. It is bounded by not saving configuration with live camera text
on screen, which §23 shall note; it is not otherwise solvable from this side.

**Rendering:** text is upper-cased (`sl_toupper`, [INAV] `custom_elements.c:215`).

### 10.2 Parity and slot ownership

INAV offers 8 × 15 characters against Betaflight's 4 × 16. Functional parity with §8 is
therefore comfortably met, with room for the optional fields in §8. Exact visual layout need
not match Betaflight.

Element ownership is configurable exactly as in §9.1 (default: elements 1–4), since INAV
custom elements are equally a shared resource with the Programming Framework.

### 10.3 Hard constraint

INAV support shall require: stock INAV 8 firmware; no custom target; no firmware patch; no
special FC hardware; only the same MSP UART used by the bridge. It shall not abuse
RC channels, RSSI, aircraft battery, altitude or navigation values.

## 11. Video-System Independence

The bridge shall inject into the FC's OSD model rather than communicate with the VTX.
Camera telemetry is therefore compatible with any OSD output the selected FC supports — DJI,
HDZero, Walksnail/Avatar, and MSP DisplayPort. No VTX-specific bridge firmware shall be
required.

## 12. Recording Control

Camera controls are mapped directly to AUX channels and value ranges in the **bridge**
configuration. This control model is intentionally independent of FC mode configuration
**and of actual FC armed state**:

- `USER1..USER4`, PINIO, Camera Control modes, and other FC box modes are not required and
  are never bound automatically to a camera control;
- existing FC mode/PINIO configuration is never modified, and is read only for the
  setup-time advisory of §12.6;
- the bridge shall never issue `MSP_SET_MODE_RANGE`, `MSP_SET_RAW_RC`,
  `MSP2_INAV_SET_AUX_RC`, RC override/overlay commands, or any equivalent command that
  changes FC RC state (§3.1);
- Betaflight and INAV share one bridge-side AUX/range evaluation implementation;
- actual FC armed/disarmed state is not part of the camera-control model (§5.2).

Each configured control consists of an AUX channel plus one or more non-overlapping value
ranges. The bridge evaluates the latest `MSP_RC` sample locally. Configuration may expose
common 2-state and 3-state presets, but the stored representation shall be **explicit
ranges** so unusual radio endpoints remain supported.

### 12.1 Control modes

1. **Arm Record (default convenience preset)** — a normal **level-controlled** Record
   mapping whose AUX channel/range is normally set to the same channel/range used by the
   pilot's radio arm switch. Entering the range requests start recording; leaving it
   requests stop. The bridge does not inspect whether the FC actually armed. A rejected FC
   arm may therefore still start the camera, by design.
2. **AUX Toggle** — one configured AUX range is active → record; outside that range → stop.
   Arm Record is functionally this mode with a conventional setup choice.
3. **Push Button** — entry into a configured AUX active range is a momentary event that
   toggles recording or triggers the configured action, subject to §12.3.
4. **Combi** — a level control provides the base intent while a Push Button provides a
   manual override. Arbitration is specified in §12.1.1 and is **not** left to the
   implementation.

In all modes, displayed state follows camera acknowledgement (§7), never the input.

#### 12.1.1 Combi arbitration — required

Prose alone permits two conforming implementations to behave completely differently: with
the level control held in its record range, does a button-stop persist, or does the next
10 Hz evaluation immediately restart recording? The following is normative.

```
baseIntent      = level control evaluation (RECORD | STOP)
manualStop      = latching boolean, cleared at boot
effectiveIntent = baseIntent == RECORD && !manualStop ? RECORD : STOP
```

Transitions:

| Event | Effect |
| --- | --- |
| Push Button while `baseIntent == RECORD` | toggle `manualStop` |
| Push Button while `baseIntent == STOP` | no effect on `manualStop` (see below) |
| `baseIntent` transitions to `STOP` | clear `manualStop` |

The resulting behaviour is the intuitive one:

```
arm switch ON   → record
button          → stop     (manualStop set; level control does not restart it)
button          → record   (manualStop cleared)
arm switch OFF  → stop     (manualStop cleared)
arm switch ON   → record   (normal; the earlier override does not persist)
```

Requirements:

1. `manualStop` shall not restart recording by itself; it can only subtract from
   `baseIntent`. A Combi button cannot start a recording the level control is not asking
   for. Where a user wants button-only start, that is Push Button mode (§12.1, mode 3), not
   Combi.
2. `manualStop` is **camera-control state, not input state.** It is therefore part of what
   §12.4 freezes, and it survives an RC-validity freeze intact. It shall not be cleared by
   entering, or by leaving, a freeze.
3. `manualStop` shall be cleared on bridge reboot, so that a level control alone determines
   state after a power cycle (§12.1, recoverability).
4. Where a Combi button is mapped to an action other than stop/restart (for example
   HiLight), that action is stateless and does not participate in this arbitration.

**Level control is the specified default for the Record function, and the reason is
recoverability.** A level control reconciles to the correct state after any interruption —
RC loss, bridge reboot, or the Betaflight recovery window of §12.4. An edge-triggered
control cannot: an event that occurs while the bridge is not accepting transitions is gone.
Push Button remains available and supported, but shall not be the default for Record.

**Setups without an arm switch.** INAV still supports yaw-stick arming. A pilot who arms by
stick has no arm-switch AUX channel to map, so the Arm Record preset does not apply to them;
they shall configure a Record control on any other AUX channel. Betaflight is unaffected
(stick arming was removed). Setup documentation shall state this rather than presenting
Arm Record as universally available.

### 12.2 Reading RC values

The bridge reads the FC's current RC values using standard `MSP_RC` (105 — [BF]
`msp/msp_protocol.h:176`) on both platforms:

| Platform | Serialization | Source |
| --- | --- | --- |
| Betaflight | `rcData[0 .. rxRuntimeState.channelCount-1]` | [BF] `msp/msp.c:1317-1321` |
| INAV | `rxGetChannelValue(0 .. rxRuntimeConfig.channelCount-1)` | [INAV] `fc/fc_msp.c:621-625` |

The **response length**, not a hard-coded channel count, determines how many channels are
available.

The first four channels are the primary flight controls; subsequent channels are addressed
to the user as AUX1, AUX2, … . **The bridge shall accept only AUX channels as camera-control
inputs; channels 1–4 are out of scope for control mapping.** This avoids platform-specific
AETR/AERT primary-channel ordering entirely.

**Numeric note.** Betaflight's `rcData` is `float` ([BF] `rx/rx.c:128`) serialized through
`sbufWriteU16`, i.e. truncated. Bridge range comparisons may therefore differ by 1 µs from
the FC's own arithmetic at an exact boundary, and no operational meaning shall be placed on
a single microsecond boundary.

#### 12.2.1 Range evaluator — required semantics

"Reasonable hysteresis" is not a requirement, because nothing can be tested against it. The
evaluator is specified:

1. **Hysteresis is ±10 µs** around each configured range boundary, applied as a Schmitt
   trigger: a channel is considered to have *entered* a range at `min + 10` / `max - 10`,
   and to have *left* it at `min - 10` / `max + 10`. The value is configurable; 10 µs is the
   default and the tested value.
2. **The bound is not arbitrary.** Betaflight quantises mode ranges to a 25 µs grid —
   `MODE_STEP_TO_CHANNEL_VALUE(step) = 900 + 25 * step`, [BF] `fc/rc_modes.h:101`. A
   hysteresis of ±25 µs would span a full step and make two adjacent ranges mutually
   ambiguous; ±10 µs stays safely under half a step, so a user who lays bridge ranges out on
   the same grid as their FC modes cannot create an overlap by enabling hysteresis.
3. **Minimum range width.** Setup shall reject any configured range narrower than
   `4 × hysteresis` (40 µs at the default), and shall reject two ranges on the same channel
   whose boundaries are closer than `2 × hysteresis`.
4. Hysteresis applies to the evaluator only. The stored configuration remains the explicit
   range the user entered, so a displayed range always matches what was typed.

### 12.3 Push Button timing

`MSP_RC` is polled, so a momentary switch shorter than the sample interval can be missed
entirely or observed once at an arbitrary phase. The specification shall not leave this to
the implementation.

- The bridge shall require a configured AUX range to be observed active on **at least two
  consecutive samples** before emitting a Push Button event.
- The documented minimum hold time shall be **250 ms**, and setup shall state it to the
  user.
- Consecutive Push Button events shall be separated by a minimum interval (default 400 ms)
  so a single press cannot double-toggle.
- When any Push Button control is enabled, `MSP_RC` shall be polled at 20 Hz rather than
  10 Hz (§21.1).
- **A Push Button press that occurs while camera-control transitions are frozen is lost.**
  On Betaflight this includes the recovery window of §12.4, which persists for
  `failsafe_recovery_delay` after valid RC data returns — an FC-configured value the bridge
  neither reads nor needs (§12.4). Setup documentation shall state this limitation where a
  Push Button control is configured.
- **Both qualifying samples of a Push Button event shall be guarded** per §12.4. A press
  detected across a sample pair in which either member failed its validity bracket shall be
  discarded, not committed.

### 12.4 RC-link loss and failsafe behaviour

The two supported FC families do not expose identical `MSP_RC` semantics during RX
loss, so the platform adapters shall handle them differently. The common semantic
requirement is **RX loss shall not create a camera-control transition**; the implementation
mechanism need not be identical.

#### Betaflight

Betaflight `MSP_RC` serializes `rcData[]`. `rcData` is explicitly the *scaled, modified,
checked and constrained* channel value ([BF] `rx/rx.c:128`), and the RX failsafe path can
replace an AUX channel with its configured HOLD/SET failsafe value before `MSP_RC` is
generated — `getRxfailValue()` at [BF] `rx/rx.c:619`, applied at `:724`, `:730`, `:744`
and written to `rcData` at `:762`. AUX channels default to `RX_FAILSAFE_MODE_HOLD`
([BF] `rx/rx.c:158`), which is benign; `RX_FAILSAFE_MODE_SET` is the hazard.

**Scope of the hazard, stated precisely.** Under the stock `HOLD` default the substituted
value *is* the last accepted value, so no transition can be manufactured and the guard has
nothing to catch. The guard exists for `RX_FAILSAFE_MODE_SET`, which a pilot may legitimately
configure on the bridge's AUX channel. Implementers shall not read routine RX glitches as
hazards; they shall implement the guard anyway, because a spec that works only under default
config is not a spec.

```
Record AUX HIGH
    ↓
RX loss
    ↓
Betaflight substitutes configured failsafe AUX value
    ↓
MSP_RC appears to leave Record range
    ↓
naive bridge stops recording
```

**Requirement:** while Betaflight reports RX data invalid, the bridge shall freeze the last
accepted camera-control state and shall not act on AUX transitions.

The V1 guard uses `ARMING_DISABLED_RX_FAILSAFE` from the `armingDisableFlags` U32 appended
to `MSP_STATUS` ([BF] `msp/msp.c:1128`, within the `MSP_STATUS` case at `:1094-1095`).
`failsafeOnValidDataFailed()` sets this flag unconditionally when RX data fails, including
`BOXFAILSAFE`-induced loss ([BF] `flight/failsafe.c:196-209`, set at `:202`), and
`setArmingDisabled()` is an unconditional bitwise OR with no armed-state check ([BF]
`fc/runtime_config.c:73-76`). **The guard is therefore valid while armed**, and its use does
not constitute FC armed-state decoding.

##### 12.4.1 Guarded commit — required

`MSP_STATUS` and `MSP_RC` are **separate MSP transactions and do not represent one FC
instant.** v0.6 polled them "together" and treated the pair as a snapshot. It is not one.

The ordering inside Betaflight is what makes this exploitable — and also what makes the fix
work. In `calculateRxChannelsAndUpdateFailsafe`, substituted values are written to `rcData[]`
in the per-channel loop ([BF] `rx/rx.c:762`) and the failsafe flag is updated only
*afterwards*, at [BF] `rx/rx.c:767,770`. So the data leads the flag. A status read taken
before the RC read can legitimately report *valid* while the RC reply that follows already
carries substituted values:

```
bridge: MSP_STATUS  →  guard clear
                       FC: rcData ← failsafe SET value   (rx.c:762)
                       FC: guard asserted                (rx.c:770)
bridge: MSP_RC      →  substituted value, guard looked clear
```

Because the FC's internal data→flag gap is microseconds while the bridge's inter-transaction
gap is milliseconds, a **trailing** status read is a reliable detector. `STATUS₂` is the
load-bearing leg.

**Requirement.** A Betaflight RC sample that would produce a camera-control transition shall
be committed only under a validity bracket:

```
STATUS₁  → must be clear
MSP_RC   → candidate sample
STATUS₂  → must be clear
         → commit
```

If either status read reports the guard asserted, the candidate is **discarded** and the
last accepted camera-control state is frozen (§12.4.2).

Three transactions per poll are not required. The permitted optimisation is:

```
STATUS → MSP_RC
    if the sample implies no control transition:   accept, done
    if the sample implies a transition:            issue STATUS₂, commit only if clear
```

This keeps the steady-state cost at two transactions and pays the third only on the rare
poll that actually changes something. Per §12.3, **both** qualifying samples of a Push
Button event shall be individually bracketed.

This requirement is Betaflight-only. INAV needs no bracket, because it freezes values rather
than substituting them.

##### 12.4.2 Guard edge semantics — required

`ARMING_DISABLED_RX_FAILSAFE` is an *arming-permission* flag borrowed as an *input-validity*
flag. It works, but its edges are shaped for arming, not for camera control.

**Assert is immediate. Clear is delayed.** The flag clears only after
`receivingRxDataPeriodPreset` of continuously valid data ([BF] `flight/failsafe.c:186-192`),
which is `rxDataRecoveryPeriod` — `failsafe_recovery_delay`, floored at
`PERIOD_RXDATA_RECOVERY` = 100 ms ([BF] `flight/failsafe.c:101-105,111`).

**The bridge shall not encode the delay's value.** It waits for the flag to clear, whatever
the reason and whatever the latency. For documentation only: the stock 2025.12.5 default is
**500 ms** (`DEFAULT_FAILSAFE_RECOVERY_DELAY = 5`, or `1` = 100 ms under `USE_RACE_PRO` —
[BF] `flight/failsafe.c:66-70`), and the setting is user-configurable.

> *Correction against v0.6, which stated a 1.0 s default.* That figure came from stale
> comments inside Betaflight's own `failsafe.c` (`:188`, `:191` say "1.0s"/"typically 1s"),
> which `:172` in the same file contradicts by correctly saying 500 ms. The file disagrees
> with itself; the reset template is authoritative. This is a good argument for the rule in
> §0 — and a reminder that reading source means reading the code, not the comments.

**`BOXFAILSAFE` recovery may be immediate, but not universally.** Where Betaflight has
entered the switch-induced failsafe path it sets `receivingRxDataPeriodPreset = 0` ([BF]
`flight/failsafe.c:339`), which removes the recovery delay for that recovery. This is a
property of one branch of `failsafeUpdateState`, not a guarantee attaching to every state in
which `BOXFAILSAFE` has been touched. The bridge shall therefore not special-case
`BOXFAILSAFE` at all: **wait for the guard to clear, regardless of why or how quickly.**

Consequences the implementation shall honour:

1. After real RX loss, the guard holds camera state **past** the return of valid pilot
   input, by the configured recovery delay. This is deliberate and conservative; it is not a
   defect.
2. Switch-induced and transmitter-loss recovery can produce visibly different latencies
   (§24.2). Both are correct. A tester who sees only one will report the other as a bug.
3. Level controls reconcile to the correct camera state once the guard clears. Edge
   controls do not — see §12.3.
4. `manualStop` (§12.1.1) is camera-control state and is frozen with it, not reset by it.

##### 12.4.3 Startup qualification — Betaflight — required

The bridge's control input has an explicit qualification state and it starts unqualified. The
whole difficulty is the qualification *criterion*, because on Betaflight the obvious one does
not work.

**A clear guard is not proof that valid RC data was ever received.** It is only proof that
the FC has not yet concluded otherwise — and immediately after boot it has not yet had time
to conclude anything:

- `failsafeReset()` does **not** set the flag ([BF] `flight/failsafe.c:94-115`). It sets
  `rxLinkState = FAILSAFE_RXLINK_DOWN` and nothing more.
- The flag is asserted later, by one of: `failsafeOnValidDataReceived()` on the **first**
  valid data, via the `validRxDataFailedAt == 0` branch ([BF] `flight/failsafe.c:176-183`);
  `failsafeOnValidDataFailed()` on invalid data (`:202`); or
  `failsafeCheckDataFailurePeriod()` (`:212-221`, set at `:219`), which the scheduler runs
  every 10 ms with **no `failsafeIsMonitoring()` gate** — that gate applies only to
  `failsafeUpdateState` at `:231` — from [BF] `scheduler/scheduler.c:569-571`.
- With no receiver present at all, that third path asserts only after `rxDataFailurePeriod`
  elapses, i.e. `failsafe_delay`, whose **stock default is 15 = 1.5 s** ([BF]
  `flight/failsafe.c:75`).

*v0.7 established all of the above and then defined qualification as "observe one valid
guarded RC snapshot" — a criterion this very window satisfies. `STATUS₁` clear, `MSP_RC`
returning `midrc`, `STATUS₂` clear: the bracket passes, the bridge qualifies, and the baseline
it captures is an initialisation value. For a Record range spanning mid-stick that baseline
is `ACTIVE`, so the first act of a "correctly" qualified bridge is to reconcile a level
control into recording. The section refuted itself; this revision fixes the criterion.*

So there is a window of up to ~1.5 s in which the guard reads clear and `rcData[]` holds
initialisation values: `midrc` for every channel ([BF] `rx/rx.c:308`).

The concrete failure this prevents:

| Control | Init value | Boot-window behaviour without qualification |
| --- | --- | --- |
| **Arm Record** | ARM AUX is explicitly initialised to an **OFF** value ([BF] `rx/rx.c:314` ff.) | safe — by luck, not by design |
| **AUX Toggle / Push Button** on any other AUX | `midrc` (1500) | a configured range spanning mid-stick reads **ACTIVE at boot** — the camera starts recording on the bench with the transmitter switched off |

**The qualification criterion.** The bridge shall exclude the FC's initial no-RX detection
window before treating a clear guard as meaningful:

```
FC detected
    ↓
read failsafe_delay  (MSP_FAILSAFE_CONFIG)
    ↓
control input = UNQUALIFIED          ← no camera-control transitions of any kind
    ↓
qualify on either:

  A. the guard has been observed ASSERTED, and subsequently observed CLEAR,
     under a passing bracket (§12.4.1)

  B. the guard has been observed continuously CLEAR, under passing brackets,
     for longer than  grace = max(failsafe_delay × 100 ms, 100 ms) + margin,
     measured from the bridge's first MSP exchange with this FC
    ↓
establish baseline from that snapshot
    ↓
control input = QUALIFIED
```

**The first qualified sample may reconcile a level control. It shall never generate an edge
event.**

`MSP_FAILSAFE_CONFIG` returns `failsafe_delay` as its first byte ([BF] `msp/msp.c:1657`).
Three details govern how that byte is used:

1. **It is a U8 in tenths of a second**, and the FC floors the derived period:
   ```c
   failsafeState.rxDataFailurePeriod = failsafeConfig()->failsafe_delay * MILLIS_PER_TENTH_SECOND;
   if (failsafeState.rxDataFailurePeriod < PERIOD_RXDATA_RECOVERY) {
       failsafeState.rxDataFailurePeriod = PERIOD_RXDATA_RECOVERY;   // 100 ms
   }
   ```
   [BF] `flight/failsafe.c:96-100`. A pilot who sets `failsafe_delay = 0` gets 100 ms, not
   zero, and the bridge shall apply the same floor.
2. **`margin` exists to guarantee the assertion is *observed*, not to guarantee it has
   occurred.** `failsafeReset()` sets `validRxDataReceivedAt = 0` ([BF]
   `flight/failsafe.c:106`) and `failsafeCheckDataFailurePeriod()` compares it against raw
   `millis()` (`:215`), so the FC's deadline runs from **MCU boot**. Bridge sync is
   necessarily at or after FC boot, which makes B's expiry unconditionally later than the
   FC's own assertion — condition B cannot fire first. What the margin buys is polling time.
   The assertion is sticky, re-asserted every 10 ms while data is absent, so `margin` need
   only exceed a small number of the bridge's own poll intervals.
3. **If the read fails or is unsupported, condition B is unavailable and the bridge shall
   qualify on condition A only.** There is no safe fixed fallback: `failsafe_delay` is
   user-settable well beyond its default, so no hardcoded grace bounds it. Falling back to
   A-only costs late-attach convenience and costs nothing in safety.

Why the two conditions together are sufficient — the guard is asserted on **every** boot
path, either by `failsafeOnValidDataReceived()` on first valid data (`:180-183`) or by the
timeout (`:219`). There is no sequence in which it stays clear indefinitely. So:

| Case | Path |
| --- | --- |
| Fresh FC, transmitter **on**, bridge up within the window | first RC asserts the guard, which then clears → **A** |
| Fresh FC, transmitter **off** | guard asserts at `failsafe_delay` before B's grace expires → stays **UNQUALIFIED**, correctly |
| Fresh FC, transmitter **on**, bridge slower to boot than ~1.5 s | assert→clear already happened unobserved → **B**, after the grace |
| Bridge attached to an FC running for minutes | guard long since clear → **B** |

Row three is the one to document rather than fix: a bridge that finishes MSP sync more than
`failsafe_delay` after FC boot misses the edge and waits out condition B, so normal power-up
qualification can take roughly `failsafe_delay + grace` ≈ 3 s at stock settings. This is
slower than camera BLE connect and therefore invisible in use — but it **will** be filed as a
bug if it is not written down, which is why it is written down.

Requirements:

- the `UNQUALIFIED` state shall be treated as **normal, not a fault**; no RC-loss warning
  shall be raised for it (§18);
- qualification shall not be inferred from elapsed time alone, from the FC being detected,
  from a guard state observed before any RC sample was bracketed, or from a clear guard
  before the FC's no-RX window has been excluded;
- **every** later re-qualification — after an RX-loss freeze, after a bridge reboot, after an
  FC reboot — follows the same rule: re-baseline, reconcile level controls, emit no edge. An
  FC reboot restarts the FC's window too, so a re-qualification following one is subject to
  A/B exactly as the first was;
- the grace clock of condition B shall restart on any FC re-detection.

Because this is expressed as qualification rather than as an assumption about the FC's boot
timing, it holds identically whether the bridge powers up with the FC or connects to an FC
that has been running for ten minutes. v0.6's framing covered only the first case; v0.7's
covered both cases and admitted a false qualification in each.

#### INAV 8

INAV 8 deliberately behaves differently, and does so through **two independent mechanisms**.

Per-channel, an invalid pulse is replaced by the last good value:

```c
// Apply invalid pulse value logic
if (!isRxPulseValid(sample)) {
    sample = rcChannels[channel].data;   // hold channel, replace with old value
```
[INAV 8.0.1] `rx/rx.c:489`

And globally, no channel's data updates at all unless both the flight channels and the RX
signal are valid:

```c
// Update channel input value if receiver is not in failsafe mode
// If receiver is in failsafe (not receiving signal or sending invalid channel values) - last good input values are retained
if (rxFlightChannelsValid && rxSignalReceived) {
    for (int channel = 0; channel < rxChannelCount; channel++) {
        rcChannels[channel].data = rcStaging[channel];
    }
}
```
[INAV 8.0.1] `rx/rx.c:500-506`

There is consequently no path by which an AUX value changes during RX loss, and INAV has no
per-channel failsafe SET equivalent to Betaflight's. `MSP_RC` naturally freezes.

**No separate INAV RC-validity *runtime* gate is required for V1.** The hardware test shall
confirm that the observed `MSP_RC` value remains at the last-good level through RX loss
(§27.1).

Because INAV never freezes *transitions* — it freezes *values* — there is no INAV recovery
window and no INAV equivalent of the swallowed-Push-Button case above. Recovery is
immediate and continuous.

**Startup is the exception, and it is not covered by any of the above.** See §12.4.4.

##### 12.4.4 Startup qualification — INAV — required

INAV has the same boot exposure as Betaflight. Every channel is initialised to mid-stick:

```c
rcChannels[i].raw  = PWM_RANGE_MIDDLE;
rcChannels[i].data = PWM_RANGE_MIDDLE;
```
[INAV] `rx/rx.c:271-272`, with the ARM AUX specially initialised to an OFF value at
`:279-289` — the identical safe-by-luck asymmetry documented in §12.4.3's table. So a fresh
INAV FC with no transmitter returns 1500 from `MSP_RC`, and a Record range spanning mid-stick
reads ACTIVE, before any real RC packet has existed.

v0.7 required startup qualification on both platforms in §24.1 and specified it only for
Betaflight. This section closes that.

**INAV needs no timer and no configuration read, because it has a signal Betaflight lacks.**
`ARMING_DISABLED_RC_LINK` (bit 18, [INAV] `fc/runtime_config.h:38`) is driven directly from
link state:

```c
/* CHECK: RX signal */
if (!failsafeIsReceivingRxData()) {
    ENABLE_ARMING_FLAG(ARMING_DISABLED_RC_LINK);
} else {
    DISABLE_ARMING_FLAG(ARMING_DISABLED_RC_LINK);
}
```
[INAV] `fc/fc_core.c:220-225`

and `failsafeIsReceivingRxData()` is simply `rxLinkState == FAILSAFE_RXLINK_UP` ([INAV]
`flight/failsafe.c:289`), with `failsafeReset()` setting `rxLinkState = FAILSAFE_RXLINK_DOWN`
(`:165`). There is **no timeout path** — the flag is asserted from the first
`updateArmingStatus()` pass and clears only when data actually arrives. On INAV, unlike
Betaflight, a **clear guard genuinely proves valid RC data was received.**

The flag is exposed as part of the full U32 `armingFlags` in `MSP2_INAV_STATUS`
(`sbufWriteU32(dst, armingFlags)`, [INAV] `fc/fc_msp.c:459`).

**The one trap.** That entire check block sits inside the `else` of:

```c
static void updateArmingStatus(void)
{
    if (ARMING_FLAG(ARMED)) {
        LED0_ON;
    } else {
        /* ... every arming check, including RC_LINK ... */
```
[INAV] `fc/fc_core.c:200-225`

**While armed, `ARMING_DISABLED_RC_LINK` is not maintained** — it holds whatever value it had
at the arming instant. This is why the flag is startup-only, and the reason shall be stated
wherever the requirement is: it is not a design preference that a later implementer may
revisit for a cleaner runtime gate than Betaflight's. Promoting it to one yields a validity
signal that stops updating at exactly the moment flight begins.

**Requirement.**

```
FC detected (INAV)
    ↓
control input = UNQUALIFIED
    ↓
qualify when:  ARMED is not set   AND   ARMING_DISABLED_RC_LINK is clear
    ↓
establish baseline from the concurrent MSP_RC sample
    ↓
control input = QUALIFIED       ← ARMED is never read again
```

No bracket is required: INAV's flag and its RC data cannot disagree the way Betaflight's can,
because nothing substitutes values (§12.4). A single `MSP2_INAV_STATUS` followed by `MSP_RC`
is sufficient. As on Betaflight, the first qualified sample may reconcile a level control and
shall never emit an edge event.

**Known consequence.** A bridge that reboots while the aircraft is armed cannot qualify until
the aircraft disarms, because the flag it needs is not being updated. It therefore performs no
camera-control transitions for the remainder of that flight, holding whatever the camera was
doing. That is the safe direction and it is deliberate; it shall be documented in release
notes rather than worked around, since every available workaround involves trusting a flag the
FC has stopped maintaining.

### 12.5 What `MSP_RC` actually contains

`MSP_RC` is the FC's **effective channel state**, not a raw tap of the receiver protocol.

- On Betaflight it includes channel scaling and may include failsafe substitution (§12.4).
- On INAV, `rxGetChannelValue()` may additionally return Logic-Condition-overridden values
  when the override flag is set:

  ```c
  int16_t rxGetChannelValue(unsigned channelNumber) {
      if (LOGIC_CONDITION_GLOBAL_FLAG(LOGIC_CONDITION_GLOBAL_FLAG_OVERRIDE_RC_CHANNEL)) {
          return getRcChannelOverride(channelNumber, rcChannels[channelNumber].data);
      } else {
          return rcChannels[channelNumber].data;
      }
  }
  ```
  [INAV 8.0.1] `rx/rx.c:657`

INAV versions later than 8.0.1 additionally expose an MSP AUX overlay for CH9–CH32
(`MSP2_INAV_SET_AUX_RC`), settable by any MSP client; this is **not present in 8.0.1** and is
noted only so that a future retarget re-examines it. None of this violates §3.1; the bridge
overrides nothing. Documentation shall therefore describe the control source as the
**FC-effective AUX value**, not necessarily the physical transmitter switch position.

### 12.6 Setup-time overlap advisory

At setup, and never as part of runtime control evaluation, the bridge may read
`MSP_MODE_RANGES` (34) for the sole purpose of warning that a proposed camera AUX range
overlaps a configured FC mode.

`MSP_MODE_RANGES` is the **only** command required here, and it is the only one available on
both platforms: [BF] `msp/msp_protocol.h:106` and [INAV] `msp/msp_protocol.h:127`. It
carries AUX index and active range, which is all the advisory needs.

`MSP_MODE_RANGES_EXTRA` (238) is **Betaflight-only** ([BF] `msp/msp_protocol.h:263`); it does
not exist in INAV 8.0.1. Betaflight backends may additionally query it for richer diagnostic
context, but no advisory behaviour shall depend on it, or the feature silently degrades to
nothing on INAV. *(v0.6 listed both without platform qualification.)*

- These are read-only queries. No corresponding SET is ever issued (§3.1).
- The warning is **advisory and non-blocking**; the user may proceed.
- A common **intended** overlap is the ARM range: Arm Record is normally configured to the
  same AUX/range used by the pilot's arm switch.
- Other overlaps are reported because they may be accidental.
- A failed or unsupported query shall suppress the advisory, not block setup.

The advisory reports overlap; it does not attempt to interpret the current active mode or
the FC's armed state.

### 12.7 Optional INAV trigger — Logic Condition or Global Variable

**Optional and non-blocking. AUX ranges (§12.2) remain the required default on both
platforms.** A camera control may instead be bound to an INAV Logic Condition or Global
Variable, read through:

| Purpose | Command | Source |
| --- | --- | --- |
| Logic condition results | `MSP2_INAV_LOGIC_CONDITIONS_STATUS` (0x2026) | [INAV] `fc/fc_msp.c:570`; `msp_protocol_v2_inav.h:68` |
| Global variable values | `MSP2_INAV_GVAR_STATUS` (0x2027) | [INAV] `fc/fc_msp.c:575`; `msp_protocol_v2_inav.h:69` |

Both are read-only. They sit behind the same `USE_PROGRAMMING_FRAMEWORK` gate as the
custom OSD elements of §10.

**`MSP2_INAV_LOGIC_CONDITIONS_CONFIGURED` (0x203C) shall not be used.** v0.6 listed it as a
third query. It **does not exist in INAV 8.0.1** — it appears in neither
`msp/msp_protocol_v2_inav.h` nor `fc/fc_msp.c` at that tag, and is a later addition. A
bridge that requires it would fail against the minimum supported release. Where the bridge
needs to know which Logic Conditions are meaningful, it shall infer that from the status
reply and from the user's explicit selection during setup, not from a capability query that
the supported baseline cannot answer.

A user may deliberately encode FC state, **including armed state**, inside an INAV Logic
Condition. In that case the bridge still does not interpret arming semantics; it observes
only the user-selected Logic Condition result.

No equivalent FC-mode trigger is required on Betaflight. The bridge shall never repurpose a
functional mode as a generic camera trigger.

## 13. Camera Mode / Preset Control

*(V1.2 — see §26.)*

Where supported by the camera and positively advertised by the driver:

- photo/video mode may be assigned to a configured AUX control;
- up to three camera presets may be mapped to one AUX channel using three configured value
  ranges;
- switching shall be possible while armed.

## 14. GPS and Time Push

*(V1.2 — see §26.)*

Where the camera protocol allows it, the bridge may push:

- **UTC time**, for cameras that lose RTC state when depowered (notably naked/action cams).
  Source: `MSP_RTC` — Betaflight returns year/month/day/h/m/s/ms ([BF] `msp/msp.c:2179`);
  INAV returns seconds-since-epoch as U32 plus millis U16 ([INAV] `fc/fc_msp.c:1459`). The
  two formats differ; the backend abstracts this.
- **Live GPS position**, for cameras that accept it. DJI's official reference pushes GPS at
  10 Hz to tag footage, but does so over the R SDK protocol, which the Osmo Nano does not
  speak (§20.2). Whether the Nano accepts a GPS push over DUML is unknown and is not part
  of the §27.4 pass criteria; treat it as speculative until a driver exists.

Neither shall affect recording or flight operation when unavailable. GPS push rate counts
against the §21 MSP budget and shall not exceed the camera's documented rate.

## 15. Pairing

The bridge stores one active camera identity.

- physical pairing button;
- enters pairing mode without phone or app;
- discovers supported cameras, binds the selected one, remembers it across power cycles;
- reconnects automatically on subsequent boots;
- replaced only by an explicit pairing action.

**Broad BLE discovery is an explicit user action, not a recovery mechanism.**

Normal startup and runtime recovery shall perform a **directed reconnect** to the known
camera. If directed reconnect fails, the bridge backs off according to §21.3. It shall not
fall back to broad discovery automatically. Broad discovery is entered only after an
explicit Pair/Setup action, which per §16 means a **physical button press on the bridge**.

This rule is independent of FC arm state.

## 16. Setup Interface

Normal operation requires no setup UI. Advanced setup may use an ESP-hosted Wi-Fi page
exposing: recording control mode, AUX channel/range assignments, Push Button timing (§12.3),
OSD slot ownership (§9.1, §10.2), camera preset assignments, OSD layout, warnings,
clock/GPS sync, reconnect policy, and optional camera capabilities.

Setup shall surface the §12.6 overlap advisory when a range is assigned, and shall state the
§12.3 Push Button limitation where such a control is configured.

**Setup Mode entry requires a physical button press on the bridge.** It shall not be
enterable by any network-initiated action, by MSP, or by any remote request. This is the
mechanism by which "explicit user action" is guaranteed to mean *a person is physically at
the aircraft* — and it requires no knowledge of FC state, so it is consistent with §5.2.

Setup Mode is an **operating procedure**, not an FC-arm-state-controlled feature. The bridge
does not query actual FC armed state before entering Setup Mode. Documentation shall state
that Setup Mode is not to be entered while the aircraft is being operated.

### 16.1 Wi-Fi is a memory-safety constraint, not only a UX rule

The bridge shall explicitly arbitrate radio/software ownership between Wi-Fi setup and BLE
camera operation:

1. **Wi-Fi setup and BLE camera operation shall not be active simultaneously.** Entering
   Setup Mode deinitialises/suspends the BLE owner before bringing up the Wi-Fi AP; leaving
   Setup Mode tears Wi-Fi down before BLE camera operation resumes.
2. Setup Mode shall never be entered autonomously because of a camera or BLE failure.
3. Any heap headroom gate shall be **measured with the AP up and a client attached**. A
   free-heap figure logged before the AP driver is resident overstates the true floor.
   Figures measured on other boards or other firmware do not transfer to this build.

These requirements are resource-safety properties. They do not require or imply an
armed-state interlock.

## 17. Camera Telemetry Freshness

Every telemetry field tracks:

```
value, valid, lastUpdated
```

Values the camera does not report remain unavailable (`valid = false`). Estimated values
are never marked valid.

### 17.1 Thresholds

| Condition | Result |
| --- | --- |
| Status update received | Fields valid, `lastUpdated` refreshed |
| No status update for > 3 s | Live fields (timer, battery, storage) invalidated; state shows `CAM STALE` |
| No status update for > 10 s, or BLE disconnected | All camera fields invalid; state shows `CAM LOST` |

Stale recording state shall never remain displayed indefinitely as confirmed `REC`.

## 18. Warnings

Configurable warnings:

```
CAM LOST, CAM BAT LOW, CARD LOW, CARD FULL, REC STOPPED, CAM ERROR
```

Recording unexpectedly stopping **while the configured Record control remains active**
shall be treated as a high-priority camera warning.

The warning is based on camera-confirmed state versus camera-control intent; it does not
depend on actual FC armed state.

**`REC STOPPED` shall remain active during a §12.4 freeze.** v0.6 suppressed it there, on the
grounds that intent could not be evaluated. That was wrong, and self-contradictory: §12.4
freezes *the last accepted intent*, so intent is precisely what is known. The rule is:

```
effective control intent during a freeze = last accepted intent
```

and camera-confirmed state continues to be compared against it. RX loss prevents **new RC
transitions**; it does not erase what the pilot last asked for.

The scenario this protects is the one that matters most: last accepted intent is `RECORD`,
the RC link drops, and the camera then stops on its own for card-full, battery or fault.
Suppressing the warning would hide a camera failure during exactly the flight segment the
pilot most wants recorded.

The one thing that is genuinely unevaluable during a freeze is a *new* control edge, and
that is already handled by §12.4.1 and §12.3 — not by silencing a camera-health warning.

**`REC STOPPED` requires a camera that said so.** The warning compares camera-**confirmed**
state against intent, so it can only be raised while the camera is connected and reporting.
A camera that has gone away — powered off, out of range, BLE dropped — produces `CAM LOST`
(§17.1), and `CAM LOST` is not a stop:

```
camera connected, reports recording = false   →  REC STOPPED
camera gone, last known state was recording   →  CAM LOST      ← never REC STOPPED
```

This is not a fine distinction. Deriving `REC STOPPED` from a disconnect would have the OSD
assert a camera-confirmed event on the strength of the camera being unreachable, which is the
one thing §7 exists to prevent. The corresponding acceptance criteria are two separate tests
(§27.1, §24.2), and v0.7 conflated them into one that could not pass.

Separately, the `UNQUALIFIED` startup state of §12.4.3 and §12.4.4 raises **no** RC-loss
warning: there is no accepted intent yet to compare against, so no camera-health claim is
being suppressed.

## 19. Firmware Updates

Bridge firmware shall be field-updatable, to allow new camera models, camera BLE protocol
changes, new FC versions, and compatibility fixes.

Firmware update is an **explicit maintenance procedure**, reachable only through Setup Mode
and therefore only after a physical button press on the bridge (§16). Documentation shall
state that bridge, camera and FC firmware updates must not be performed while the aircraft
is being operated.

The bridge does not query FC armed state or implement an armed-state firmware-update
interlock. This is the same procedural boundary used for camera firmware maintenance: the
operator, not the bridge, is responsible for not initiating maintenance during flight. The
physical-button requirement is what keeps that boundary meaningful — without it, "explicit
user action" would be satisfiable remotely.

Camera support claims shall be tied to tested bridge firmware versions.

## 20. Camera Compatibility Policy

**Verified** — tested on physical camera hardware at a stated firmware version.
**Expected** — protocol-compatible model, not physically validated.
**Unsupported** — known incompatibility or missing protocol support.

Documentation and UI shall clearly distinguish verified from expected. A camera shall not be
promoted to verified based on protocol similarity alone.

### 20.1 GoPro

Open GoPro is GoPro's official, actively maintained programme (API version 2.0; repository
last updated 2026-06-08). BLE covers what §8 needs: start/stop capture, recording status,
battery, SD capacity, and resolution/frame-rate settings.

The supported-model list is an input to the compatibility matrix and shall be read per
release. Newest HERO models are not automatically covered.

### 20.2 DJI Osmo Nano

**Status: Unsupported, pending the §27.4 probe.** The Nano is the intended DJI-family
target, but unlike the Osmo Action line it has no official control protocol, and this
specification does not assume one will appear.

**Two protocol families**

**1. The official "R SDK" protocol** — what DJI's own Osmo Action GPS Bluetooth Remote
speaks, with an MIT-licensed ESP32 reference at `dji-sdk/Osmo-GPS-Controller-Demo`
(ESP-IDF v5.5; ships `sdkconfig.defaults.esp32c3`). Its device-ID table covers:

| Model | `device_id` |
| --- | --- |
| Osmo Action 4 | 0xFF33 |
| Osmo Action 5 Pro | 0xFF44 |
| Osmo Action 6 | 0xFF55 |
| Osmo 360 | 0xFF66 |
| **Osmo Nano** | *"Not supported yet. Please wait for a future firmware update."* |

That quotation is DJI's own text, from `docs/protocol_data_segment.md` in the reference
repository. It is corroborated independently: DJI's published accessory compatibility list
marks the Osmo Action GPS Bluetooth Remote Controller as **not compatible** with the Osmo
Nano. The reference repository has had no commits since 2025-11-17, so the promised
firmware update has been outstanding for the better part of a year and shall not be
planned around.

**2. DUML** — DJI's universal binary protocol, reverse-engineered by the community and
carried over the same GATT profile the official protocol uses: service `0xFFF0`,
notify `0xFFF4`, write `0xFFF5`. Published implementations exist for the Pocket 3
(`lib-osmo-ble`, updated 2026-02, full DUML framing with CRC8/CRC16) and for Action 3/4/5
plus Pocket 3 (`node-osmo`). Neither covers the Nano.

**Working hypothesis**

The Nano is *expected* to speak DUML, because DUML is DJI-wide and the Nano is
architecturally closer to a Pocket 3 than to an Action. This is an inference and has not
been tested. §27.4 tests it. Until that probe passes, the Nano is **Unsupported** under
this section's own policy and shall not be advertised as anything else.

Consequence for the product: the Nano gates V1.1 only. V1 ships on GoPro and does not
depend on any of this.

**Hazard — silent write failure**

Characteristic `fff3` accepts writes *with response* and returns success while the
firmware discards the payload. Only `fff5` (write-without-response) actually processes
DUML. A driver that writes `fff3` looks entirely correct and does nothing. This is
documented behaviour that already caught an existing library; the driver shall write only
`fff5`, and the probe shall confirm it.

**Licence**

The `LICENSE` file of DJI's reference repository is layered: the R SDK **protocol
documents** are under DJI's End User License Agreement, while the **demo software** is
offered under the EULA *and* the MIT License (© 2025 SZ DJI Technology Co., Ltd).

Because the Nano path is DUML rather than R SDK, DJI's MIT-licensed code is not the
basis of the Nano driver, and the EULA-encumbered protocol documents shall not be
redistributed. Should DJI later add the Nano to the official protocol, the official path
shall be preferred over DUML and this section revisited.

## 21. Performance and MSP Citizenship

### 21.1 FC polling

| Data | Rate | Command |
| --- | --- | --- |
| Configured AUX camera controls | 10 Hz, or 20 Hz when a Push Button control is enabled (§12.3) — only when an AUX control is enabled | `MSP_RC` |
| Betaflight RX-validity guard | 10/20 Hz, bracketing the `MSP_RC` poll per §12.4.1; plus one extra read on a transition-bearing sample | `MSP_STATUS` → `armingDisableFlags.ARMING_DISABLED_RX_FAILSAFE` ([BF] `msp/msp.c:1128`) |
| **Betaflight qualification grace** | **once per FC detection**, never in steady state | `MSP_FAILSAFE_CONFIG` → first byte = `failsafe_delay`, U8 tenths ([BF] `msp/msp.c:1657`); §12.4.3 condition B |
| **INAV startup qualification** | **until qualified only**, at the control poll rate; never polled again once qualified | `MSP2_INAV_STATUS` → `armingFlags` U32 ([INAV] `fc/fc_msp.c:459`); §12.4.4 |
| Mode ranges (overlap advisory) | setup only, never runtime control | `MSP_MODE_RANGES` (both platforms); `MSP_MODE_RANGES_EXTRA` Betaflight-only, diagnostic (§12.6) |
| Logic condition / GVAR trigger (INAV, if bound) | 10 Hz | `MSP2_INAV_LOGIC_CONDITIONS_STATUS` / `..._GVAR_STATUS` |
| Camera state | event driven | — |
| Record timer | 1 Hz | — |
| Camera battery | ≤ 1 Hz | — |
| Storage | ≤ 1 Hz | — |
| Resolution / FPS | on change | — |
| GPS (only if §14 push enabled) | ≤ 10 Hz | `MSP_RAW_GPS` |

**There is no runtime arm-state poll.** The two qualification reads above are startup-only by
construction: `MSP_FAILSAFE_CONFIG` is read once per FC detection, and the INAV `armingFlags`
read stops the instant the input qualifies (§12.4.4). Neither appears in the steady-state
budget below, and an implementation in which either is still being issued during flight has
misread §5.2.

Arm Record is an AUX control preset (§12.1), so it uses the same `MSP_RC` path as every
other level-controlled recording mode. If no AUX/Logic-Condition camera control is enabled,
the bridge need not poll RC/control state at all.

On Betaflight, `MSP_STATUS` is polled around `MSP_RC` **only** to obtain the RX-validity
guard of §12.4, and it **brackets** rather than accompanies the RC read — the two are not
one FC snapshot (§12.4.1). Steady-state cost is two transactions per poll; a third is issued
only when the RC sample would produce a camera-control transition. On INAV 8 no equivalent
status poll and no bracket are required, because `MSP_RC` retains last-good channel data
during RX loss.

Worst-case Betaflight budget: 3 transactions × 20 Hz = 60 MSP round-trips per second at
115200 baud, and only while a Push Button control is enabled and actively transitioning.
Steady state is 40. If a target cannot meet §21.2 at that rate, the Push Button poll rate is
reduced before the bracket is weakened — the bracket is a correctness requirement and the
20 Hz rate is a latency preference.

The bridge shall cache the last accepted RC sample. A missing or late reply shall not block
the MSP task and shall not synthesize a control transition; an incomplete bracket is treated
exactly as a failed bracket, i.e. the candidate is discarded, not committed. Normal
retry/backoff rules in §21.2 apply.

### 21.2 Quiet-bridge requirements

The bridge is a guest on the FC's MSP port and shall:

- never hold the port busy waiting on a camera or BLE operation;
- tolerate a missing or late reply without blocking;
- never retry-storm — failed requests back off;
- never write an OSD slot whose rendered content is unchanged;
- in telemetry-disabled compatibility mode (§5.1), issue no OSD writes; only the read-only
  MSP queries required by enabled camera-control features may continue.

### 21.3 BLE duty cycle and backoff

Derived from §3.3. Concrete values are set by the §27.2 measurement; the requirements are
**state-independent** and do not use FC arm state:

1. Runtime reconnect to the paired camera is **directed only** (§15).
2. **Reconnect backoff:** consecutive failures shall increase the retry interval to a
   configured ceiling of ≥ 5 s; there is never a continuous reconnect loop.
3. **Broad/discovery scanning is allowed only after an explicit Pair/Setup action** — which
   per §16 requires a physical button press. Runtime camera loss shall never escalate
   automatically into discovery scanning.
4. **Duty-cycle ceiling:** the bridge shall have a stated maximum BLE runtime reconnect /
   scan duty cycle and shall meet it in the reconnect-storm case as well as nominal use.
5. **BLE transmit power** is an explicit coexistence parameter, configurable and stated,
   and is one of the variables swept by §27.2. The release configuration shall use the
   lowest validated power that maintains reliable camera communication.

## 22. Failure Requirements

The following shall not affect aircraft control, and each has a corresponding acceptance
test in §24.2:

- camera power-off;
- camera reboot;
- camera absent at boot;
- BLE disconnect;
- repeated BLE reconnect failures;
- unsupported camera;
- malformed camera response;
- bridge reboot;
- bridge watchdog;
- bridge held in reset;
- bridge UART failure, including TX held at either rail;
- bridge Wi-Fi failure;
- configuration corruption;
- FC reboot while the bridge is powered;
- RC link loss and failsafe while the bridge is operating (§12.4);
- **bridge powered up while the FC has not yet validated RC data** — on Betaflight the ~1.5 s
  window in which the guard still reads clear, and on both platforms the mid-stick channel
  initialisation behind it (§12.4.3, §12.4.4 startup qualification);
- **bridge attached to an FC already running**, where no assert→clear edge is available to
  observe (§12.4.3 condition B);
- **`MSP_FAILSAFE_CONFIG` unreadable or unsupported**, removing condition B entirely
  (§12.4.3);
- **bridge rebooted while the aircraft is armed**, where INAV's qualification flag is no
  longer being maintained (§12.4.4);
- **a validity bracket that straddles an FC failsafe transition** (§12.4.1 guarded commit).

The FC shall continue operating normally with the bridge electrically connected but
completely non-functional. Per §3.3, "not affecting aircraft control" includes not
degrading control-link quality.

## 23. Installation Requirement

Target installation: four solder joints and one spare UART.

1. Connect 5 V, ground, TX and RX (observing the §4.2 peak-current note).
2. Enable MSP on the selected FC UART.
3. Enable and position the required OSD elements.
4. Configure camera controls as AUX channel/range mappings in bridge setup (§12). For
   **Arm Record**, select the same AUX channel/range normally used by the radio arm switch.
   Review the overlap advisory (§12.6); the ARM overlap is expected for this preset. If the
   aircraft is armed by stick rather than switch, configure Record on any other AUX channel
   (§12.1).
5. Power camera.
6. Press Pair.
7. Fly.

No CLI commands shall be required for the default supported configuration where the FC
configurator exposes all required settings.

**One documentation note for the installer:** saving FC configuration while the OSD is
showing live camera text persists that text into the FC's stored configuration, on both
platforms (§9, §10). The bridge clears its owned slots at every startup, so this only shows
up if the bridge is later absent or slow to start. Saving with the camera off avoids it
entirely.

## 24. Acceptance Matrix

A release shall not claim platform support until §24.1 and §24.2 pass.

### 24.1 Function

| Test | Betaflight 2025.12.5 | INAV 8.0.1 |
| --- | --- | --- |
| FC auto-detection | Required | Required |
| OSD capability probe / graceful degrade (§5.1) | Required | Required |
| No runtime dependence on actual FC armed state (§5.2) | Required | Required |
| Arm Record follows configured arm-switch AUX even when FC arming is deliberately blocked | Required | Required |
| MSP writes accepted while FC is armed (bench/in-flight compatibility test only; not an arm-state dependency) | Required | Required |
| No flash write across a long operating session | Required | Required |
| `MSP_RC` returns live AUX values while FC is armed | Required | Required |
| Direct AUX record control via `MSP_RC` (§12.2) | Required | Required |
| Push Button: 250 ms press detected; 100 ms press either detected or ignored, never double-fired (§12.3) | Required | Required |
| RX loss creates no camera-control transition (§12.4) | Required | Required |
| Betaflight `ARMING_DISABLED_RX_FAILSAFE` guard asserts during real RX loss and `BOXFAILSAFE` while FC is armed | Required | n/a |
| **Startup qualification: no camera-control transition of any kind occurs before qualification completes; the first qualified sample reconciles a level control and emits no edge (§12.4.3, §12.4.4)** | Required | Required |
| **Startup, no-receiver case: with the transmitter off, no spurious record occurs during the ~1.5 s window before the guard asserts, for a control range spanning `midrc`; qualification does not complete (§12.4.3)** | Required | n/a |
| **Betaflight qualification condition A: an observed assert→clear edge qualifies the input** | Required | n/a |
| **Betaflight qualification condition B: a continuously clear guard qualifies only after `max(failsafe_delay × 100 ms, 100 ms) + margin` from first MSP exchange, and the grace tracks a reconfigured `failsafe_delay` (§12.4.3)** | Required | n/a |
| **Betaflight fallback: with `MSP_FAILSAFE_CONFIG` denied, the bridge qualifies on condition A only and never on a timer (§12.4.3)** | Required | n/a |
| **INAV startup, no-receiver case: with the transmitter off, `ARMING_DISABLED_RC_LINK` reads asserted and no spurious record occurs for a control range spanning `midrc` (§12.4.4)** | n/a | Required |
| **INAV qualification stops reading `armingFlags` once qualified — no runtime arm-state poll (§5.2, §21.1)** | n/a | Required |
| **Guarded commit: a transition-bearing RC sample is committed only under a passing `STATUS₁ → RC → STATUS₂` bracket; a failed bracket discards the candidate (§12.4.1)** | Required | n/a |
| **Recovery latency: guard clear is awaited rather than timed; level control reconciles after both TX-off recovery and `BOXFAILSAFE` revert, whatever the two latencies are (§12.4.2)** | Required | n/a |
| **`Combi` arbitration matches the §12.1.1 truth table, including override clearing when the level control leaves its range** | Required | Required |
| **Range evaluator: ±10 µs hysteresis observed; sub-40 µs range rejected at setup (§12.2.1)** | Required | Required |
| INAV 8 `MSP_RC` retains last-good AUX values through RX loss | n/a | Required |
| No spurious Push Button event on Betaflight RX recovery re-baseline (§12.4) | Required | n/a |
| Overlap advisory reports a camera range overlapping an FC mode; ARM overlap remains allowable (§12.6) | Required | Required |
| **Setup Mode cannot be entered without a physical button press (§16)** | Required | Required |
| Camera control bound to a Logic Condition (§12.7) | n/a | Optional |
| Pilot's existing USER1..4 / PINIO / mode / Programming Framework configuration unmodified after a full session — verified by Configurator `diff` before/after | Required | Required |
| Actual `REC` confirmation (§7) | Required | Required |
| Recording timer OSD | Required | Required |
| Camera battery OSD | Required | Required |
| Remaining record time OSD | Required | Required |
| Resolution/FPS OSD | Required | Required |
| Staleness → `CAM STALE` → `CAM LOST` (§17.1) | Required | Required |
| Slot ownership respected (§9.1, §10.2) | Required | Required |
| **Owned OSD slots initialised to non-authoritative content at bridge startup, before camera connection is attempted — a persisted `REC nn:nn` from a prior session is cleared and never re-asserted (§9, §10)** | Required | Required |
| No FC firmware modification | Required | Required |
| Analog OSD | Required | Required |
| Digital OSD (MSP DisplayPort) | Required | Required |

### 24.2 Failure

Every row below has two pass criteria: **(a)** FC behaviour unchanged, and **(b)**
control-link sensitivity/link margin not measurably degraded versus the bridge-absent
baseline, where RF activity is relevant.

| Injected failure | Expected bridge behaviour |
| --- | --- |
| Bridge held in reset for the whole flight | FC unaffected |
| Bridge TX line held high, then held low | FC unaffected; document any UART electrical limitation found |
| Bridge power cycled while Record AUX remains active | bridge re-detects FC/camera; no false edge event; level control reconciles to active Record state |
| **Bridge powered up simultaneously with the FC, transmitter ON** | input stays `UNQUALIFIED` until condition A's assert→clear edge is observed; no RC-loss warning; first qualified sample reconciles level controls and emits no edge (§12.4.3) |
| **Bridge powered up simultaneously with the FC, transmitter OFF, Record range spanning `midrc`** | no recording starts during the ~1.5 s pre-assertion window; the guard then asserts and qualification **never completes**; a passing bracket over init values is not sufficient (§12.4.3) |
| **Bridge attached to an FC that has been running for minutes** | no assert→clear edge exists to observe; qualification completes via condition B after the grace, not before it (§12.4.3) |
| **Bridge start delayed past `failsafe_delay` with the transmitter ON** | edge missed; qualification via condition B; total delay ≈ `failsafe_delay + grace` and no camera-control transition before it (§12.4.3) |
| **`MSP_FAILSAFE_CONFIG` request denied or timed out** | condition B is disabled; the bridge qualifies only on an observed assert→clear edge, and never on elapsed time (§12.4.3) |
| **`failsafe_delay` reconfigured to 5 (0.5 s) and to 50 (5.0 s)** | condition B's grace tracks the configured value in both runs; no grace value is encoded in the bridge (§12.4.3) |
| **INAV: bridge and FC powered together, transmitter OFF, Record range spanning `midrc`** | `ARMING_DISABLED_RC_LINK` reads asserted from the first status reply; no recording starts; qualification does not complete (§12.4.4) |
| **INAV: bridge power-cycled while the aircraft is armed** | input stays `UNQUALIFIED` for the remainder of the flight; camera continues doing whatever it was doing; no transition is committed from a flag the FC has stopped maintaining (§12.4.4) |
| **Betaflight: RX loss induced so that the FC substitutes between the bridge's `STATUS₁` and `MSP_RC`** | the trailing `STATUS₂` fails the bracket; the substituted sample is discarded, not committed (§12.4.1) |
| Bridge watchdog forced | FC unaffected |
| FC rebooted while bridge is powered and recording | bridge re-detects FC and reinstates OSD/control polling without requiring an ARM/box remap |
| Betaflight: transmitter powered off with camera AUX configured `RX_FAILSAFE_MODE_HOLD` | recording continues |
| Betaflight: transmitter powered off with camera AUX configured `RX_FAILSAFE_MODE_SET` outside active range | recording still continues; §12.4 guard blocks the substituted transition |
| **Betaflight: transmitter powered back on after the above** | guard clears after `failsafe_recovery_delay` of valid data (stock 2025.12.5 default 500 ms) — **not immediately**; level control reconciles; no Push Button event emitted |
| Betaflight: `BOXFAILSAFE` actioned while recording | no camera-control transition |
| **Betaflight: `BOXFAILSAFE` reverted** | guard clears; where the FC took the switch-induced path the recovery preset is zero and the clear is immediate. Record the observed latency; the bridge shall behave correctly for **any** latency and shall not special-case this row |
| **Betaflight: `failsafe_recovery_delay` reconfigured to an extreme (0.1 s and 2.0 s)** | camera behaviour identical in both, only the reconcile latency differs — proves no delay value is encoded in the bridge (§12.4.2) |
| INAV 8: transmitter powered off while recording | `MSP_RC` remains at last-good camera AUX value; recording continues |
| Camera absent at boot | directed reconnect backs off; no autonomous broad discovery |
| Camera powered off mid-record | OSD transitions through stale/lost policy; reconnect backs off |
| Camera rebooted | reconnect/recovery without flight effect |
| Camera carried out of BLE range and back | reconnect/recovery without flight effect |
| Sustained runtime reconnect failures | bounded directed attempts; no discovery escalation; no measurable RF degradation |
| Unsupported camera advertised nearby | ignored outside explicit Pair/Setup discovery |
| Malformed/truncated camera response injected | rejected without bridge/FC instability |
| Corrupted stored configuration | safe defaults / recovery; no FC writes outside allowed set |
| Camera stops recording on its own (card filled) while the §12.4 guard is frozen, **BLE still connected** | `REC STOPPED` **is raised**, compared against the last accepted intent (§18) |
| **Camera powered off while the §12.4 guard is frozen and last accepted intent is `RECORD`** | `CAM LOST` per §17.1 — **not** `REC STOPPED`. A disconnect is not a camera-confirmed stop (§7, §18) |
| Wi-Fi stack forced to fail at Setup init | setup fails safely; normal FC operation unchanged |

The Betaflight `RX_FAILSAFE_MODE_SET` / `BOXFAILSAFE` rows are the direct tests of the
failsafe-substitution hazard in §12.4 and shall not be waived. The **recovery** rows exist
because the guard's clear latency differs between switch-induced and transmitter-loss
failsafe, and differs again with configuration; a tester who sees only one of them will
report the others as defects. The eight **startup** rows exist because v0.6 assumed a guard
state that Betaflight does not actually guarantee and v0.7 replaced it with a criterion the
boot window satisfies. The two transmitter-OFF rows are the ones that catch both errors, and
they are the rows to run first: a bridge that starts recording on a bench with no transmitter
has failed the only test in this document that a user will discover by accident.

The two `MSP_FAILSAFE_CONFIG` rows and the `failsafe_delay` sweep exist for the same reason as
the `failsafe_recovery_delay` sweep above — to prove that a configurable FC value is being
read rather than assumed. A bridge that passes at stock settings and fails at 5.0 s has
hardcoded something.

## 25. Architecture for Future ELRS Integration

The camera core shall not depend on MSP UART implementation details.

```
CameraBridge Core
 ├── CameraService     ├── CameraStatus    ├── GoProDriver
 ├── OsmoNanoDriver    ├── ControlPolicy   └── OsdFormatter

Platform adapters
 ├── StandaloneMspAdapter        (V1)
 └── future ElrsAdapter
```

The ELRS-integrated implementation is **not** in the initial standalone validation
envelope. Because ELRS is flight-critical, it requires a separate acceptance process
proving that enabling the camera subsystem cannot materially affect ELRS timing, RF
reception, LQ, packet loss, reconnect, watchdog behaviour, memory integrity, or receiver
stability.

Note that §3.3 applies doubly there: an integrated build shares not only the band but the
same MCU, scheduler and heap as the control link.

An integrated build would also have direct access to RC channel data and link validity in the
same address space and the same instant, removing the Betaflight `MSP_RC` failsafe ambiguity
at source — and with it the guarded-commit bracket (§12.4.1) and the recovery-window
semantics (§12.4.2). Both exist only because an arming-permission flag is being borrowed as an
input-validity signal and read through a transport that cannot deliver the two together. That
is a property of that architecture, not a reason to weaken §12.4 in the standalone product.
Neither requires actual FC armed-state knowledge.

**Startup qualification does not disappear, and v0.7 was wrong to list it with the other
two.** An integrated receiver still boots before its first valid RC frame, and the question
"has real control input ever arrived?" is not a transport artifact — it is a property of every
system that reads a control input. What disappears is only the *mechanism*: no bracket, no
`MSP_FAILSAFE_CONFIG` grace, no `armingFlags` read, because an integrated build knows
`valid RC frame arrived` directly and atomically. The semantics survive intact:

```
before the first valid sample:   UNQUALIFIED, no camera-control transitions
the first valid sample:          becomes the baseline; reconcile level controls; emit NO edge
subsequent samples:              normal processing
```

This is the correct place for the seam, and drawing it here makes the shared core smaller
rather than larger:

```
PlatformAdapter  →  ControlSample { values, valid, epoch }

ControlPolicy    →  first valid sample = baseline, never an edge
                    §12.1.1 Combi arbitration
                    §12.2.1 range evaluator + hysteresis
                    §12.3   Push Button timing
```

Standalone Betaflight, standalone INAV and an integrated ELRS build then differ in exactly one
thing: **how they produce `valid`**. Betaflight produces it from a bracketed guard plus the
§12.4.3 grace; INAV from `ARMING_DISABLED_RC_LINK` while disarmed (§12.4.4); an integrated
build from the receiver itself. Everything downstream of `ControlSample` is written once.

## 26. V1 Scope

### V1 — Required

- standalone ESP32-C3 bridge;
- Betaflight 2025.12.5+ and INAV 8.0.1+ backends (§0);
- GoPro BLE driver;
- automatic directed reconnect with state-independent backoff (§21.3);
- direct AUX-controlled recording (§12), including the **Arm Record** convenience preset
  that follows a configured arm-switch AUX rather than actual FC armed state;
- Betaflight failsafe-substitution guard with guarded commit, recovery and startup-
  qualification semantics, and INAV last-good-value validation (§12.4);
- `Combi` arbitration (§12.1.1) and the ±10 µs range evaluator (§12.2.1);
- Push Button timing (§12.3);
- actual camera-state feedback (§7) with staleness handling (§17);
- the four OSD fields of §8;
- OSD warnings (§18);
- slot ownership configuration (§9.1, §10.2);
- setup-time overlap advisory (§12.6);
- one-button pairing;
- physical-button-gated Setup Mode (§16);
- firmware updates with documented maintenance procedure (§19), with no FC-arm-state
  interlock required;
- optional Wi-Fi configuration through explicit Setup Mode (§16), never entered
  autonomously;
- §24.1 and §24.2 passing.

### V1.1 — conditional on the §27.4 probe

DJI Osmo Nano driver, over DUML (§20.2). Unlike a re-host of DJI's vendor reference,
this is unproven work: no official protocol support exists for this model and no
published DUML implementation covers it. The probe runs independently of V1 and may
return "not viable", in which case V1.1 either escalates to a Mimo-app BLE capture or
substitutes an officially supported model (Osmo Action 6 / Osmo 360), which would be a
low-risk re-host of the MIT-licensed reference.

### V1.2 — conditional by camera capability

Photo capture; preset switching (§13); HiLight/tag; camera clock and GPS push (§14);
additional telemetry; optional INAV Logic Condition trigger (§12.7) if not delivered in V1.

### Explicitly out of scope

- flight-controller firmware modification;
- VTX-specific integration;
- RC override, motor or servo control;
- required mobile app;
- interpreting or enforcing FC armed/disarmed state;
- ELRS-integrated production implementation.

## 27. Engineering Spikes Before Implementation

v0.1's single gate — whether stock INAV 8 can accept runtime camera data for its Custom OSD
Elements — is answered (§10). It is replaced by four spikes, each with pass criteria.

### 27.1 FC interface, control input and OSD injection on hardware

Confirm the source reading of §5, §9, §10 and §12 on real flight controllers.

Pass criteria:

1. `MSP_RC` returns live AUX values while armed on both platforms, matching the FC Receiver
   tab for configured AUX channels.
2. **Arm Record follows the configured arm-switch AUX/range even when actual FC arming is
   deliberately prevented** (for example by an unmet prearm condition). The camera-control
   result shall be identical whether the FC accepts or rejects arming.
3. Configured AUX thresholds and level changes produce the expected camera-control events
   without modifying FC mode, USER or PINIO configuration.
4. A 250 ms Push Button press is detected reliably; a 100 ms press is either detected or
   ignored but never double-fired (§12.3).
5. **Betaflight guard — assert.** `ARMING_DISABLED_RX_FAILSAFE` in `MSP_STATUS` asserts
   during real RX loss while armed and during `BOXFAILSAFE`-induced invalid data;
   `RX_FAILSAFE_MODE_SET` on the camera AUX produces no camera-control transition. *(The
   flag's validity while armed is established in source — `setArmingDisabled()` is an
   unconditional OR, [BF] `fc/runtime_config.c:73-76` — so this is a confirmation run, not
   an open question.)*
6. **Betaflight guard — clear.** Measure and record the clear latency after transmitter-loss
   recovery and after `BOXFAILSAFE` revert, then repeat with `failsafe_recovery_delay` set to
   1 (100 ms) and to 20 (2.0 s). Camera behaviour shall be identical across all four runs and
   only the reconcile latency shall change — which is the evidence that no delay value is
   encoded in the bridge (§12.4.2). Confirm level controls reconcile and no Push Button event
   is emitted by any re-baseline.
7. **Betaflight guard — startup qualification.** Six runs (§12.4.3). In every one, confirm no
   camera-control transition before qualification completes, no RC-loss warning for the
   unqualified state, and that the first qualified sample reconciles level controls without
   emitting an edge. Instrument the bridge to log *which condition* qualified it and when.
   **(a)** Bridge and FC powered together, transmitter **on** — expect condition **A**, an
   observed assert→clear edge.
   **(b)** The same with the transmitter **off** and a Record range deliberately configured to
   span `midrc`, held for at least 30 s. No recording shall start at any point, in particular
   not during the ~1.5 s before the guard asserts, and **qualification shall never complete**.
   A run that reports QUALIFIED here is the v0.7 defect reproduced.
   **(c)** Bridge attached to an FC running for several minutes — expect condition **B**,
   qualifying after the grace and not before it.
   **(d)** Bridge start deliberately delayed past `failsafe_delay` with the transmitter on —
   expect the edge to be missed and condition **B** to qualify, total delay ≈
   `failsafe_delay + grace`.
   **(e)** `failsafe_delay` reconfigured to 5 and to 50, repeating (c). The observed grace
   shall track the configured value; if it does not, a constant has been compiled in.
   **(f)** `MSP_FAILSAFE_CONFIG` denied at the FC or blocked at the bridge, repeating (a) and
   (c). (a) shall still qualify via condition A; (c) shall **not** qualify at all.
8. **INAV startup qualification** (§12.4.4). Three runs:
   **(a)** bridge and FC powered together, transmitter **off**, Record range spanning
   `midrc` — confirm `ARMING_DISABLED_RC_LINK` (bit 18) reads asserted in the first
   `MSP2_INAV_STATUS` reply, that no recording starts, and that qualification does not
   complete;
   **(b)** transmitter switched on — confirm the flag clears, the input qualifies, level
   controls reconcile and no edge is emitted;
   **(c)** with the aircraft **armed**, confirm by MSP capture that the bridge issues no
   further `armingFlags` read once qualified. Separately, power-cycle the bridge while armed
   and confirm it remains `UNQUALIFIED` until disarm, committing no camera-control transition
   in between (§12.4.4 known consequence).
9. **Betaflight guarded commit.** Induce RX loss repeatedly while polling, with
   `RX_FAILSAFE_MODE_SET` configured outside the active range, and confirm no committed
   camera-control transition arises from a bracket that straddles the FC's substitution
   (§12.4.1). Instrument the bridge to count discarded candidates; a run that discards **zero**
   over many induced losses indicates the bracket is not actually being applied, and shall be
   treated as a failed test rather than a clean one.
10. **`Combi` arbitration.** Walk the §12.1.1 truth table on hardware, including that a
    button-stop survives repeated level-control evaluations, that leaving the level range
    clears the override, and that the override survives an RX-loss freeze unchanged.
11. **Range evaluator.** Confirm ±10 µs hysteresis at a boundary using a slowly swept
    channel — the control shall not chatter — and that setup rejects a 30 µs range and a pair
    of ranges 15 µs apart (§12.2.1).
12. **INAV 8:** RX loss leaves the camera AUX returned by `MSP_RC` at its last-good value,
    producing no camera-control transition without an additional validity flag and without a
    bracket. Confirm there is no INAV recovery window — recovery is immediate and continuous.
13. **`REC STOPPED` during a freeze.** With the guard frozen and last accepted intent
    `RECORD`, stop the camera **while BLE remains connected** — fill the card, press the
    camera's own stop button, or inject a fault that reports `recording = false` — and confirm
    the warning **is** raised against the frozen intent (§18).
14. **Camera power-off during a freeze — a separate test, not a variant of the one above.**
    Under the same conditions, power the camera down. Expected result is `CAM LOST` per §17.1
    and **no** `REC STOPPED`. A build that reports `REC STOPPED` here has synthesized a
    camera-confirmed event from a disconnect and fails §7, however sensible the OSD looks.
    *(v0.7 listed "power it down" as a way to pass criterion 13, which no conforming
    implementation could do.)*
15. **Persisted OSD content.** With owned slots holding live camera text, save FC
    configuration from the Configurator and power-cycle with the bridge disconnected: confirm
    the stale text is present, which is the exposure (§9, §10). Reconnect the bridge and
    confirm every owned slot is overwritten with non-authoritative content **before** any
    camera connection is attempted. Run on both platforms — Betaflight's `pilotConfig` is a
    parameter group like INAV's and is not the safer case.
16. Custom message / custom element writes are accepted and rendered while armed on both
    Betaflight and INAV. This validates in-flight availability of the OSD path; it does not
    create an armed-state dependency.
17. Correct rendering on analog and on MSP DisplayPort.
18. Four fields independently updateable.
19. No flash write observed across a long operating session (verify by configuration
    persistence check across power cycle, not by inference).
20. Graceful degrade confirmed on an INAV target built without `USE_PROGRAMMING_FRAMEWORK`,
    and on a Betaflight build **below the 2025.12.5 minimum** (§0) — including a 2025.12.x
    patch release earlier than .5, since those are outside the support contract and shall
    degrade rather than half-work.
21. **Baseline conformance.** Run the full set against stock **Betaflight 2025.12.5** and
    **INAV 8.0.1** specifically (§0). A pass on a newer build is evidence about that build,
    not about the supported minimum — and §0's three platform-availability errors are exactly
    the class of defect a master-only test run would have missed. Forward-compatibility runs
    against later releases are recorded as version notes and never substituted for this one.

### 27.2 RF coexistence — the §3.3 release gate

Measure the bridge's effect on a 2.4 GHz ELRS control link.

**Two instruments, two jobs.** The existing passive RF survey and Blackbox analysis tooling
slices noise floor by condition and is the right tool for *detecting* interference and for
in-flight realism. It does **not** measure receiver sensitivity in dBm, and cannot on its own
support a sensitivity claim. The release gate therefore requires a calibrated bench
measurement in addition to the survey, not instead of it.

**Required method — calibrated link budget.**

```
ELRS TX  →  fixed-attenuator pad  →  step attenuator  →  ELRS RX + bridge
                                     (calibrated, known step size)
```

- Conducted where the hardware allows it; where an antenna port is not available, a fixed
  shielded-enclosure geometry with a documented and re-verified path loss.
- Sweep the step attenuator down through the link cliff, recording PER and LQ at each step.
- The measurement is the attenuation at which a **fixed PER threshold** is crossed — the
  criterion shall be stated as a PER figure (for example 1 % or 5 % packet loss at the
  configured packet rate), not as an LQ or RSSI reading. LQ and RSSI saturate and report "no
  change" right up to the cliff, which is precisely the failure this gate exists to catch.
- Repeat the sweep ≥ 5 times per condition and report the mean and spread.

Conditions, all else held constant:

```
bridge absent → bridge idle (paired, connected)
              → bridge active (recording, telemetry flowing)
              → bridge reconnecting (sustained directed reconnect failures, camera off)
```

swept against configured BLE transmit power (§21.3), and against ELRS packet rate if more
than one is supported.

**Defining "no measurable degradation".** The phrase is meaningless without the rig's own
resolution, so it is bound to it:

```
degradation = Δ (attenuation at fixed PER) between condition and bridge-absent baseline

PASS  if  |Δ| ≤ max(attenuator step size, 2σ of the baseline repeats)
```

Both quantities shall be reported with the result. A rig whose uncertainty is 3 dB cannot
demonstrate a 1 dB claim, and shall not be used to make one — if the measured uncertainty
exceeds the degradation the gate needs to exclude, the correct outcome is to improve the rig,
not to widen the criterion.

Pass criteria:

1. **No measurable loss of receiver sensitivity or link margin** as defined above, in the
   idle and active conditions;
2. sustained directed reconnect failures remain bounded by §21.3 backoff/duty-cycle limits,
   never escalate into broad discovery, and meet the same criterion;
3. the in-flight passive survey shows no new noise-floor feature attributable to the bridge
   across the same conditions — this is the realism cross-check on the bench result, and a
   disagreement between the two is itself a finding;
4. the resulting numbers written back into §21.3 as concrete configured values, together with
   the attenuator step size and baseline spread that qualify them.

Failing this gate does not merely delay a feature — it invalidates §3's claim that the
device is a passenger.

### 27.3 Camera protocol re-host — GoPro, V1

Port Open GoPro onto NimBLE-Arduino on ESP32-C3, behind the §6 driver interface.

Pass criteria:

1. start/stop recording and full §8 telemetry from a physical camera;
2. driver capability flags correctly gate unsupported features;
3. combined firmware fits flash and meets the §16.1 heap gate measured with the AP up and a
   client attached.

### 27.4 Osmo Nano protocol probe — gates V1.1 only

Independent of V1 and of §27.1–27.3; may run immediately, and hardware is on hand. The
purpose is to answer one question cheaply — *does the Osmo Nano answer DUML at all?* —
before any driver is written.

1. **Advertisement.** Scan for the Nano and dump its manufacturer-specific data. DJI's
   reference identifies one of its cameras when manufacturer bytes 0, 1 and 4 are `0xAA`,
   `0x08`, `0xFA` (see `bsp_link_is_dji_camera_adv` in the reference's `ble.c`). Record
   whether the Nano matches that signature.
2. **GATT enumeration.** Connect and enumerate services. Confirm service `0xFFF0` with
   `0xFFF4` (notify) and `0xFFF5` (write-without-response).
3. **DUML liveness.** Subscribe to `fff4`; send a known-safe DUML query from the Pocket 3
   corpus over `fff5` — never `fff3`, per the §20.2 hazard — and check for a
   CRC-valid response.
4. **Control.** Attempt start/stop recording and a camera-status subscription.
5. **Official cross-check.** Send the R SDK connection-request frame and record the reply.
   A rejection carrying a `device_id` would both reveal the Nano's ID and indicate official
   support is nearer than the documentation suggests.

Pass criteria and branches:

| Outcome | Consequence |
| --- | --- |
| Steps 1–3 yield a CRC-valid response | DUML driver viable; Nano proceeds to V1.1 as **Expected** (§20) |
| Step 4 works on physical hardware | Nano promoted to **Verified** (§20) |
| Fails at step 2 or 3 | Nano is **Unsupported**; escalate to a DJI Mimo app BLE capture, or substitute Osmo Action 6 / Osmo 360 per §26 |

Record the result — including a negative result — against the Nano's firmware version.
§20's policy forbids promoting the model on protocol similarity alone, and the whole
premise of the DUML path is a similarity argument.
