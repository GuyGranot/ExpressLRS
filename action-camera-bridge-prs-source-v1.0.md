# Action Camera Bridge — Product Requirements Specification

**Status:** Final — implementation baseline. Requirements are frozen; further refinement
belongs in design and implementation documents.
**Minimum supported:** Betaflight **2025.12.5**, INAV **8.0.1** — also the behavioural
baseline for every citation (§0)
**Target family:** Betaflight 2025.12.5+, INAV 8.0.1+ within 8.x, subject to
forward-compatibility validation
**Initial camera families:** GoPro (V1), DJI Osmo Nano (V1.1, gated on §27.4)
**Architecture:** Standalone BLE-to-FC bridge over one spare UART
**Future architecture:** Optional ELRS-integrated implementation sharing the
camera/control/OSD core
**Distribution:** Personal / open-source

*Content-identical to PRS v0.9 apart from §16, which was rewritten around reboot-separated
`RUN` / `SETUP` / `DIAG` / `PAIR` boot modes with `SETUP` as the sole entry hub, the narrow
maintenance-interlock exception in §5.2, the §16.2.3 trigger-learning requirement, the cited
Betaflight/INAV passthrough-exit behaviour, and the consequential edits in §12.4.4, §15, §19,
§21.1, §21.3, §22, §23, §24, §25, §26, §27.5 and §27.6 (2026-08-26); revision history removed.*

---

## 0. Source baseline and citation rule

This specification's behavioural baseline is the minimum supported release, not `master`:

```
Betaflight   2025.12.5      ← all [BF] citations
INAV         8.0.1          ← all [INAV] citations
```

Citations marked **[BF]** are `betaflight/betaflight` at tag `2025.12.5`; **[INAV]** are
`iNavFlight/inav` at tag `8.0.1`. Line numbers were read from those tags on 2026-08-25, and
the §12.4 control-path and §5.1 version-floor citations re-read on 2026-08-26. Forward
compatibility against `master` is a separate audit, and its findings shall be recorded as
version notes, never by silently re-pointing a citation.

**Version contract.** The baseline and the support floor are the same versions, and the
document shall not describe them in two ways:

```
Minimum supported:   Betaflight 2025.12.5      INAV 8.0.1
Target family:       Betaflight 2025.12.5+     INAV 8.0.1+ within 8.x
```

Earlier patch releases in those lines — 2025.12.1 through .4, or 8.0.0 — are **not**
supported, and shall not be described as such by writing `2025.12+` or `8.x`. The range starts
at `.1` because Betaflight published no `2025.12.0`: that line's GA tags are 2025.12.1 …
2025.12.5, preceded only by `2025.12.0-RC1` … `-RC4` (§9). That loose
form is what permits a graceful-degrade test to be written against "a build older than
2025.12", which is a different question from the one this product needs answered. Newer
releases are targeted but require forward-compatibility validation before support is
claimed (§27.1).

**Standing rule.** Line numbers are the part of this document most likely to rot, and the
part most likely to be regenerated from memory rather than read. Re-verification before
each release means opening the file at the pinned tag, not recalling it — and a citation
that is right on `master` is still wrong here. Reading source also means reading the code,
not the comments: Betaflight's `failsafe.c` contains comments that its own reset template
contradicts (§12.4.2).

Three claims that hold on `master` and fail on the supported baseline are recorded as
normative platform facts in the sections that use them: `MSP_MODE_RANGES_EXTRA` is
Betaflight-only (§12.6), `MSP2_INAV_LOGIC_CONDITIONS_CONFIGURED` does not exist in INAV
8.0.1 (§12.7), and INAV 8.0.1 carries armed guards on several MSP commands (§10).

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

No phone, camera app, or bridge configuration interface is required for normal use. Normal
operation shall require no interaction with the bridge itself.

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

**MSP command policy is closed by default.** The bridge shall send only the commands listed in
the allowlist below. **Every other MSP command is prohibited, whether or not its side effects are
currently known**, and an implementation shall have no code path capable of emitting one. Adding a
command to this table is a specification change requiring the §3.1.1 evidence for it; it is not an
implementation decision.

This is deliberately the inverse of an enumerated-hazards rule, and the reason is empirical. A
prohibition list can only forbid what its author already knows to be dangerous, and the audit
behind this revision found two commands that no reasonable denylist would have contained:
`MSP_SET_OSD_CANVAS` writes flash *and reboots the FC* with no `MSP_EEPROM_WRITE` and no armed
guard, and `MSP_RTC` is a **read on one platform and one digit away from a write on the other**
(§3.1.2). Under a closed policy neither is reachable, because neither is listed — no foresight
required.

#### 3.1.1 The allowlist — normative

Every entry is a command the bridge is permitted to originate. IDs are given per platform because
they are **not** always the same on both (§3.1.2). Anything not in this table is prohibited.

| Command | BF | INAV | Dir | When the bridge may send it | § |
| --- | --- | --- | --- | --- | --- |
| `MSP_API_VERSION` | 1 | 1 | read | FC identification | §5 |
| `MSP_FC_VARIANT` | 2 | 2 | read | FC identification | §5 |
| `MSP_FC_VERSION` | 3 | 3 | read | version floor, before any capability probe | §5.1 |
| `MSP_RC` | 105 | 105 | read | steady-state control input | §12.2 |
| `MSP_STATUS` | 101 | — | read | Betaflight validity bracket only | §12.4.1 |
| `MSP2_INAV_STATUS` | — | 0x2000 | read | INAV qualification; stops when qualified | §12.4.4 |
| `MSP_FAILSAFE_CONFIG` | 75 | — | read | once per FC detection, condition B | §12.4.3 |
| `MSP_BOXIDS` | 119 | — | read | once at identification, **only** where a Maintenance Entry AUX is configured | §5.2, §16 |
| `MSP_MODE_RANGES` | 34 | 34 | read | setup-time overlap advisory only | §12.6 |
| `MSP_MODE_RANGES_EXTRA` | 238 | — | read | setup-time overlap advisory only | §12.6 |
| `MSP2_INAV_LOGIC_CONDITIONS_STATUS` | — | 0x2026 | read | where a Logic Condition trigger is configured | §12.7 |
| `MSP2_INAV_GVAR_STATUS` | — | 0x2027 | read | where a GVAR trigger is configured | §12.7 |
| `MSP2_INAV_CUSTOM_OSD_ELEMENTS` | — | 0x2100 | read | OSD capability probe | §5.1, §10 |
| `MSP_RTC` | **247** | **246** | read | only where §14 time push is enabled — **see §3.1.2** | §14 |
| `MSP_RAW_GPS` | 106 | 106 | read | only where §14 GPS push is enabled | §14 |
| `MSP_OSD_CANVAS` | 189 | — | read | optional; **never** 188 | §9 |
| **`MSP2_SET_TEXT`** | **0x3007** | — | **write** | Betaflight OSD injection | §9 |
| **`MSP2_INAV_SET_CUSTOM_OSD_ELEMENTS`** | — | **0x2102** | **write** | INAV OSD injection | §10 |

**There are exactly two permitted writes, and both are OSD text injection.** That is the whole of
what this product writes to a flight controller. Every other row is a read. If an implementation
holds a third write path, it has left the specification — and the two-write property is what §3's
"the bridge is a passenger" claim actually rests on, so it is stated as a number rather than
implied by a list of things that are absent.

#### 3.1.2 Command IDs are not portable — required

`MSP_RTC` is **247 on Betaflight and 246 on INAV**, and each platform assigns the other's value to
`MSP_SET_RTC` ([BF] `msp/msp_protocol.h:269-270`; [INAV] `msp/msp_protocol.h:301-302`):

| ID | Betaflight `2025.12.5` | INAV `8.0.1` |
| --- | --- | --- |
| 246 | `MSP_SET_RTC` — **in message, sets the FC clock** | `MSP_RTC` — out message |
| 247 | `MSP_RTC` — out message | `MSP_SET_RTC` — **in message, sets the FC clock** |

A single shared `MSP_RTC` constant therefore **sets the flight controller's clock** on whichever
platform it is wrong for. This is not a decode failure that surfaces as a bad timestamp; it is a
configuration write issued by a product that promises to issue none. §14 notes that the two
*response* formats differ, which makes the request look shared — it is not.

Consequently: **the allowlist is per platform, and command IDs shall be resolved through the
platform adapter (§25), never through a shared constant.** A constant used on both platforms shall
be proven identical on both at the pinned tags before it is shared.

#### 3.1.3 Why an implementer reaches for the prohibited ones — rationale, not mechanism

The closed policy above is the mechanism, and it stands alone. The following is retained because
knowing *why* each is tempting is what stops someone proposing it in review; none of it is load
bearing, and nothing here narrows §3.1.1.

- **RC injection** — `MSP_SET_RAW_RC`, `MSP2_INAV_SET_AUX_RC`. The apparently clean way to make
  the camera "just follow a switch". It writes the pilot's control state.
- **Mode-range writes** — `MSP_SET_MODE_RANGE` (35, [BF] `msp/msp_protocol.h:107`). The apparent
  way to bind a camera action to a box mode.
- **Programming Framework writes** — `MSP2_INAV_SET_LOGIC_CONDITIONS` (0x2023),
  `MSP2_INAV_SET_GVAR` (0x2214). Not RC state, so not covered by the RC-injection reasoning at
  all; a GVAR write is the obvious "helpful" shortcut and reaches silently into the pilot's flight
  programming.
- **`MSP_SET_OSD_CANVAS`** (188, [BF] `msp/msp_protocol.h:231`) — writes `osdConfig` and
  `vcdProfile`, then calls `writeEEPROM()` **and `systemReset()`** where the FC was not already in
  HD + MSP-DisplayPort mode, with no `MSP_EEPROM_WRITE` and **no armed guard** ([BF]
  `msp/msp.c:4391`, `:4404-4405`). It is one digit from the permitted read `MSP_OSD_CANVAS` (189)
  in both name and ID, and an implementer fetching canvas dimensions to format §8 fields is one
  keystroke from an in-flight FC reset.
- **`MSP_SET_RTC`** — see §3.1.2; reachable by getting one constant backwards rather than by
  intending anything.
- **`MSP_EEPROM_WRITE`, `MSP_REBOOT`**, motor, servo, arming, flight-mode and PID/configuration
  commands — dangerous for reasons that need no elaboration.

**Reads of configuration are permitted; writes are not.** That is why `MSP_FAILSAFE_CONFIG`
(§12.4.3) and INAV's `armingFlags` (§12.4.4) appear in the allowlist despite naming subsystems this
document otherwise stays clear of. Reading a pilot's failsafe timing so as not to depend on its
default value is the opposite of interfering with it.

### 3.2 Electrical isolation

A bridge crash, reset, watchdog event, brownout, UART failure or camera failure shall leave
flight-controller behavior unchanged. The FC shall continue operating normally with the
bridge electrically connected but completely non-functional, including with the bridge's TX
line held at either rail.

### 3.3 RF coexistence

This is the only path by which a correctly-isolated passenger device can still endanger a
flight.

The standalone bridge contains its own ESP32-C3 2.4 GHz BLE transmitter. A 2.4 GHz ELRS
receiver on the same aircraft is a separate radio, but the two antennas may be only
centimetres apart. BLE transmission or scanning can therefore create in-band desense even
though the bridge is logically and electrically outside the control path.

The adverse case is camera loss: a naive implementation may respond by scanning more
aggressively exactly when the bridge is already degraded. **Degraded bridge operation must
not become degraded control-link operation.**

The RF policy is deliberately independent of FC arm state. The bridge is not required to
know whether the FC is armed, and RF safety shall not depend on that information being
available or correct.

Requirements:

1. The bridge shall bound BLE transmit and scan duty cycle (§21.3).
2. Runtime reconnect to a paired camera shall be **directed** and shall back off rather than
   scan continuously (§15, §21.3).
3. Broad/discovery scanning shall occur **only after an explicit user Pair/Setup action**. It
   shall never be entered autonomously because a paired camera is absent or disconnected.
4. BLE transmit power shall be treated as a coexistence parameter and minimized to the
   lowest validated level that maintains reliable camera communication (§21.3, §27.2).
5. The bridge shall be shipped only after its effect on control-link sensitivity/link margin
   has been **measured**, not asserted (§24.2, §27.2).

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
guidance shall state the peak figure and the required local bulk capacitance, because the FC
5 V rail is frequently shared with the camera itself.

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

### 5.1 Version floor, then capability probing

The version contract in §0 is normative, not advisory. A firmware release below the
supported minimum shall be **refused**, not partially accepted.

> **Version floor.** After FC identification, and **before** capability probing, the bridge
> shall verify the minimum supported firmware version. Betaflight below **2025.12.5** and
> INAV below **8.0.1** shall enter the `UNSUPPORTED_FC_VERSION` state. In that state no
> camera-control input shall be acted on and no platform-specific OSD write shall be issued.
> Identification, setup, diagnostic and firmware-update functions remain available, and the
> state shall be surfaced to the user (§16) with the detected and required versions.

The order is fixed, because a capability probe answered by an out-of-contract build is not
evidence that the build behaves as this document describes:

```
identify FC  →  supported variant?  →  version floor?  →  capability probe  →  normal backend
```

**Why the floor gates camera control and not only OSD writes.** Every behavioural claim in
§12 — the failsafe guard's assertion paths, `rxDataFailurePeriod`, the recovery delay, the
`armingFlags` bit position — is read from the pinned baselines and is verified for those tags
only (§0). Acting on control input from an unverified build means acting on a control path
whose failsafe semantics have not been checked, which is the precise defect §0 exists to
prevent. A bridge that does nothing is a correct bridge; a bridge that records on an
unverified failsafe path is not.

**What is compared.** Both platforms answer `MSP_FC_VERSION` with three unsigned bytes. The
bridge shall compare that byte triple lexicographically against the floor and shall **not**
parse the version string.

```
Betaflight 2025.12.5  →  25, 12,  5     [BF]   msp/msp.c:642-647, build/version.h:28-35
INAV       8.0.1      →   8,  0,  1     [INAV] fc/fc_msp.c:369-373
```

Three properties of that encoding are load-bearing, and each is a way to get the gate wrong:

- Betaflight sends `FC_VERSION_YEAR - FC_CALVER_BASE_YEAR`, with the base year 2000 ([BF]
  `build/version.h:28`). The first byte of a 2025 release is **25, not 2025** — a gate
  comparing it against a literal `2025` refuses every supported build, and one comparing
  against `20` accepts a hypothetical 2020 release.
- Betaflight 4.5 and earlier were semantically versioned and placed `4` in that byte, so the
  triple is **well-ordered across the calendar-versioning transition** (4 < 25). A single
  lexicographic compare against `(25, 12, 5)` is therefore correct for every Betaflight the
  bridge can meet, with no special case for the version scheme.
- Betaflight appends a Pascal-string version after the three bytes ([BF] `msp/msp.c:646`);
  INAV does not. A parser that requires a fixed payload length fails on one platform or the
  other.

**Capability probing** runs only on a build that has passed the floor. The bridge shall not
infer OSD capability from the version alone.

- **INAV:** issue `MSP2_INAV_CUSTOM_OSD_ELEMENTS` (0x2100). The FC replies with
  `MAX_CUSTOM_ELEMENTS`, `OSD_CUSTOM_ELEMENT_TEXT_SIZE - 1`, and `CUSTOM_ELEMENTS_PARTS`
  ([INAV] `fc/fc_msp.c:1732-1737`). A missing or error reply means the target was built
  without `USE_PROGRAMMING_FRAMEWORK` and has no custom elements at all — this is the correct
  trigger for telemetry-disabled compatibility mode. `USE_PROGRAMMING_FRAMEWORK` is enabled
  by default ([INAV] `target/common.h:172`) but small flash targets may undefine it.
- **Betaflight:** there is no runtime capability probe, and none is required. On a build that
  has passed the version floor the custom-message slots are unconditional: the
  `MSP2_SET_TEXT` handler writes `pilotConfig.message[]` with no `USE_OSD` guard on that case
  ([BF] `msp/msp.c:4080-4090`; only the craft/pilot-name re-analysis at `:4101-4104` is
  guarded). Acceptance of the write is therefore **not** evidence that the target renders an
  OSD at all; that is confirmed once on the bench (§27.1), not at runtime. The version floor
  is the whole programmatic test on this platform.

The two degraded states are distinct and shall not be conflated:

| State | Trigger | Camera control | Platform OSD writes |
| --- | --- | --- | --- |
| `UNSUPPORTED_FC_VERSION` | §5.1 floor not met | **suppressed** | none |
| telemetry-disabled compatibility | supported version, no OSD capability | active | none |

Unsupported FC variants shall enter telemetry-disabled compatibility mode and shall not send
platform-specific OSD writes.

The bridge shall track FC family, FC firmware version, MSP/API version and available OSD
capabilities.

### 5.2 FC operational state is deliberately not a control input

The bridge shall **not** query or track actual FC armed/disarmed state for camera control,
BLE reconnect policy or setup availability. One narrowly drawn maintenance interlock is
permitted, and only one — §16's Maintenance Entry AUX gesture, specified below.

This is a deliberate product boundary:

- camera controls represent **configured RC intent**, not whether the FC accepted that
  intent;
- arming authorization can be denied for reasons unrelated to the camera (prearm, throttle,
  GPS, calibration, failsafe, configuration, etc.);
- coupling camera behaviour to those decisions adds platform-specific semantics without
  improving camera-state truth;
- a passenger device should not need to interpret the aircraft's operational state.

Accordingly, no `MSP_BOXIDS` ARM lookup is part of the V1 **control** contract. Where §16's
optional maintenance gesture is configured, Betaflight requires that lookup once during
identification in order to locate the ARM box bit; it is absent from every build in which no
Maintenance Entry AUX has been selected, which is the default.

**The rule:**

> **Actual FC armed state is never a camera-control or camera-policy input.** INAV may
> inspect the `ARMED` bit only while control input is `UNQUALIFIED`, and solely to determine
> whether `ARMING_DISABLED_RC_LINK` is currently being maintained. This can delay
> qualification after a bridge reboot in flight, but it never changes what a camera control
> means or what the camera does in response to one.

The distinction that matters is between an input and a gate:

```
forbidden:  armed state changes what a control means, or what the camera does with it
permitted:  armed state determines whether another reading is fresh enough to act on
permitted:  armed state gates entry to a maintenance mode that has no camera actuation
```

**The third line is §16's gesture interlock, and it is deliberately narrow.** It reads armed state
only while a maintenance window is open, only when a Maintenance Entry AUX has been configured, and
it decides one thing: whether an operator's transmitter gesture may restart the bridge into
`SETUP`. No camera control consults it, no camera behaviour differs because of it, and with the
gesture unconfigured it does not execute at all. It exists because the alternative is worse: the
bridge is powered from the FC's 5 V rail (§4.2), so an in-flight rail interruption presents as a
power-on reset, and without the interlock a mid-flight switch pattern could restart the bridge into
a mode that raises a Wi-Fi AP — the condition §3.3 exists to gate.

`ARMING_DISABLED_RC_LINK` lives inside the `else` branch of `if (ARMING_FLAG(ARMED))` in
`updateArmingStatus()` ([INAV] `fc/fc_core.c:200-225`), so while armed it is frozen at
whatever value it held at the arming instant. Reading `ARMED` is therefore a **staleness
qualifier on another reading**, in the same category as a timestamp.

The rule is deliberately the weaker one. It is not true that no camera behaviour anywhere
differs by armed state: §12.4.4 makes an INAV bridge that reboots while armed stay
`UNQUALIFIED` until disarm, which is observably different behaviour. What is invariant, and
what §24.1 tests, is the mapping itself — a given control in a given range produces the same
camera action regardless of armed state. Armed state can delay the bridge starting to act. It
can never change what acting means.

"Arm Record" (§12.1) means that the Record AUX control is configured to the same radio
channel/range normally used by the pilot's arm switch. If the switch enters that range but the
FC remains disarmed, the camera still follows the configured Record control. This is
intentional.

The only FC status metadata used by the control path is a Betaflight-specific RX-loss guard
needed to reject failsafe-substituted AUX values (§12.4). That guard is about input validity,
not arming state — but note that Betaflight implements it *as* an arming permission flag,
which is why §12.4 must specify its edges rather than treat it as a clean boolean.

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

Unsupported information shall remain **unknown**, not fabricated. Capability flags shall gate
both behaviour and UI: GoPro-specific functionality shall not appear for DJI cameras or vice
versa.

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
gone". Thresholds are in §17.

## 8. OSD Requirements

Field content is constrained by verified per-platform limits (§9, §10). Because INAV
upper-cases all custom element text at render time ([INAV] `io/osd/custom_elements.c:215`),
the bridge shall compose all OSD strings in uppercase on every platform, so Betaflight and
INAV render identically.

Minimum common dataset — four fields, each ≤ 15 characters so one layout satisfies both
platforms:

| # | Field | Examples | Max used |
| --- | --- | --- | --- |
| 1 | Camera state + timer | `REC 01:23`, `VID READY`, `CAM LOST`, `CAM STALE` | 12 |
| 2 | Camera battery | `CAM BAT 72%` | 11 |
| 3 | Remaining record capacity | `REC LEFT 38M` | 12 |
| 4 | Capture configuration | `4K100`, `1080P240`, `5.3K25` | 8 |

If a camera does not report a value, that field shall be blank or omitted. No estimated value
may be presented as confirmed camera telemetry.

Where a platform offers more slots than the minimum (INAV, §10), additional optional fields
may be presented: camera temperature, storage remaining, active preset, warnings.

## 9. Betaflight OSD Backend

**Minimum version:** Betaflight **2025.12.5** (§0), enforced by the §5.1 version floor before
any write in this section is issued. The custom-message feature merged **2024-12-22** (PR #14097,
*"support for custom OSD messages from external device"*), but feature availability is not the
contract: the supported floor is the .5 patch release, which is also the citation baseline.
**2025.12.1 through .4 carry the feature and are still `UNSUPPORTED_FC_VERSION`** — verified
present in `msp/msp.c` at each of those tags. The range starts at `.1` because **there is no
`2025.12.0` release**: the GA series is 2025.12.1 … 2025.12.5, preceded only by
`2025.12.0-RC1` … `-RC4`. A version floor is a comparison against real releases, so the document
shall not name one that was never tagged.

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
`msp.c` apply to unrelated commands. Writes are accepted while armed. (Confirm on hardware per
§27.1 before relying on this.)

**Persistence:** writes land in the `pilotConfig` parameter group in RAM, and this handler itself
never touches flash ([BF] `msp/msp.c:4052` ff.). The general rule that *only* `MSP_EEPROM_WRITE`
persists is **not** true of Betaflight and shall not be relied on: `MSP_SET_OSD_CANVAS` writes flash
and resets the FC by itself (§3.1). What holds is the narrower and sufficient statement — **no
command the bridge is permitted to send causes a flash write**, which is a property of §3.1's
prohibition list rather than of the MSP protocol, and is what §24.1's no-flash-write row verifies.

**But the bridge is not the only client.** `pilotConfig_t` carries
`char message[OSD_CUSTOM_MSG_COUNT][MAX_NAME_LENGTH + 1]` and is a parameter group ([BF]
`pg/pilot.h:29-35`), so a Configurator or CLI `save` performed by the user persists whatever
the bridge last wrote — exactly as on INAV. Betaflight is **not** the safer platform here. See
§10 for the shared requirement.

The user shall enable and position OSD elements `Custom Message 1..4`. No custom Betaflight
build shall be required.

### 9.1 Slot ownership

The four custom-message slots are a shared global resource. Another peripheral (a lap timer, a
radar, a rangefinder) writing the same slot will silently fight the bridge for it.

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

**Minimum version:** INAV **8.0.1** (§0), within the 8.x family, enforced by the §5.1 version
floor before any write in this section is issued. INAV 8.0.0 answers the capability probe
successfully and is nonetheless `UNSUPPORTED_FC_VERSION` — the probe reports what the target
was built with, never whether the build is inside the support contract. INAV support is a
release-blocking requirement, not an optional compatibility mode.

Runtime injection into INAV Custom OSD Elements is confirmed available: the mechanism exists,
is reachable while armed, and does not touch flash. No `MSP_SET_NAME` → Craft Name fallback is
required or specified.

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
| Parts per element | `CUSTOM_ELEMENTS_PARTS = 3` | [INAV] `io/osd/custom_elements.h:23` |
| Text size | `OSD_CUSTOM_ELEMENT_TEXT_SIZE = 16` (15 usable + NUL) | [INAV] `io/osd/custom_elements.h:22` |
| Handler | `mspFcProcessInCommand` | [INAV] `fc/fc_msp.c:3519` |
| Build gate | `USE_PROGRAMMING_FRAMEWORK` (default on) | [INAV] `target/common.h:172` |

### 10.1 Payload format

Exactly **28 bytes**. The handler enforces
`dataSize == (OSD_CUSTOM_ELEMENT_TEXT_SIZE - 1) + (CUSTOM_ELEMENTS_PARTS * 3) + 4`
([INAV] `fc/fc_msp.c:3521`) — short writes are rejected with `MSP_RESULT_ERROR`.

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

(Type and visibility enums: [INAV] `io/osd/custom_elements.h:26-63`; `CUSTOM_ELEMENT_TYPE_TEXT` at
`:28`, `CUSTOM_ELEMENT_VISIBILITY_ALWAYS` at `:60`.)

**Whole-element rewrite.** Every SET replaces parts, visibility and text together. The bridge
shall cache complete element state and resend it intact on each update; it cannot patch one
field.

**Armed behaviour:** the custom-element setter carries **no** `ARMING_FLAG(ARMED)` guard —
[INAV] `fc/fc_msp.c:3519-3540` — and is therefore reachable while armed. (Confirm on hardware
per §27.1.)

INAV 8.0.1 does carry armed guards on other MSP commands, at [INAV]
`fc/fc_msp.c:1434,1894,2617,2628,2635,2643,2651,3316,3324`. None of them covers
`MSP2_INAV_SET_CUSTOM_OSD_ELEMENTS`, so the conclusion holds — but the claim shall be stated as
"this handler is unguarded", never as "INAV guards only reboot". The latter would license
writes that are in fact blocked.

**Persistence:** the handler writes `osdCustomElementsMutable(n)` — the parameter group in RAM.
No flash write.

**Persisted camera text is a telemetry-truth problem, not a flight-safety one.** If the user
saves configuration from the Configurator, whatever the bridge last wrote becomes persisted.
That is harmless to flight control and is **not** harmless to the §7 truth model:

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
non-authoritative content: blank, or a connection-state string such as `CONNECTING`. This runs
on every bridge start, whether or not a camera is expected, and it is the only write the bridge
makes before it has camera state to report.

A bridge that never comes up cannot clear anything, so the residual case — persisted text plus
a dead bridge — remains. It is bounded by not saving configuration with live camera text on
screen, which §23 shall note; it is not otherwise solvable from this side.

**Rendering:** text is upper-cased (`sl_toupper`, [INAV] `io/osd/custom_elements.c:215`).

### 10.2 Parity and slot ownership

INAV offers 8 × 15 characters against Betaflight's 4 × 16. Functional parity with §8 is
therefore comfortably met, with room for the optional fields in §8. Exact visual layout need
not match Betaflight.

Element ownership is configurable exactly as in §9.1 (default: elements 1–4), since INAV custom
elements are equally a shared resource with the Programming Framework.

### 10.3 Hard constraint

INAV support shall require: stock **INAV 8.0.1 or a later validated INAV 8.x release** (§0,
§5.1); no custom target; no firmware patch; no special FC hardware; only the same MSP UART used
by the bridge. It shall not abuse RC channels, RSSI, aircraft battery, altitude or navigation
values.

## 11. Video-System Independence

The bridge shall inject into the FC's OSD model rather than communicate with the VTX. Camera
telemetry is therefore compatible with any OSD output the selected FC supports — DJI, HDZero,
Walksnail/Avatar, and MSP DisplayPort. No VTX-specific bridge firmware shall be required.

## 12. Recording Control

Camera controls are mapped directly to AUX channels and value ranges in the **bridge**
configuration. This control model is intentionally independent of FC mode configuration **and
of actual FC armed state**:

- `USER1..USER4`, PINIO, Camera Control modes, and other FC box modes are not required and are
  never bound automatically to a camera control;
- existing FC mode/PINIO configuration is never modified, and is read only for the setup-time
  advisory of §12.6;
- the bridge shall never issue `MSP_SET_MODE_RANGE`, `MSP_SET_RAW_RC`, `MSP2_INAV_SET_AUX_RC`,
  RC override/overlay commands, or any equivalent command that changes FC RC state (§3.1);
- Betaflight and INAV share one bridge-side AUX/range evaluation implementation;
- actual FC armed/disarmed state is not part of the camera-control model (§5.2).

Each configured control consists of an AUX channel plus one or more non-overlapping value
ranges. The bridge evaluates the latest `MSP_RC` sample locally. Configuration may expose common
2-state and 3-state presets, but the stored representation shall be **explicit ranges** so
unusual radio endpoints remain supported.

### 12.1 Control modes

1. **Arm Record (default convenience preset)** — a normal **level-controlled** Record mapping
   whose AUX channel/range is normally set to the same channel/range used by the pilot's radio
   arm switch. Entering the range requests start recording; leaving it requests stop. The
   bridge does not inspect whether the FC actually armed. A rejected FC arm may therefore still
   start the camera, by design.
2. **AUX Toggle** — one configured AUX range is active → record; outside that range → stop. Arm
   Record is functionally this mode with a conventional setup choice.
3. **Push Button** — entry into a configured AUX active range is a momentary event that toggles
   recording or triggers the configured action, subject to §12.3.
4. **Combi** — a level control provides the base intent while a Push Button provides a manual
   override. Arbitration is specified in §12.1.1 and is **not** left to the implementation.

In all modes, displayed state follows camera acknowledgement (§7), never the input.

#### 12.1.1 Combi arbitration — required

Prose alone permits two conforming implementations to behave completely differently: with the
level control held in its record range, does a button-stop persist, or does the next 10 Hz
evaluation immediately restart recording? The following is normative.

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

1. `manualStop` shall not restart recording by itself; it can only subtract from `baseIntent`.
   A Combi button cannot start a recording the level control is not asking for. Where a user
   wants button-only start, that is Push Button mode (§12.1, mode 3), not Combi.
2. `manualStop` is **camera-control state, not input state.** It is therefore part of what
   §12.4 freezes, and it survives an RC-validity freeze intact. It shall not be cleared by
   entering, or by leaving, a freeze.
3. `manualStop` shall be cleared on bridge reboot, so that a level control alone determines
   state after a power cycle (§12.1, recoverability).
4. Where a Combi button is mapped to an action other than stop/restart (for example HiLight),
   that action is stateless and does not participate in this arbitration.

**Level control is the specified default for the Record function, and the reason is
recoverability.** A level control reconciles to the correct state after any interruption — RC
loss, bridge reboot, or the Betaflight recovery window of §12.4. An edge-triggered control
cannot: an event that occurs while the bridge is not accepting transitions is gone. Push Button
remains available and supported, but shall not be the default for Record.

**Setups without an arm switch.** Both platforms have a path that arms with no arm-switch AUX
channel configured, and the platform attribution is the opposite of the common belief:
**Betaflight has not removed stick arming, and INAV has none.**

| Platform | Path that needs no arm switch | Enabled when | Default |
| --- | --- | --- | --- |
| Betaflight `2025.12.5` | **yaw-stick arming** — throttle low, yaw high, pitch and roll centred, held for `ARM_DELAY_MS`, then `tryArm()` ([BF] `fc/rc_controls.c:225`, `:226`, `:229-230`) | `enable_stick_arming` is `ON` **and** no `ARM` mode range is configured ([BF] `fc/rc_controls.c:430`, `cli/settings.c:1730`); additionally suppressed while the `STICK COMMAND DISABLE` mode is active ([BF] `fc/rc_controls.c:225`) | `OFF` ([BF] `config/config.c:121`) |
| INAV `8.0.1` | **fixed-wing auto-arm on throttle** above `min_check` ([INAV] `fc/rc_controls.c:215-219`) | `fixed_wing_auto_arm` is `ON`, the model is an airplane, and motorstop is enabled — INAV's own description adds *"Requires enabled motorstop and no arm switch configured"* | `OFF` ([INAV] `fc/settings.yaml:1545-1548`) |

INAV `8.0.1` arms only from `BOXARM` ([INAV] `fc/rc_controls.c:213`, `:223-225`) or the fixed-wing
path above. Its stick-command block is reached only when disarmed ([INAV] `fc/rc_controls.c:249-250`)
and, past the comment that introduces it ([INAV] `fc/rc_controls.c:258`), handles calibration,
profile selection and trim only — no arming. Betaflight's `isUsingSticksToArm` initialises `true`
([BF] `fc/rc_controls.c:71-72`) and is resolved once at `rcControlsInit()` ([BF]
`fc/rc_controls.c:430`); its own code treats stick arming as a live, less-safe configuration rather
than a removed one — for example launch control requires switch arming explicitly ([BF]
`fc/core.c:201`).

**The requirement is unchanged by the correction**, because it never depended on which platform:
a pilot with no `ARM` mode range configured has no arm-switch AUX channel to map, so the Arm Record
preset (§23) does not apply to them, and they shall configure a Record control on any other AUX
channel. Both paths above are **non-default on both platforms**, so Arm Record remains the correct
documented common case. Setup documentation shall state the exception rather than presenting Arm
Record as universally available, and shall not attribute it to the wrong platform.

### 12.2 Reading RC values

The bridge reads the FC's current RC values using standard `MSP_RC` (105 — [BF]
`msp/msp_protocol.h:176`) on both platforms:

| Platform | Serialization | Source |
| --- | --- | --- |
| Betaflight | `rcData[0 .. rxRuntimeState.channelCount-1]` | [BF] `msp/msp.c:1317-1321` |
| INAV | `rxGetChannelValue(0 .. rxRuntimeConfig.channelCount-1)` | [INAV] `fc/fc_msp.c:621-625` |

The **response length**, not a hard-coded channel count, determines how many channels are
available.

The first four channels are the primary flight controls; subsequent channels are addressed to
the user as AUX1, AUX2, … . **The bridge shall accept only AUX channels as camera-control
inputs; channels 1–4 are out of scope for control mapping.** This avoids platform-specific
AETR/AERT primary-channel ordering entirely.

**Numeric note.** Betaflight's `rcData` is `float` ([BF] `rx/rx.c:128`) serialized through
`sbufWriteU16`, i.e. truncated. Bridge range comparisons may therefore differ by 1 µs from the
FC's own arithmetic at an exact boundary, and no operational meaning shall be placed on a
single microsecond boundary.

#### 12.2.1 Range evaluator — required semantics

1. **Hysteresis is ±10 µs** around each configured range boundary, applied as a Schmitt
   trigger: a channel is considered to have *entered* a range at `min + 10` / `max - 10`, and
   to have *left* it at `min - 10` / `max + 10`. The value is configurable; 10 µs is the
   default and the tested value.
2. **The bound is not arbitrary.** Betaflight quantises mode ranges to a 25 µs grid —
   `MODE_STEP_TO_CHANNEL_VALUE(step) = 900 + 25 * step`, [BF] `fc/rc_modes.h:101`. A hysteresis
   of ±25 µs would span a full step and make two adjacent ranges mutually ambiguous; ±10 µs
   stays safely under half a step, so a user who lays bridge ranges out on the same grid as
   their FC modes cannot create an overlap by enabling hysteresis.
3. **Minimum range width.** Setup shall reject any configured range narrower than
   `4 × hysteresis` (40 µs at the default), and shall reject two ranges on the same channel
   whose boundaries are closer than `2 × hysteresis`.
4. Hysteresis applies to the evaluator only. The stored configuration remains the explicit
   range the user entered, so a displayed range always matches what was typed.

### 12.3 Push Button timing

`MSP_RC` is polled, so a momentary switch shorter than the sample interval can be missed
entirely or observed once at an arbitrary phase. The specification shall not leave this to the
implementation.

- The bridge shall require a configured AUX range to be observed active on **at least two
  consecutive samples** before emitting a Push Button event.
- The documented minimum hold time shall be **250 ms**, and setup shall state it to the user.
- Consecutive Push Button events shall be separated by a minimum interval (default 400 ms) so a
  single press cannot double-toggle.
- When any Push Button control is enabled, `MSP_RC` shall be polled at 20 Hz rather than 10 Hz
  (§21.1).
- **A Push Button press that occurs while camera-control transitions are frozen is lost.** On
  Betaflight this includes the recovery window of §12.4, which persists for
  `failsafe_recovery_delay` after valid RC data returns — an FC-configured value the bridge
  neither reads nor needs (§12.4). Setup documentation shall state this limitation where a Push
  Button control is configured.
- **Both qualifying samples of a Push Button event shall be guarded** per §12.4. A press
  detected across a sample pair in which either member failed its validity bracket shall be
  discarded, not committed.

### 12.4 RC-link loss and failsafe behaviour

The two supported FC families do not expose identical `MSP_RC` semantics during RX loss, so the
platform adapters shall handle them differently. The common semantic requirement is **RX loss
shall not create a camera-control transition**; the implementation mechanism need not be
identical.

#### Betaflight

Betaflight `MSP_RC` serializes `rcData[]`. `rcData` is explicitly the *scaled, modified,
checked and constrained* channel value ([BF] `rx/rx.c:128`), and the RX failsafe path can
replace an AUX channel with its configured HOLD/SET failsafe value before `MSP_RC` is generated
— `getRxfailValue()` at [BF] `rx/rx.c:619`, applied at `:724`, `:730`, `:744` and written to
`rcData` at `:762`. AUX channels default to `RX_FAILSAFE_MODE_HOLD` ([BF] `rx/rx.c:158`), which
is benign; `RX_FAILSAFE_MODE_SET` is the hazard.

**Scope of the hazard, stated precisely.** Under the stock `HOLD` default the substituted value
*is* the last accepted value, so no transition can be manufactured and the guard has nothing to
catch. The guard exists for `RX_FAILSAFE_MODE_SET`, which a pilot may legitimately configure on
the bridge's AUX channel. Implementers shall not read routine RX glitches as hazards; they
shall implement the guard anyway, because a spec that works only under default config is not a
spec.

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

The V1 guard uses `ARMING_DISABLED_RX_FAILSAFE` from the `armingDisableFlags` U32 appended to
`MSP_STATUS` ([BF] `msp/msp.c:1128`, within the `MSP_STATUS` case at `:1094-1095`).
`failsafeOnValidDataFailed()` sets this flag unconditionally when RX data fails, including
`BOXFAILSAFE`-induced loss ([BF] `flight/failsafe.c:196-209`, set at `:202`), and
`setArmingDisabled()` is an unconditional bitwise OR with no armed-state check ([BF]
`fc/runtime_config.c:73-76`). **The guard is therefore valid while armed**, and its use does not
constitute FC armed-state decoding.

##### 12.4.1 Guarded commit — required

`MSP_STATUS` and `MSP_RC` are **separate MSP transactions and do not represent one FC instant.**

The ordering inside Betaflight is what makes this exploitable — and also what makes the fix
work. In `calculateRxChannelsAndUpdateFailsafe`, substituted values are written to `rcData[]` in
the per-channel loop ([BF] `rx/rx.c:762`) and the failsafe flag is updated only *afterwards*, at
[BF] `rx/rx.c:767,770`. So the data leads the flag. A status read taken before the RC read can
legitimately report *valid* while the RC reply that follows already carries substituted values:

```
bridge: MSP_STATUS  →  guard clear
                       FC: rcData ← failsafe SET value   (rx.c:762)
                       FC: guard asserted                (rx.c:770)
bridge: MSP_RC      →  substituted value, guard looked clear
```

Because the FC's internal data→flag gap is microseconds while the bridge's inter-transaction gap
is milliseconds, a **trailing** status read is a reliable detector. `STATUS₂` is the load-bearing
leg.

**Requirement.** A Betaflight RC sample that would produce a camera-control transition shall be
committed only under a validity bracket:

```
STATUS₁  → must be clear
MSP_RC   → candidate sample
STATUS₂  → must be clear
         → commit
```

If either status read reports the guard asserted, the candidate is **discarded** and the last
accepted camera-control state is frozen (§12.4.2).

Three transactions per poll are not required. The permitted optimisation is:

```
STATUS → MSP_RC
    if the sample implies no control transition:   accept, done
    if the sample implies a transition:            issue STATUS₂, commit only if clear
```

This keeps the steady-state cost at two transactions and pays the third only on the rare poll
that actually changes something. Per §12.3, **both** qualifying samples of a Push Button event
shall be individually bracketed.

This requirement is Betaflight-only. INAV needs no bracket, because it freezes values rather
than substituting them.

##### 12.4.2 Guard edge semantics — required

`ARMING_DISABLED_RX_FAILSAFE` is an *arming-permission* flag borrowed as an *input-validity*
flag. It works, but its edges are shaped for arming, not for camera control.

**Assert is immediate. Clear is delayed.** The flag clears only after
`receivingRxDataPeriodPreset` of continuously valid data ([BF] `flight/failsafe.c:186-192`),
which is `rxDataRecoveryPeriod` — `failsafe_recovery_delay`, floored at `PERIOD_RXDATA_RECOVERY`
= 100 ms ([BF] `flight/failsafe.c:101-105,111`).

**The bridge shall not encode the delay's value.** It waits for the flag to clear, whatever the
reason and whatever the latency. For documentation only: the stock 2025.12.5 default is
**500 ms** (`DEFAULT_FAILSAFE_RECOVERY_DELAY = 5`, or `1` = 100 ms under `USE_RACE_PRO` — [BF]
`flight/failsafe.c:66-70`), and the setting is user-configurable. Comments elsewhere in that
file say "1.0s"; the reset template is authoritative.

**`BOXFAILSAFE` recovery may be immediate, but not universally.** Where Betaflight has entered
the switch-induced failsafe path it sets `receivingRxDataPeriodPreset = 0` ([BF]
`flight/failsafe.c:339`), which removes the recovery delay for that recovery. This is a property
of one branch of `failsafeUpdateState`, not a guarantee attaching to every state in which
`BOXFAILSAFE` has been touched. The bridge shall therefore not special-case `BOXFAILSAFE` at
all: **wait for the guard to clear, regardless of why or how quickly.**

Consequences the implementation shall honour:

1. After real RX loss, the guard holds camera state **past** the return of valid pilot input, by
   the configured recovery delay. This is deliberate and conservative; it is not a defect.
2. Switch-induced and transmitter-loss recovery can produce visibly different latencies (§24.2).
   Both are correct. A tester who sees only one will report the other as a bug.
3. Level controls reconcile to the correct camera state once the guard clears. Edge controls do
   not — see §12.3.
4. `manualStop` (§12.1.1) is camera-control state and is frozen with it, not reset by it.

##### 12.4.3 Startup qualification — Betaflight — required

The bridge's control input has an explicit qualification state and it starts unqualified. The
whole difficulty is the qualification *criterion*, because on Betaflight the obvious one does
not work.

**A clear guard is not proof that valid RC data was ever received.** It is only proof that the
FC has not yet concluded otherwise — and immediately after boot it has not yet had time to
conclude anything:

- `failsafeReset()` does **not** set the flag ([BF] `flight/failsafe.c:94-115`). It sets
  `rxLinkState = FAILSAFE_RXLINK_DOWN` and nothing more.
- The flag is asserted later, by one of: `failsafeOnValidDataReceived()` on the **first** valid
  data, via the `validRxDataFailedAt == 0` branch ([BF] `flight/failsafe.c:176-183`);
  `failsafeOnValidDataFailed()` on invalid data (`:202`); or `failsafeCheckDataFailurePeriod()`
  (`:212-221`, set at `:219`), which the scheduler runs every 10 ms with **no
  `failsafeIsMonitoring()` gate** — that gate applies only to `failsafeUpdateState` at `:231` —
  from [BF] `scheduler/scheduler.c:569-571`.
- With no receiver present at all, that third path asserts only after `rxDataFailurePeriod`
  elapses, i.e. `failsafe_delay`, whose **stock default is 15 = 1.5 s** ([BF]
  `flight/failsafe.c:75`).

So there is a window of up to ~1.5 s in which the guard reads clear and `rcData[]` holds
initialisation values: `midrc` for every channel ([BF] `rx/rx.c:308`). A criterion of "observe
one valid guarded RC snapshot" is satisfied by that window — `STATUS₁` clear, `MSP_RC` returning
`midrc`, `STATUS₂` clear — and the baseline it would capture is an initialisation value.

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
     for longer than  grace,  measured from the bridge's first MSP exchange
     with this FC, where

         grace  = max(failsafe_delay × 100 ms, 100 ms) + margin
         margin = max(2 × qualification poll period, 100 ms)
    ↓
establish baseline from that snapshot
    ↓
control input = QUALIFIED
```

**The first qualified sample may reconcile a level control. It shall never generate an edge
event.**

`MSP_FAILSAFE_CONFIG` returns `failsafe_delay` as its first byte ([BF] `msp/msp.c:1657`). Three
details govern how that byte is used:

1. **It is a U8 in tenths of a second**, and the FC floors the derived period:
   ```c
   failsafeState.rxDataFailurePeriod = failsafeConfig()->failsafe_delay * MILLIS_PER_TENTH_SECOND;
   if (failsafeState.rxDataFailurePeriod < PERIOD_RXDATA_RECOVERY) {
       failsafeState.rxDataFailurePeriod = PERIOD_RXDATA_RECOVERY;   // 100 ms
   }
   ```
   [BF] `flight/failsafe.c:96-100`. A pilot who sets `failsafe_delay = 0` gets 100 ms, not zero,
   and the bridge shall apply the same floor.
2. **`margin` exists to guarantee the assertion is *observed*, not to guarantee it has
   occurred.** `failsafeReset()` sets `validRxDataReceivedAt = 0` ([BF]
   `flight/failsafe.c:106`) and `failsafeCheckDataFailurePeriod()` compares it against raw
   `millis()` (`:215`), so the FC's deadline runs from **MCU boot**. Bridge sync is necessarily
   at or after FC boot, which makes B's expiry unconditionally later than the FC's own
   assertion — condition B cannot fire first. What the margin buys is polling time.

   **`margin = max(2 × qualification poll period, 100 ms)`** — normative, not advisory. The
   derivation:

   - the assertion is **sticky**, re-asserted every 10 ms while data is absent ([BF]
     `flight/failsafe.c:212-221`), so it does not have to be caught in a narrow window — it only
     has to be sampled once while it stands;
   - the interval in which it stands but B has not yet expired is at least `margin` long, so a
     margin of two poll periods yields **at least two** sampling opportunities, tolerating one
     lost or failed transaction;
   - the formula scales automatically with the poll rate: 10 Hz → 200 ms, 20 Hz → 100 ms;
   - the 100 ms floor exists because MSP round-trip latency and scheduler jitter are not zero.
     At a hypothetical 50 Hz qualification poll, `2 × 20 ms` would be comparable to a single
     transaction's own latency, which is precisely the kind of unstated timing assumption
     §12.4.3 exists to remove.

   **Continuity is measured in observations, not in wall time.** A bridge whose status polls are
   timing out is not observing anything, and shall not qualify by letting the clock run: each
   unanswered or failed status transaction during a condition-B interval extends that interval
   by one poll period. An interval containing no successful observation cannot qualify at all,
   whatever its duration.
3. **If the read fails or is unsupported, condition B is unavailable and the bridge shall
   qualify on condition A only.** There is no safe fixed fallback: `failsafe_delay` is
   user-settable well beyond its default, so no hardcoded grace bounds it. Falling back to
   A-only costs late-attach convenience and costs nothing in safety.

Why the two conditions together are sufficient — the guard is asserted on **every** boot path,
either by `failsafeOnValidDataReceived()` on first valid data (`:180-183`) or by the timeout
(`:219`). There is no sequence in which it stays clear indefinitely. So:

| Case | Path |
| --- | --- |
| Fresh FC, transmitter **on**, bridge up within the window | first RC asserts the guard, which then clears → **A** |
| Fresh FC, transmitter **off** | guard asserts at `failsafe_delay` before B's grace expires → stays **UNQUALIFIED**, correctly |
| Fresh FC, transmitter **on**, bridge slower to boot than ~1.5 s | assert→clear already happened unobserved → **B**, after the grace |
| Bridge attached to an FC running for minutes | guard long since clear → **B** |

Row three is the one to document rather than fix: a bridge that finishes MSP sync more than
`failsafe_delay` after FC boot misses the edge and waits out condition B, so normal power-up
qualification can take roughly `failsafe_delay + grace` ≈ 3 s at stock settings. This is slower
than camera BLE connect and therefore invisible in use — but it **will** be filed as a bug if it
is not written down, which is why it is written down.

Requirements:

- the `UNQUALIFIED` state shall be treated as **normal, not a fault**; no RC-loss warning shall
  be raised for it (§18);
- qualification shall not be inferred from elapsed time alone, from the FC being detected, from
  a guard state observed before any RC sample was bracketed, or from a clear guard before the
  FC's no-RX window has been excluded;
- **every** later re-qualification — after an RX-loss freeze, after a bridge reboot, after an FC
  reboot — follows the same rule: re-baseline, reconcile level controls, emit no edge. An FC
  reboot restarts the FC's window too, so a re-qualification following one is subject to A/B
  exactly as the first was;
- the grace clock of condition B shall restart on any FC re-detection.

Because this is expressed as qualification rather than as an assumption about the FC's boot
timing, it holds identically whether the bridge powers up with the FC or connects to an FC that
has been running for ten minutes.

#### INAV 8

INAV 8 deliberately behaves differently, and does so through **two independent mechanisms**.

Per-channel, an invalid pulse is replaced by the last good value:

```c
// Apply invalid pulse value logic
if (!isRxPulseValid(sample)) {
    sample = rcChannels[channel].data;   // hold channel, replace with old value
```
[INAV 8.0.1] `rx/rx.c:489`

And globally, no channel's data updates at all unless both the flight channels and the RX signal
are valid:

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
confirm that the observed `MSP_RC` value remains at the last-good level through RX loss (§27.1).

Because INAV never freezes *transitions* — it freezes *values* — there is no INAV recovery
window and no INAV equivalent of the swallowed-Push-Button case above. Recovery is immediate and
continuous.

**Startup is the exception, and it is not covered by any of the above.** See §12.4.4.

##### 12.4.4 Startup qualification — INAV — required

INAV has the same boot exposure as Betaflight. Every channel is initialised to mid-stick:

```c
rcChannels[i].raw  = PWM_RANGE_MIDDLE;
rcChannels[i].data = PWM_RANGE_MIDDLE;
```
[INAV] `rx/rx.c:271-272`, with the ARM AUX specially initialised to an OFF value at `:279-289` —
the identical safe-by-luck asymmetry documented in §12.4.3's table. So a fresh INAV FC with no
transmitter returns 1500 from `MSP_RC`, and a Record range spanning mid-stick reads ACTIVE,
before any real RC packet has existed.

**INAV needs no timer and no configuration read, because it has a signal Betaflight lacks.**
`ARMING_DISABLED_RC_LINK` (bit 18, [INAV] `fc/runtime_config.h:38`) is driven directly from link
state:

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

**While armed, `ARMING_DISABLED_RC_LINK` is not maintained** — it holds whatever value it had at
the arming instant. This is why the flag is startup-only, and the reason shall be stated wherever
the requirement is: it is not a design preference that a later implementer may revisit for a
cleaner runtime gate than Betaflight's. Promoting it to one yields a validity signal that stops
updating at exactly the moment flight begins.

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
control input = QUALIFIED       ← ARMED is not read again for control
```

No bracket is required: INAV's flag and its RC data cannot disagree the way Betaflight's can,
because nothing substitutes values (§12.4). A single `MSP2_INAV_STATUS` followed by `MSP_RC` is
sufficient. Where §16's optional maintenance gesture is configured, `ARMED` continues to be read
from that same word for as long as the gesture window stays open, and not afterwards; that
interlock is scoped in §5.2 and is absent by default. As on Betaflight, the first qualified sample may reconcile a level control and shall
never emit an edge event.

**Known consequence.** A bridge that reboots while the aircraft is armed cannot qualify until the
aircraft disarms, because the flag it needs is not being updated. It therefore performs no
camera-control transitions for the remainder of that flight, holding whatever the camera was
doing. That is the safe direction and it is deliberate; it shall be documented in release notes
rather than worked around, since every available workaround involves trusting a flag the FC has
stopped maintaining.

### 12.5 What `MSP_RC` actually contains

`MSP_RC` is the FC's **effective channel state**, not a raw tap of the receiver protocol.

- On Betaflight it includes channel scaling and may include failsafe substitution (§12.4).
- On INAV, `rxGetChannelValue()` may additionally return Logic-Condition-overridden values when
  the override flag is set:

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

At setup, and never as part of runtime control evaluation, the bridge may read `MSP_MODE_RANGES`
(34) for the sole purpose of warning that a proposed camera AUX range overlaps a configured FC
mode.

`MSP_MODE_RANGES` is the **only** command required here, and it is the only one available on
both platforms: [BF] `msp/msp_protocol.h:106` and [INAV] `msp/msp_protocol.h:127`. It carries
AUX index and active range, which is all the advisory needs.

`MSP_MODE_RANGES_EXTRA` (238) is **Betaflight-only** ([BF] `msp/msp_protocol.h:263`); it does not
exist in INAV 8.0.1. Betaflight backends may additionally query it for richer diagnostic context,
but no advisory behaviour shall depend on it, or the feature silently degrades to nothing on
INAV.

- These are read-only queries. No corresponding SET is ever issued (§3.1).
- The warning is **advisory and non-blocking**; the user may proceed.
- A common **intended** overlap is the ARM range: Arm Record is normally configured to the same
  AUX/range used by the pilot's arm switch.
- Other overlaps are reported because they may be accidental.
- A failed or unsupported query shall suppress the advisory, not block setup.

The advisory reports overlap; it does not attempt to interpret the current active mode or the
FC's armed state.

### 12.7 Optional INAV trigger — Logic Condition or Global Variable

**Optional and non-blocking. AUX ranges (§12.2) remain the required default on both platforms.**
A camera control may instead be bound to an INAV Logic Condition or Global Variable, read
through:

| Purpose | Command | Source |
| --- | --- | --- |
| Logic condition results | `MSP2_INAV_LOGIC_CONDITIONS_STATUS` (0x2026) | [INAV] `fc/fc_msp.c:570`; `msp_protocol_v2_inav.h:68` |
| Global variable values | `MSP2_INAV_GVAR_STATUS` (0x2027) | [INAV] `fc/fc_msp.c:575`; `msp_protocol_v2_inav.h:69` |

Both are read-only. They sit behind the same `USE_PROGRAMMING_FRAMEWORK` gate as the custom OSD
elements of §10.

**`MSP2_INAV_LOGIC_CONDITIONS_CONFIGURED` (0x203C) shall not be used.** It **does not exist in
INAV 8.0.1** — it appears in neither `msp/msp_protocol_v2_inav.h` nor `fc/fc_msp.c` at that tag,
and is a later addition. A bridge that requires it would fail against the minimum supported
release. Where the bridge needs to know which Logic Conditions are meaningful, it shall infer
that from the status reply and from the user's explicit selection during setup, not from a
capability query that the supported baseline cannot answer.

A user may deliberately encode FC state, **including armed state**, inside an INAV Logic
Condition. In that case the bridge still does not interpret arming semantics; it observes only
the user-selected Logic Condition result.

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
  Source: `MSP_RTC` — Betaflight returns year/month/day/h/m/s/ms ([BF] `msp/msp.c:2179`); INAV
  returns seconds-since-epoch as U32 plus millis U16 ([INAV] `fc/fc_msp.c:1459`). The two
  formats differ; the backend abstracts this. **The two command IDs differ as well — 247 on
  Betaflight, 246 on INAV — and each platform uses the other's value for `MSP_SET_RTC`, so a
  shared constant sets the FC clock on one of them (§3.1.2).** The format difference is the
  visible one and the ID difference is the dangerous one; neither may be assumed away.
- **Live GPS position**, for cameras that accept it. DJI's official reference pushes GPS at 10 Hz
  to tag footage, but does so over the R SDK protocol, which the Osmo Nano does not speak
  (§20.2). Whether the Nano accepts a GPS push over DUML is unknown and is not part of the §27.4
  pass criteria; treat it as speculative until a driver exists.

Neither shall affect recording or flight operation when unavailable. GPS push rate counts against
the §21 MSP budget and shall not exceed the camera's documented rate.

## 15. Pairing

The bridge stores one active camera identity.

- physical pairing button;
- enters pairing mode without phone or app;
- discovers supported cameras, binds the selected one, remembers it across power cycles;
- reconnects automatically on subsequent boots;
- replaced only by an explicit pairing action.

**Broad BLE discovery is an explicit user action, not a recovery mechanism.**

Normal startup and runtime recovery shall perform a **directed reconnect** to the known camera.
If directed reconnect fails, the bridge backs off according to §21.3. It shall not fall back to
broad discovery automatically. Broad discovery is entered only after an explicit Pair/Setup
action, which per §16 means one of that section's four **entry authorities**, none of which is
reachable over a network, over BLE or over runtime MSP, and each of which requires either hands on
the aircraft or a wired host attached to the flight controller. The Setup-Mode pairing
workflow that this permits, and the selection rules that bound it, are specified in §16.2.2.

This rule is independent of FC arm state.

## 16. Setup Interface

Normal operation requires no setup UI. Advanced setup may use an ESP-hosted Wi-Fi page exposing:
recording control mode, AUX channel/range assignments, Push Button timing (§12.3), OSD slot
ownership (§9.1, §10.2), camera preset assignments, OSD layout, warnings, clock/GPS sync,
reconnect policy, optional camera capabilities, camera pairing (§16.2.2), and the live trigger
monitor (§16.2.1).

Setup shall surface the §12.6 overlap advisory when a range is assigned, and shall state the
§12.3 Push Button limitation where such a control is configured.

**Setup Mode entry is never remotely reachable, and requires either hands on the aircraft or a
cable into the flight controller.** Four entry authorities are defined, and no others:

| # | Authority | The physical act | Available when |
| --- | --- | --- | --- |
| 1 | Setup button pressed while running | hands on the bridge | the bridge is reachable |
| 2 | Setup button held through a power-on or reset | hands on the bridge | always — depends on nothing persistent |
| 3 | Maintenance Entry AUX gesture in the startup window | transmitter in hand, aircraft in front of the operator | configured, disarmed, power-on boot |
| 4 | `BOOT_SETUP` over Betaflight USB serial passthrough | a USB cable into the flight controller | a host is attached; Betaflight |

**Three of the four prove presence per entry; authority 4 proves a wired host.** Authorities 1
and 2 require hands on the bridge, and authority 3 requires the transmitter in hand inside a window
that opens only on a power-on boot and only while the FC reports disarmed — each is a fresh act for
each entry. **Authority 4 is different, and shall be described as what it is:** a host already
attached over USB can issue `BOOT_SETUP` repeatedly without any further physical act, so what it
establishes is a physical wired connection to the flight controller, not presence at the moment of
entry. The bridge cannot close that gap by demanding a fresh attachment — it observes UART bytes
and has no visibility of USB attachment at the FC — and demanding one would defeat the purpose,
since a bench host's value is repeated cycles without recabling.

The guarantee that holds for all four, and the one this section actually rests on, is narrower and
true: **no authority is reachable over a network, over runtime MSP, over BLE, or from any
autonomous path.** Authority 4's reach is bounded by a cable.

**All four target `SETUP`. `DIAG` and `PAIR` have no entry authority of their own** — both are
launched from an already-authorised Setup session (§16.1). That hub structure is deliberate: every
route into the maintenance surface passes through the one mode that has no live camera actuation,
so an entry path that misfires costs an unexpected Wi-Fi AP rather than an unexpected recording.

Setup Mode **shall not be enterable** by any network-initiated action, by runtime MSP, by a bonded
or previously authorised BLE peer, or by any other remote request. None of the four authorities
requires knowledge of FC state, except authority 3's arming interlock, which is scoped in §5.2.

**That guarantee is per entry, not per enrolment.** A remote peer authorised once by a physical
press is not thereby authorised to cause later entries; such a scheme substitutes presence at some
earlier moment for presence at the moment of entry, which is not the property this requirement
states. Authority 4 is not a counter-example: a cable's reach is bounded by its length and by
physical access to the airframe, whereas a bond's reach is a radio's. The consequence is not
theoretical: entering Setup Mode is a reboot (§16.1),
and on INAV a bridge reboot while the aircraft is armed leaves the control input `UNQUALIFIED` for
the remainder of that flight (§12.4.4, §22). A remotely triggerable entry would therefore be a
remote path to disabling camera control in flight, and to raising a Wi-Fi AP on the shared 2.4 GHz
band at a moment nobody at the aircraft chose (§3.3).

Setup Mode is an **operating procedure**, not an FC-arm-state-controlled feature. Beyond authority
3's interlock the bridge does not consult FC armed state to decide whether Setup Mode is available.
Documentation shall state that Setup Mode is not to be entered while the aircraft is being operated.

#### Maintenance Entry AUX gesture — required

Authority 3 exists because the bridge may be mounted where no hand reaches it. It shall reach
`SETUP` — never `DIAG` or `PAIR` directly — using only the operator's transmitter:

```
boot reason == ESP_RST_POWERON                     eligibility, decided once at startup
        ↓
FC detection and RC qualification (§12.4.3, §12.4.4)
        ↓
FC reports disarmed                                interlock, re-checked continuously
        ↓
Maintenance Entry AUX observed in its baseline state
        ↓
window opens — 10–15 s
        ↓
four transitions of the Maintenance Entry AUX within ~3 s
        ↓
retained request = SETUP; deliberate restart
```

1. **Unconfigured by default.** With no Maintenance Entry AUX selected there is no gesture, no
   window and no added surface, and every statement §5.2 makes remains literally true. The physical
   button is available in every configuration.
2. **The Maintenance Entry AUX shall not be derived automatically from the Record mapping**, or
   from any other camera-control mapping. §23 step 4 puts the default **Arm Record** preset on the
   aircraft's own ARM AUX, so a gesture inherited from the camera mapping would make rapid toggling
   of the arm switch the documented way to enter Setup Mode — with props fitted.
3. **Setup shall reject an overlapping selection, not advise it.** Where the chosen AUX and range
   overlap ARM, PREARM, failsafe, rescue/return-to-home or another safety-relevant FC mode, it
   shall be refused as a maintenance-entry source. This reuses §12.6's read-only overlap machinery
   with the **opposite verdict**: for a camera-control mapping the ARM overlap is an expected
   advisory (§23); for a maintenance-entry source it is a prohibition. A spare AUX is the clean
   choice.
4. **Power-on boots only.** Eligibility shall be decided from `ESP_RST_POWERON` alone. A software
   restart, watchdog, panic or brownout shall never open the window — Betaflight can requalify a
   control input while airborne (§12.4.3 condition B), so a window tied to qualification alone
   would reopen mid-flight after any in-flight bridge reset.
5. **Arming interlock.** The gesture shall not be recognised while the FC reports armed.
   Requirement 4 does not close the in-flight case by itself: the bridge is powered from the FC's
   5 V rail (§4.2), and a rail collapse and recovery presents as a power-on reset. The cost is
   bounded — on INAV `ARMED` is in the same `armingFlags` word §12.4.4 already reads; on Betaflight
   `MSP_STATUS` is already polled for the §12.4.1 bracket and only the box-index mapping is new,
   read once during identification. Neither adds steady-state traffic. **This is a maintenance
   interlock, not a control input**, and is the sole exception §5.2 permits.
6. **The window starts at qualification, not at power-on.** Betaflight's condition-B grace is
   configuration-dependent and can run to seconds (§12.4.3); a window measured from power-on would
   be partly consumed before the input is trustworthy.
7. **Sequence, never static state.** A static AUX position present at startup shall never be
   sufficient. Both platforms initialise channels to mid-stick (§12.4.3, §12.4.4) and failsafe can
   hold an arbitrary value; only an intentional sequence separates user intent from initialisation.
   Only qualified samples shall count toward recognition.
8. **Recognition resets** on RC-validity loss, on an out-of-pattern transition, on the FC arming,
   on expiry of the pattern timer defined below, and on window expiry.
9. **The window shall be visible.** While it is open the bridge shall display `SETUP GESTURE` in
   its owned primary OSD slot, and `SETUP n/4` as transitions register, returning to normal camera
   content when the window closes. Without feedback the user performs a timed sequence inside an
   invisible window that opens at an invisible moment, and a failure is indistinguishable from an
   unsupported feature.

**The recogniser, precisely — required.** "Four transitions within about three seconds" is not a
specification for an entry authority. The following is:

| Term | Definition |
| --- | --- |
| Position | `IN` (inside the configured maintenance range) or `OUT` (outside it), evaluated with the §12.2.1 ±10 µs hysteresis, from qualified samples only |
| Baseline | the position held when the window opens |
| Transition | a change of position **sustained for at least two consecutive qualified samples**; a single-sample excursion is not a transition and shall not reset recognition either |
| Pattern | exactly **four** transitions, alternating, beginning and ending at the baseline — `OUT→IN→OUT→IN→OUT`, or `IN→OUT→IN→OUT→IN` where the baseline is `IN` |
| Pattern timer | starts at the **first** transition, not at window open; the fourth shall complete within **3.0 s** of the first, after which recognition resets and the window continues to run |

The pattern ends where it began, so a control left displaced cannot complete it — a switch knocked
in transit produces one transition and then nothing.

**Sample budget.** The two-sample dwell costs at least 100 ms per transition at 20 Hz and 200 ms at
10 Hz, so the pattern's floor is 400 ms or 800 ms respectively against a 3.0 s budget: comfortable
at 20 Hz, workable at 10 Hz. The dwell is defined in samples rather than in milliseconds for
exactly that reason — it scales with whatever rate §21.1 is running, and a millisecond dwell would
silently become a one-sample dwell at the lower rate.

#### Host-assisted entry — Betaflight USB passthrough

Authority 4 delivers a Setup Mode request from a host over USB, through the flight controller's
serial passthrough to the bridge's UART:

```
host ──USB──► Betaflight ──passthrough──► bridge maintenance protocol
                                                BOOT_SETUP
                                                     ↓
                                          bridge restarts into SETUP
                                                     ↓
                                   host drops DTR and the FC resets itself,
                                   or the operator power-cycles the aircraft
```

**Passthrough is a command-delivery channel, not a diagnostic one.** While it is active the FC
copies bytes rather than serving MSP, so `MSP_RC` is unavailable and no bridge mode that needs live
FC input can run underneath it: `serialPassthrough()` is an unconditional `while (1)` byte-copy
loop that never returns ([BF] `io/serial.c:692`, loop at `:708`; [INAV] `io/serial.c:526`). The
requested mode begins only after both ends have restarted.

The bridge-side maintenance protocol shall be minimal and platform-independent:

```
PING        GET_INFO        BOOT_SETUP        REBOOT
```

**No firmware-transfer verbs.** `BOOT_SETUP` reaches the Wi-Fi AP and §19's existing update path
takes it from there. A serial flasher would be a second complete update mechanism, with its own
integrity story and its own interrupted-update recovery story, reaching a destination the first
mechanism already reaches.

Requirements:

- maintenance frames shall be unambiguously distinguishable from MSP traffic on the same UART. The
  bridge is an MSP **client** on that link and shall neither mistake a response for a command nor
  emit a frame an FC could interpret as MSP;
- the protocol shall not be reachable through ordinary runtime MSP, RC input, BLE, or any
  autonomous recovery path. Its authority rests on one assumption, which shall be stated rather
  than left implicit: **nothing can place bytes on the bridge's UART except a host attached over
  USB through FC-initiated passthrough**;
- `BOOT_SETUP` targets `SETUP` only. There shall be no `BOOT_DIAG` or `BOOT_PAIR` verb — authority
  4 enters the hub like every other authority;
- INAV may reuse the same protocol, but INAV 8.0.1 offers no equivalent automatic passthrough exit,
  so seamless host-side recovery is a Betaflight property and shall not be claimed for INAV.

#### Passthrough exit behaviour — cited

**Betaflight's host-side exit is opt-in, and the invocation must ask for it.** By default the only
exit is a power cycle: `cliSerialPassthrough()` prints `"Forwarding, power cycle %sto exit."`
([BF] `cli/cli.c:1629`) before entering the non-returning loop ([BF] `cli/cli.c:1671`). A DTR-driven
exit exists only when the `reset` DTR action was given on the command line — `strcasecmp(tok,
"reset") == 0` sets `port1ResetOnDtr` ([BF] `cli/cli.c:1510`) — **and** only when the second port is
the USB VCP ([BF] `cli/cli.c:1625`). Under those two conditions the CLI registers `cbCtrlLine_reset`
([BF] `cli/cli.c:1633`), which calls `systemReset()` when the host de-asserts DTR ([BF]
`cli/cli.c:1398`). The syntax carrying that argument is
`serialpassthrough <id1> [<baud1>] [<mode1>] [none|<dtr pinio>|reset] [<id2>] [<baud2>] [<mode2>]`
([BF] `cli/cli.c:6681`; a build without `USE_PINIO` offers `[none|reset]`, `:6683`).

**The host tooling shall invoke passthrough with the `reset` DTR action**, and shall document a
power cycle as the fallback exit. This specification shall not assume that a bare
`serialpassthrough` invocation returns control — for every default configuration it does not.

**INAV 8.0.1 has no equivalent.** Its `cliSerialPassthrough()` accepts no DTR-action argument
(`serialpassthrough <id> [baud] [mode] [options]`, [INAV] `fc/cli.c:4889`), prints
`"Forwarding data to %d, power cycle to exit."` ([INAV] `fc/cli.c:1032`), and enters the same
non-returning loop ([INAV] `fc/cli.c:1034` → `io/serial.c:526`). That file contains no
`CTRL_LINE_STATE_DTR` handling and no `serialSetCtrlLineStateCb` registration of any kind. On INAV
the exit is a power cycle, and seamless host-side recovery shall not be claimed.

### 16.1 Operating modes are selected at boot

The bridge has **four operating modes, one per boot**. A mode is selected during startup and does
not change while the bridge is running; every transition between modes is a restart.

| Mode | Wi-Fi AP | BLE | FC MSP link | Camera control |
| --- | --- | --- | --- | --- |
| `RUN` | never initialised | camera central | polled (§21.1) | live `ControlPolicy` |
| `SETUP` | AP up | never initialised | polled | none — monitor (§16.2.1) and learning (§16.2.3) |
| `DIAG` | AP up | camera central | polled | live `ControlPolicy`, instrumented (§16.2.4) |
| `PAIR` | never initialised | scan / directed bind | not initialised | none |

This replaces in-place arbitration of the radio between Wi-Fi and BLE. Ownership is fixed for the
duration of a boot, and no mode tears a radio subsystem down to reach another. Three of the four
modes hold at most one radio stack, so for them the peak-resource state is unreachable by any code
path, error path or timeout path rather than merely forbidden.

`DIAG` is the exception and is the one mode that holds both (§16.2.4). It pays for that with a
separate resource gate rather than with an exception to an invariant: it is a mode entered once per
boot, not a window during which a rule is suspended, so it adds a measurement without adding a
transition.

#### Boot selection — required

```
boot
 │
 1. Setup button held?
 │      → SETUP
 │
 2. else reset reason == ESP_RST_SW  AND  retained boot request valid?
 │      → copy the request to RAM
 │        invalidate the retained request
 │        execute the RAM copy
 │
 3. otherwise
        → RUN
```

1. **The held-button path has absolute priority and depends on nothing persistent.** It shall not
   consult the retained request, the configuration store, stored camera identity or BLE bonding
   state. Setup Mode is the only route to firmware update (§19) and to recovering a corrupted
   configuration (§22); an entry mechanism that itself depends on healthy persistent storage would
   make those unreachable in exactly the case they exist for.
2. **Special-mode intent is transient, not persistent.** The boot request shall be held in
   retained RAM that survives a deliberate software restart, not in flash. It therefore causes no
   flash write, no wear, and has no durable-clear step and no clear-failure path to specify or
   test.
3. **Consume before enter — required.** The retained request shall be invalidated *before* the
   requested mode is executed, not after. `ESP_RST_SW` is not exclusive to the bridge's own mode
   transitions — completing a firmware update restarts the same way (§19), as may a library or
   fatal-handler restart — and a request left valid would be replayed into an unintended
   maintenance boot with nobody present.
4. **Every other reset reason selects `RUN`, whatever the retained memory contains.** Power-on,
   external reset, brownout, watchdog and panic all decline the request without inspecting it.
   This is what makes *an interrupted maintenance boot never resumes itself* a property of the
   reset path rather than of a write ordering an implementer must get right.
5. **Who may create a boot request is enumerated, and the list is closed.**

   | Request | May be created by, and by nothing else |
   | --- | --- |
   | `SETUP` | entry authorities 1, 3 and 4 (§16); `PAIR` on completion, returning its result to the session; and `DIAG` on an explicit exit to `SETUP` |
   | `DIAG` | an authorised Setup session only |
   | `PAIR` | an authorised Setup session only |

   The last two are **returns to an already-authorised session, not entries** — `PAIR` and `DIAG`
   are only ever reached from `SETUP` in the first place (§16.1), so neither can manufacture an
   authorisation that did not already exist. `DIAG` may also exit to `RUN`, which creates no
   request at all.

   **Authority 2 creates no request at all.** It is boot rule 1 above: it enters `SETUP` directly,
   with absolute priority, consulting nothing persistent. An implementation that routed it through
   a retained request would reintroduce the dependency on healthy retained state that rule 1 exists
   to avoid.

   The property this preserves is the one that matters: **no network-accessible interface exists in
   `RUN` or in `PAIR` from which any request could be created.** A network request issued *inside*
   an already physically authorised Setup session may schedule a single-shot `DIAG` or `PAIR` boot;
   that is not remote entry into Setup Mode.

#### Transitions — required

```
  authority 1  button press           ──┐
  authority 3  maintenance AUX gesture ──┼── request = SETUP, restart ──►  SETUP
  authority 4  BOOT_SETUP over USB     ──┘                                  ▲
                                                                            │
  authority 2  button held at boot ── boot rule 1, no request ever ─────────┤
                                                                            │
                                     Exit ── no request ── restart ──► RUN  │
                                                                            │
                        ┌──── request = DIAG ── restart ──► DIAG ───────────┤
                        │                                    │              │
                        │              Exit ── to SETUP, or to RUN ─────────┤
                        │                                                   │
                        └──── request = PAIR ── restart ──► PAIR            │
                                                    Wi-Fi off, MSP off      │
                                                            │               │
                     normal completion ── result retained,  │               │
                     (including failure)  request = SETUP ──┴───── restart ─┘

  watchdog · panic · brownout · external reset · power loss, in any mode  ──►  RUN
        unless the Setup button is held at the resulting boot, which wins under boot rule 1
```

`DIAG` and `PAIR` are reached only from `SETUP`, by an explicit action in the setup interface. Both
return there, so a maintenance session is a sequence of boots around one hub rather than a graph.

**Operational failure is not abnormal termination.** A `PAIR` boot that finds no camera, times out,
or fails to bind has *completed normally*: it retains an intelligible result and returns to
`SETUP`. Only an uncontrolled reset lands in `RUN`. A user who is returned to `RUN` without
explanation concludes the bridge crashed, so the distinction is a user-visible requirement and not
an implementation detail. `PAIR` shall additionally have a **bounded maximum lifetime of 120 s**,
enforced independently of every operation inside it, so that a fault in a BLE library cannot leave
the device indefinitely in a maintenance boot. On expiry the boot terminates and the next boot is
`RUN` (§24.2). The cap is deliberately well clear of the sum of the §16.2.2 operation bounds: it is
a backstop against a hung stack, not a schedule, and a `PAIR` boot that routinely approaches it is
reporting a defect rather than working slowly.

**Exiting Setup Mode is a fresh bridge start, not a resumption.** `RUN` re-runs FC detection (§5),
the version floor and capability probe (§5.1), startup qualification (§12.4.3, §12.4.4), owned-slot
initialisation (§9, §10) and directed camera reconnect (§15). On Betaflight, an FC that has been
running throughout offers no assert→clear edge, so qualification completes through condition B
after the computed grace — the delay is specified behaviour, not a regression. No camera-control
edge is synthesised from switch activity that occurred during Setup Mode.

This is also what makes Setup isolation structural. `manualStop` (§12.1.1) is latching
camera-control state cleared at bridge reboot by requirement 3 of that section; because Setup Mode
both begins and ends with a restart, no diagnostic switch activity — whether through the monitor
or through the user's own transmitter — can carry into normal operation. There is no state to
preserve and no isolation for an implementation to enforce correctly.

#### Reboot is a maintenance mechanism only — required

> For standalone-bridge setup and maintenance operations only, transitions between mutually
> exclusive radio-ownership modes shall use reboot boundaries rather than live Wi-Fi/BLE teardown
> and reinitialisation. **Reboot shall never be used as an autonomous runtime recovery mechanism**
> for camera loss, BLE failure, MSP failure, or any other normal operating fault.

The prohibition is the load-bearing half. §12.4.4 and §22 record that a bridge reboot while INAV is
armed leaves the control input unqualified for the remainder of that flight, because the flag it
needs is not maintained in the armed branch. A "recover by restarting" policy would convert a
recoverable camera fault into an unrecoverable loss of camera control mid-flight.

#### Resource, MSP and OSD obligations per mode — required

1. **Setup Mode shall never be entered autonomously** because of a camera or BLE failure, or any
   other runtime fault.
2. **Heap.** Every heap figure shall be measured on production ESP32-C3 hardware, in the
   configuration it is quoted for, with each radio stack the mode actually holds already resident.
   A free-heap figure logged before a stack is up overstates the true floor, and figures measured
   on other boards or other firmware do not transfer to this build. For the two AP-resident peaks
   that means **the AP up and a client attached**; for `RUN` and `PAIR` it means the opposite
   configuration, and the rule is the same one — measure what you quote. There are **two** such
   peaks and they are not interchangeable:

   | Peak | State | Gates |
   | --- | --- | --- |
   | 1 | AP up, client attached, BLE down (`SETUP`) | `SETUP`, including §16.2.3 trigger learning |
   | 2 | AP up, client attached, BLE connected to the camera (`DIAG`) | `DIAG` only (§16.2.4) |

   **Neither peak gates `RUN` or `PAIR`, and neither shall be quoted for them.** `RUN` holds a BLE
   central with no AP; `PAIR` holds a BLE scan or directed bind with no AP. A heap figure measured
   with the AP resident and BLE down does not bound either configuration — the stacks draw on
   different pools, and the measured case does not dominate the unmeasured ones. `RUN` and `PAIR`
   shall each be measured in their own configuration and shall each meet the criterion below. Flash
   fit is a property of the image and may be established once for all modes.

   **The pass criterion — two figures, not one.**

   | Figure | Floor | Why this one |
   | --- | --- | --- |
   | Minimum free internal heap | **≥ 32 KiB** | headroom for the allocation burst a reconnect or a client session makes |
   | Largest allocatable internal block | **≥ 16 KiB** | a fragmented heap fails a single large allocation while reporting plenty free; 16 KiB also leaves a clean reserve for transient buffers added later |

   Free bytes alone are not a criterion on this part. ESP-IDF's allocator fragments under
   repeated Wi-Fi and NimBLE connect/disconnect cycles, so a mode can report ample total free heap
   and still fail the one contiguous allocation a reconnect needs.

   **Both floors shall hold simultaneously, in each mode, after every stack that mode holds is
   resident and while the mode is under its specified worst-case workload** — sampled at the
   mode's own worst moment, never at entry. **In addition, neither figure shall show monotonic
   degradation across repeated mode-specific operations**: a floor that holds for the duration of
   one observation says nothing about a leak, and a slow leak is the failure this product is most
   exposed to, because `RUN` is the mode that runs for hours and reconnects on every camera
   dropout.

   | Mode | Soak | Workload it shall be held under |
   | --- | --- | --- |
   | `RUN` | **≥ 30 min** | ≥ 10 camera disconnect/reconnect cycles; a single minute cannot surface a reconnect leak, and this is the mode that runs for a whole flight |
   | `SETUP` peak 1 | ≥ 60 s | AP up with a client attached and a page actively polling |
   | `PAIR` | ≥ 60 s | a completed scan followed by a directed bind attempt |
   | `DIAG` peak 2 | ≥ 60 s | AP, client and a live camera link held together |

   These floors are **provisional and shall be confirmed or replaced by the §27.3 measurement**,
   which is the first point at which real per-mode figures exist. This is the §21.3 pattern: a
   stated value now so tests can be written against it, a named measurement that closes it, and a
   named destination to write the result back to — this table. What shall **not** happen is the
   criterion remaining the word *acceptable*: a gate whose threshold is unstated cannot be failed,
   and §26 makes `DIAG`'s existence depend on peak 2 passing this.

   Peak 2 is strictly higher than peak 1 and shall be measured separately. Where it does not pass,
   `DIAG` shall be **unavailable**, and that outcome shall not affect `RUN`, `SETUP`, `PAIR`, the
   §16.2.1 live trigger monitor, the §16.2.2 pairing workflow or §16.2.3 trigger learning — none of
   which requires a camera connection, and all of which are reachable in `SETUP`.
3. **The FC MSP link is not arbitrated in `SETUP` or `DIAG` and continues throughout.** §16.2.1 is
   inert without live `MSP_RC`, §16.2.4 more so, and the UART contends for neither the radio nor
   the Wi-Fi/BLE heap. `PAIR` does not initialise the MSP link at all; it is a BLE-only maintenance
   boot.
4. **OSD content is mode-specific.** In `SETUP` the bridge shall write its owned primary state slot
   to `SETUP` — or to the §16 gesture indications while a maintenance window is open in `RUN` — and
   blank the remaining owned slots, and shall not raise `CAM LOST` merely because Setup Mode
   deliberately disconnected the camera (§17.1, §18). `SETUP` satisfies §9 and §10's requirement
   that owned slots carry non-authoritative content whenever camera truth is unavailable, and it
   does so more usefully than a warning, because it explains why the telemetry went away. `DIAG`
   has a live camera and shall present normal camera truth exactly as `RUN` does. `PAIR` writes
   nothing; the FC retains whatever the preceding `SETUP` boot left, for the duration of that boot.

These requirements are resource-safety and user-comprehension properties. They do not require or
imply an armed-state interlock.

### 16.2 Configuration diagnostics — standalone bridge only

This section, and the boot-mode architecture of §16.1 that carries it, apply to the standalone
ESP32-C3 bridge. Neither is a requirement of the future ELRS-integrated implementation, and nothing
in either shall be promoted into the shared core of §25. The seam stays where §25 draws it.

The purpose is to make configuration observable without changing the runtime control model:

```
learn the mapping by exercising the control      (§16.2.3, SETUP or DIAG)
      ↓
observe what the bridge sees
      ↓
observe what the bridge would do                 (§16.2.1, dry run)
      ↓
optionally watch the real camera confirm it      (§16.2.4, DIAG)
```

Nothing in this section creates or modifies FC mode ranges, USER modes, PINIO configuration, RC
values or any other flight-controller state (§3.1). The control model remains explicit bridge-side
AUX channel/range mappings (§12).

#### 16.2.1 Live trigger monitor — required

While Setup Mode is active and a client is attached, the setup interface shall provide a live
dry-run view of every configured camera-control input. For each control it shall expose, as
applicable:

- the FC-effective AUX channel value currently observed — per §12.5 the FC-effective value, which
  is not necessarily the transmitter's switch position;
- the configured range or ranges, as stored;
- **the hysteresis latch**: which side of the §12.2.1 Schmitt trigger each range is currently
  latched to, and the effective entry/exit thresholds. Showing only the stored range beside an
  active/inactive verdict makes correct behaviour at a boundary look like a defect;
- whether each configured range is currently active;
- the resulting control intent or action — `RECORD`, `STOP`, `BUTTON EVENT`, or the configured
  camera action;
- whether the control input is `QUALIFIED`, `UNQUALIFIED` (§12.4.3, §12.4.4), or frozen by the
  applicable platform validity mechanism (§12.4);
- for Push Button controls, whether a qualified button event has been recognised (§12.3).

**Same implementation, not a re-approximation — required.** The displayed result shall be produced
by the same platform input path, range evaluator, Push Button logic, Combi arbitration,
qualification logic and `ControlPolicy` implementation used during normal operation. The setup
interface shall not implement a separate approximation of runtime trigger behaviour; a monitor that
agrees with the runtime path only by having been written to agree with it is a monitor that will
one day disagree silently.

**No live camera-control policy exists in `SETUP`.** The monitor's `ControlPolicy` instance is the
only one in that boot, and it is destroyed with the boot:

```
adapter ──ControlSample──► monitor ControlPolicy ──► display only
                                                     (no camera commands)
```

BLE is not initialised in `SETUP` (§16.1), so there is no camera to command, and the boot boundary
disposes of the instance's state. In `DIAG` the rule is the opposite — the interface displays the
**real** runtime instance operating the camera (§16.2.4) — and the two are reconciled by the
consumer table in §16.2.3: the mode selects which policy consumers of `ControlSample` are
permitted, not which implementation is used. This is what closes the `manualStop` hazard at its root: under an
architecture where a live policy ran alongside the monitor, the user's own switch movements — made
in order to exercise the monitor — would reach the live instance through the ordinary input path,
set `manualStop` (§12.1.1), and suppress the pilot's level control after Setup Mode exited. That
failure is not reachable here, and no implementation discipline is required to avoid it.

**Dry-run monitoring shall issue no command to the camera.** Its purpose is to let the user
validate channel selection, ranges, switch positions, button timing and the resulting bridge
behaviour before enabling camera actuation.

**The monitor shall be explicit about an unqualified input.** With the transmitter off the input
does not qualify (§12.4.3 case (b), §12.4.4), and the monitor shall display `UNQUALIFIED` together
with the reason rather than presenting an inert display. This is the state a bench user meets
first, and the monitor is the only place in the product where it is ever explained.

#### 16.2.2 Camera pairing from Setup Mode — required

After Setup Mode has been entered through one of §16's entry authorities, the setup
interface shall permit the user to view the currently paired camera, initiate discovery of
supported cameras, view the discovered candidates, select one to pair, and replace the stored
paired-camera identity.

**This is consistent with §15 and §3.3 as written, and is not a relaxation of either.** Both
already condition broad discovery on "an explicit Pair/**Setup** action"; Setup Mode entry is one
of the two qualifying actions, and it is itself gated on one of §16's entry authorities, none of
which is remotely reachable (§16). **No second authorisation shall be required** to authorise pairing from within an
already-authorised Setup session — scheduling a `PAIR` boot consumes no additional press, and the restart between them does
not revoke the authorisation the session already carries.

**Pairing runs in the `PAIR` boot mode (§16.1).** Discovery and directed binding are BLE-only
operations; scheduling one suspends the setup session by restart rather than by tearing the AP down
in place:

```
SETUP ── request = PAIR ── restart ──► PAIR ── result retained,
                                               request = SETUP ── restart ──► SETUP
```

`PAIR` is parameterised by an optional target: with no target it scans; with a target it performs a
directed bind. Scan and bind are not separate operating modes, because both are BLE-only and
neither arbitrates against anything.

**Bounded by construction.** Each operation shall be bounded in duration and shall terminate
automatically; a `PAIR` boot shall never remain indefinitely in discovery or in connection retry,
and shall have the bounded maximum lifetime of §16.1. An authorised Setup session does not license
continuous scanning for its lifetime (§3.3 requirements 1 and 4).

**These are maxima, not durations.** Each operation returns the instant it has its answer; a
camera found in 2 s ends the scan at 2 s. Nothing waits out its bound.

| Bound | Maximum | Expiry outcome |
| --- | --- | --- |
| Discovery scan | **30 s** | `SCAN_TIMEOUT`, or `NO_CAMERAS` where the scan completed early with nothing supported found |
| Directed bind attempt | **15 s** | `BIND_TIMEOUT` |
| Whole `PAIR` boot lifetime | **120 s** (§16.1) | boot terminates; next boot is `RUN` — **not** a return to `SETUP`, because a lifetime expiry is a fault, not a normal outcome |

The 120 s cap sits far above the worst legitimate path — a 30 s scan, a 15 s bind attempt and
bookkeeping, well under half the cap — precisely so that reaching it means a stuck BLE stack and
nothing else. It is a watchdog, not a schedule.

The first two are **normal outcomes and return to `SETUP`** with a result to display; only the
third is abnormal, and it is the only one of the three that lands in `RUN`. An implementation that
returns `SCAN_TIMEOUT` to `SETUP` but also lets the boot run to 120 s has confused the two.

**Every normal outcome returns to `SETUP` with an intelligible result:**

| Outcome | Meaning |
| --- | --- |
| `PAIRED` | directed bind succeeded; stored identity replaced |
| `MULTIPLE_CANDIDATES` | scan completed; user selection required |
| `NO_CAMERAS` | scan completed; nothing supported found |
| `SCAN_TIMEOUT` | scan reached its bound without completing |
| `BIND_FAILED` | camera rejected or refused the directed connection |
| `BIND_TIMEOUT` | directed connection did not complete within its bound |

**Candidate cache.** Scan results are retained data, not a live connection state, and are held in
the same retained-memory context as the boot request (§16.1). The cache shall hold **at most 8
entries** — a scan finding more shall retain the first 8 supported candidates and report that the
list was truncated, rather than dropping the excess silently or growing to fit — shall hold only
enough identity to let the user choose and the bridge later direct a bind,
and shall never be treated as authoritative camera identity. A cache that fails its validity check
is empty.

**Scan token.** Each completed scan shall stamp the cache with a fresh **opaque scan token**. A
selection shall be accepted only when it carries the current cache's token *and* names an entry
that exists in that cache. The token shall not be predictably reused while a page from an earlier
scan could plausibly still submit a selection — a monotonic counter restarting from a fixed value
does not satisfy this, because a stale page's selection would eventually match a later session's
first scan. Entering `RUN` shall unconditionally empty the candidate cache and invalidate its
token; that unconditional clearing is what makes a stale selection harmless, and shall not be made
conditional.

**Selection rules.**

| Supported candidates discovered | Behaviour |
| --- | --- |
| Exactly one | may be selected automatically, without user confirmation |
| Two or more | explicit user selection is **required** |
| Two or more, no selection made | **no change** — the stored identity is left exactly as it was |

The bridge shall never bind to the strongest advertiser as a default. Pairing to a nearby
stranger's camera is a silent failure that survives power cycles, and "the closest one" is not a
statement of user intent.

#### 16.2.3 Trigger learning — required

Trigger learning lets a user configure a bridge action by exercising the intended transmitter
control, without knowing its AUX channel number or its numerical endpoints in advance.

**It is required in `SETUP`.** Where `DIAG` is implemented (§16.2.4, V1.2), the identical learner
shall be available there too, so that a mapping can be learned and then validated against the real
camera without an intervening restart. Learning shall not be implemented twice, and `DIAG`'s
availability shall not affect it in `SETUP`.

**It needs no camera and no BLE.** Learning consumes qualified FC-effective `ControlSample`s and
writes bridge-side configuration; nothing in it touches the camera. It therefore sits at §16.1's
peak 1 and is **not** conditional on the peak-2 gate. That separation is deliberate: the primary
mechanism for discovering and configuring control mappings must not depend on a resource
measurement that may not pass.

```
                    ┌─► TriggerLearner       → proposal → user confirmation → §12 mapping
ControlSample ──────┤
                    ├─► SETUP monitor policy → display only
                    └─► DIAG live policy     → camera commands
```

Mode rules select which consumers are permitted, which is what reconciles §16.2.1's rule that no
live policy exists in `SETUP` with §16.2.4's rule that `DIAG` displays the real runtime instance:

| | `SETUP` | `DIAG` |
| --- | --- | --- |
| `TriggerLearner` | active when requested | active when requested, where the mode exists |
| Policy instance | monitor only, display only | the real runtime instance |
| Camera actuation | none | live, **suspended while learning is active** |

**Learning starts only on an explicit user action** in the setup interface. Normal RC activity
outside an active learning operation shall never alter persistent bridge configuration.

**Polling — required.** While an explicit trigger-learning operation is active, `MSP_RC` shall be
polled at 20 Hz regardless of the currently configured control modes, and normal polling policy
(§21.1) shall resume when learning ends. Without this the operation runs at 10 Hz, because the
20 Hz rate is enabled only when a Push Button control already exists — the very thing being
learned. §12.3 records the consequence directly: *a momentary switch shorter than the sample
interval can be missed entirely or observed once at an arbitrary phase*. At 20 Hz the documented
250 ms hold yields about five samples rather than two or three.

**Camera actuation is suspended during learning — required.** Qualified samples continue to feed
the learner and the diagnostic display but shall not produce camera commands, and runtime control
state shall not be modified by movements performed solely during learning. In `DIAG` the user is
being instructed to move controls; without this rule those movements also reach every mapping
already configured, so learning a Photo control could start a recording. When learning ends the
live policy re-baselines — level intents reconcile, and no edge is emitted.

**Candidate detection.** At the start of learning the bridge shall capture a baseline of the
available AUX channels, then identify the intended control from a significant, stable change
against that baseline. A candidate shall be proposed only where the input is qualified, exactly one
AUX channel moved, and its resulting position is stable. Those three terms are defined here rather
than left to the implementer, because the whole of this subsection is testable from synthetic
samples and none of it is testable without them:

```
learning rate        = 20 Hz                       (required above)

baseline             = median of 5 consecutive qualified samples

channel moved        = |value - baseline| >= 100 µs
                       for >= 2 consecutive qualified samples

stable position      = 5 consecutive qualified samples
                       whose max - min <= 20 µs

ambiguous            = >= 2 AUX channels satisfy "moved"
                       during candidate acquisition

momentary candidate  = baseline -> stable moved position -> baseline

acquisition timeout  = 15 s
```

Every line is a mechanical predicate over a sample buffer. Nothing here needs a camera, a radio or
a clock beyond sample counting, which is what makes the whole of this subsection testable from
synthetic input.

**Why each figure.**

- **Median of 5, not mean.** A median is unmoved by one outlier sample; a mean is not, and the one
  outlier is exactly what a switch bounce or a dropped frame produces at the moment a baseline is
  captured.
- **100 µs sits in a wide gap.** Jitter on a settled channel is a few microseconds; the smallest
  step §12.2.1 permits a runtime range to resolve is 40 µs; a physical two- or three-position
  switch moves 400–500 µs. It rejects noise and a nudged gimbal while accepting every real switch
  throw, including a transmitter with substantially non-standard endpoints.
- **Two consecutive samples to count as moved.** A single sample over threshold is a glitch, not a
  movement — the same reasoning §12.3 applies to Push Button detection and §16 applies to gesture
  transitions.
- **Spread, not a ± band.** *Stable* is defined as `max - min <= 20 µs` across the window rather
  than "within ±10 µs", because a ± band requires first deciding what the centre is, and two
  implementations may reasonably pick the mean, the median or the first sample and disagree about
  identical input. A spread has no centre and no ambiguity. The width is the same; the testability
  is not.
- **5 samples = 250 ms at 20 Hz**, which is exactly §12.3's documented minimum Push Button hold.
  A momentary control a user is able to demonstrate is therefore, by construction, a control the
  runtime can detect — the learner cannot accept something the live path would miss.
- **15 s acquisition timeout.** Without it the ambiguity rule is unbounded in time: two channels
  moved thirty seconds apart are indistinguishable from two moved together, and a learner left
  open on a forgotten browser tab accumulates state forever. Expiry returns to idle, changes no
  configuration, and reverts polling (§21.1).

**This threshold is not §12.2.1's hysteresis and shall not be conflated with it.** They answer
different questions: §12.2.1 decides whether a *configured* range is currently active at runtime;
100 µs decides whether a channel was *deliberately exercised* during setup.

**Three outcomes, and they are distinct.** Where the operation is **ambiguous**, the attempt shall
be rejected and the user asked to move only one control; **the bridge shall not guess** — not by
largest movement, not by lowest channel index, not by first to cross the threshold. Where **no**
channel satisfies *moved* before the acquisition timeout, the attempt shall be rejected as *no
movement detected*. Where a channel moves but never reaches a *stable position*, it shall be
rejected as *unstable*. The three tell the user to do different things — move one control, move a
control at all, hold the position — and shall not be collapsed into a single failure.

**Characterisation.** For a multi-position switch the interface may guide the user through each
position and record the observed stable values, then derive proposed explicit ranges and present
them for confirmation:

```
Detected: AUX7

POSITION 1    ≈ 1005
POSITION 2    ≈ 1502
POSITION 3    ≈ 1993
```

**Nominal transmitter values of exactly 1000, 1500 or 2000 µs shall not be assumed.** Where a
control moves from a baseline position into another stable position and returns to the baseline,
the interface may propose `Push Button` semantics.

**Inference is a proposal, never authoritative.** Trigger learning cannot guarantee recognition of
arbitrary short transmitter events. The interface shall state the minimum demonstrated hold time
(§12.3), and any inferred trigger type shall remain a proposal the user may override before
confirmation. Every proposed control type, range and action requires user confirmation before it
becomes active configuration.

**Admissibility — learning weakens nothing.** Every proposed mapping shall pass the same checks as
a manually entered one: §12.2.1's minimum range width and boundary separation, hysteresis validity,
the §12.6 overlap advisory, and every action-specific restriction — including §16's prohibition on
a Maintenance Entry AUX overlapping ARM, PREARM, failsafe, rescue or another protected function.
**User confirmation is not an override for a rejected safety constraint.** A proposal that cannot
yield a valid §12 representation shall be rejected with the reason shown, and learning shall not
silently widen, merge or otherwise alter observed positions to make them valid:

```
POSITION A   1490
POSITION B   1510

Cannot create valid ranges: positions are closer than §12.2.1 permits
```

**Confirmation re-baselines and never emits an edge — required.** Activating a newly confirmed
mapping shall establish a baseline from the current qualified control state and shall emit no edge
event as a consequence of confirmation. A level mapping takes the current level as its intent; an
edge-triggered mapping requires a subsequent qualifying transition before an event may be emitted.
This is the same semantic as startup and requalification, and shall use the same mechanism
(§12.4.3, §12.4.4, §25).

```
learn RECORD; the switch is currently HIGH
        ↓
confirm mapping
        ↓
baseline = HIGH, intent = RECORD
        ↓
no synthetic "entered range" event
```

Where live actuation resumes after learning ends, the real policy reconciles to that level in the
normal way. Reconciliation is not a fabricated edge, and the distinction is the whole point of the
rule.

**Persisted representation.** A confirmed learned trigger shall be stored using the explicit model
of §12 — AUX channel, explicit range or ranges, trigger semantics, target action — so that a
learned mapping and an equivalent manually entered mapping are **behaviourally identical** in `RUN`
and in `DIAG`. Trigger learning is a configuration mechanism and shall not create an alternative
runtime control representation. It shall never modify FC mode ranges, USER modes, PINIO
configuration, Programming Framework configuration or transmitter configuration, shall never inject
or override RC values, and shall require no FC configuration write (§3.1).

**Learnable actions** are generic to bridge actions rather than specific to recording: Record/Stop,
photo capture, preset selection, camera mode selection, HiLight/tag, camera power, and any other
explicitly supported action.

**Capability gating shall not depend on a live camera connection.** `SETUP` initialises no BLE
(§16.1), so there is no connected camera to interrogate and the absence of one carries no
information whatever. The offered set shall be derived from the **stored paired-camera identity and
its known capability profile**, together with the product-version scope of §26. Where no camera is
paired, or no trustworthy profile exists for the one that is, the interface shall offer the
product-version set and state that capability is unconfirmed. It shall **never infer that a
capability is absent from the absence of BLE.** Live confirmation, where it matters, belongs to
`DIAG` or to `RUN`.

**The generic learner shall never create an entry authority.** §16's authorities are fixed, audited
maintenance mechanisms, and the camera-action binding system shall not produce one whatever the
user confirms.

Selecting the Maintenance Entry AUX is a **separate and explicitly labelled workflow**, not an
action offered by that learner. It may reuse the same channel-discovery mechanism — there is no
reason to make a user count AUX channels by hand for this one setting — but it shall run every §16
prohibition unchanged, including the refusal of a channel or range overlapping ARM, PREARM,
failsafe, rescue or another safety-relevant FC mode, and **user confirmation shall not override a
refusal**. Discovery is shared; authority is not.

#### 16.2.4 `DIAG` — live end-to-end diagnostics

*(V1.2 — see §26; conditional on the peak-2 validation of §16.1.)*

`DIAG` is an explicitly entered bench mode that runs the **real** control path with a Wi-Fi
observability interface attached to it. It is the one mode in which the Wi-Fi AP and a camera BLE
connection are both resident, and the only capability in this specification that requires it.

**What `DIAG` adds over `SETUP` is exactly one thing: the camera.** Trigger learning (§16.2.3) and
dry-run observability (§16.2.1) run in `SETUP` at peak 1 and are V1 requirements. `DIAG` extends the
same picture to the right-hand end of the path — command sent, acknowledgement, camera-confirmed
state — and that extension is the whole of the mode. Where `DIAG` is unavailable a user can still
discover, configure and dry-run every mapping, and validate it in `RUN` through normal camera
behaviour and the OSD.

**This whole subsection is V1.2 and conditional on the §16.1 peak-2 validation (§26).** Holding the
AP and a camera BLE connection at once *is* peak 2, so nothing in `DIAG` survives that measurement
failing; a mode without its camera would be `SETUP` under a second name. Requirements below are
therefore of the form *where `DIAG` is implemented*, and none of them gates V1.

```
FC MSP_RC
   ↓
qualification / validity  (§12.4)
   ↓
ControlPolicy             (§12.1.1, §12.2.1, §12.3)
   ↓
camera command
   ↓
BLE camera
   ↓
camera-confirmed state    (§7)
   ↓
setup interface observes
```

The interface **observes** that path; §16.2.1's rule that the setup interface shall not reimplement
runtime trigger behaviour applies here unchanged. At minimum it shall distinguish:

```
observed RC/AUX input
range and hysteresis state
qualification state
resulting intent
camera command generated / sent
command failure or timeout
camera acknowledgement
camera-confirmed resulting state
```

For example:

```
AUX5              1812 µs
RECORD range       ACTIVE
Input              QUALIFIED
Intent             RECORD
Command            START_RECORD
BLE                SENT
Camera             ACK
Confirmed state    REC 00:03
```

That taxonomy is the feature's whole value. Watching the OSD tells a user whether recording
started; it cannot separate *command never generated* from *command sent and lost* from *capability
unavailable on this camera*, which are three different configuration errors with three different
fixes.

Requirements:

- `DIAG` shall be entered only by explicit action from an authorised Setup session (§16, §16.1),
  and never autonomously;
- trigger learning (§16.2.3) shall be available in `DIAG` on the same terms as in `SETUP`, so that
  learning a mapping and validating it against the real camera complete in one session with no
  intervening restart. Camera actuation is suspended while a learning operation is active, per that
  section;
- **camera-confirmed state remains authoritative exactly as in normal operation** (§7). The
  interface shall not report success merely because a command was issued;
- live `ControlPolicy` state advances in `DIAG`, `manualStop` (§12.1.1) included. That is the point
  of the mode, and it is safe because `DIAG` exits by restart and §12.1.1 clears `manualStop` at
  boot. **No "reset diagnostics state" path shall be added**; the boot boundary is that path;
- `DIAG` exits by restart, either back to `SETUP` or to `RUN`;
- where the §16.1 peak-2 gate does not pass, `DIAG` shall be unavailable, and its absence shall not
  affect `RUN`, `SETUP`, `PAIR`, the §16.2.1 monitor or the §16.2.2 pairing workflow;
- **`DIAG` is a configuration diagnostic, not evidence about `RUN`.** Its observations are taken
  with an AP resident and a client attached, which changes heap and radio scheduling. Timing or
  reliability figures gathered in `DIAG` shall not be quoted as `RUN` behaviour.

**The interface shall make the two modes unmistakable.** A user shall not be able to mistake
dry-run observation for end-to-end camera operation, in either direction — believing a dry run is
live wastes a bench session, and believing a live session is a dry run points a running camera at
an untested mapping. `SETUP` shall state that camera commands are disabled and `DIAG` shall state
that controls operate the real camera, for example:

```
SETUP                                    DIAG
DRY RUN — CAMERA COMMANDS DISABLED       LIVE CAMERA TEST — CONTROLS OPERATE CAMERA
```

This agrees with what the OSD already shows: `SETUP` writes `SETUP` to its owned primary slot,
while `DIAG` presents normal camera truth exactly as `RUN` does (§16.1).

**The resource and coexistence validation is functional, and is not a §3.3 safety condition.** §16
requires that Setup Mode not be entered while the aircraft is being operated, and `DIAG` is reached
only through Setup Mode, so no flight is in progress and §3.3's control-link concern does not
arise. The question is narrower: whether the C3's single 2.4 GHz radio serves an AP and a BLE
connection reliably enough for the mode's output to be trustworthy. **No AP+BLE condition is
therefore added to the §27.2 sweep**, whose conditions are flight conditions.


## 17. Camera Telemetry Freshness

Every telemetry field tracks:

```
value, valid, lastUpdated
```

Values the camera does not report remain unavailable (`valid = false`). Estimated values are
never marked valid.

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

Recording unexpectedly stopping **while the configured Record control remains active** shall be
treated as a high-priority camera warning.

The warning is based on camera-confirmed state versus camera-control intent; it does not depend
on actual FC armed state.

**`REC STOPPED` shall remain active during a §12.4 freeze.** §12.4 freezes *the last accepted
intent*, so intent is precisely what is known. The rule is:

```
effective control intent during a freeze = last accepted intent
```

and camera-confirmed state continues to be compared against it. RX loss prevents **new RC
transitions**; it does not erase what the pilot last asked for.

The scenario this protects is the one that matters most: last accepted intent is `RECORD`, the RC
link drops, and the camera then stops on its own for card-full, battery or fault. Suppressing the
warning would hide a camera failure during exactly the flight segment the pilot most wants
recorded.

The one thing that is genuinely unevaluable during a freeze is a *new* control edge, and that is
already handled by §12.4.1 and §12.3 — not by silencing a camera-health warning.

**`REC STOPPED` requires a camera that said so.** The warning compares camera-**confirmed** state
against intent, so it can only be raised while the camera is connected and reporting. A camera
that has gone away — powered off, out of range, BLE dropped — produces `CAM LOST` (§17.1), and
`CAM LOST` is not a stop:

```
camera connected, reports recording = false   →  REC STOPPED
camera gone, last known state was recording   →  CAM LOST      ← never REC STOPPED
```

This is not a fine distinction. Deriving `REC STOPPED` from a disconnect would have the OSD assert
a camera-confirmed event on the strength of the camera being unreachable, which is the one thing
§7 exists to prevent. The corresponding acceptance criteria are two separate tests (§27.1, §24.2).

Separately, the `UNQUALIFIED` startup state of §12.4.3 and §12.4.4 raises **no** RC-loss warning:
there is no accepted intent yet to compare against, so no camera-health claim is being suppressed.

## 19. Firmware Updates

Bridge firmware shall be field-updatable, to allow new camera models, camera BLE protocol
changes, new FC versions, and compatibility fixes.

Firmware update is an **explicit maintenance procedure**, reachable only through Setup Mode and
therefore only through one of §16's four entry authorities. This is also why §16's USB-passthrough
maintenance protocol carries no firmware-transfer verbs: `BOOT_SETUP` reaches the AP, and this
section's update path takes it from there. Completing an update restarts the
bridge, and that restart carries the reset reason `ESP_RST_SW` like any other deliberate one — which
is precisely why §16.1 requires the retained boot request to be invalidated *before* a mode is
entered rather than after. A request left valid would be replayed by the post-update restart.
Documentation shall state that
bridge, camera and FC firmware updates must not be performed while the aircraft is being operated.

The bridge does not query FC armed state or implement an armed-state firmware-update interlock.
This is the same procedural boundary used for camera firmware maintenance: the operator, not the
bridge, is responsible for not initiating maintenance during flight. §16's entry authorities are
what keep that boundary meaningful: each requires either hands on the aircraft or a USB cable into
the flight controller, and none is satisfiable over a network or a radio link. Firmware update is
reachable only from an authorised Setup session and therefore inherits exactly that bound.

Camera support claims shall be tied to tested bridge firmware versions.

## 20. Camera Compatibility Policy

**Verified** — tested on physical camera hardware at a stated firmware version.
**Expected** — protocol-compatible model, not physically validated.
**Unsupported** — known incompatibility or missing protocol support.

Documentation and UI shall clearly distinguish verified from expected. A camera shall not be
promoted to verified based on protocol similarity alone.

### 20.1 GoPro

Open GoPro is GoPro's official, actively maintained programme (API version 2.0; repository last
updated 2026-06-08). BLE covers what §8 needs: start/stop capture, recording status, battery, SD
capacity, and resolution/frame-rate settings.

The supported-model list is an input to the compatibility matrix and shall be read per release.
Newest HERO models are not automatically covered.

### 20.2 DJI Osmo Nano

**Status: Unsupported, pending the §27.4 probe.** The Nano is the intended DJI-family target, but
unlike the Osmo Action line it has no official control protocol, and this specification does not
assume one will appear.

**Two protocol families**

**1. The official "R SDK" protocol** — what DJI's own Osmo Action GPS Bluetooth Remote speaks,
with an MIT-licensed ESP32 reference at `dji-sdk/Osmo-GPS-Controller-Demo` (ESP-IDF v5.5; ships
`sdkconfig.defaults.esp32c3`). Its device-ID table covers:

| Model | `device_id` |
| --- | --- |
| Osmo Action 4 | 0xFF33 |
| Osmo Action 5 Pro | 0xFF44 |
| Osmo Action 6 | 0xFF55 |
| Osmo 360 | 0xFF66 |
| **Osmo Nano** | *"Not supported yet. Please wait for a future firmware update."* |

That quotation is DJI's own text, from `docs/protocol_data_segment.md` in the reference
repository. It is corroborated independently: DJI's published accessory compatibility list marks
the Osmo Action GPS Bluetooth Remote Controller as **not compatible** with the Osmo Nano. The
reference repository has had no commits since 2025-11-17, so the promised firmware update has been
outstanding for the better part of a year and shall not be planned around.

**2. DUML** — DJI's universal binary protocol, reverse-engineered by the community and carried
over the same GATT profile the official protocol uses: service `0xFFF0`, notify `0xFFF4`, write
`0xFFF5`. Published implementations exist for the Pocket 3 (`lib-osmo-ble`, updated 2026-02, full
DUML framing with CRC8/CRC16) and for Action 3/4/5 plus Pocket 3 (`node-osmo`). Neither covers the
Nano.

**Working hypothesis**

The Nano is *expected* to speak DUML, because DUML is DJI-wide and the Nano is architecturally
closer to a Pocket 3 than to an Action. This is an inference and has not been tested. §27.4 tests
it. Until that probe passes, the Nano is **Unsupported** under this section's own policy and shall
not be advertised as anything else.

Consequence for the product: the Nano gates V1.1 only. V1 ships on GoPro and does not depend on
any of this.

**Hazard — silent write failure**

Characteristic `fff3` accepts writes *with response* and returns success while the firmware
discards the payload. Only `fff5` (write-without-response) actually processes DUML. A driver that
writes `fff3` looks entirely correct and does nothing. This is documented behaviour that already
caught an existing library; the driver shall write only `fff5`, and the probe shall confirm it.

**Licence**

The `LICENSE` file of DJI's reference repository is layered: the R SDK **protocol documents** are
under DJI's End User License Agreement, while the **demo software** is offered under the EULA
*and* the MIT License (© 2025 SZ DJI Technology Co., Ltd).

Because the Nano path is DUML rather than R SDK, DJI's MIT-licensed code is not the basis of the
Nano driver, and the EULA-encumbered protocol documents shall not be redistributed. Should DJI
later add the Nano to the official protocol, the official path shall be preferred over DUML and
this section revisited.

## 21. Performance and MSP Citizenship

### 21.1 FC polling

| Data | Rate | Command |
| --- | --- | --- |
| Configured AUX camera controls | 10 Hz, or 20 Hz when a Push Button control is enabled (§12.3) — only when an AUX control is enabled | `MSP_RC` |
| **Trigger learning, while an operation is active** | **20 Hz regardless of configured control modes**; normal policy resumes when learning ends (§16.2.3) | `MSP_RC` |
| Betaflight RX-validity guard | 10/20 Hz, bracketing the `MSP_RC` poll per §12.4.1; plus one extra read on a transition-bearing sample | `MSP_STATUS` → `armingDisableFlags.ARMING_DISABLED_RX_FAILSAFE` ([BF] `msp/msp.c:1128`) |
| **Betaflight qualification grace** | **once per FC detection**, never in steady state | `MSP_FAILSAFE_CONFIG` → first byte = `failsafe_delay`, U8 tenths ([BF] `msp/msp.c:1657`); §12.4.3 condition B |
| **INAV startup qualification** | **until qualified only**, at the control poll rate; never polled again once qualified, and once any §16 maintenance window has closed | `MSP2_INAV_STATUS` → `armingFlags` U32 ([INAV] `fc/fc_msp.c:459`); §12.4.4 |
| **§16 maintenance-entry window, while open** | ≥ 10 Hz, **regardless of whether any AUX camera control is enabled**; stops permanently when the window closes (§16) | `MSP_RC` |
| **§16 maintenance-entry armed interlock, while the window is open** | at the same rate as that window's `MSP_RC` poll; stops permanently when the window closes (§5.2, §16) | Betaflight: `MSP_STATUS` flight-mode flags, resolved through the `MSP_BOXIDS` mapping read once at identification. INAV: the `armingFlags` U32 already read for §12.4.4 |
| Mode ranges (overlap advisory) | setup only, never runtime control | `MSP_MODE_RANGES` (both platforms); `MSP_MODE_RANGES_EXTRA` Betaflight-only, diagnostic (§12.6) |
| Logic condition / GVAR trigger (INAV, if bound) | 10 Hz | `MSP2_INAV_LOGIC_CONDITIONS_STATUS` / `..._GVAR_STATUS` |
| Camera state | event driven | — |
| Record timer | 1 Hz | — |
| Camera battery | ≤ 1 Hz | — |
| Storage | ≤ 1 Hz | — |
| Resolution / FPS | on change | — |
| GPS (only if §14 push enabled) | ≤ 10 Hz | `MSP_RAW_GPS` |

**There is no steady-state arm-state poll after qualification.** That is the accurate form of the
rule; "no runtime arm-state poll" would not be, because a bridge that reboots at altitude on INAV
*must* keep issuing `MSP2_INAV_STATUS` during flight until it sees a trustworthy disarmed state
with `ARMING_DISABLED_RC_LINK` clear (§12.4.4).

**The maintenance window is bounded, not steady state.** Its two rows exist only while a §16
window is open — which requires a configured Maintenance Entry AUX and a power-on boot — and stop
permanently when it closes, whether or not the gesture was recognised. With no Maintenance Entry
AUX configured neither row exists at all. The first of the two is required because the ordinary
`MSP_RC` row above polls *only when an AUX camera control is enabled*: a bridge with no camera
control configured yet — the state a bench user is most likely to be in when reaching for the
gesture — would otherwise have no RC input for the recogniser to consume.

The two qualification reads are bounded by qualification, not by flight phase:
`MSP_FAILSAFE_CONFIG` is read once per FC detection, and the INAV `armingFlags` read stops the
instant the input qualifies and never resumes unless the input becomes unqualified again. Neither
appears in the steady-state budget below. An implementation still issuing either **after
qualification** has misread §5.2; one issuing them *before* qualification, in flight, is behaving
correctly.

Arm Record is an AUX control preset (§12.1), so it uses the same `MSP_RC` path as every other
level-controlled recording mode. If no AUX/Logic-Condition camera control is enabled, the bridge
need not poll RC/control state at all.

On Betaflight, `MSP_STATUS` is polled around `MSP_RC` **only** to obtain the RX-validity guard of
§12.4, and it **brackets** rather than accompanies the RC read — the two are not one FC snapshot
(§12.4.1). Steady-state cost is two transactions per poll; a third is issued only when the RC
sample would produce a camera-control transition. On INAV 8 no equivalent status poll and no
bracket are required, because `MSP_RC` retains last-good channel data during RX loss.

Worst-case Betaflight budget: 3 transactions × 20 Hz = 60 MSP round-trips per second at 115200
baud, and only while a Push Button control is enabled and actively transitioning. Steady state is
40. If a target cannot meet §21.2 at that rate, the Push Button poll rate is reduced before the
bracket is weakened — the bracket is a correctness requirement and the 20 Hz rate is a latency
preference.

The bridge shall cache the last accepted RC sample. A missing or late reply shall not block the
MSP task and shall not synthesize a control transition; an incomplete bracket is treated exactly
as a failed bracket, i.e. the candidate is discarded, not committed. Normal retry/backoff rules in
§21.2 apply.

### 21.2 Quiet-bridge requirements

The bridge is a guest on the FC's MSP port and shall:

- never hold the port busy waiting on a camera or BLE operation;
- tolerate a missing or late reply without blocking;
- never retry-storm — failed requests back off;
- never write an OSD slot whose rendered content is unchanged;
- in telemetry-disabled compatibility mode (§5.1), issue no OSD writes; only the read-only MSP
  queries required by enabled camera-control features may continue.

### 21.3 BLE duty cycle and backoff

Derived from §3.3. Concrete values are set by the §27.2 measurement; the requirements are
**state-independent** and do not use FC arm state:

1. Runtime reconnect to the paired camera is **directed only** (§15).
2. **Reconnect backoff:** consecutive failures shall increase the retry interval to a configured
   ceiling of ≥ 5 s; there is never a continuous reconnect loop.
3. **Broad/discovery scanning is allowed only after an explicit Pair/Setup action** — which per
   §16 requires one of four entry authorities, and which takes place in the `PAIR` boot mode
   (§16.1),
   never in `RUN`. Runtime camera loss shall never escalate automatically into discovery scanning.
4. **Duty-cycle ceiling:** the bridge shall have a stated maximum BLE runtime reconnect / scan
   duty cycle and shall meet it in the reconnect-storm case as well as nominal use.
5. **BLE transmit power** is an explicit coexistence parameter, configurable and stated, and is
   one of the variables swept by §27.2. The release configuration shall use the lowest validated
   power that maintains reliable camera communication.

## 22. Failure Requirements

The following shall not affect aircraft control, and each has a corresponding acceptance test in
§24.2:

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
- **`MSP_FAILSAFE_CONFIG` unreadable or unsupported**, removing condition B entirely (§12.4.3);
- **status polls timing out through a qualification interval**, so that wall-clock time passes
  without any observation being made (§12.4.3);
- **bridge rebooted while the aircraft is armed**, where INAV's qualification flag is no longer
  being maintained (§12.4.4);
- **a validity bracket that straddles an FC failsafe transition** (§12.4.1 guarded commit);
- **interruption of a `PAIR` boot** by watchdog, panic, brownout, external reset or power loss
  (§16.1);
- **a retained boot request surviving into an unrelated software restart**, including the restart
  that completes a firmware update (§16.1 consume-before-enter, §19);
- **a stale setup page submitting a candidate selection** from a scan that is no longer current
  (§16.2.2 scan token);
- **an in-flight interruption of the bridge's 5 V supply presenting as a power-on reset**, which
  would otherwise re-open §16's maintenance gesture window while airborne (§4.2, §16, §5.2);
- **a maintenance-protocol frame arriving on the FC UART** outside a host-initiated passthrough
  session (§16);
- **`DIAG` requested on a build whose peak-2 resource gate did not pass** (§16.1, §16.2.4).

The FC shall continue operating normally with the bridge electrically connected but completely
non-functional. Per §3.3, "not affecting aircraft control" includes not degrading control-link
quality.

## 23. Installation Requirement

Target installation: four solder joints and one spare UART.

1. Connect 5 V, ground, TX and RX (observing the §4.2 peak-current note).
2. Enable MSP on the selected FC UART.
3. Enable and position the required OSD elements.
4. Enter `SETUP` (§16) and configure camera controls by **learning** them (§16.2.3): select the
   bridge function, exercise the intended transmitter control, review the proposed AUX channel,
   positions, ranges and control type, and confirm. Verify the result against the dry-run monitor
   (§16.2.1) before enabling actuation.
5. Power camera.
6. Pair (§16.2.2).
7. Verify end to end — in `DIAG` where it is available (§16.2.4), otherwise by returning to `RUN`
   and watching normal camera behaviour and the OSD.
8. Fly.

```
SETUP → select function → Learn Trigger → move the control → review → confirm → dry-run verify
                                                                                      ↓
                                              DIAG available?  ── yes ──► live camera verify
                                                                └─ no ──► RUN, verify via OSD
```

**Manual AUX/range entry remains available as the advanced and fallback path**, not the primary
installation route. For **Arm Record**, the control learned is the one normally used by the radio
arm switch; review the overlap advisory (§12.6), where the ARM overlap is expected for this preset.
If the aircraft is armed by stick rather than switch, configure Record on any other AUX channel
(§12.1).

No CLI commands shall be required for the default supported configuration where the FC
configurator exposes all required settings.

**One documentation note for the installer:** saving FC configuration while the OSD is showing
live camera text persists that text into the FC's stored configuration, on both platforms (§9,
§10). The bridge clears its owned slots at every startup, so this only shows up if the bridge is
later absent or slow to start. Saving with the camera off avoids it entirely.

## 24. Acceptance Matrix

A release shall not claim platform support until §24.1 and §24.2 pass.

### 24.1 Function

| Test | Betaflight 2025.12.5 | INAV 8.0.1 |
| --- | --- | --- |
| FC auto-detection | Required | Required |
| **Version floor: a build below the §0 minimum enters `UNSUPPORTED_FC_VERSION` — no camera-control input acted on, no platform OSD write issued — while identification and setup remain available (§5.1)** | Required | Required |
| **Version floor precedes capability probing: on an out-of-contract build that would answer the probe successfully, the probe result does not enable the backend (§5.1)** | n/a | Required |
| **`MSP_FC_VERSION` byte triple decoded correctly across the calendar-versioning transition — a 4.5-era build and a 2025.12-era build are both ordered against the floor by the same comparison (§5.1)** | Required | n/a |
| OSD capability probe / graceful degrade (§5.1) | Required | Required |
| **The control mapping is invariant under armed state: a given control in a given range produces the same camera action whether the FC is armed or disarmed (§5.2)** | Required | Required |
| **No arm-state poll in steady state after qualification, verified by MSP capture across a full flight (§5.2, §21.1)** | Required | Required |
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
| **Betaflight qualification condition B: a continuously clear guard qualifies only after the computed `grace` from first MSP exchange — measured, and **never earlier** than computed — and the grace tracks both a reconfigured `failsafe_delay` and a changed poll rate (§12.4.3)** | Required | n/a |
| **Betaflight condition B continuity: status-poll timeouts during the interval extend it rather than being counted as clear observations (§12.4.3)** | Required | n/a |
| **Betaflight fallback: with `MSP_FAILSAFE_CONFIG` denied, the bridge qualifies on condition A only and never on a timer (§12.4.3)** | Required | n/a |
| **INAV startup, no-receiver case: with the transmitter off, `ARMING_DISABLED_RC_LINK` reads asserted and no spurious record occurs for a control range spanning `midrc` (§12.4.4)** | n/a | Required |
| **INAV qualification stops reading `armingFlags` once qualified, and once any §16 maintenance window has closed — no runtime arm-state poll thereafter (§5.2, §21.1)** | n/a | Required |
| **Guarded commit: a transition-bearing RC sample is committed only under a passing `STATUS₁ → RC → STATUS₂` bracket; a failed bracket discards the candidate (§12.4.1)** | Required | n/a |
| **Recovery latency: guard clear is awaited rather than timed; level control reconciles after both TX-off recovery and `BOXFAILSAFE` revert, whatever the two latencies are (§12.4.2)** | Required | n/a |
| **`Combi` arbitration matches the §12.1.1 truth table, including override clearing when the level control leaves its range** | Required | Required |
| **Range evaluator: ±10 µs hysteresis observed; sub-40 µs range rejected at setup (§12.2.1)** | Required | Required |
| INAV 8 `MSP_RC` retains last-good AUX values through RX loss | n/a | Required |
| No spurious Push Button event on Betaflight RX recovery re-baseline (§12.4) | Required | n/a |
| Overlap advisory reports a camera range overlapping an FC mode; ARM overlap remains allowable (§12.6) | Required | Required |
| **Setup Mode is entered only through one of §16's four authorities; no network, runtime-MSP, BLE or remote path reaches it (§16)** | Required | Required |
| **`DIAG` and `PAIR` cannot be entered except from an authorised Setup session — no entry authority reaches either directly (§16, §16.1)** | Required | Required |
| **Boot with the Setup button held enters `SETUP` regardless of the retained boot request's contents, and without consulting the configuration store or stored camera identity (§16.1)** | Required | Required |
| **A deliberate `RUN → SETUP` restart carries the retained boot request across `esp_restart()` on the pinned ESP32-C3 build (§16.1, §27.5)** | Required | Required |
| **The retained request is invalidated before the requested mode executes: a later unrelated `ESP_RST_SW` — including the restart that completes a firmware update — does not replay a consumed request (§16.1, §19)** | Required | Required |
| **`SETUP` runs no live `ControlPolicy`: switch activity during Setup Mode leaves no `manualStop`, baseline or camera-control state to carry into the following `RUN` (§16.1, §16.2.1, §12.1.1)** | Required | Required |
| **Live trigger monitor reflects the runtime path: observed AUX value, hysteresis latch, range activity, resulting intent and qualification state all match what the live control path does with the same input (§16.2.1)** | Required | Required |
| **Monitor with the transmitter off displays `UNQUALIFIED` with its reason, and issues no camera command (§16.2.1)** | Required | Required |
| **Pairing from an already-authorised Setup session requires no second entry authorisation, and each scan is bounded and terminates on its own (§16.2.2)** | Required | Required |
| **A `PAIR` boot that finds nothing, times out, or fails to bind returns to `SETUP` with `NO_CAMERAS` / `SCAN_TIMEOUT` / `BIND_FAILED` / `BIND_TIMEOUT` — not to `RUN` (§16.1, §16.2.2)** | Required | Required |
| **A `PAIR` boot that reaches its 120 s maximum lifetime terminates rather than continuing to scan or retry, and the next boot is `RUN` — a lifetime expiry is a fault, not a normal outcome (§16.1, §16.2.2)** | Required | Required |
| **`PAIR` operation bounds are enforced individually: a 30 s scan expiry yields `SCAN_TIMEOUT` and a 15 s directed-bind expiry yields `BIND_TIMEOUT`, and both return to `SETUP` rather than running the boot to its lifetime cap (§16.2.2)** | Required | Required |
| **A scan finding more than 8 supported candidates retains 8 and reports the list as truncated, rather than dropping the excess silently or growing the cache (§16.2.2)** | Required | Required |
| **A candidate selection carrying a stale scan token is rejected, and entering `RUN` empties the candidate cache and invalidates its token (§16.2.2)** | Required | Required |
| **Exit `SETUP` → `RUN` performs full FC detection, version/capability validation and startup qualification, and synthesises no camera-control edge from switch activity that occurred during Setup Mode (§16.1, §12.4.3, §12.4.4)** | Required | Required |
| **FC MSP polling continues throughout `SETUP`; `PAIR` initialises no MSP link (§16.1)** | Required | Required |
| **Owned OSD slots read `SETUP` during Setup Mode, with no `CAM LOST` raised by the deliberate disconnection (§16.1, §17.1, §18)** | Required | Required |
| **Trigger learning identifies the exercised AUX channel and its stable positions, proposes explicit §12 ranges without assuming 1000/1500/2000 µs, and stores a mapping behaviourally identical to the same mapping entered by hand (§16.2.3)** | Required | Required |
| **Learning predicates match §16.2.3 exactly: baseline is the median of 5 qualified samples; 100 µs over 2 consecutive samples is *moved* while 40 µs and a single-sample 100 µs excursion are not; a 5-sample window of max−min ≤ 20 µs is *stable* while a 22 µs spread and a 4-sample window are not (§16.2.3)** | Required | Required |
| **Learning's three rejections are distinct and differently reported: *ambiguous* (≥2 channels moved), *no movement detected* (none moved before the 15 s acquisition timeout) and *unstable* (moved but never held a stable position) (§16.2.3)** | Required | Required |
| **A learning acquisition left open expires at 15 s, returns to idle, writes no configuration and reverts the poll rate (§16.2.3, §21.1)** | Required | Required |
| **Learning thresholds are not §12.2.1's: a mapping learned at these thresholds and the same mapping entered by hand evaluate identically at runtime under §12.2.1's ±10 µs hysteresis (§16.2.3, §12.2.1)** | Required | Required |
| **Trigger learning is available in `SETUP` with no camera present and no BLE initialised — it does not depend on the §16.1 peak-2 gate (§16.2.3)** | Required | Required |
| **Resource floors (§16.1): each mode meets ≥ 32 KiB free and ≥ 16 KiB largest allocatable block under its specified workload, and neither figure degrades monotonically across repeated operations — `RUN` over ≥ 30 min and ≥ 10 reconnect cycles (§16.1, §27.3)** | Required | Required |
| **MSP command policy is closed: an MSP capture across a full session contains only §3.1.1 allowlist commands, and exactly two write commands appear in the whole product — the platform's OSD injection command and nothing else (§3.1)** | Required | Required |
| **`MSP_RTC` is resolved per platform — 247 on Betaflight, 246 on INAV — and no shared constant is used; an MSP capture shows no `MSP_SET_RTC` on either platform (§3.1.2, §14)** | Required | Required |
| **`MSP_RC` is polled at 20 Hz for the duration of a learning operation regardless of configured control modes, and reverts afterwards; a 250 ms momentary demonstration is characterised, not missed (§16.2.3, §21.1, §12.3)** | Required | Required |
| **Camera actuation is suspended while learning is active, and the live policy re-baselines when it ends — movements made solely to teach a control produce no camera command and change no runtime control state (§16.2.3)** | Required | Required |
| **Confirming a mapping whose control is already inside the learned range establishes a baseline and emits no edge; an edge-triggered mapping requires a later qualifying transition (§16.2.3, §12.4.3, §12.4.4)** | Required | Required |
| **A learned proposal that cannot yield a valid §12 representation — positions closer than §12.2.1 permits — is rejected with the reason shown, and is never silently widened or merged (§16.2.3, §12.2.1)** | Required | Required |
| **Learning is subject to every restriction a manual mapping is: user confirmation does not override a rejected safety constraint, and the generic camera-action learner never produces a §16 entry authority (§16.2.3, §16)** | Required | Required |
| **Maintenance Entry AUX selection, where offered, runs §16's overlap prohibition unchanged: a channel or range overlapping ARM, PREARM, failsafe or rescue is refused, and user confirmation does not override the refusal (§16, §16.2.3)** | Required | Required |
| **Learnable actions are offered from the stored paired-camera capability profile and the product-version scope, never inferred from the absence of BLE; with no camera paired the product-version set is offered and marked unconfirmed (§16.2.3, §16.1)** | Required | Required |
| **Where `DIAG` is implemented: `SETUP` and `DIAG` are distinguishable in the interface — dry-run cannot be mistaken for live camera operation (§16.2.4)** | Optional | Optional |
| **Maintenance gesture pattern: four sustained transitions beginning and ending at the baseline, the fourth within 3.0 s of the first; a single-sample excursion is neither a transition nor a reset; a control left displaced never completes the pattern (§16)** | Required | Required |
| **Maintenance gesture: with a Maintenance Entry AUX configured, the pattern in the startup window restarts the bridge into `SETUP`, and a static AUX position present at startup never does (§16)** | Required | Required |
| **Maintenance gesture is absent by default: with no Maintenance Entry AUX configured, no window opens, no OSD gesture text appears, and no arm-state lookup is performed (§16, §5.2)** | Required | Required |
| **Setup refuses a Maintenance Entry AUX whose range overlaps ARM, PREARM, failsafe or rescue — a rejection, not the §12.6 advisory — and never derives the selection from the Record mapping (§16, §12.6, §23)** | Required | Required |
| **Gesture window is visible: `SETUP GESTURE` while open and `SETUP n/4` as transitions register, returning to normal camera content at expiry (§16)** | Required | Required |
| **Where `DIAG` is implemented: input, intent, command-sent, acknowledgement and camera-confirmed state are distinguished, and success is never reported from command issue alone (§16.2.4, §7)** | Optional | Optional |
| **Where `DIAG` is implemented: a mapping is learned and then validated against the real camera within one session, with no restart between confirmation and validation (§16.2.3, §16.2.4)** | Optional | Optional |
| **`DIAG` exits by restart, and `manualStop` exercised there does not survive into the following `RUN` (§16.2.4, §12.1.1)** | Optional | Optional |
| **Betaflight USB passthrough `BOOT_SETUP` reaches `SETUP` after both ends restart, with passthrough invoked using the `reset` DTR action and a power cycle documented as the fallback exit; the protocol exposes no firmware-transfer verb and no `BOOT_DIAG` / `BOOT_PAIR` (§16, §19)** | Required | n/a |
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

Every row below has two pass criteria: **(a)** FC behaviour unchanged, and **(b)** control-link
sensitivity/link margin not measurably degraded versus the bridge-absent baseline, where RF
activity is relevant.

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
| **Betaflight: every `MSP_STATUS` reply suppressed during a condition-B interval** | the interval never accumulates a successful observation; the bridge never qualifies, however long it is left (§12.4.3) |
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
| **Two supported cameras advertising during a `PAIR` scan, no selection made** | neither is bound; the stored paired identity is unchanged; no strongest-advertiser default (§16.2.2) |
| **Watchdog forced during a `PAIR` boot** | next boot is `RUN`; the retained request is not honoured; `PAIR` does not resume itself (§16.1) |
| **Brownout or external reset during a `PAIR` boot** | next boot is `RUN` (§16.1) |
| **Power removed during a `PAIR` boot** | next power-on is `RUN`; no maintenance mode is entered unattended (§16.1) |
| **BLE stack hung during a `PAIR` boot so that no operation bound ever fires** | the 120 s lifetime cap terminates the boot independently of every operation inside it; next boot is `RUN`; the device does not sit in a maintenance boot (§16.1, §16.2.2) |
| **Retained boot-request memory corrupted or filled with arbitrary content, then a deliberate `esp_restart()`** | the request fails its validity check and the bridge enters `RUN`; no mode is entered from unvalidated retained state (§16.1) |
| **Setup button held during a boot whose retained memory holds a valid `PAIR` request** | `SETUP` is entered; the held-button path takes priority and depends on nothing persistent (§16.1) |
| **Configuration store corrupted, then Setup entered by held button** | Setup Mode is reachable and firmware update remains available; the escape hatch does not depend on the damaged store (§16.1, §19, §22) |
| **Candidate selection submitted from a setup page held open across a later scan** | the stale scan token is rejected; no camera is bound from a superseded candidate list (§16.2.2) |
| **Bridge 5 V supply interrupted and restored while the FC stays armed, then the gesture pattern is performed** | the arming interlock refuses recognition; no restart into `SETUP` and no Wi-Fi AP is raised while armed (§16, §5.2, §4.2) |
| **Gesture pattern performed after a watchdog, panic or software restart rather than a power-on** | no window ever opens; eligibility is decided from `ESP_RST_POWERON` alone (§16) |
| **Gesture partially performed, then RC validity lost / an out-of-pattern transition / the FC arms / the window expires** | recognition resets in every case; a later resumption of the pattern does not complete it (§16) |
| **Maintenance-protocol frame injected on the FC UART outside a passthrough session** | rejected; the bridge does not enter `SETUP`, and its own MSP client state is unaffected (§16) |
| **`DIAG` requested on a build whose peak-2 resource gate did not pass** | `DIAG` is unavailable; `RUN`, `SETUP`, `PAIR`, the §16.2.1 monitor, §16.2.2 pairing and §16.2.3 trigger learning are unaffected, and every mapping remains configurable (§16.1, §16.2.4) |
| **Passthrough invoked without the `reset` DTR action, then `BOOT_SETUP` sent** | the bridge restarts into `SETUP` normally; the FC remains in its byte-copy loop and serves no MSP until power-cycled ([BF] `io/serial.c:692`). This is a host-invocation defect, not a bridge fault, and the bridge shall not attempt to recover the FC (§16) |
| **Maintenance Entry AUX configured with no AUX camera control enabled** | the window still polls `MSP_RC` and the gesture is recognisable; the ordinary control-poll row's "only when an AUX control is enabled" condition does not apply to the window (§21.1, §16) |
| **Maintenance gesture partially performed, then the control left displaced** | the pattern never completes — it must end at the baseline — recognition resets on pattern-timer expiry, and the window closes normally into `RUN` (§16) |
| **Two or more AUX channels moved ≥ 100 µs during a learning operation** | the attempt is rejected as ambiguous and a repeat requested; no channel is guessed — not by largest movement, not by lowest index, not by first to cross — and no configuration is written (§16.2.3) |
| **A learning operation in which no channel moves ≥ 100 µs before the 15 s acquisition timeout** | rejected with *no movement detected*, distinguished from the ambiguous case above; a channel drifting or jittering below the threshold never becomes a candidate; the operation returns to idle and reverts the poll rate (§16.2.3, §21.1) |
| **A channel moved well past 100 µs but never held within a 20 µs spread for 5 samples** | rejected as *unstable* — a third outcome distinct from ambiguous and from no-movement; no range is proposed from an unsettled value (§16.2.3) |
| **Two channels each moved ≥ 100 µs, 10 s apart, inside one acquisition window** | rejected as ambiguous exactly as if they had moved together; the acquisition timeout is what makes "within one operation" a bounded claim (§16.2.3) |
| **Learning operation abandoned — client disconnects, page closed, or the operation times out** | no persistent configuration change; polling reverts to normal policy; live actuation resumes with a re-baseline and no edge (§16.2.3, §21.1) |
| **RC validity lost partway through a learning operation** | unqualified samples are not characterised; the operation does not complete from stale or substituted values (§16.2.3, §12.4) |
| **Learning attempted with the transmitter off** | the input never qualifies, no candidate is proposed, and the monitor states `UNQUALIFIED` with its reason (§16.2.3, §16.2.1, §12.4.3, §12.4.4) |

The Betaflight `RX_FAILSAFE_MODE_SET` / `BOXFAILSAFE` rows are the direct tests of the
failsafe-substitution hazard in §12.4 and shall not be waived. The **recovery** rows exist because
the guard's clear latency differs between switch-induced and transmitter-loss failsafe, and
differs again with configuration; a tester who sees only one of them will report the others as
defects. The two transmitter-OFF rows are the ones to run first: a bridge that starts recording on
a bench with no transmitter has failed the only test in this document that a user will discover by
accident.

The two `MSP_FAILSAFE_CONFIG` rows and the `failsafe_delay` sweep exist for the same reason as the
`failsafe_recovery_delay` sweep above — to prove that a configurable FC value is being read rather
than assumed. A bridge that passes at stock settings and fails at 5.0 s has hardcoded something.

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

**The boot-mode architecture of §16.1 is standalone-only.** `RUN` / `SETUP` / `PAIR`, the retained
boot request, the reset-reason gate and the mode-specific OSD behaviour are properties of the
standalone bridge and shall not be promoted into the shared core, nor assumed by an integrated
build. An integrated receiver cannot treat a reboot as a cheap maintenance transition, because the
radio it would restart is the control link; and it has no Wi-Fi/BLE ownership conflict of this
shape to resolve. §16.2's configuration diagnostics are scoped the same way.

The ELRS-integrated implementation is **not** in the initial standalone validation envelope.
Because ELRS is flight-critical, it requires a separate acceptance process proving that enabling
the camera subsystem cannot materially affect ELRS timing, RF reception, LQ, packet loss,
reconnect, watchdog behaviour, memory integrity, or receiver stability.

Note that §3.3 applies doubly there: an integrated build shares not only the band but the same
MCU, scheduler and heap as the control link.

An integrated build would also have direct access to RC channel data and link validity in the same
address space and the same instant, removing the Betaflight `MSP_RC` failsafe ambiguity at source
— and with it the guarded-commit bracket (§12.4.1) and the recovery-window semantics (§12.4.2).
Both exist only because an arming-permission flag is being borrowed as an input-validity signal and
read through a transport that cannot deliver the two together. That is a property of that
architecture, not a reason to weaken §12.4 in the standalone product. Neither requires actual FC
armed-state knowledge.

**Startup qualification does not disappear.** An integrated receiver still boots before its first
valid RC frame, and the question "has real control input ever arrived?" is not a transport artifact
— it is a property of every system that reads a control input. What disappears is only the
*mechanism*: no bracket, no `MSP_FAILSAFE_CONFIG` grace, no `armingFlags` read, because an
integrated build knows `valid RC frame arrived` directly and atomically. The semantics survive
intact:

```
before the first valid sample:   UNQUALIFIED, no camera-control transitions
the first valid sample:          becomes the baseline; reconcile level controls; emit NO edge
subsequent samples:              normal processing
```

This is the correct place for the seam, and drawing it here makes the shared core smaller rather
than larger:

```
PlatformAdapter  →  ControlSample { values, valid, epoch }

ControlPolicy    →  first valid sample = baseline, never an edge
                    §12.1.1 Combi arbitration
                    §12.2.1 range evaluator + hysteresis
                    §12.3   Push Button timing
```

Standalone Betaflight, standalone INAV and an integrated ELRS build then differ in exactly one
thing: **how they produce `valid`**. Betaflight produces it from a bracketed guard plus the §12.4.3
grace; INAV from `ARMING_DISABLED_RC_LINK` while disarmed (§12.4.4); an integrated build from the
receiver itself. Everything downstream of `ControlSample` is written once.

## 26. V1 Scope

### V1 — Required

- standalone ESP32-C3 bridge;
- Betaflight 2025.12.5+ and INAV 8.0.1+ backends (§0);
- GoPro BLE driver;
- automatic directed reconnect with state-independent backoff (§21.3);
- direct AUX-controlled recording (§12), including the **Arm Record** convenience preset that
  follows a configured arm-switch AUX rather than actual FC armed state;
- Betaflight failsafe-substitution guard with guarded commit, recovery and startup-qualification
  semantics, and INAV last-good-value validation (§12.4);
- `Combi` arbitration (§12.1.1) and the ±10 µs range evaluator (§12.2.1);
- Push Button timing (§12.3);
- actual camera-state feedback (§7) with staleness handling (§17);
- the four OSD fields of §8;
- OSD warnings (§18);
- slot ownership configuration (§9.1, §10.2);
- setup-time overlap advisory (§12.6);
- one-button pairing, with the Setup-Mode pairing workflow and its selection rules (§16.2.2);
- Setup Mode (§16) under its four entry authorities, including the live trigger monitor (§16.2.1);
- reboot-separated `RUN` / `SETUP` / `PAIR` boot modes with the boot-selection rule,
  consume-before-enter semantics and mode-specific OSD behaviour of §16.1;
- §16's four Setup Mode entry authorities, with `SETUP` as the sole hub from which `DIAG` and
  `PAIR` are launched — including the optional Maintenance Entry AUX gesture (power-on-gated,
  arming-interlocked, OSD-visible, unconfigured by default) and the four-verb Betaflight USB
  passthrough protocol;
- **trigger learning (§16.2.3) in `SETUP`**, with dry-run observability, using qualified
  FC-effective input and storing into §12's existing mapping representation. This is the primary
  route by which a user configures a control mapping, and it is **unconditional** — it needs no
  camera, no BLE and no peak-2 validation;
- firmware updates with documented maintenance procedure (§19), with no FC-arm-state interlock
  required;
- optional Wi-Fi configuration through explicit Setup Mode (§16), never entered autonomously;
- §24.1 and §24.2 passing.

### V1.1 — conditional on the §27.4 probe

DJI Osmo Nano driver, over DUML (§20.2). Unlike a re-host of DJI's vendor reference, this is
unproven work: no official protocol support exists for this model and no published DUML
implementation covers it. The probe runs independently of V1 and may return "not viable", in which
case V1.1 either escalates to a Mimo-app BLE capture or substitutes an officially supported model
(Osmo Action 6 / Osmo 360), which would be a low-risk re-host of the MIT-licensed reference.

### V1.2 — conditional by camera capability

Photo capture; preset switching (§13); HiLight/tag; camera clock and GPS push (§14); additional
telemetry; optional INAV Logic Condition trigger (§12.7) if not delivered in V1; **the `DIAG` boot
mode in its entirety (§16.1, §16.2.4)**, conditional on the §16.1 peak-2 validation passing on
production ESP32-C3 hardware.

**`DIAG` is V1.2 whole, and no part of it is a V1 requirement.** The mode is defined by holding the
Wi-Fi AP and a camera BLE connection at once, which *is* peak 2 — so there is no residue of `DIAG`
that could survive the measurement failing, and listing the mode in V1 while its only content is
V1.2 would be a requirement that peak-2 failure falsifies. What `DIAG` adds over `SETUP` is the
camera (§16.2.4); without it the mode would be `SETUP` under another name.

**Simultaneous Wi-Fi AP and BLE operation is required by `DIAG` and by nothing else.** It is a
property of one boot mode rather than an exception granted inside a running one, which is why it
costs a measurement rather than an invariant.

**Failure of the peak-2 validation shall not block V1 release, and shall not affect trigger
learning or any other configuration capability.** Learned mappings remain fully usable and are
validated in `RUN` through normal camera behaviour and the OSD. Live validation is scoped to V1.2
rather than to a conditional V1 requirement precisely so that this cannot become a release
question: a capability that may be absent is not a release criterion (§24).

### Explicitly out of scope

- flight-controller firmware modification;
- primary-stick maintenance gestures (§27.6 records the spike that would be needed first);
- a serial firmware flasher over the §16 maintenance protocol;
- VTX-specific integration;
- RC override, motor or servo control;
- required mobile app;
- interpreting or enforcing FC armed/disarmed state;
- ELRS-integrated production implementation.

## 27. Engineering Spikes Before Implementation

Six spikes, each with pass criteria. Whether stock INAV 8 can accept runtime camera data for its
Custom OSD Elements is already answered in source (§10); §27.1 confirms it on hardware.

### 27.1 FC interface, control input and OSD injection on hardware

Confirm the source reading of §5, §9, §10 and §12 on real flight controllers.

Pass criteria:

1. `MSP_RC` returns live AUX values while armed on both platforms, matching the FC Receiver tab
   for configured AUX channels.
2. **Arm Record follows the configured arm-switch AUX/range even when actual FC arming is
   deliberately prevented** (for example by an unmet prearm condition). The camera-control result
   shall be identical whether the FC accepts or rejects arming.
3. Configured AUX thresholds and level changes produce the expected camera-control events without
   modifying FC mode, USER or PINIO configuration.
4. A 250 ms Push Button press is detected reliably; a 100 ms press is either detected or ignored
   but never double-fired (§12.3).
5. **Betaflight guard — assert.** `ARMING_DISABLED_RX_FAILSAFE` in `MSP_STATUS` asserts during real
   RX loss while armed and during `BOXFAILSAFE`-induced invalid data; `RX_FAILSAFE_MODE_SET` on the
   camera AUX produces no camera-control transition. *(The flag's validity while armed is
   established in source — `setArmingDisabled()` is an unconditional OR, [BF]
   `fc/runtime_config.c:73-76` — so this is a confirmation run, not an open question.)*
6. **Betaflight guard — clear.** Measure and record the clear latency after transmitter-loss
   recovery and after `BOXFAILSAFE` revert, then repeat with `failsafe_recovery_delay` set to 1
   (100 ms) and to 20 (2.0 s). Camera behaviour shall be identical across all four runs and only
   the reconcile latency shall change — which is the evidence that no delay value is encoded in the
   bridge (§12.4.2). Confirm level controls reconcile and no Push Button event is emitted by any
   re-baseline.
7. **Betaflight guard — startup qualification.** Seven runs (§12.4.3). In every one, confirm no
   camera-control transition before qualification completes, no RC-loss warning for the unqualified
   state, and that the first qualified sample reconciles level controls without emitting an edge.
   Instrument the bridge to log *which condition* qualified it and when.
   **(a)** Bridge and FC powered together, transmitter **on** — expect condition **A**, an observed
   assert→clear edge.
   **(b)** The same with the transmitter **off** and a Record range deliberately configured to span
   `midrc`, held for at least 30 s. No recording shall start at any point, in particular not during
   the ~1.5 s before the guard asserts, and **qualification shall never complete**.
   **(c)** Bridge attached to an FC running for several minutes — expect condition **B**. Timestamp
   the bridge's first MSP exchange and the qualification instant, and assert that the measured
   interval is **≥ the computed `grace`**, with `margin = max(2 × qualification poll period,
   100 ms)`. Repeat at 10 Hz and 20 Hz qualification polling: the observed `grace` shall differ by
   the margin term. A qualification that lands early is a failure even though nothing visibly
   misbehaves — it means the interval is not being computed.
   **(d)** Bridge start deliberately delayed past `failsafe_delay` with the transmitter on — expect
   the edge to be missed and condition **B** to qualify, total delay ≈ `failsafe_delay + grace`.
   **(e)** `failsafe_delay` reconfigured to 5 and to 50, repeating (c). The observed grace shall
   track the configured value; if it does not, a constant has been compiled in.
   **(f)** `MSP_FAILSAFE_CONFIG` denied at the FC or blocked at the bridge, repeating (a) and (c).
   (a) shall still qualify via condition A; (c) shall **not** qualify at all.
   **(g)** Repeat (c) while dropping a defined fraction of `MSP_STATUS` replies. Qualification shall
   be delayed by one poll period per unanswered transaction, and a run in which every status poll
   times out shall never qualify however long it is left — the clock alone does not qualify anything
   (§12.4.3).
8. **INAV startup qualification** (§12.4.4). Three runs:
   **(a)** bridge and FC powered together, transmitter **off**, Record range spanning `midrc` —
   confirm `ARMING_DISABLED_RC_LINK` (bit 18) reads asserted in the first `MSP2_INAV_STATUS` reply,
   that no recording starts, and that qualification does not complete;
   **(b)** transmitter switched on — confirm the flag clears, the input qualifies, level controls
   reconcile and no edge is emitted;
   **(c)** with the aircraft **armed**, confirm by MSP capture that the bridge issues no further
   `armingFlags` read once qualified. Separately, power-cycle the bridge while armed and confirm it
   remains `UNQUALIFIED` until disarm, committing no camera-control transition in between (§12.4.4
   known consequence).
9. **Betaflight guarded commit.** Induce RX loss repeatedly while polling, with
   `RX_FAILSAFE_MODE_SET` configured outside the active range, and confirm no committed
   camera-control transition arises from a bracket that straddles the FC's substitution (§12.4.1).
   Instrument the bridge to count discarded candidates; a run that discards **zero** over many
   induced losses indicates the bracket is not actually being applied, and shall be treated as a
   failed test rather than a clean one.
10. **`Combi` arbitration.** Walk the §12.1.1 truth table on hardware, including that a button-stop
    survives repeated level-control evaluations, that leaving the level range clears the override,
    and that the override survives an RX-loss freeze unchanged.
11. **Range evaluator.** Confirm ±10 µs hysteresis at a boundary using a slowly swept channel — the
    control shall not chatter — and that setup rejects a 30 µs range and a pair of ranges 15 µs
    apart (§12.2.1).
12. **INAV 8:** RX loss leaves the camera AUX returned by `MSP_RC` at its last-good value, producing
    no camera-control transition without an additional validity flag and without a bracket. Confirm
    there is no INAV recovery window — recovery is immediate and continuous.
13. **`REC STOPPED` during a freeze.** With the guard frozen and last accepted intent `RECORD`, stop
    the camera **while BLE remains connected** — fill the card, press the camera's own stop button,
    or inject a fault that reports `recording = false` — and confirm the warning **is** raised
    against the frozen intent (§18).
14. **Camera power-off during a freeze — a separate test, not a variant of the one above.** Under
    the same conditions, power the camera down. Expected result is `CAM LOST` per §17.1 and **no**
    `REC STOPPED`. A build that reports `REC STOPPED` here has synthesized a camera-confirmed event
    from a disconnect and fails §7, however sensible the OSD looks.
15. **Persisted OSD content.** With owned slots holding live camera text, save FC configuration from
    the Configurator and power-cycle with the bridge disconnected: confirm the stale text is
    present, which is the exposure (§9, §10). Reconnect the bridge and confirm every owned slot is
    overwritten with non-authoritative content **before** any camera connection is attempted. Run on
    both platforms — Betaflight's `pilotConfig` is a parameter group like INAV's and is not the
    safer case.
16. Custom message / custom element writes are accepted and rendered while armed on both Betaflight
    and INAV. This validates in-flight availability of the OSD path; it does not create an
    armed-state dependency.
17. Correct rendering on analog and on MSP DisplayPort.
18. Four fields independently updateable.
19. No flash write observed across a long operating session (verify by configuration persistence
    check across power cycle, not by inference).
20. **Degrade paths, both platforms and both kinds.** Confirm the two degraded states are reached
    independently and behave differently (§5.1):

    a. *Capability degrade.* An INAV target built without `USE_PROGRAMMING_FRAMEWORK` enters
       telemetry-disabled compatibility mode: no OSD writes, camera control still works.

    b. *Version degrade, Betaflight.* **2025.12.4** — a build that carries the custom-message
       feature and would otherwise half-work — enters `UNSUPPORTED_FC_VERSION`: no OSD writes **and**
       no camera control, with the reason shown to the user.

    c. *Version degrade, INAV.* **8.0.0** — a build whose capability probe succeeds — enters
       `UNSUPPORTED_FC_VERSION` on the same terms. This is the case that fails if the implementation
       probes before it gates.

    d. *Ordering.* In both (b) and (c), confirm by MSP capture that no camera-control input was
       acted on at any point, including during the interval between identification and the gate
       decision.
21. **Baseline conformance.** Run the full set against stock **Betaflight 2025.12.5** and **INAV
    8.0.1** specifically (§0). A pass on a newer build is evidence about that build, not about the
    supported minimum — and §0's three platform-availability corrections are exactly the class of
    defect a master-only test run would have missed. Forward-compatibility runs against later
    releases are recorded as version notes and never substituted for this one.

### 27.2 RF coexistence — the §3.3 release gate

Measure the bridge's effect on a 2.4 GHz ELRS control link.

**Two instruments, two jobs.** The existing passive RF survey and Blackbox analysis tooling slices
noise floor by condition and is the right tool for *detecting* interference and for in-flight
realism. It does **not** measure receiver sensitivity in dBm, and cannot on its own support a
sensitivity claim. The release gate therefore requires a calibrated bench measurement in addition
to the survey, not instead of it.

**Required method — calibrated link budget.**

```
ELRS TX  →  fixed-attenuator pad  →  step attenuator  →  ELRS RX + bridge
                                     (calibrated, known step size)
```

- Conducted where the hardware allows it; where an antenna port is not available, a fixed shielded-
  enclosure geometry with a documented and re-verified path loss.
- Sweep the step attenuator down through the link cliff, recording PER and LQ at each step.
- The measurement is the attenuation at which a **fixed PER threshold** is crossed — the criterion
  shall be stated as a PER figure (for example 1 % or 5 % packet loss at the configured packet
  rate), not as an LQ or RSSI reading. LQ and RSSI saturate and report "no change" right up to the
  cliff, which is precisely the failure this gate exists to catch.
- Repeat the sweep ≥ 5 times per condition and report the mean and spread.

Conditions, all else held constant:

```
bridge absent → bridge idle (paired, connected)
              → bridge active (recording, telemetry flowing)
              → bridge reconnecting (sustained directed reconnect failures, camera off)
```

swept against configured BLE transmit power (§21.3), and against ELRS packet rate if more than one
is supported.

**Defining "no measurable degradation".** The phrase is meaningless without the rig's own
resolution, so it is bound to it:

```
degradation = Δ (attenuation at fixed PER) between condition and bridge-absent baseline

PASS  if  |Δ| ≤ max(attenuator step size, 2σ of the baseline repeats)
```

Both quantities shall be reported with the result. A rig whose uncertainty is 3 dB cannot
demonstrate a 1 dB claim, and shall not be used to make one — if the measured uncertainty exceeds
the degradation the gate needs to exclude, the correct outcome is to improve the rig, not to widen
the criterion.

Pass criteria:

1. **No measurable loss of receiver sensitivity or link margin** as defined above, in the idle and
   active conditions;
2. sustained directed reconnect failures remain bounded by §21.3 backoff/duty-cycle limits, never
   escalate into broad discovery, and meet the same criterion;
3. the in-flight passive survey shows no new noise-floor feature attributable to the bridge across
   the same conditions — this is the realism cross-check on the bench result, and a disagreement
   between the two is itself a finding;
4. the resulting numbers written back into §21.3 as concrete configured values, together with the
   attenuator step size and baseline spread that qualify them.

Failing this gate does not merely delay a feature — it invalidates §3's claim that the device is a
passenger.

### 27.3 Camera protocol re-host — GoPro, V1

Port Open GoPro onto NimBLE-Arduino on ESP32-C3, behind the §6 driver interface.

Pass criteria:

1. start/stop recording and full §8 telemetry from a physical camera;
2. driver capability flags correctly gate unsupported features;
3. combined firmware **fits flash** — one figure for the image, established once — **and meets
   §16.1's two heap floors (≥ 32 KiB minimum free, ≥ 16 KiB largest allocatable block), including
   its no-monotonic-degradation condition and its per-mode soak durations, measured separately in
   each mode it must run in**: `RUN` (BLE central, no AP), `SETUP` peak 1 (AP up, client attached,
   BLE down) and `PAIR` (BLE scan, no AP). These are three measurements, not one: peak 1 does not
   bound `RUN` or `PAIR` (§16.1). `DIAG` peak 2 (AP up, client attached, BLE connected) is measured
   only when qualifying V1.2 and shall not gate this criterion (§26). **This spike is where §16.1's
   provisional floors are confirmed or replaced**; the measured per-mode figures shall be written
   back into §16.1's table whatever they show, including where they show the provisional floors
   were too generous to be met.

### 27.4 Osmo Nano protocol probe — gates V1.1 only

Independent of V1 and of §27.1–27.3; may run immediately, and hardware is on hand. The purpose is
to answer one question cheaply — *does the Osmo Nano answer DUML at all?* — before any driver is
written.

1. **Advertisement.** Scan for the Nano and dump its manufacturer-specific data. DJI's reference
   identifies one of its cameras when manufacturer bytes 0, 1 and 4 are `0xAA`, `0x08`, `0xFA` (see
   `bsp_link_is_dji_camera_adv` in the reference's `ble.c`). Record whether the Nano matches that
   signature.
2. **GATT enumeration.** Connect and enumerate services. Confirm service `0xFFF0` with `0xFFF4`
   (notify) and `0xFFF5` (write-without-response).
3. **DUML liveness.** Subscribe to `fff4`; send a known-safe DUML query from the Pocket 3 corpus
   over `fff5` — never `fff3`, per the §20.2 hazard — and check for a CRC-valid response.
4. **Control.** Attempt start/stop recording and a camera-status subscription.
5. **Official cross-check.** Send the R SDK connection-request frame and record the reply. A
   rejection carrying a `device_id` would both reveal the Nano's ID and indicate official support is
   nearer than the documentation suggests.

Pass criteria and branches:

| Outcome | Consequence |
| --- | --- |
| Steps 1–3 yield a CRC-valid response | DUML driver viable; Nano proceeds to V1.1 as **Expected** (§20) |
| Step 4 works on physical hardware | Nano promoted to **Verified** (§20) |
| Fails at step 2 or 3 | Nano is **Unsupported**; escalate to a DJI Mimo app BLE capture, or substitute Osmo Action 6 / Osmo 360 per §26 |

Record the result — including a negative result — against the Nano's firmware version. §20's policy
forbids promoting the model on protocol similarity alone, and the whole premise of the DUML path is
a similarity argument.

### 27.5 Retained boot request and reset-reason gate — gates §16.1

The cheapest spike, and the one to run first: §16.1's entire failure story rests on two toolchain
behaviours, and neither should be assumed from documentation written against a different ESP-IDF
release than the one this product pins.

**The verification target is deliberately narrow.** Because the reset reason is checked *before*
the retained request is inspected, it does not matter whether retained memory survives a watchdog,
a panic or a brownout — its contents are declined on those paths regardless. Only two things are
load-bearing:

1. a value placed in the selected retained region survives a deliberate `esp_restart()`;
2. `esp_reset_reason()` reliably distinguishes that deliberate restart from power-on, external
   reset, brownout, watchdog and panic.

Both shall be confirmed on production ESP32-C3 hardware against the **pinned** toolchain, not
against a documentation claim for another release. Note that the ESP32-C3 has no RTC slow memory:
`RTC_DATA_ATTR`, `RTC_SLOW_ATTR` and `RTC_FAST_ATTR` all resolve to RTC fast memory on this part,
so the region actually used shall be named in the design and the test shall exercise that region.

Pass criteria:

| Check | Expected |
| --- | --- |
| Write request, `esp_restart()` | request readable, reason `ESP_RST_SW` |
| Write request, force task watchdog | reason is a watchdog reason; boot selects `RUN` |
| Write request, force panic | reason `ESP_RST_PANIC`; boot selects `RUN` |
| Write request, remove power | reason `ESP_RST_POWERON`; boot selects `RUN` |
| Write request, assert external reset | reason `ESP_RST_EXT`; boot selects `RUN` |
| Consume a request, then `esp_restart()` again | request is invalid; boot selects `RUN` |

The last row is the replay test, and it is the one that fails silently if consume-before-enter is
implemented as consume-*after*-enter. Where check 1 does not hold on the pinned build, §16.1's
retained-memory mechanism shall be replaced by an NVS-backed request that is written, cleared and
**verified cleared** before the requested mode is entered, with a durable-clear failure falling
back to `RUN`. That fallback costs a flash write per transition and a failure path to test, which
is why it is the fallback and not the design.


### 27.6 Primary-stick maintenance gesture — gates nothing in V1

`DIAG` and `SETUP` are reachable without this, so the spike exists only to decide whether a
stick-based no-touch entry could ever be added safely. It has an explicit kill branch, and the
expected outcome is that it is not worth it.

Three questions, in order, and any one of them can end it:

1. **Do native stick commands leave a safe hold position?** Both platforms recognise stick commands
   from *sustained* positions, so only the gesture's held stage must be free of them; transitions
   may pass through a command position provided they are shorter than the platform's recognition
   hold. Enumerate the command tables at `2025.12.5` and `8.0.1` and determine whether a free hold
   position exists on both.
2. **Is an arming path reachable from that position?** This is the safety question, and the
   configuration space it must cover is now known and cited in §12.1.1. Two concrete hazards, not
   hypotheses: on Betaflight with `enable_stick_arming` `ON` and no `ARM` mode range, throttle low
   + yaw high held for `ARM_DELAY_MS` **arms** ([BF] `fc/rc_controls.c:225-230`); on an INAV
   fixed-wing with `fixed_wing_auto_arm` `ON`, throttle above `min_check` **arms**, with no stick
   pattern involved at all ([INAV] `fc/rc_controls.c:215-219`). The second is the harder
   constraint, because it makes throttle position — not a pattern — the trigger: the gesture's hold
   position and every transition through it shall keep throttle low. Both settings are non-default,
   which is exactly why the audit shall cover the configuration space rather than one stock build.
3. **Is primary-channel ordering resolvable?** §12.2 keeps channels 1–4 out of scope precisely to
   avoid platform-specific ordering. `MSP_RC` is downstream of the FC's own channel remapping, so
   the transmitter's AETR/TAER choice does not reach the bridge — but the two platforms' internal
   channel constants shall be confirmed to agree at the pinned tags before any gesture indexes
   them.

| Outcome | Consequence |
| --- | --- |
| A hold position exists on both platforms, is unreachable from any arming configuration, and channel order agrees | the gesture may be specified as an **additional** entry authority, never as a replacement for §16's Maintenance Entry AUX |
| Any question fails | the feature is dropped and this section records why, per §20's rule that a negative result is recorded rather than discarded |

Whatever the outcome, the §16 AUX gesture remains the specified no-touch route. A stick gesture
would remove one setup field at the cost of the entire audit above, which is why it is not in V1.

**Related open item — closed 2026-08-26.** §12.1.1 previously stated that INAV supports yaw-stick
arming and that Betaflight removed it. Re-read at the pinned tags, **both halves were false and the
attribution was inverted**: Betaflight `2025.12.5` retains yaw-stick arming as an opt-in
(`enable_stick_arming`, default `OFF`), and INAV `8.0.1` has no stick arming at all. §12.1.1 now
carries the cited version, and question 2 above is written against it. The underlying §12.1.1
requirement did not change — it depends on whether an `ARM` mode range exists, not on which
firmware is running.
