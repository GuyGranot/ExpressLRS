# Action Camera Bridge — Product Requirements Specification v0.4

**Status:** Draft
**Supersedes:** v0.3
**Target platforms:** Betaflight 2025.12+, INAV 8.x
**Initial camera families:** GoPro (V1), DJI Osmo Nano (V1.1, gated on §27.4)
**Architecture:** Standalone BLE-to-FC bridge over one spare UART
**Future architecture:** Optional ELRS-integrated implementation sharing the camera/control/OSD core
**Distribution:** Personal / open-source

---

## What changed from v0.3

v0.3 replaced the box-mode control path with direct AUX-range evaluation over `MSP_RC`.
That decision is retained — it is correct, and it sheds the PINIO gating problem, the mode
range mirroring and the drift risk that motivated the rewrite. v0.4 repairs the two things
the rewrite dropped along with box modes.

| Change | Section |
| --- | --- |
| `MSP_BOXIDS` restored, with a single narrow purpose: locating the ARM bit on Betaflight. Betaflight `MSP_STATUS` carries no armed field — v0.3's default control mode did not work as specified | §5.1, §5.2, §12.1 |
| RC-link validity gate added. `MSP_RC` reports failsafe-substituted values indistinguishable from pilot input; camera-control transitions are now gated on link validity | §12.4, §21.1, §24.1, §27.1 |
| Push Button given a minimum hold time and an inter-event interval; poll rate raised when a Push Button control is enabled. A 10 Hz poll aliases a momentary switch | §12.3, §21.1 |
| Stated that `MSP_RC` is not the receiver's output — it is post-mapping, post-failsafe, and on INAV post-override | §12.5 |
| Setup-time mode-range overlap warning restored as a read-only advisory. It was never in the control path | §12.6, §16, §23 |
| INAV Logic Condition / GVAR restored as an **optional, non-blocking** trigger. AUX remains the required default on both platforms | §12.7 |
| Programming Framework write prohibitions restored to §3.1 (`MSP2_INAV_SET_LOGIC_CONDITIONS`, `MSP2_INAV_SET_GVAR`) | §3.1 |
| Corrected two wrong line citations in v0.3 §12; restored the line number lost from §5.1; documented the Betaflight/INAV first-four channel-order asymmetry | §5.1, §12.2 |
| §24.1's "existing configuration unchanged" row given a test method; failure rows added for failsafe and RC loss | §24.1, §24.2 |

**Source-verification note.** Every MSP command, constant and limit in this document was
read from Betaflight and INAV `master` on **2026-08-25** and is cited by file and line.
These are upstream moving targets; re-verify before each release. Citations marked
**[BF]** are `betaflight/betaflight`, **[INAV]** are `iNavFlight/inav`.

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
5. Arming, mode switches, or buttons control recording according to configuration.
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

The bridge transmits Bluetooth Low Energy in the 2.4 GHz ISM band. On ESP32-C3 this uses a
single shared radio and, on ELRS-derived hardware, typically a single antenna. When the
aircraft's control link is 2.4 GHz ELRS, the bridge is an in-band interferer mounted
centimetres from the receiving antenna.

The failure mode is adverse: a bridge that is "non-functional" in the §22 sense — unable to
find or reconnect to its camera — is precisely the state in which it scans and advertises
most aggressively. **Degraded bridge operation must not become degraded link operation.**

Requirements:

1. The bridge shall bound its BLE transmit and scan duty cycle (§21.3).
2. Reconnect attempts shall back off rather than scan continuously (§21.3).
3. Discovery-class scanning shall be suppressed or heavily throttled while the aircraft is
   armed (§21.3).
4. The bridge shall be shipped only after its effect on control-link quality has been
   **measured**, not asserted (§24.2).

Requirement 4 is a release gate. Compliance is demonstrated by the §27.2 spike.

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
| Box-ID map — **Betaflight only**, one shot at startup (§5.2) | `MSP_BOXIDS` (119) — [BF] `msp/msp_protocol.h:187` |
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
  correct trigger for telemetry-disabled compatibility mode. `USE_PROGRAMMING_FRAMEWORK` is
  enabled by default ([INAV] `src/main/target/common.h:173`) but small flash targets
  may undefine it.
- **Betaflight:** custom messages require 2025.12 or later. Below that, enter
  telemetry-disabled compatibility mode.

Unsupported FC variants shall enter telemetry-disabled compatibility mode and shall not
send platform-specific MSP writes.

The bridge shall track: FC family, FC firmware version, MSP/API version, available OSD
capabilities, and — on Betaflight — the resolved ARM bit index (§5.2).

### 5.2 Arming state is not symmetric between platforms

v0.3 stated that arming state comes from `MSP_STATUS` / `MSP2_INAV_STATUS` and removed
`MSP_BOXIDS` from the specification. That works on INAV and does not work on Betaflight.

**INAV — direct.** `MSP2_INAV_STATUS` writes `armingFlags` as a raw U32
([INAV] `fc/fc_msp.c:513`) and `ARMED = (1 << 2)` is a fixed bit
([INAV] `fc/runtime_config.h:22`). No box mapping is required.

**Betaflight — indirect.** `MSP_STATUS` carries no armed field. The only arm indicator is
the `flightModeFlags` bitmap ([BF] `msp/msp.c:1169-1181`), and `packFlightModeFlags` packs
it by *active box index*, not by box ID:

```c
// Serialize the flags in the order we delivered them, ignoring BOXNAMES and BOXINDEXES
unsigned mspBoxIdx = 0;
for (boxId_e boxId = 0; boxId < CHECKBOX_ITEM_COUNT; boxId++) {
    if (activeBoxIdGet(boxId)) {
        if (getBoxIdState(boxId)) bitArraySet(mspFlightModeFlags, mspBoxIdx);
        mspBoxIdx++;
    }
}
```
[BF] `msp/msp_box.c:410-426`

Which bit is ARM therefore depends on which boxes that build and configuration activated.
`MSP_BOXIDS` resolves it: `serializeBoxReply` walks the identical `activeBoxIdGet` order
emitting permanent IDs ([BF] `msp/msp_box.c:182-198`, `:171-178`), and `BOXARM` is
permanent ID **0** ([BF] `msp/msp_box.c:49`).

Requirements:

- On Betaflight the bridge shall issue `MSP_BOXIDS` once at startup and record the bit
  index whose permanent ID is 0.
- This is the **only** use of `MSP_BOXIDS`. The bridge shall not read, interpret or depend
  on any other box, and no camera control is bound to a box mode (§12).
- If `MSP_BOXIDS` fails or contains no permanent ID 0, Arm Record shall be reported
  unavailable at setup rather than silently mis-triggering on another box's bit.
- The box-ID map shall be re-read after any detected FC reboot (§22, §24.2), because the
  active box set can change with configuration.

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

Manual camera controls are mapped directly to AUX channels and value ranges in the **bridge**
configuration. This control model is intentionally independent of FC mode configuration:

- `USER1..USER4`, PINIO, Camera Control modes, and other FC box modes are not required and
  are never bound to a camera control;
- existing FC mode/PINIO configuration is never modified, and is read only for the
  setup-time advisory of §12.6;
- the bridge shall never issue `MSP_SET_MODE_RANGE`, `MSP_SET_RAW_RC`,
  `MSP2_INAV_SET_AUX_RC`, RC override/overlay commands, or any equivalent command that
  changes FC RC state (§3.1);
- Betaflight and INAV share one bridge-side AUX/range evaluation implementation.

Each configured control consists of an AUX channel plus one or more non-overlapping value
ranges. The bridge evaluates the latest `MSP_RC` sample locally. Configuration may expose
common 2-state and 3-state presets, but the stored representation shall be **explicit
ranges** so unusual radio endpoints remain supported.

### 12.1 Modes

1. **Arm Record (default)** — ARM → start, DISARM → stop. Arming state is read per §5.2:
   `armingFlags` bit 2 on INAV, the `MSP_BOXIDS`-resolved ARM bit on Betaflight. No
   `MSP_RC` polling is required if this is the only enabled control mode.
2. **AUX Toggle** — one configured AUX range is active → record; outside that range → stop.
3. **Push Button** — entry into a configured AUX active range is a momentary event that
   toggles recording or triggers the configured action, subject to §12.3.
4. **Combi** — arm/disarm automation stays active while a configured AUX button can
   manually stop or restart recording during the flight.

In all modes, displayed state follows camera acknowledgement (§7), never the input.

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
to the user as AUX1, AUX2, … . AUX channels are never remapped on either platform
([INAV] `docs/Rx.md:67` — "channels above 8 are mapped straight, with no remapping"), so
the bridge shall not depend on transmitter-side channel order or CRSF framing.

**Channel-order asymmetry.** INAV documents that `MSP_RC` returns `AERT` regardless of
channel map ([INAV] `docs/Rx.md:208`). Betaflight returns its own `rcData` index order
(AETR). This is irrelevant to AUX channels and therefore to this specification, but the
bridge shall not read channels 1–4 for any purpose without re-deriving the order per
platform.

**Numeric note.** Betaflight's `rcData` is `float` ([BF] `rx/rx.c:128`) serialized through
`sbufWriteU16`, i.e. truncated. Bridge range comparisons may therefore differ by 1 µs from
the FC's own arithmetic at an exact boundary. This is immaterial for the explicit ranges of
§12, but any attempt to reproduce an FC mode range bit-exactly would be defeated by it.

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

### 12.4 RC-link validity gate

**`MSP_RC` does not report receiver output. It reports post-failsafe values, and the bridge
cannot distinguish them from pilot input.** Betaflight's `rcData` is declared "scaled,
**modified**, checked and constrained values" ([BF] `rx/rx.c:128`), and failsafe writes
directly into it:

```c
sample = getRxfailValue(channel);   // rx.c:781, 787, 801
...
rcData[channel] = sample;           // rx.c:819 — "either validated incoming values, or failsafe-modified values"
```

`getRxfailValue` ([BF] `rx/rx.c:676-710`) returns, per channel mode:

- `RX_FAILSAFE_MODE_HOLD` — last good value. This is the **AUX default**
  ([BF] `rx/rx.c:158`) and is benign: the camera switch freezes and recording continues.
- `RX_FAILSAFE_MODE_SET` — the pilot's configured failsafe step value.

The second case is the hazard. A pilot who has set a failsafe value on the channel carrying
a camera control gets: **RX loss → failsafe substitutes the value → the bridge reads a range
exit → recording stops**, at precisely the moment the footage matters most. The
`BOXFAILSAFE` switch applies stage-1 values immediately ([BF] `rx/rx.c:787`). INAV holds
equivalently ([INAV] `rx/rx.c:492`).

Requirement: **while the RC link is invalid, the bridge shall hold its last camera-control
state and shall not act on `MSP_RC` transitions.** Validity is read from the status reply
already being polled, as a raw U32 requiring no box mapping:

| Platform | Field | Bits |
| --- | --- | --- |
| Betaflight | `armingDisableFlags` U32 in `MSP_STATUS` ([BF] `msp/msp.c:1200-1203`) | `ARMING_DISABLED_RX_FAILSAFE (1<<2)`, `ARMING_DISABLED_BOXFAILSAFE (1<<4)` — [BF] `fc/runtime_config.h:45,47` |
| INAV | `armingFlags` U32 in `MSP2_INAV_STATUS` ([INAV] `fc/fc_msp.c:513`) | `ARMING_DISABLED_RC_LINK (1<<18)` — [INAV] `fc/runtime_config.h:38` |

These are primarily arming-permission flags. **Their behaviour while armed is not verified
and is a §27.1 pass criterion.** If the bench finds they are not maintained while armed, the
fallback is the `BOXFAILSAFE` box state in the flight-mode bitmap, which is unconditionally
exported on Betaflight — at the cost of reintroducing a second box-ID lookup.

On leaving the invalid state, the bridge shall re-baseline: the first valid sample
establishes the current position without emitting a transition, so recovery cannot fire a
spurious Push Button event.

### 12.5 What `MSP_RC` actually contains

Stated plainly, because three separate mechanisms sit between the pilot's stick and the
value the bridge reads:

1. **Channel mapping and range scaling** — applied before `rcData` on both platforms.
2. **Failsafe substitution** — §12.4.
3. **Override, INAV only** — `rxGetChannelValue` returns Logic-Condition-overridden values
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
   [INAV] `rx/rx.c:685-691`

   INAV additionally carries an MSP aux overlay — "non-zero values override
   `rcChannels[].data` for CH9-CH32" ([INAV] `rx/rx.c:97,538`) — settable by any MSP client
   via `MSP2_INAV_SET_AUX_RC`.

None of this violates §3.1; the bridge overrides nothing. But the bridge may **observe**
values injected by a Logic Condition or by another MSP peripheral, and the documentation
shall not claim that a camera control reflects "the pilot's switch position". It reflects
the FC's current channel value.

### 12.6 Setup-time overlap advisory

At setup, and never in flight, the bridge may read `MSP_MODE_RANGES` (34 — [BF]
`msp/msp_protocol.h:106`) and `MSP_MODE_RANGES_EXTRA` (238 — [BF] `msp_protocol.h:264`) for
the sole purpose of warning the user that a proposed camera AUX range overlaps a configured
flight mode.

- These are read-only queries. No corresponding SET is ever issued (§3.1).
- The warning is **advisory and non-blocking**; the user may proceed.
- The failure it prevents is severe and otherwise silent: binding a camera control to the
  same AUX range as ARM, or to a range overlapping a flight mode, is discoverable only in
  the air.
- A failed or unsupported query shall suppress the advisory, not block setup.

Sharing a channel with a flight mode is legitimate — a pilot may deliberately want
recording to follow a mode switch. The advisory reports overlap; it does not forbid it.

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
custom OSD elements of §10, so the §5.1 capability probe settles both the display backend
and this trigger path — they can never be available separately.

The value is composability: a pilot can express "switch up **and** armed **and** above 10 m"
as one logic condition and the bridge simply reads its result, implementing none of it.

**No equivalent exists on Betaflight.** Every unconditionally-exported box performs a real
action — `BOXFAILSAFE`, `BOXBEEPERON`, `BOXBEEPERMUTE`, `BOXOSD`, `BOXPARALYZE` — several
destructive to flight or to the OSD this product depends on, and `BOXCAMERA1..3` are gated
on `USE_RCDEVICE`. The bridge shall never repurpose a functional mode as a trigger.

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

Normal startup shall perform a **directed reconnect** to the known camera, not broad
discovery. Broad discovery is a §3.3 RF concern and is restricted by §21.3.

## 16. Setup Interface

Normal operation requires no setup UI. Advanced setup may use an ESP-hosted Wi-Fi page
exposing: recording control mode, AUX channel/range assignments, Push Button timing (§12.3),
OSD slot ownership (§9.1, §10.2), camera preset assignments, OSD layout, warnings,
clock/GPS sync, reconnect policy, and optional camera capabilities.

Setup shall surface the §12.6 overlap advisory at the moment a range is assigned, and shall
display the resolved arming-detection method per §5.2 so a failed `MSP_BOXIDS` lookup is
visible rather than silent.

### 16.1 Wi-Fi is a memory-safety constraint, not only a UX rule

Wi-Fi setup shall be inactive during normal flight operation. On these parts, the state in
which the Wi-Fi stack, TCP/IP services and the BLE controller and host are all resident
simultaneously is the peak memory condition of the whole system; nothing later in a
session costs more.

Two requirements follow:

1. Wi-Fi and BLE shall not both be active in flight. Where both can be requested, exactly
   one owner shall be arbitrated, and the loser's stack deinitialised.
2. Any heap headroom gate shall be **measured with the AP up and a client attached**. A
   free-heap figure logged before the AP driver is resident overstates the true floor.
   Figures measured on other boards or other firmware do not transfer to this build.

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

Recording unexpectedly stopping while the aircraft remains armed shall be treated as a
high-priority camera warning.

## 19. Firmware Updates

Bridge firmware shall be field-updatable, to allow new camera models, camera BLE protocol
changes, new FC versions, and compatibility fixes.

**The bridge shall never begin a firmware update while the aircraft is armed.** Update is
the one bridge action that could plausibly wedge the MSP UART mid-flight, and it is the one
case where §3.2's "bridge failure is harmless" reasoning is weakest.

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
| Arming state + RC-link validity | 10 Hz | `MSP_STATUS` / `MSP2_INAV_STATUS` |
| Configured manual camera AUX controls | 10 Hz, or 20 Hz when a Push Button control is enabled (§12.3) — only when enabled | `MSP_RC` |
| Box-ID map (Betaflight) | once at startup, and after a detected FC reboot | `MSP_BOXIDS` |
| Mode ranges (overlap advisory) | setup only, never in flight | `MSP_MODE_RANGES`, `MSP_MODE_RANGES_EXTRA` |
| Logic condition / GVAR trigger (INAV, if bound) | 10 Hz | `MSP2_INAV_LOGIC_CONDITIONS_STATUS` / `..._GVAR_STATUS` |
| Camera state | event driven | — |
| Record timer | 1 Hz | — |
| Camera battery | ≤ 1 Hz | — |
| Storage | ≤ 1 Hz | — |
| Resolution / FPS | on change | — |
| GPS (only if §14 push enabled) | ≤ 10 Hz | `MSP_RAW_GPS` |

The status poll is **not optional** even in pure Arm Record: it carries both the arming
state and the RC-link validity of §12.4.

`MSP_RC` is a read-only common MSP query (105) on both supported FC families. The bridge
parses the number of returned u16 values from the reply length and evaluates only the
configured AUX channels (§12.2). If no manual AUX camera control is enabled, the bridge
shall not poll `MSP_RC`; pure Arm Record therefore adds no RC traffic.

The bridge shall cache the last valid RC sample. A missing or late `MSP_RC` response shall
not block the MSP task and shall not synthesize a control transition; normal retry/backoff
rules in §21.2 apply. Aliasing of short inputs is handled separately by §12.3 — a sample
that arrives correctly can still have missed an event between polls.

### 21.2 Quiet-bridge requirements

The bridge is a guest on the FC's MSP port and shall:

- never hold the port busy waiting on a camera or BLE operation;
- tolerate a missing or late reply without blocking;
- never retry-storm — failed requests back off;
- never write an OSD slot whose rendered content is unchanged;
- in telemetry-disabled compatibility mode (§5.1), issue no OSD writes; only the read-only
  MSP queries required by enabled camera-control features may continue.

### 21.3 BLE duty cycle and backoff

Derived from §3.3. Concrete values are set by the §27.2 measurement; the requirements are:

1. Directed reconnect to a known camera is preferred over discovery at all times (§15).
2. **Reconnect backoff:** consecutive failures shall increase the retry interval to a
   configured ceiling of ≥ 5 s, never a continuous scan.
3. **Armed restriction:** broad/discovery scanning shall be suppressed while armed. Only
   directed reconnect to the paired camera, at the backed-off interval, is permitted.
4. **Duty-cycle ceiling:** the bridge shall have a stated maximum BLE scan duty cycle in
   flight, and shall meet it in the reconnect-storm case as well as the nominal case.
5. **BLE transmit power** is an explicit coexistence parameter, configurable and stated,
   and is one of the variables swept by §27.2.

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
- **FC reboot while the bridge is powered** (invalidates the §5.2 box-ID map);
- **RC link loss and failsafe while the bridge is operating** (§12.4).

The FC shall continue operating normally with the bridge electrically connected but
completely non-functional. Per §3.3, "not affecting aircraft control" includes not
degrading control-link quality.

## 23. Installation Requirement

Target installation: four solder joints and one spare UART.

1. Connect 5 V, ground, TX and RX (observing the §4.2 peak-current note).
2. Enable MSP on the selected FC UART.
3. Enable and position the required OSD elements.
4. Configure any manual camera control as an AUX channel/range in the bridge setup (§12),
   reviewing the overlap advisory (§12.6) before accepting it.
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
| ARM bit resolved via `MSP_BOXIDS`; correct across a config change that alters the active box set (§5.2) | Required | n/a |
| Arming state read directly from `armingFlags` (§5.2) | n/a | Required |
| MSP writes accepted while armed | Required | Required |
| No flash write across a long armed session | Required | Required |
| Arm-record | Required | Required |
| `MSP_RC` returns live AUX values while armed | Required | Required |
| Direct AUX record control via `MSP_RC` (§12.2) | Required | Required |
| Push Button: 250 ms press detected; 100 ms press either detected or ignored, never double-fired (§12.3) | Required | Required |
| RC-link validity gate holds camera state through failsafe (§12.4) | Required | Required |
| No spurious transition on failsafe recovery re-baseline (§12.4) | Required | Required |
| Overlap advisory fires on a range colliding with ARM (§12.6) | Required | Required |
| Camera control bound to a Logic Condition (§12.7) | n/a | Optional |
| Pilot's existing USER1..4 / PINIO / mode / Programming Framework configuration unmodified after a full session — **verified by Configurator `diff` captured before and after**, not by bridge self-report | Required | Required |
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
control-link LQ/RSSI not measurably degraded versus the bridge-absent baseline.

| Injected failure | Maps to §22 |
| --- | --- |
| Bridge held in reset for the whole flight | bridge reboot / reset |
| Bridge TX line held high, then held low | UART failure |
| **Bridge power cycled mid-record while the FC is armed** | bridge reboot — bridge must re-detect the FC and resume without emitting a camera-control transition on its first RC sample (§12.4 re-baseline) |
| Bridge watchdog forced | bridge watchdog |
| **FC rebooted while the bridge is powered and recording** | FC reboot — bridge must re-detect and re-resolve the §5.2 box-ID map |
| **RC transmitter powered off mid-record, camera channel set to `RX_FAILSAFE_MODE_HOLD`** | RC loss — recording must continue |
| **RC transmitter powered off mid-record, camera channel set to `RX_FAILSAFE_MODE_SET` to a value outside the active range** | RC loss — recording must **still** continue; this is the §12.4 gate |
| **`BOXFAILSAFE` switch actioned mid-record** | RC loss — no camera state change |
| Camera absent at boot (never pairs) | camera absent |
| Camera powered off mid-record | camera power-off |
| Camera rebooted mid-flight | camera reboot |
| Camera carried out of BLE range and back | BLE disconnect |
| Sustained BLE reconnect storm while armed | repeated reconnect failures |
| Unsupported camera advertised nearby | unsupported camera |
| Malformed/truncated camera response injected | malformed response |
| Corrupted stored configuration | configuration corruption |
| Wi-Fi stack forced to fail at init | Wi-Fi failure |

The reconnect-storm row is the one that most directly tests §3.3 and shall not be waived.
The two `RX_FAILSAFE_MODE_SET` and `BOXFAILSAFE` rows are the ones that most directly test
§12.4 and shall not be waived.

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

An integrated build would also have direct access to RC channel data and link state,
removing the §12.4 ambiguity at source — but that is a property of that architecture, not
a reason to weaken §12.4 in the standalone product.

## 26. V1 Scope

### V1 — Required

- standalone ESP32-C3 bridge;
- Betaflight 2025.12+ and INAV 8.x backends;
- GoPro BLE driver;
- automatic directed reconnect with backoff (§21.3);
- arm-record and direct AUX-controlled record (§12), with the RC-link validity gate (§12.4)
  and Push Button timing (§12.3);
- actual camera-state feedback (§7) with staleness handling (§17);
- the four OSD fields of §8;
- OSD warnings (§18);
- slot ownership configuration (§9.1, §10.2);
- setup-time overlap advisory (§12.6);
- one-button pairing;
- firmware updates (no OTA while armed);
- optional Wi-Fi configuration (inactive in flight, §16.1);
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
- ELRS-integrated production implementation.

## 27. Engineering Spikes Before Implementation

v0.1's single gate — whether stock INAV 8 can accept runtime camera data for its Custom OSD
Elements — is answered (§10). It is replaced by four spikes, each with pass criteria.

### 27.1 FC interface, control input and OSD injection on hardware

Confirm the source reading of §5.2, §9, §10 and §12 on real flight controllers.

Pass criteria:

1. `MSP_BOXIDS` resolves permanent ID 0 to a bit index, and that index tracks ARM correctly
   on Betaflight — **including after a configuration change that adds or removes an active
   box**, which is the case that would silently shift it;
2. INAV arming read directly from `armingFlags` bit 2 agrees with the Betaflight-side result
   under the same conditions;
3. **The RC-validity flags of §12.4 are maintained while armed.** This is the one assumption
   in this document taken from a flag's declaration rather than its observed runtime
   behaviour. If it fails, fall back to the `BOXFAILSAFE` box state and re-cost §12.4;
4. `MSP_RC` returns live AUX values while armed on both platforms, matching the FC Receiver
   tab;
5. configured AUX thresholds and edges produce the expected camera-control events without
   modifying FC mode, USER or PINIO configuration;
6. a 250 ms Push Button press is detected reliably; a 100 ms press is either detected or
   ignored but never double-fired (§12.3);
7. failsafe injection with the camera channel in `RX_FAILSAFE_MODE_SET` produces **no**
   camera-control transition, and recovery re-baselines without firing one;
8. custom message / custom element writes accepted and rendered while armed, on both
   Betaflight and INAV;
9. correct rendering on analog and on MSP DisplayPort;
10. four fields independently updateable;
11. no flash write observed across a long armed session (verify by configuration
    persistence check across power cycle, not by inference);
12. graceful degrade confirmed on an INAV target built without `USE_PROGRAMMING_FRAMEWORK`,
    and on a Betaflight build older than 2025.12.

### 27.2 RF coexistence — the §3.3 release gate

Measure the bridge's effect on a 2.4 GHz ELRS control link using the existing passive RF
survey and Blackbox analysis tooling, which already slices noise floor by condition.

Conditions, all else held constant:

```
bridge absent → bridge idle (paired, connected)
              → bridge active (recording, telemetry flowing)
              → bridge reconnect storm (camera powered off)
```

swept against configured BLE transmit power (§21.3).

Pass criteria:

1. **No measurable loss of receiver sensitivity or link margin** versus the bridge-absent
   baseline in the idle and active conditions. The metric is the Δ in ELRS input power
   required to hold a fixed PER/LQ threshold — LQ and RSSI alone saturate and will report
   "no change" right up to the cliff;
2. the reconnect-storm condition bounded by the §21.3 backoff and duty-cycle limits, and
   also showing no measurable degradation;
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
