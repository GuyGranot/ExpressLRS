# Action Camera Bridge — Product Requirements Specification v0.6

**Status:** Draft
**Supersedes:** v0.5
**Target platforms:** Betaflight 2025.12+, INAV 8.x
**Initial camera families:** GoPro (V1), DJI Osmo Nano (V1.1, gated on §27.4)
**Architecture:** Standalone BLE-to-FC bridge over one spare UART
**Future architecture:** Optional ELRS-integrated implementation sharing the camera/control/OSD core
**Distribution:** Personal / open-source

---

## What changed from v0.5

v0.5's architectural decision — that the bridge observes configured RC intent and never
interprets FC armed state — is **retained unchanged**. It is correct, and both source claims
that justify it have now been verified (§12.4).

v0.6 fixes what v0.5 got wrong about the *edges* of the Betaflight guard it adopted.
`ARMING_DISABLED_RX_FAILSAFE` is an arming-permission flag being borrowed as an
input-validity flag. It works — that is now proven in source — but its assert and clear
behaviour is shaped for arming, not for camera control, and v0.5 treated it as a clean
boolean.

| Change | Rationale | Section |
| --- | --- | --- |
| Betaflight guard given explicit **recovery semantics**: it clears only after `failsafe_recovery_delay` (default 1.0 s) of valid data, and immediately when `BOXFAILSAFE` is reverted | The flag is asymmetric in time; unspecified, the lag reads as a bug and the two §24.2 failure rows appear to disagree | §12.4, §24.2, §27.1 |
| **Startup state specified**: the flag is set at FC initialisation with nothing wrong; the first observed clear is a re-baseline and raises no warning | A bridge powering up with the FC otherwise either waits forever or fires a spurious event on first clear | §12.4, §27.1 |
| Push Button events **cannot survive a recovery window**; level control is now the specified default for Record, with the reason stated | The re-baseline consumes the edge. Level controls reconcile; edge controls lose the event | §12.1, §12.3, §12.4 |
| Setup Mode entry restricted to a **physical button press**; never network-initiated | v0.5 said "explicit user action" without saying how. Requires no FC state, so it is consistent with §5.2 | §15, §16, §19 |
| Stick-armed setups documented as having **no Arm Record preset** | INAV still supports yaw-stick arming; those pilots have no arm-switch AUX to map | §12.1, §23 |
| **Six source citations corrected** | Every citation newly introduced in v0.5 was wrong; one pointed at the opposite function from the one the prose described | §12.2, §12.4, §21.1 |

### Citation corrections applied

| v0.5 cited | Corrected to | For |
| --- | --- | --- |
| [BF] `msp/msp.c:1276-1280` | `msp/msp.c:1396-1400` | `MSP_RC` serialization |
| [BF] `msp/msp.c:1096-1101` | `msp/msp.c:1200-1203` | `armingDisableFlags` in `MSP_STATUS` |
| [BF] `rx/rx.c:119` | `rx/rx.c:128` | `rcData` float declaration |
| [BF] `rx/rx.c:723-760` | `rx/rx.c:676-710`, `781,787,801`, `819` | failsafe substitution |
| [BF] `flight/failsafe.c:192-205` | `flight/failsafe.c:208-222` (set at `:214`) | `failsafeOnValidDataFailed` |
| [INAV 8.0.1] `rx/rx.c:469-475` | `rx/rx.c:489` and `:500-506` | last-good retention |

**Source-verification note.** Every MSP command, constant, limit and behavioural claim in
this document was **read from source on 2026-08-25**: Betaflight `master`, and INAV pinned
to tag **8.0.1** where RX/failsafe behaviour is version-sensitive. Citations marked **[BF]**
are `betaflight/betaflight`, **[INAV]** are `iNavFlight/inav`.

Line numbers are the part of this document most likely to rot, and the part most likely to
be *regenerated from memory rather than read*. Every citation edited between revisions of
this specification has so far been wrong, while every citation carried forward verbatim has
been right. Re-verification before each release means opening the file, not recalling it.

---

## 1. Product Goal

Provide reliable in-flight control and telemetry for supported action cameras without
modifying flight-controller firmware or inserting the device into any flight-critical
control path.

The bridge shall:

- control supported cameras over Bluetooth;
- read the camera's actual reported state;
- display camera state and telemetry through the flight controller's native OSD;
- support Betaflight 2025.12+ and INAV 8.x;
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
draws is **write versus read**, not subsystem.

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
  ([INAV] `fc/fc_msp.c:1826-1832`). A missing or error reply means the target was built
  without `USE_PROGRAMMING_FRAMEWORK` and has no custom elements at all — this is the
  correct trigger for telemetry-disabled compatibility mode. `USE_PROGRAMMING_FRAMEWORK`
  is enabled by default ([INAV] `src/main/target/common.h:173`) but small flash targets
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

Accordingly, no `MSP_BOXIDS` ARM lookup and no INAV `armingFlags` ARM-bit interpretation are
part of the V1 runtime contract.

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
`src/main/io/osd/custom_elements.c:217`), the bridge shall compose all OSD strings in
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

**Minimum version:** Betaflight 2025.12. The custom-message feature merged 2024-12-22 to
that milestone.

Transport:

```
MSP2_SET_TEXT (0x3007) → subtype MSP2TEXT_CUSTOM_MSG_0..+3 → pilotConfig.message[0..3] → OSD
```

Verified constants:

| Item | Value | Source |
| --- | --- | --- |
| Command | `MSP2_SET_TEXT = 0x3007` | [BF] `msp/msp_protocol_v2_betaflight.h:28` |
| Subtype base | `MSP2TEXT_CUSTOM_MSG_0 = 7` (so 7, 8, 9, 10) | [BF] `msp_protocol_v2_betaflight.h:52` |
| Slot count | `OSD_CUSTOM_MSG_COUNT = 4` | [BF] `pg/pilot.h:27` |
| Chars per slot | `MAX_NAME_LENGTH = 16` | [BF] `pg/pilot.h:26,32` |
| Handler | `mspProcessInCommand`, `case MSP2_SET_TEXT` | [BF] `msp/msp.c:4386,4419` |

Payload: `[subtype u8][length u8][chars...]`.

**Armed behaviour:** the handler carries no `ARMING_FLAG(ARMED)` guard; the armed checks in
`msp.c` apply to unrelated commands. Writes are accepted while armed. (Confirm on hardware
per §27.1 before relying on this.)

**Persistence:** writes land in the `pilotConfig` parameter group in RAM. No flash write
occurs unless something separately issues `MSP_EEPROM_WRITE`, which the bridge shall never
send (§3.1).

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

**Minimum version:** INAV 8.x. INAV support is a release-blocking requirement, not an
optional compatibility mode.

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
| Command | `MSP2_INAV_SET_CUSTOM_OSD_ELEMENTS = 0x2102` | [INAV] `msp/msp_protocol_v2_inav.h:117` |
| Capability query | `MSP2_INAV_CUSTOM_OSD_ELEMENTS = 0x2100` | [INAV] `msp_protocol_v2_inav.h:115` |
| Element count | `MAX_CUSTOM_ELEMENTS = 8` | [INAV] `io/osd/custom_elements.h:24` |
| Parts per element | `CUSTOM_ELEMENTS_PARTS = 3` | [INAV] `custom_elements.h:23` |
| Text size | `OSD_CUSTOM_ELEMENT_TEXT_SIZE = 16` (15 usable + NUL) | [INAV] `custom_elements.h:22` |
| Handler | `mspFcProcessInCommand` | [INAV] `fc/fc_msp.c:3783` |
| Build gate | `USE_PROGRAMMING_FRAMEWORK` (default on) | [INAV] `target/common.h:173` |

### 10.1 Payload format

Exactly **28 bytes**. The handler enforces
`dataSize == (OSD_CUSTOM_ELEMENT_TEXT_SIZE - 1) + (CUSTOM_ELEMENTS_PARTS * 3) + 4`
([INAV] `fc_msp.c:3785`) — short writes are rejected with `MSP_RESULT_ERROR`.

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

(Type and visibility enums: [INAV] `custom_elements.h:26-63`.)

**Whole-element rewrite.** Every SET replaces parts, visibility and text together. The
bridge shall cache complete element state and resend it intact on each update; it cannot
patch one field.

**Armed behaviour:** `mspFcProcessCommand` gates only `MSP_REBOOT` on `ARMING_FLAG(ARMED)`
([INAV] `fc_msp.c:4715`). The setter is reachable while armed. (Confirm on hardware per
§27.1.)

**Persistence:** the handler writes `osdCustomElementsMutable(n)` — the parameter group in
RAM. No flash write. Note the corollary: if the user later saves configuration from the
Configurator, whatever the bridge last wrote becomes persisted. This is harmless but shall
be documented.

**Rendering:** text is upper-cased (`sl_toupper`, [INAV] `custom_elements.c:217`).

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
4. **Combi** — recording normally follows a configured level control (often the arm-switch
   AUX), while a configured button may stop/restart or perform another supported action.

In all modes, displayed state follows camera acknowledgement (§7), never the input.

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
| Betaflight | `rcData[0 .. rxRuntimeState.channelCount-1]` | [BF] `msp/msp.c:1396-1400` |
| INAV | `rxGetChannelValue(0 .. rxRuntimeConfig.channelCount-1)` | [INAV] `fc/fc_msp.c:673-677` |

The **response length**, not a hard-coded channel count, determines how many channels are
available.

The first four channels are the primary flight controls; subsequent channels are addressed
to the user as AUX1, AUX2, … . **The bridge shall accept only AUX channels as camera-control
inputs; channels 1–4 are out of scope for control mapping.** This avoids platform-specific
AETR/AERT primary-channel ordering entirely.

**Numeric note.** Betaflight's `rcData` is `float` ([BF] `rx/rx.c:128`) serialized through
`sbufWriteU16`, i.e. truncated. Bridge range comparisons may therefore differ by 1 µs from
the FC's own arithmetic at an exact boundary. Explicit ranges shall include reasonable
hysteresis/deadband and shall not place operational meaning on a single microsecond
boundary.

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
  On Betaflight this includes the recovery window of §12.4, which may last up to
  `failsafe_recovery_delay` (default 1.0 s) after valid RC data returns. Setup documentation
  shall state this limitation where a Push Button control is configured.

### 12.4 RC-link loss and failsafe behaviour

The two supported FC families do not expose identical `MSP_RC` semantics during RX
loss, so the platform adapters shall handle them differently. The common semantic
requirement is **RX loss shall not create a camera-control transition**; the implementation
mechanism need not be identical.

#### Betaflight

Betaflight `MSP_RC` serializes `rcData[]`. `rcData` is explicitly the *scaled, modified,
checked and constrained* channel value ([BF] `rx/rx.c:128`), and the RX failsafe path can
replace an AUX channel with its configured HOLD/SET failsafe value before `MSP_RC` is
generated — `getRxfailValue()` at [BF] `rx/rx.c:676-710`, applied at `:781`, `:787`, `:801`
and written to `rcData` at `:819`. AUX channels default to `RX_FAILSAFE_MODE_HOLD`
([BF] `rx/rx.c:158`), which is benign; `RX_FAILSAFE_MODE_SET` is the hazard.

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
to `MSP_STATUS` ([BF] `msp/msp.c:1200-1203`). `failsafeOnValidDataFailed()` sets this flag
unconditionally when RX data fails, including `BOXFAILSAFE`-induced loss ([BF]
`flight/failsafe.c:208-222`, set at `:214`), and `setArmingDisabled()` is an unconditional
bitwise OR with no armed-state check ([BF] `fc/runtime_config.c:74-77`). **The guard is
therefore valid while armed**, and its use does not constitute FC armed-state decoding.

##### Guard edge semantics — required

`ARMING_DISABLED_RX_FAILSAFE` is an *arming-permission* flag borrowed as an *input-validity*
flag. It works, but it is asymmetric in time and it is asserted at initialisation. Both
must be handled explicitly.

**Assert is immediate. Clear is delayed.** The flag clears only after
`receivingRxDataPeriodPreset` of continuously valid data ([BF] `flight/failsafe.c:198-205`),
which is `rxDataRecoveryPeriod` — `failsafe_recovery_delay`, **default 1.0 s**, minimum
100 ms ([BF] `flight/failsafe.c:110-113,120`). The one exception is `BOXFAILSAFE`: when the
switch is reverted the preset is set to zero and the flag clears immediately ([BF]
`flight/failsafe.c:183`).

Consequences the implementation shall honour:

1. After real RX loss, the guard holds camera state for up to ~1 s **past** the return of
   valid pilot input. This is deliberate and conservative; it is not a defect.
2. Recovery from a `BOXFAILSAFE`-induced freeze is effectively immediate, while recovery
   from transmitter loss takes the recovery delay. The two therefore produce visibly
   different latencies (§24.2).
3. Level controls reconcile to the correct camera state once the guard clears. Edge
   controls do not — see §12.3.

**The flag is set at FC initialisation with nothing wrong.** Betaflight asserts it after
init until valid data has flowed for the recovery period, to show RXLOSS and block arming
([BF] `flight/failsafe.c:188-195`). A bridge powering up alongside the FC will therefore read
"RC invalid" on its very first `MSP_STATUS`. Requirements:

- the initial assertion shall be treated as **normal, not a fault**;
- the bridge shall not raise an RC-loss warning for the startup assertion;
- the **first observed clear shall be a re-baseline**, not an edge: the first valid RC
  sample establishes current position without emitting a Push Button event, after which
  level controls reconcile camera state to the observed level.

The same re-baseline rule applies to every subsequent recovery.

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

**No separate INAV RC-validity gate is required for V1.** The hardware test shall confirm
that the observed `MSP_RC` value remains at the last-good level through RX loss (§27.1).

Because INAV never freezes *transitions* — it freezes *values* — there is no INAV recovery
window and no INAV equivalent of the swallowed-Push-Button case above. Recovery is
immediate and continuous.

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
`MSP_MODE_RANGES` (34) and `MSP_MODE_RANGES_EXTRA` (238) for the sole purpose of warning
that a proposed camera AUX range overlaps a configured FC mode.

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
| Logic condition results | `MSP2_INAV_LOGIC_CONDITIONS_STATUS` (0x2026) | [INAV] `fc/fc_msp.c:599-603` |
| Global variable values | `MSP2_INAV_GVAR_STATUS` (0x2027) | [INAV] `fc/fc_msp.c:627-631` |
| Which LCs are configured | `MSP2_INAV_LOGIC_CONDITIONS_CONFIGURED` (0x203C) | [INAV] `fc/fc_msp.c:604` |

All three are read-only. They sit behind the same `USE_PROGRAMMING_FRAMEWORK` gate as the
custom OSD elements of §10.

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
  Source: `MSP_RTC` — Betaflight returns year/month/day/h/m/s/ms ([BF] `msp.c:2277`);
  INAV returns seconds-since-epoch as U32 plus millis U16 ([INAV] `fc_msp.c:1521`). The
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
depend on actual FC armed state. It shall be suppressed while camera-control transitions are
frozen by the §12.4 guard, since intent cannot be evaluated in that window.

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
| Betaflight RX-validity guard | 10/20 Hz, paired with the `MSP_RC` poll when an AUX control is enabled | `MSP_STATUS` → `armingDisableFlags.ARMING_DISABLED_RX_FAILSAFE` ([BF] `msp/msp.c:1200-1203`) |
| Mode ranges (overlap advisory) | setup only, never runtime control | `MSP_MODE_RANGES`, `MSP_MODE_RANGES_EXTRA` |
| Logic condition / GVAR trigger (INAV, if bound) | 10 Hz | `MSP2_INAV_LOGIC_CONDITIONS_STATUS` / `..._GVAR_STATUS` |
| Camera state | event driven | — |
| Record timer | 1 Hz | — |
| Camera battery | ≤ 1 Hz | — |
| Storage | ≤ 1 Hz | — |
| Resolution / FPS | on change | — |
| GPS (only if §14 push enabled) | ≤ 10 Hz | `MSP_RAW_GPS` |

**There is no runtime arm-state poll.**

Arm Record is an AUX control preset (§12.1), so it uses the same `MSP_RC` path as every
other level-controlled recording mode. If no AUX/Logic-Condition camera control is enabled,
the bridge need not poll RC/control state at all.

On Betaflight, `MSP_STATUS` is polled alongside `MSP_RC` **only** to obtain the RX-validity
guard of §12.4. On INAV 8 no equivalent status poll is required for AUX failsafe handling
because `MSP_RC` retains last-good channel data during RX loss.

The bridge shall cache the last accepted RC sample. A missing or late reply shall not block
the MSP task and shall not synthesize a control transition; normal retry/backoff rules in
§21.2 apply.

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
- **bridge powered up while the FC has not yet validated RC data** (§12.4 startup
  assertion).

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

## 24. Acceptance Matrix

A release shall not claim platform support until §24.1 and §24.2 pass.

### 24.1 Function

| Test | Betaflight 2025.12+ | INAV 8.x |
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
| **Startup: guard asserted at FC boot is not reported as a fault; first clear re-baselines without emitting an event (§12.4)** | Required | n/a |
| **Recovery latency: guard clears ~`failsafe_recovery_delay` after TX-off recovery, and immediately on `BOXFAILSAFE` revert; level control reconciles in both cases (§12.4)** | Required | n/a |
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
| **Bridge powered up simultaneously with the FC** | guard reads asserted initially; no RC-loss warning raised; first clear re-baselines without emitting an event (§12.4) |
| Bridge watchdog forced | FC unaffected |
| FC rebooted while bridge is powered and recording | bridge re-detects FC and reinstates OSD/control polling without requiring an ARM/box remap |
| Betaflight: transmitter powered off with camera AUX configured `RX_FAILSAFE_MODE_HOLD` | recording continues |
| Betaflight: transmitter powered off with camera AUX configured `RX_FAILSAFE_MODE_SET` outside active range | recording still continues; §12.4 guard blocks the substituted transition |
| **Betaflight: transmitter powered back on after the above** | guard clears after ~`failsafe_recovery_delay` (default 1.0 s) of valid data — **not immediately**; level control reconciles; no Push Button event emitted |
| Betaflight: `BOXFAILSAFE` actioned while recording | no camera-control transition |
| **Betaflight: `BOXFAILSAFE` reverted** | guard clears **immediately** (recovery preset is zero); latency differs from the TX-off row above and both are correct |
| INAV 8: transmitter powered off while recording | `MSP_RC` remains at last-good camera AUX value; recording continues |
| Camera absent at boot | directed reconnect backs off; no autonomous broad discovery |
| Camera powered off mid-record | OSD transitions through stale/lost policy; reconnect backs off |
| Camera rebooted | reconnect/recovery without flight effect |
| Camera carried out of BLE range and back | reconnect/recovery without flight effect |
| Sustained runtime reconnect failures | bounded directed attempts; no discovery escalation; no measurable RF degradation |
| Unsupported camera advertised nearby | ignored outside explicit Pair/Setup discovery |
| Malformed/truncated camera response injected | rejected without bridge/FC instability |
| Corrupted stored configuration | safe defaults / recovery; no FC writes outside allowed set |
| Wi-Fi stack forced to fail at Setup init | setup fails safely; normal FC operation unchanged |

The Betaflight `RX_FAILSAFE_MODE_SET` / `BOXFAILSAFE` rows are the direct tests of the
failsafe-substitution hazard in §12.4 and shall not be waived. The two **recovery** rows
exist because the guard's clear latency differs between switch-induced and
transmitter-loss failsafe; a tester who sees only one of them will report the other as a
defect. The sustained reconnect row is the direct test of §3.3 and shall not be waived.

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

An integrated build would also have direct access to RC channel data and link validity,
removing the Betaflight `MSP_RC` failsafe ambiguity at source — and with it the recovery-window
and startup-assertion semantics of §12.4, which exist only because an arming-permission flag
is being borrowed as an input-validity signal. That is a property of that architecture, not
a reason to weaken §12.4 in the standalone product. It still does not require actual FC
armed-state knowledge.

## 26. V1 Scope

### V1 — Required

- standalone ESP32-C3 bridge;
- Betaflight 2025.12+ and INAV 8.x backends;
- GoPro BLE driver;
- automatic directed reconnect with state-independent backoff (§21.3);
- direct AUX-controlled recording (§12), including the **Arm Record** convenience preset
  that follows a configured arm-switch AUX rather than actual FC armed state;
- Betaflight failsafe-substitution guard with its startup and recovery semantics, and INAV
  last-good-value validation (§12.4);
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
   unconditional OR, [BF] `fc/runtime_config.c:74-77` — so this is a confirmation run, not
   an open question.)*
6. **Betaflight guard — clear.** Measure the clear latency in both directions: after
   transmitter-loss recovery it shall be approximately `failsafe_recovery_delay`
   (default 1.0 s); after `BOXFAILSAFE` revert it shall be effectively immediate. Confirm
   that level controls reconcile correctly in both cases and that no Push Button event is
   emitted by the re-baseline.
7. **Betaflight guard — startup.** With bridge and FC powered together, confirm the guard
   reads asserted before RC data has been valid for the recovery period, that the bridge
   raises no RC-loss warning for it, and that the first clear re-baselines silently.
8. **INAV 8:** RX loss leaves the camera AUX returned by `MSP_RC` at its last-good value,
   producing no camera-control transition without an additional validity flag. Confirm there
   is no INAV recovery window — recovery is immediate and continuous.
9. Custom message / custom element writes are accepted and rendered while armed on both
   Betaflight and INAV. This validates in-flight availability of the OSD path; it does not
   create an armed-state dependency.
10. Correct rendering on analog and on MSP DisplayPort.
11. Four fields independently updateable.
12. No flash write observed across a long operating session (verify by configuration
    persistence check across power cycle, not by inference).
13. Graceful degrade confirmed on an INAV target built without `USE_PROGRAMMING_FRAMEWORK`,
    and on a Betaflight build older than 2025.12.

### 27.2 RF coexistence — the §3.3 release gate

Measure the bridge's effect on a 2.4 GHz ELRS control link using the existing passive RF
survey and Blackbox analysis tooling, which already slices noise floor by condition.

Conditions, all else held constant:

```
bridge absent → bridge idle (paired, connected)
              → bridge active (recording, telemetry flowing)
              → sustained directed reconnect failures (camera powered off)
```

swept against configured BLE transmit power (§21.3).

Pass criteria:

1. **No measurable loss of receiver sensitivity or link margin** versus the bridge-absent
   baseline in the idle and active conditions. The metric is the Δ in ELRS input power
   required to hold a fixed PER/LQ threshold — LQ and RSSI alone saturate and will report
   "no change" right up to the cliff;
2. sustained directed reconnect failures remain bounded by §21.3 backoff/duty-cycle limits,
   never escalate into broad discovery, and show no measurable degradation;
3. the resulting numbers written back into §21.3 as concrete configured values.

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
