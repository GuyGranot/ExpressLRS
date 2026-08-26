# Action Camera Bridge — Product Requirements Specification v0.2

**Status:** Draft
**Supersedes:** v0.1
**Target platforms:** Betaflight 2025.12+, INAV 8.x
**Initial camera families:** GoPro (V1), DJI Osmo Nano (V1.1, gated on §27.4)
**Architecture:** Standalone BLE-to-FC bridge over one spare UART
**Future architecture:** Optional ELRS-integrated implementation sharing the camera/control/OSD core
**Distribution:** Personal / open-source

### What changed from v0.1

| Change | Section |
| --- | --- |
| INAV OSD injection gate **resolved** — mechanism verified in source, no longer release-blocking | §10, §27 |
| `MSP_SET_NAME` → Craft Name fallback **deleted** — not needed | §10 |
| **RF coexistence added as a safety property** — the gap in v0.1 | §3, §21, §24 |
| Control inputs respecified: bridge-owned AUX range by default, `BOXUSER1..4` as an optional upgrade | §12, §13, §21 |
| `MSP_RC` retained at 10 Hz — `BOXUSER1..4` are **not** reliably exported (see §12.1) | §21 |
| OSD layouts written against verified hard limits (BF 4×16, INAV 8×15 uppercase) | §8, §9, §10 |
| Failure-mode acceptance tests added; every §22 event now has a §24 row | §24 |
| Custom message/element **slot ownership** made configurable — new requirement | §9, §10 |
| "Quiet bridge" MSP-citizenship requirement added | §21 |
| Numeric staleness thresholds added | §7, §17 |
| §14 broadened from clock sync to GPS + time push | §14 |
| V1 scope re-cut: GoPro first, DJI to V1.1, presets/sync to V1.2 | §26 |
| DJI target changed to **Osmo Nano**, which has no official control protocol — reclassified Unsupported pending a probe, with a DUML path | §20.2, §26, §27.4 |

### Changed after external review of v0.2

| Change | Section |
| --- | --- |
| **`BOXUSER1..4` are not reliably exported** — control input redesigned around a bridge-owned AUX range, with `USER` binding as a runtime-detected option | §12.1, §12.2, §21.1 |
| Read-only observation of the pilot's mode config made an explicit prohibition | §3.1 |
| RF hazard restated as near-field coupling between two separate radios/antennas; shared-MCU case confined to §25 | §3.3, §25 |
| RF acceptance metric changed from LQ/RSSI to **Δ input power at a fixed PER/LQ threshold** | §27.2 |
| BLE transmit power made a coexistence parameter, established by measurement | §21.3 |
| INAV owned elements read back via 0x2101 and restored | §10.2.1 |
| Older-Betaflight backend kept architecturally possible, without enlarging V1 acceptance | §9.3 |
| Calendar versioning used consistently; unmeasured heap estimate removed | §9, §16.1 |
| State-recovery acceptance cases added (FC reboot; bridge reboot while armed; RX loss) | §24.3 |
| Bridge holds camera state through failsafe rather than acting on failsafe switch positions | §12.2.1 |
| Setup UI shows channels already carrying mode ranges, so switch overlap is visible up front | §12.2, §16 |
| INAV Logic Condition / GVAR added as the preferred optional trigger; no Betaflight equivalent exists | §12.2, §3.1 |

> **Source-verification note.** Every MSP command, constant and limit in this document was
> read from Betaflight and INAV `master` on **2026-08-25** and is cited by file and line.
> These are upstream moving targets; re-verify before each release. Citations marked
> **[BF]** are `betaflight/betaflight`, **[INAV]** are `iNavFlight/inav`.

---

## 1. Product Goal

Provide reliable in-flight control and telemetry for supported action cameras without
modifying flight-controller firmware or inserting the device into any flight-critical
control path.

The bridge shall:

* control supported cameras over Bluetooth;
* read the camera's actual reported state;
* display camera state and telemetry through the flight controller's native OSD;
* support Betaflight 2025.12+ and INAV 8.x;
* work independently of the video system;
* require only power and one bidirectional FC UART;
* continue to have no effect on flight if the bridge, Bluetooth connection, or camera fails;
* **not measurably degrade the aircraft's control link.**

The primary value is **confirmed camera state**, rather than inferred state from the
pilot's switch position.

---

## 2. Core User Experience

After initial installation and pairing:

1. Aircraft powers up.
2. Bridge boots and identifies the FC.
3. Bridge automatically reconnects to the paired camera.
4. Camera telemetry appears in the FC OSD.
5. Arming, mode switches, or buttons control recording according to configuration.
6. Camera responses update the OSD.
7. Loss of camera communication is explicitly indicated.
8. No phone, camera app, or bridge configuration interface is required for normal use.

Normal operation shall require no interaction with the bridge itself.

---

## 3. Safety Architecture

The bridge is a **passenger device**. It is isolated from flight control in three
independent dimensions: **logical**, **electrical**, and **radio-frequency**.

It shall not participate in:

* RC reception;
* motor control;
* servo output;
* PID processing;
* navigation;
* failsafe;
* arming authorization;
* receiver-to-FC signal transport.

```text
RC receiver ───────────────► Flight Controller ───► flight control
                                  ▲
                                  │ MSP (read + non-flight-critical writes)
                                  │
Camera ◄──── BLE ───── Camera Bridge
```

### 3.1 Logical isolation

The bridge may read FC state and write explicitly supported non-flight-critical
display data. It shall never use:

* RC override;
* motor commands;
* servo commands;
* arming commands;
* flight-mode commands;
* PID/configuration writes;
* any command that triggers a flash/EEPROM write (notably `MSP_EEPROM_WRITE`);
* `MSP_REBOOT`.

**The bridge is a read-only observer of the pilot's mode and programming configuration.** It
shall never issue `MSP_SET_MODE_RANGE` (35), alter `BOXUSER1..4` mode ranges, modify PINIO /
PINIO_BOX configuration, or — on INAV — write the Programming Framework via
`MSP2_INAV_SET_LOGIC_CONDITIONS` (0x2023) or `MSP2_INAV_SET_GVAR` (0x2214). Betaflight exposes no PINIO-box write over MSP today,
so this is partly a prohibition against a door that is currently shut — it is stated
explicitly so that a later implementation cannot decide to "helpfully" configure USER1 on
the pilot's behalf, on either platform.

### 3.2 Electrical isolation

A bridge crash, reset, watchdog event, brownout, UART failure or camera failure shall
leave flight-controller behavior unchanged. The FC shall continue operating normally with
the bridge electrically connected but completely non-functional, including with the
bridge's TX line held at either rail.

### 3.3 RF coexistence — *new in v0.2*

**This is the only path by which a correctly-isolated passenger device can still endanger
a flight, and it is the dimension v0.1 omitted.**

The bridge transmits Bluetooth Low Energy in the 2.4 GHz ISM band. When the aircraft's
control link is 2.4 GHz ELRS, the bridge is an in-band interferer mounted centimetres from
the receiving antenna.

**For V1 standalone hardware the hazard is near-field, in-band coupling between two
separate radios with two separate antennas.** The bridge's BLE transmitter is the ESP32-C3
radio; the control link is a separate SX1280/LR1121 in a separate receiver module with its
own antenna. Nothing is shared between them but the air and the airframe, and that is
sufficient — a transmitter tens of millimetres away can desense a receiver operating near
its sensitivity threshold regardless of how well isolated the two are logically.

The ESP32-C3's radio *is* shared, but between **Wi-Fi and BLE only**. That constraint drives
§16.1's memory and transport arbitration rules, not this section. The compounded case —
BLE and the control link sharing one MCU, scheduler, heap and SPI bus, in addition to the
band — belongs to the ELRS-integrated architecture and is treated in §25.

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

---

## 4. Hardware Interface

### 4.1 Flight Controller

Four-wire interface:

```text
Bridge        FC
5 V    ───── 5 V
GND    ───── GND
TX     ───── RX
RX     ───── TX
```

Requirements:

* one free full-duplex UART, configured for MSP;
* no additional FC wiring;
* no connection to RC receiver, VTX, or camera (other than Bluetooth).

Default MSP baud rate: **115200**. Higher rates may be supported where validated.

### 4.2 Power

The bridge shall document its **peak** current draw, not only its average. BLE transmit
bursts on ESP32-C3 draw several hundred milliamps for short intervals; the installation
guidance shall state the peak figure and the required local bulk capacitance, because the
FC 5 V rail is frequently shared with the camera itself.

The bridge shall tolerate brownout and reset cleanly (§22).

---

## 5. FC Platform Detection

At startup the bridge shall identify the FC using standard MSP identification queries:

| Purpose | Command |
| --- | --- |
| MSP/API version | `MSP_API_VERSION` |
| FC family | `MSP_FC_VARIANT` → `"BTFL"` / `"INAV"` |
| FC firmware version | `MSP_FC_VERSION` |
| Exported box IDs (permanent IDs) | `MSP_BOXIDS` (119) |
| Box names, for user-facing labels | `MSP_BOXNAMES` (116) |
| Mode range configuration | `MSP_MODE_RANGES` (34) |
| Mode logic and links (Betaflight) | `MSP_MODE_RANGES_EXTRA` (238) |
| INAV OSD capability probe | `MSP2_INAV_CUSTOM_OSD_ELEMENTS` (0x2100) |

Supported variants:

```text
BTFL → Betaflight backend
INAV → INAV backend
```

### 5.1 Capability probing

The bridge shall not infer OSD capability from the version string alone.

* **INAV:** issue `MSP2_INAV_CUSTOM_OSD_ELEMENTS` (0x2100). The FC replies with
  `MAX_CUSTOM_ELEMENTS`, `OSD_CUSTOM_ELEMENT_TEXT_SIZE - 1`, and `CUSTOM_ELEMENTS_PARTS`
  **[INAV]** `fc_msp.c` (out-command). A missing or error reply means the target was built
  without `USE_PROGRAMMING_FRAMEWORK` and **has no custom elements at all** — this is the
  correct trigger for telemetry-disabled compatibility mode. `USE_PROGRAMMING_FRAMEWORK` is
  enabled by default (**[INAV]** `src/main/target/common.h:173`) but small flash targets
  may undefine it.
* **Betaflight:** custom messages require 2025.12 or later. Below that, enter
  telemetry-disabled compatibility mode (but see §9.3).

Unsupported FC variants shall enter telemetry-disabled compatibility mode and shall not
send platform-specific MSP writes.

The bridge shall track: FC family, FC firmware version, MSP/API version, resolved box IDs,
and available OSD capabilities.

---

## 6. Camera Abstraction

Camera-specific protocols shall not leak into the FC interface.

```text
CameraService
 ├── GoProDriver          (V1)   — Open GoPro BLE
 ├── OsmoNanoDriver       (V1.1) — DUML over BLE, gated on §27.4
 └── future drivers
```

Common camera state:

```text
connected, mode, recording, recordingTime, batteryPercent,
recordTimeRemaining, storageRemaining, resolution, frameRate, errorState
```

Optional capabilities, each behind an explicit capability flag:

```text
photoCapture, modeSwitch, presetSwitch, highlight, cameraPower,
clockSet, gpsPush, remainingStorage, temperature, other camera-specific telemetry
```

Unsupported information shall remain **unknown**, not fabricated. Capability flags shall
gate both behaviour and UI: GoPro-specific functionality shall not appear for DJI cameras
or vice versa.

---

## 7. Camera State Truth Model

OSD information shall represent **camera-confirmed state**.

```text
Pilot requests RECORD → Bridge sends BLE command → Camera reports recording → OSD shows REC
```

The bridge shall not display `REC` solely because a control input was issued.

States:

```text
CAM OFF, CONNECTING, READY, PHOTO, VIDEO, REC, CAM STALE, CAM LOST, CAM ERR
```

`CAM STALE` is new in v0.2 and covers the interval between "last update is too old to
trust" and "the link is gone", which v0.1 left undefined. Thresholds are in §17.

---

## 8. OSD Requirements

Field content is constrained by verified per-platform limits (§9, §10). Because INAV
upper-cases all custom element text at render time (**[INAV]**
`src/main/io/osd/custom_elements.c:217`), **the bridge shall compose all OSD strings in
uppercase on every platform**, so Betaflight and INAV render identically.

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

---

## 9. Betaflight OSD Backend

**Minimum version:** Betaflight **2025.12**. Betaflight uses calendar versioning
(`FC_VERSION_STRING` is year.month.patch, **[BF]** `build/version.h:53`); this document uses
that form throughout. The pre-calendar milestone label for the release carrying this feature
was `4.6`, noted once here only so the merge can be found in project history.

Release context as of 2026-08: current stable is **2026.6.1**, master is `2026.12.0-alpha`,
and the legacy **4.5.x** line remains actively maintained (4.5.5 shipped 2026-06-28). That
last point matters — there is a live installed base with no custom-message support, which is
why §9.3 requires the backend interface to keep an older-Betaflight path possible.

Transport:

```text
MSP2_SET_TEXT (0x3007) → subtype MSP2TEXT_CUSTOM_MSG_0..+3 → pilotConfig.message[0..3] → OSD
```

Verified constants:

| Item | Value | Source |
| --- | --- | --- |
| Command | `MSP2_SET_TEXT` = `0x3007` | **[BF]** `msp/msp_protocol_v2_betaflight.h:28` |
| Subtype base | `MSP2TEXT_CUSTOM_MSG_0` = `7` (so 7, 8, 9, 10) | **[BF]** `msp_protocol_v2_betaflight.h:52` |
| Slot count | `OSD_CUSTOM_MSG_COUNT` = **4** | **[BF]** `pg/pilot.h:27` |
| Chars per slot | `MAX_NAME_LENGTH` = **16** | **[BF]** `pg/pilot.h:26,32` |
| Handler | `mspProcessInCommand`, `case MSP2_SET_TEXT` | **[BF]** `msp/msp.c:4386,4419` |

Payload: `[subtype u8][length u8][chars...]`.

**Armed behaviour:** the handler carries no `ARMING_FLAG(ARMED)` guard; the armed checks in
`msp.c` apply to unrelated commands. Writes are accepted while armed. *(Confirm on hardware
per §27.1 before relying on this.)*

**Persistence:** writes land in the `pilotConfig` parameter group in RAM. No flash write
occurs unless something separately issues `MSP_EEPROM_WRITE`, which the bridge shall never
send (§3.1).

The user shall enable and position OSD elements `Custom Message 1..4`. No custom Betaflight
build shall be required.

### 9.1 Slot ownership — *new in v0.2*

The four custom-message slots are a **shared global resource**. Another peripheral (a lap
timer, a radar, a rangefinder) writing the same slot will silently fight the bridge for it.

The bridge shall therefore:

* expose which slot indices it owns as configuration (default: all four);
* write only owned slots;
* leave unowned slots untouched.

### 9.2 Update policy

| Data | Rate |
| --- | --- |
| State changes | Immediate |
| Recording timer | 1 Hz maximum |
| Battery / storage | On change, or low-rate periodic refresh (≤ 1 Hz) |
| Resolution / FPS | On change |

The bridge shall not rewrite a slot whose rendered content is unchanged.

### 9.3 Legacy backends shall remain architecturally possible

Betaflight older than 2025.12 is telemetry-disabled in V1 (§5.1) and **shall not be added to
the V1 acceptance matrix**. But the OSD backend interface shall not be shaped so that such a
path is impossible to add later: a Craft Name / Pilot Name backend via
`MSP2TEXT_CRAFT_NAME` or `MSP2TEXT_PILOT_NAME` (§9, subtypes 2 and 1) could later serve the
4.5.x installed base, or act as an emergency fallback.

The reason this is a hedge and not a feature: those fields are **user-visible persistent
configuration**, not dedicated scratch slots. Overwriting a pilot's craft name is
destructive in a way that writing `message[0..3]` is not. Any such backend shall therefore
be explicit opt-in and shall implement read-back and restore as in §10.2.1. A single dynamic
line does not satisfy §8, and shall never be presented as full platform support (§20).

---

## 10. INAV 8 OSD Backend

**Minimum version:** INAV 8.x. INAV support is a **release-blocking requirement**, not an
optional compatibility mode.

> **Gate status: RESOLVED.** v0.1 treated runtime injection into INAV Custom OSD Elements as
> an unknown that could sink the backend, and hedged with an `MSP_SET_NAME` → Craft Name
> fallback. The mechanism exists, is reachable while armed, and does not touch flash. The
> fallback is **removed from this specification**.

Transport:

```text
MSP2_INAV_SET_CUSTOM_OSD_ELEMENTS (0x2102) → osdCustomElements[n] → Custom OSD Element n
```

Verified constants:

| Item | Value | Source |
| --- | --- | --- |
| Command | `MSP2_INAV_SET_CUSTOM_OSD_ELEMENTS` = `0x2102` | **[INAV]** `msp/msp_protocol_v2_inav.h:117` |
| Capability query | `MSP2_INAV_CUSTOM_OSD_ELEMENTS` = `0x2100` | **[INAV]** `msp_protocol_v2_inav.h:115` |
| Element count | `MAX_CUSTOM_ELEMENTS` = **8** | **[INAV]** `io/osd/custom_elements.h:24` |
| Parts per element | `CUSTOM_ELEMENTS_PARTS` = **3** | **[INAV]** `custom_elements.h:23` |
| Text size | `OSD_CUSTOM_ELEMENT_TEXT_SIZE` = **16** (15 usable + NUL) | **[INAV]** `custom_elements.h:22` |
| Handler | `mspFcProcessInCommand` | **[INAV]** `fc/fc_msp.c:3783` |
| Build gate | `USE_PROGRAMMING_FRAMEWORK` (default on) | **[INAV]** `target/common.h:173` |

### 10.1 Payload format

Exactly **28 bytes**. The handler enforces
`dataSize == (OSD_CUSTOM_ELEMENT_TEXT_SIZE - 1) + (CUSTOM_ELEMENTS_PARTS * 3) + 4`
(**[INAV]** `fc_msp.c:3785`) — short writes are rejected with `MSP_RESULT_ERROR`.

```text
[element index      u8 ]   < MAX_CUSTOM_ELEMENTS
[part0.type         u8 ]   [part0.value u16]
[part1.type         u8 ]   [part1.value u16]
[part2.type         u8 ]   [part2.value u16]
[visibility.type    u8 ]   [visibility.value u16]
[text              15 B]   NUL is appended by the FC
```

For a plain text field the bridge sets:

```text
part0.type = CUSTOM_ELEMENT_TYPE_TEXT     (1)   part0.value = 0
part1.type = CUSTOM_ELEMENT_TYPE_NONE     (0)   part1.value = 0
part2.type = CUSTOM_ELEMENT_TYPE_NONE     (0)   part2.value = 0
visibility.type = CUSTOM_ELEMENT_VISIBILITY_ALWAYS (0)   visibility.value = 0
text = 15 uppercase characters, space padded
```

(Type and visibility enums: **[INAV]** `custom_elements.h:26-63`.)

**Whole-element rewrite.** Every SET replaces parts, visibility *and* text together. The
bridge shall cache complete element state and resend it intact on each update; it cannot
patch one field.

**Armed behaviour:** `mspFcProcessCommand` gates only `MSP_REBOOT` on `ARMING_FLAG(ARMED)`
(**[INAV]** `fc_msp.c:4715`). The setter is reachable while armed. *(Confirm on hardware per
§27.1.)*

**Persistence:** the handler writes `osdCustomElementsMutable(n)` — the parameter group in
RAM. No flash write. Note the corollary: if the user later saves configuration from the
Configurator, whatever the bridge last wrote becomes persisted. This is harmless but shall
be documented.

**Rendering:** text is upper-cased (`sl_toupper`, **[INAV]** `custom_elements.c:217`).

### 10.2 Parity and slot ownership

INAV offers 8 × 15 characters against Betaflight's 4 × 16. Functional parity with §8 is
therefore comfortably met, with room for the optional fields in §8. Exact visual layout need
not match Betaflight.

Element ownership is configurable exactly as in §9.1 (default: elements 1–4), since INAV
custom elements are equally a shared resource with the Programming Framework.

### 10.2.1 Read back and restore

Because the handler mutates live parameter-group values, an unrelated Configurator save by
the pilot would persist whatever the bridge last wrote. The bridge shall therefore:

* **read and cache** each owned element via `MSP2_INAV_CUSTOM_OSD_ELEMENT` (0x2101) before
  overwriting it for the first time — the handler returns parts, visibility and text
  (**[INAV]** `fc_msp.c:4534`), so a faithful restore is possible;
* **restore** the cached originals on graceful shutdown and on transition into Wi-Fi setup
  mode.

Scope this honestly: an aircraft is normally powered off by unplugging a battery, so the
graceful path covers the setup-mode transition far more often than a real shutdown. The
primary protection against destroying the pilot's configuration is **ownership** — never
writing an element the bridge does not own — and restore is a second line behind it, not a
substitute.

### 10.3 Hard constraint

INAV support shall require: stock INAV 8 firmware; no custom target; no firmware patch; no
special FC hardware; only the same MSP UART used by the bridge. It shall not abuse
RC channels, RSSI, aircraft battery, altitude or navigation values.

---

## 11. Video-System Independence

The bridge shall inject into the FC's OSD model rather than communicate with the VTX.
Camera telemetry is therefore compatible with any OSD output the selected FC supports:
analog, DJI, HDZero, Walksnail/Avatar, and MSP DisplayPort. No VTX-specific bridge firmware
shall be required.

---

## 12. Recording Control Modes

### 12.1 Why `BOXUSER1..4` cannot be the only path

An earlier draft specified that camera switches be read purely as `BOXUSER1..4` mode flags
from `MSP_STATUS`. **That does not work, and the reason is not obvious from the MSP
documentation.** It is recorded here so it is not re-proposed.

Betaflight exports `BOXUSER1..4` as active boxes **only if** `USE_PINIOBOX` is compiled in
*and* a PINIO pin is assigned to that specific box (**[BF]** `msp/msp_box.c:334-354`; the
source comment states "Turn BOXUSERx only if pinioBox facility monitors them, as the
facility is the only BOXUSERx observer"). INAV is more permissive, adding all four
unconditionally, but still only under `#ifdef USE_PINIOBOX` (**[INAV]**
`fc/fc_msp_box.c:332-338`).

The consequence cascades further than a missing status bit:

* A non-exported box is absent from `MSP_BOXIDS`, so the Configurator's Modes tab never
  offers `USER1` — **the pilot cannot create the mode range at all.**
* Reading `MSP_MODE_RANGES` therefore does not rescue the design: there is nothing to read.
* The remaining routes to create the range are assigning a spare PINIO, or the CLI `aux`
  command. §23 forbids requiring CLI, and consuming a PINIO is neither always possible nor
  reasonable to ask of a pilot.

Any design in which the bridge mirrors Betaflight's mode evaluation also inherits
`modeLogic` (AND/OR) and `linkedTo` semantics from `MSP_MODE_RANGES_EXTRA` (238); using
`MSP_MODE_RANGES` alone silently mis-evaluates linked or AND-combined modes. That is a copy
of the FC's mode engine living in the bridge, free to drift across FC releases.

### 12.2 Control input architecture

**Default path — bridge-owned AUX range.** The bridge holds its own channel index and range
for each camera function, configured in the bridge's setup interface (§16). It polls
`MSP_RC` (105) at 10 Hz and evaluates its own range locally.

This requires no FC-side configuration, no CLI, no PINIO, and behaves identically on both
platforms and on every target. It is the only path guaranteed to be available, and it is
therefore the default.

**Optional path — bind to an exported `USER` mode.** Where the FC *does* export
`BOXUSER1..4`, the pilot may instead bind a camera function to a USER mode and configure it
normally in the Modes tab. The bridge then reads the mode flag from
`MSP_STATUS` / `MSP2_INAV_STATUS` and needs no `MSP_RC` poll for that function.

Availability shall be **detected at runtime** from `MSP_BOXIDS`, never assumed from the FC
family or version. Where the box is absent, the option shall not be offered in the setup UI.
Box IDs shall be resolved via permanent-ID mapping; bit positions shall never be assumed.

Where `USE_CUSTOM_BOX_NAMES` is present, Betaflight lets the pilot rename `USER1..4`
(**[BF]** `msp/msp_box.c:147-156`), so a bound mode can read as `CAMERA` rather than
`USER1`. The bridge shall display the name reported by `MSP_BOXNAMES` rather than a
hard-coded label.

**Optional path, INAV only — bind to a Logic Condition or Global Variable.** INAV's
Programming Framework exposes live evaluation results over MSP, read-only:

| Command | Returns |
| --- | --- |
| `MSP2_INAV_LOGIC_CONDITIONS_STATUS` (0x2026) | live value of each logic condition (**[INAV]** `fc_msp.c:599`) |
| `MSP2_INAV_GVAR_STATUS` (0x2027) | live value of each global variable (**[INAV]** `fc_msp.c:627`) |
| `MSP2_INAV_LOGIC_CONDITIONS_CONFIGURED` (0x203C) | 64-bit bitmask of configured logic conditions |

This is the **preferred optional path on INAV**, ahead of `USER` mode binding. It is
purpose-built for pilot-defined logic, arbitrarily expressive, not gated on PINIO, and
discoverable — 0x203C lets the setup UI offer only the logic conditions the pilot has
actually configured, rather than a list of empty slots.

It shares the `USE_PROGRAMMING_FRAMEWORK` build gate with the custom OSD elements of §10, so
on INAV a single capability probe (§5.1) establishes both the OSD backend and this trigger
path: if one is available, so is the other.

**No equivalent exists on Betaflight.** Of the boxes Betaflight exports unconditionally —
`BOXFAILSAFE`, `BOXBEEPERON`, `BOXBEEPERMUTE`, `BOXOSD`, `BOXPARALYZE` — every one performs a
real action, several of them destructive to flight or to the OSD this product depends on.
The remainder are feature-gated and equally non-spare. `USER1..4` are the only
side-effect-free boxes in the list, which is precisely why they are gated on their only
consumer. The bridge shall never repurpose a functional mode as a trigger.

In both paths the bridge only ever *reads* (§3.1). Existing `USER1..4` and PINIO
configuration remains the pilot's, untouched and independent.

**Fidelity of the default path.** `MSP_RC` serializes `rcData[]` directly (**[BF]**
`msp/msp.c:1398`), and the FC evaluates its own mode ranges from that same `rcData[]` in
25 µs steps from 900 µs (**[BF]** `fc/rc_modes.c:101-110`). A bridge-owned range therefore
computes on identical input to the FC's mode engine, and shall use the same step
arithmetic, so that binding a function to a `USER` mode and configuring the equivalent range
in the bridge produce the same result.

**Channel sharing is observation, not contention.** Two readers of one channel do not
conflict: if a channel already drives a `USER` mode and PINIO output, pointing the bridge at
it leaves PINIO working exactly as before — the switch simply now does two things. Because
the bridge already reads `MSP_MODE_RANGES` at startup, the setup interface (§16) **shall
show which channels already carry mode ranges** so the pilot can see such an overlap before
creating it, rather than discovering it in flight.

### 12.2.1 Behaviour on RX loss

On failsafe or RX loss the FC populates `rcData[]` with failsafe values, so both the FC's
mode engine and the bridge's evaluation see the switch in its failsafe position. The bridge
shall **hold its last commanded camera state** rather than act on a failsafe-driven
transition. A failsafe is precisely when the footage matters, and a spurious stop would
destroy the most valuable recording of the flight. Camera-side telemetry continues to be
governed by §17.

### 12.3 Modes

Each mode below names a *camera switch*, which resolves to either path of §12.2 according
to configuration.

**Arm Record** (default) — `ARM → start`, `DISARM → stop`. Uses no camera switch at all, so
it works on every FC and target with zero configuration.

**Switch Toggle** — camera switch active → record, inactive → stop.

**Push Button** — momentary camera switch toggles recording or triggers a configured action.

**Combi** — arm/disarm automation stays active while a camera switch can manually stop or
restart recording during the flight.

Arming state is always taken from `MSP_STATUS` / `MSP2_INAV_STATUS`, which is unconditional
on both platforms and never subject to §12.1.

In all modes, displayed state follows camera acknowledgement (§7), never the input.

---

## 13. Camera Mode / Preset Control

*(V1.2 — see §26.)*

Where supported by the camera and positively advertised by the driver:

* photo/video mode may be assigned to a camera switch (§12.2);
* up to three camera presets may be mapped to a three-position range on one camera switch;
* switching shall be possible while armed.

---

## 14. GPS and Time Push

*(V1.2 — see §26.)*

Where the camera protocol allows it, the bridge may push:

1. **UTC time**, for cameras that lose RTC state when depowered (notably naked/action cams).
   Source: `MSP_RTC` — Betaflight returns year/month/day/h/m/s/ms (**[BF]** `msp.c:2277`);
   INAV returns seconds-since-epoch as U32 plus millis U16 (**[INAV]** `fc_msp.c:1521`). The
   two formats differ; the backend abstracts this.
2. **Live GPS position**, for cameras that accept it — a stronger feature than clock sync
   alone, and absent from v0.1. DJI's official reference pushes GPS at 10 Hz to tag
   footage, but does so over the R SDK protocol, which the Osmo Nano does not speak
   (§20.2). Whether the Nano accepts a GPS push over DUML is unknown and is **not** part of
   the §27.4 pass criteria; treat it as speculative until a driver exists.

Neither shall affect recording or flight operation when unavailable. GPS push rate counts
against the §21 MSP budget and shall not exceed the camera's documented rate.

---

## 15. Pairing

The bridge stores one active camera identity.

* physical pairing button;
* enters pairing mode without phone or app;
* discovers supported cameras, binds the selected one, remembers it across power cycles;
* reconnects automatically on subsequent boots;
* replaced only by an explicit pairing action.

Normal startup shall perform a **directed** reconnect to the known camera, not broad
discovery. Broad discovery is a §3.3 RF concern and is restricted by §21.3.

---

## 16. Setup Interface

Normal operation requires no setup UI. Advanced setup may use an ESP-hosted Wi-Fi page
exposing: recording control mode, camera switch assignments — channel and range, or a
binding to an exported `USER` mode (§12.2) — OSD slot ownership (§9.1, §10.2),
camera preset assignments, OSD layout, warnings, clock/GPS sync, reconnect policy, and
optional camera capabilities.

### 16.1 Wi-Fi is a memory-safety constraint, not only a UX rule

Wi-Fi setup shall be inactive during normal flight operation. On these parts, the state in
which the Wi-Fi stack, TCP/IP services and the BLE controller and host are **all resident
simultaneously** is the peak memory condition of the whole system; nothing later in a
session costs more.

Two requirements follow:

1. Wi-Fi and BLE shall not both be active in flight. Where both can be requested, exactly
   one owner shall be arbitrated, and the loser's stack deinitialised.
2. Any heap headroom gate shall be **measured with the AP up and a client attached**. A
   free-heap figure logged before the AP driver is resident overstates the true floor by
   the AP driver's full allocation and shall not be used as evidence. No estimate of that
   allocation is stated here: figures from other boards and other firmware do not transfer,
   and the measurable requirement is stronger than any number this document could offer.

---

## 17. Camera Telemetry Freshness

Every telemetry field tracks:

```text
value, valid, lastUpdated
```

Values the camera does not report remain unavailable (`valid = false`). Estimated values
are never marked valid.

### 17.1 Thresholds

| Condition | Result |
| --- | --- |
| Status update received | Fields valid, `lastUpdated` refreshed |
| No status update for **> 3 s** | Live fields (timer, battery, storage) invalidated; state shows `CAM STALE` |
| No status update for **> 10 s**, or BLE disconnected | All camera fields invalid; state shows `CAM LOST` |

Stale recording state shall never remain displayed indefinitely as confirmed `REC`.

---

## 18. Warnings

Configurable warnings:

```text
CAM LOST, CAM BAT LOW, CARD LOW, CARD FULL, REC STOPPED, CAM ERROR
```

Recording unexpectedly stopping while the aircraft remains armed shall be treated as a
high-priority camera warning.

---

## 19. Firmware Updates

Bridge firmware shall be field-updatable, to allow new camera models, camera BLE protocol
changes, new FC versions, and compatibility fixes.

**The bridge shall never begin a firmware update while the aircraft is armed.** Update is
the one bridge action that could plausibly wedge the MSP UART mid-flight, and it is the one
case where §3.2's "bridge failure is harmless" reasoning is weakest.

Camera support claims shall be tied to tested bridge firmware versions.

---

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

The **supported-model list is an input to the compatibility matrix and shall be read per
release.** Newest HERO models are not automatically covered.

### 20.2 DJI Osmo Nano

**Status: Unsupported, pending the §27.4 probe.** The Nano is the intended DJI-family
target, but unlike the Osmo Action line it has **no official control protocol**, and this
specification does not assume one will appear.

#### Two protocol families

**1. The official "R SDK" protocol** — what DJI's own Osmo Action GPS Bluetooth Remote
speaks, with an MIT-licensed ESP32 reference at `dji-sdk/Osmo-GPS-Controller-Demo`
(ESP-IDF v5.5; ships `sdkconfig.defaults.esp32c3`). Its device-ID table covers:

| Model | device_id |
| --- | --- |
| Osmo Action 4 | `0xFF33` |
| Osmo Action 5 Pro | `0xFF44` |
| Osmo Action 6 | `0xFF55` |
| Osmo 360 | `0xFF66` |
| **Osmo Nano** | **"Not supported yet. Please wait for a future firmware update."** |

That quotation is DJI's own text, from `docs/protocol_data_segment.md` in the reference
repository. It is corroborated independently: DJI's published accessory compatibility list
marks the Osmo Action GPS Bluetooth Remote Controller as **not compatible** with the Osmo
Nano. The reference repository has had no commits since **2025-11-17**, so the promised
firmware update has been outstanding for the better part of a year and shall not be
planned around.

**2. DUML** — DJI's universal binary protocol, reverse-engineered by the community and
carried over the **same GATT profile** the official protocol uses: service `0xFFF0`,
notify `0xFFF4`, write `0xFFF5`. Published implementations exist for the Pocket 3
(`lib-osmo-ble`, updated 2026-02, full DUML framing with CRC8/CRC16) and for Action 3/4/5
plus Pocket 3 (`node-osmo`). **Neither covers the Nano.**

#### Working hypothesis

The Nano is expected to speak DUML, because DUML is DJI-wide and the Nano is
architecturally closer to a Pocket 3 than to an Action. **This is an inference and has not
been tested.** §27.4 tests it. Until that probe passes, the Nano is *Unsupported* under
this section's own policy and shall not be advertised as anything else.

Consequence for the product: the Nano gates **V1.1 only**. V1 ships on GoPro and does not
depend on any of this.

#### Hazard — silent write failure

Characteristic `fff3` accepts writes *with response* and returns success while the
firmware discards the payload. Only `fff5` (write-without-response) actually processes
DUML. A driver that writes `fff3` looks entirely correct and does nothing. This is
documented behaviour that already caught an existing library; the driver shall write only
`fff5`, and the probe shall confirm it.

#### Licence

The `LICENSE` file of DJI's reference repository is layered: the **R SDK protocol
documents** are under DJI's End User License Agreement, while the **demo software** is
offered under the EULA *and* the MIT License (© 2025 SZ DJI Technology Co., Ltd).

Because the Nano path is DUML rather than R SDK, DJI's MIT-licensed code is **not** the
basis of the Nano driver, and the EULA-encumbered protocol documents shall not be
redistributed. Should DJI later add the Nano to the official protocol, the official path
shall be preferred over DUML and this section revisited.

---

## 21. Performance and MSP Citizenship

### 21.1 FC polling

| Data | Rate | Command |
| --- | --- | --- |
| Arming state (and any bound `USER` modes) | 10 Hz | `MSP_STATUS` / `MSP2_INAV_STATUS` |
| Camera switches, default path (§12.2) | 10 Hz | `MSP_RC` (105) |
| Mode range / logic metadata | once at startup | `MSP_MODE_RANGES`, `MSP_MODE_RANGES_EXTRA` |
| Camera state | event driven | — |
| Record timer | 1 Hz | — |
| Camera battery | ≤ 1 Hz | — |
| Storage | ≤ 1 Hz | — |
| Resolution / FPS | on change | — |
| GPS (only if §14 push enabled) | ≤ 10 Hz | `MSP_RAW_GPS` |

`MSP_RC` is polled at **10 Hz, not v0.1's 10–20 Hz** — the fastest real event is a switch
flip, and 10 Hz is ample. Where every configured camera switch is bound to an exported
`USER` mode (§12.2, optional path), the `MSP_RC` poll shall be dropped entirely and
`MSP_STATUS` alone used. The bridge shall not poll `MSP_RC` for functions it is not
configured to use.

At 115200 with 16 channels this is well under 1 kB/s in total, but §21.2 still applies.

### 21.2 Quiet-bridge requirements — *new in v0.2*

The bridge is a guest on the FC's MSP port and shall:

* never hold the port busy waiting on a camera or BLE operation;
* tolerate a missing or late reply without blocking;
* never retry-storm — failed requests back off;
* never write an OSD slot whose rendered content is unchanged;
* issue no MSP traffic at all while in telemetry-disabled compatibility mode (§5.1), beyond
  identification.

### 21.3 BLE duty cycle and backoff — *new in v0.2*

Derived from §3.3. Concrete values are set by the §27.2 measurement; the requirements are:

* **Directed reconnect** to a known camera is preferred over discovery at all times (§15).
* **Reconnect backoff:** consecutive failures shall increase the retry interval to a
  configured ceiling of **≥ 5 s**, never a continuous scan.
* **Armed restriction:** broad/discovery scanning shall be suppressed while armed. Only
  directed reconnect to the paired camera, at the backed-off interval, is permitted.
* **Duty-cycle ceiling:** the bridge shall have a stated maximum BLE scan duty cycle in
  flight, and shall meet it in the reconnect-storm case as well as the nominal case.
* **Transmit power:** normal-operation BLE TX power shall be the **minimum validated level
  that maintains reliable camera communication**. The camera sits centimetres from the
  bridge, so the default maximum is far more power than the link needs, and reducing it
  attacks receiver desense (§3.3) and peak current (§4.2) at once.

  Two caveats keep this honest, and both are for §27.2 to resolve rather than this document
  to assert. First, the bridge controls only its own transmitter — the camera's BLE
  transmissions are equally in-band and equally close, and cannot be turned down. Second,
  reducing power too far raises the retransmission rate, which *increases* occupied airtime;
  the optimum is measured, not assumed. The chosen level shall be an output of §27.2.

---

## 22. Failure Requirements

The following shall not affect aircraft control, and each has a corresponding acceptance
test in §24.2:

* camera power-off;
* camera reboot;
* camera absent at boot;
* BLE disconnect;
* repeated BLE reconnect failures;
* unsupported camera;
* malformed camera response;
* bridge reboot;
* bridge watchdog;
* bridge held in reset;
* bridge UART failure, including TX held at either rail;
* bridge Wi-Fi failure;
* configuration corruption.

The FC shall continue operating normally with the bridge electrically connected but
completely non-functional. Per §3.3, "not affecting aircraft control" includes not
degrading control-link quality.

---

## 23. Installation Requirement

Target installation: **four solder joints and one spare UART.**

1. Connect 5 V, ground, TX and RX (observing the §4.2 peak-current note).
2. Enable MSP on the selected FC UART.
3. Enable and position the required OSD elements.
4. Configure camera switches for any non-default control mode (§12.2). The default
   arm-record mode needs none of this.
5. Power camera.
6. Press Pair.
7. Fly.

No CLI commands shall be required for the default supported configuration where the FC
configurator exposes all required settings.

---

## 24. Acceptance Matrix

A release shall not claim platform support until §24.1 **and** §24.2 pass.

### 24.1 Function

| Test | Betaflight 2025.12+ | INAV 8.x |
| --- | ---: | ---: |
| FC auto-detection | Required | Required |
| OSD capability probe / graceful degrade (§5.1) | Required | Required |
| MSP writes accepted while armed | Required | Required |
| No flash write across a long armed session | Required | Required |
| Arm-record | Required | Required |
| Camera switch record — bridge-owned AUX path (§12.2) | Required | Required |
| Camera switch record — bound `USER` mode, where the FC exports it (§12.2) | Required | Required |
| `USER` binding correctly **not offered** when the box is absent from `MSP_BOXIDS` | Required | Required |
| Camera switch record — bound Logic Condition (§12.2) | n/a | Required |
| Pilot's existing `USER1..4` / PINIO / Programming Framework configuration unmodified after a full session | Required | Required |
| Actual REC confirmation (§7) | Required | Required |
| Recording timer OSD | Required | Required |
| Camera battery OSD | Required | Required |
| Remaining record time OSD | Required | Required |
| Resolution/FPS OSD | Required | Required |
| Staleness → `CAM STALE` → `CAM LOST` (§17.1) | Required | Required |
| Slot ownership respected (§9.1, §10.2) | Required | Required |
| No FC firmware modification | Required | Required |
| Analog OSD | Required | Required |
| Digital OSD (MSP DisplayPort) | Required | Required |

### 24.2 Failure — *new in v0.2*

v0.1's matrix had fourteen rows and tested only functions. Every row below has **two** pass
criteria: **(a)** FC behaviour unchanged, and **(b)** control-link LQ/RSSI not measurably
degraded versus the bridge-absent baseline.

| Injected failure | Maps to §22 |
| --- | --- |
| Bridge held in reset for the whole flight | bridge reboot / reset |
| Bridge TX line held high, then held low | UART failure |
| Bridge power cycled mid-record | bridge reboot |
| Bridge watchdog forced | bridge watchdog |
| Camera absent at boot (never pairs) | camera absent |
| Camera powered off mid-record | camera power-off |
| Camera rebooted mid-flight | camera reboot |
| Camera carried out of BLE range and back | BLE disconnect |
| **Sustained BLE reconnect storm while armed** | repeated reconnect failures |
| Unsupported camera advertised nearby | unsupported camera |
| Malformed/truncated camera response injected | malformed response |
| Corrupted stored configuration | configuration corruption |
| Wi-Fi stack forced to fail at init | Wi-Fi failure |

### 24.3 State recovery

Two cases that are not failures of the bridge but of sequencing, and which the matrix above
does not reach:

| Scenario | Required behaviour |
| --- | --- |
| **FC reboots** while bridge and camera stay powered | Bridge re-identifies the FC (§5), re-resolves box IDs and mode ranges, reinstalls its runtime OSD state — which the FC lost, since none of it is persisted (§9, §10) — and resumes without re-pairing the camera |
| **Bridge reboots while the FC is armed** | Bridge performs **no discovery scan** (§21.3), reacquires FC state, and follows the armed reconnect policy before issuing any camera command or OSD write |
| **RX loss / failsafe with recording active** | Bridge holds last commanded camera state (§12.2.1); recording is not stopped by the failsafe switch positions, and resumes normal control on RX recovery |

The second is the sharper test: it is the only case that exercises the armed-scan
restriction on a cold-started bridge, which cannot know the aircraft is armed until it has
completed identification.

The reconnect-storm row is the one that most directly tests §3.3 and shall not be waived.

---

## 25. Architecture for Future ELRS Integration

The camera core shall not depend on MSP UART implementation details.

```text
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

Note that §3.3 applies *doubly* there: an integrated build shares not only the band but the
same MCU, scheduler and heap as the control link.

---

## 26. V1 Scope

v0.1's V1 was four products at once (two camera families × two FC families × Wi-Fi config ×
presets × clock sync). Re-cut:

### V1 — Required

* standalone ESP32-C3 bridge;
* Betaflight 2025.12+ **and** INAV 8.x backends;
* **GoPro BLE driver**;
* automatic directed reconnect with backoff (§21.3);
* arm-record and camera-switch record, both paths of §12.2;
* actual camera-state feedback (§7) with staleness handling (§17);
* the four OSD fields of §8;
* OSD warnings (§18);
* slot ownership configuration (§9.1, §10.2);
* one-button pairing;
* firmware updates (no OTA while armed);
* optional Wi-Fi configuration (inactive in flight, §16.1);
* §24.1 **and** §24.2 passing.

### V1.1 — conditional on the §27.4 probe

* **DJI Osmo Nano driver, over DUML** (§20.2). Unlike a re-host of DJI's vendor reference,
  this is unproven work: no official protocol support exists for this model and no
  published DUML implementation covers it. The probe runs independently of V1 and may
  return "not viable", in which case V1.1 either escalates to a Mimo-app BLE capture or
  substitutes an officially supported model (Osmo Action 6 / Osmo 360), which *would* be a
  low-risk re-host of the MIT-licensed reference.

### V1.2 — conditional by camera capability

* photo capture; preset switching (§13); HiLight/tag; camera clock and GPS push (§14);
  additional telemetry.

### Explicitly out of scope

* flight-controller firmware modification;
* VTX-specific integration;
* RC override, motor or servo control;
* required mobile app;
* ELRS-integrated production implementation.

---

## 27. Engineering Spikes Before Implementation

v0.1's single gate — whether stock INAV 8 can accept runtime camera data for its Custom OSD
Elements — is **answered** (§10). It is replaced by three ordered spikes, each with pass
criteria.

### 27.1 OSD injection on hardware

Confirm the source reading of §9 and §10 on real flight controllers.

Pass criteria:

* custom message / custom element writes accepted and rendered **while armed**, on both
  Betaflight and INAV;
* correct rendering on **analog** and on **MSP DisplayPort**;
* four fields independently updateable;
* **no flash write** observed across a long armed session (verify by configuration
  persistence check across power cycle, not by inference);
* graceful degrade confirmed on an INAV target built without `USE_PROGRAMMING_FRAMEWORK`,
  and on a Betaflight build older than 2025.12.

### 27.2 RF coexistence — the §3.3 release gate

Measure the bridge's effect on a 2.4 GHz ELRS control link using the existing passive RF
survey and Blackbox analysis tooling, which already slices noise floor by condition.

Conditions, all else held constant:

```text
bridge absent → bridge idle (paired, connected)
              → bridge active (recording, telemetry flowing)
              → bridge reconnect storm (camera powered off)
```

#### Metric: link margin, not LQ

**LQ and RSSI on a strong bench link are saturated metrics and will pass this test
trivially.** A BLE transmitter centimetres from the receiving antenna can consume several dB
of link margin while a close-range bench link still reports 100% LQ throughout. Measuring
only LQ would produce a confident, meaningless pass.

The acceptance metric is therefore the **change in ELRS input power required to hold a fixed
PER / LQ threshold**:

```text
Δ P_in  =  P_in(threshold, bridge active)  −  P_in(threshold, bridge absent)
```

measured by stepping a calibrated attenuator between transmitter and receiver to find the
LQ knee in each condition, and comparing where the knee sits. Conducted measurement via u.FL
is preferred where the receiver exposes it; controlled attenuation is acceptable.

Pass criteria:

* `Δ P_in` ≤ the measurement uncertainty of the setup, in the idle and active conditions;
* the reconnect-storm condition bounded by the §21.3 backoff and duty-cycle limits, and
  also within uncertainty;
* the BLE transmit power level of §21.3 established as an output of this measurement,
  including the retransmission-airtime trade-off noted there;
* the resulting numbers written back into §21.3 as concrete configured values.

The measurement uncertainty shall be **stated, not implied** — at minimum the attenuator
step size, which bounds resolution, and the repeat count. A pass criterion of "within
uncertainty" is unfalsifiable until that figure exists.

Both ELRS and BLE hop across the band, so the interference is statistical rather than
fixed-frequency. Each condition shall dwell long enough to average over both hop sequences;
a short capture can miss the collisions entirely.

Failing this gate does not merely delay a feature — it invalidates §3's claim that the
device is a passenger.

### 27.3 Camera protocol re-host — GoPro, V1

Port Open GoPro onto NimBLE-Arduino on ESP32-C3, behind the §6 driver interface.

Pass criteria:

* start/stop recording and full §8 telemetry from a physical camera;
* driver capability flags correctly gate unsupported features;
* combined firmware fits flash and meets the §16.1 heap gate measured with the AP up and a
  client attached.

### 27.4 Osmo Nano protocol probe — gates V1.1 only

Independent of V1 and of §27.1–27.3; may run immediately, and hardware is on hand. The
purpose is to answer one question cheaply — **does the Osmo Nano answer DUML at all?** —
before any driver is written.

1. **Advertisement.** Scan for the Nano and dump its manufacturer-specific data. DJI's
   reference identifies one of its cameras when manufacturer bytes 0, 1 and 4 are `0xAA`,
   `0x08`, `0xFA` (see `bsp_link_is_dji_camera_adv` in the reference's `ble.c`). Record
   whether the Nano matches that signature.
2. **GATT enumeration.** Connect and enumerate services. Confirm service `0xFFF0` with
   `0xFFF4` (notify) and `0xFFF5` (write-without-response).
3. **DUML liveness.** Subscribe to `fff4`; send a known-safe DUML query from the Pocket 3
   corpus over `fff5` — **never `fff3`**, per the §20.2 hazard — and check for a
   CRC-valid response.
4. **Control.** Attempt start/stop recording and a camera-status subscription.
5. **Official cross-check.** Send the R SDK connection-request frame and record the reply.
   A rejection carrying a `device_id` would both reveal the Nano's ID and indicate official
   support is nearer than the documentation suggests.

**Pass criteria and branches:**

| Outcome | Consequence |
| --- | --- |
| Steps 1–3 yield a CRC-valid response | DUML driver viable; Nano proceeds to V1.1 as **Expected** (§20) |
| Step 4 works on physical hardware | Nano promoted to **Verified** (§20) |
| Fails at step 2 or 3 | Nano is **Unsupported**; escalate to a DJI Mimo app BLE capture, or substitute Osmo Action 6 / Osmo 360 per §26 |

Record the result — including a negative result — against the Nano's firmware version.
§20's policy forbids promoting the model on protocol similarity alone, and the whole
premise of the DUML path is a similarity argument.
