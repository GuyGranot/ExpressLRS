# Action Camera Bridge — Product Requirements Specification

**Status:** **v1.2 — frozen audited baseline, 2026-08-26.** Seven audits clean; 246 requirements, 201
validation cases, 37 platform facts. Subsequent findings — including Osmo Nano / DUML results — land as a new
delta with its own ledger section; **this audit history is not edited in place.**
Compressed from PRS v1.0 (`action-camera-bridge-prs-source-v1.0.md`,
sha256 `e6178686acfa71e932784cf44041a988f9d72e8ff64162c89588bce8a700b473`) under the compression rule
of 2026-08-26, **plus the separately reviewed v1.1 and v1.2 normative deltas recorded in Traceability
Ledger §5a and §5b.** The compression boundary itself changed no requirement; the deltas since it did, and
each is itemised. Every disposition is recorded in the Ledger.
**Minimum supported:** Betaflight **2025.12.5**, INAV **8.0.1**
**Architecture:** Standalone BLE-to-FC bridge over one spare UART
**Distribution:** Personal / open-source

---

## 0. The document set

This is one of **five** documents. Each holds exactly one kind of statement, and no statement is
duplicated across them.

| Document | Holds | A statement belongs here when |
| --- | --- | --- |
| **PRS** (this file) | normative requirements | removing it could let two conforming implementations behave differently |
| **Platform Evidence** `action-camera-bridge-evidence.md` | platform facts and rationale | removing it removes only the *proof* that a requirement is correct |
| **Validation** `action-camera-bridge-validation.md` | test cases and procedures | removing it removes only the *demonstration* of compliance |
| **Traceability Ledger** `action-camera-bridge-traceability.md` | the disposition of every v1.0 statement, the seven audits, and the post-compression deltas | — |
| **Compression Rule** `action-camera-bridge-compression-rule.md` | the method the set was produced under | — |

**Conformance is defined by this document alone.** Evidence and Validation are not conditions of
conformance and contain no requirement. Where a platform fact is itself an input to conformance — a
command ID, a byte encoding, a bit position, an ordering the implementation must respect — that fact
is stated **here**, and Evidence carries only its citation and derivation.

### 0.1 Reading rules

1. **Each requirement has one canonical definition**, at its ID. Elsewhere the ID is referenced, never
   restated.
2. **IDs are stable and are never reused or renumbered.** A retired requirement's ID stays retired.
3. **A `[BF]` or `[INAV]` marker on a requirement makes it platform-specific.** Unmarked requirements
   apply to both.
4. **Every numeric value in this document is normative** unless it is explicitly marked *provisional*,
   in which case it names the validation that closes it and the destination the result is written
   back to.
5. **`shall`** is a requirement. **`may`** is a permission whose exercise is optional but whose bounds,
   where stated, are requirements.

### 0.2 Version contract — normative

```
SCOPE-01   Minimum supported:  Betaflight 2025.12.5      INAV 8.0.1
           Target family:      Betaflight 2025.12.5+     INAV 8.0.1+ within 8.x
```

These are the same versions as the citation baseline for all platform evidence. Earlier patch
releases in those lines — Betaflight 2025.12.1 through .4, or INAV 8.0.0 — are **not** supported, and
the support range shall not be written as `2025.12+` or `8.x`. There is no Betaflight `2025.12.0`
release (`PF-BF-01`).

**SCOPE-02.** Newer releases are targeted but require forward-compatibility validation before support
is claimed. A pass on a newer build is evidence about that build only.

---

## 1. Product goal

Provide in-flight control and telemetry for supported action cameras without modifying
flight-controller firmware or entering any flight-critical control path. The primary value is
**camera-confirmed state**, not state inferred from the pilot's switch position.

**PROD-01.** The bridge shall control supported cameras over Bluetooth, read the camera's actual
reported state, display camera state and telemetry through the FC's native OSD, work independently of
the video system, and require only power and one bidirectional FC UART.

**PROD-02.** Normal operation shall require no interaction with the bridge, no phone, no camera app
and no bridge configuration interface.

**PROD-03.** Failure of the bridge, the Bluetooth connection or the camera shall continue to have no
effect on flight, and shall not measurably degrade the aircraft's control link (`SAFE-06`, `RF-05`).

---

## 2. Safety architecture

The bridge is a **passenger device**, isolated from flight control in three independent dimensions:
logical (`SAFE-01`…`SAFE-05`), electrical (`SAFE-06`), and radio-frequency (`RF-*`).

**SAFE-01.** The bridge shall not participate in RC reception, motor control, servo output, PID
processing, navigation, failsafe, arming authorization, or receiver-to-FC signal transport.

### 2.1 MSP command policy

**SAFE-02 — closed by default.** The bridge shall send only the commands listed in `SAFE-03`. **Every
other MSP command is prohibited, whether or not its side effects are currently known**, and an
implementation shall have no code path capable of emitting one. Adding a row to `SAFE-03` is a
specification change; it is not an implementation decision.

**SAFE-03 — the allowlist.** IDs are given per platform because they are not always the same on both
(`SAFE-05`). Anything absent from this table is prohibited.

| Command | BF | INAV | Dir | When the bridge may send it | Requirement |
| --- | --- | --- | --- | --- | --- |
| `MSP_API_VERSION` | 1 | 1 | read | FC identification | `FC-03` |
| `MSP_FC_VARIANT` | 2 | 2 | read | FC identification | `FC-03` |
| `MSP_FC_VERSION` | 3 | 3 | read | version floor, before any capability probe | `FC-04` |
| `MSP_RC` | 105 | 105 | read | steady-state control input; trigger learning (`LEARN-04`); maintenance-entry window while open (`MSP-04`) | `CTRL-12` |
| `MSP_STATUS` | 101 | — | read | Betaflight validity bracket (`RCV-03`, `RCV-08`); maintenance-entry armed interlock while the window is open (`SETUP-09`) | `RCV-03` |
| `MSP2_INAV_STATUS` | — | 0x2000 | read | INAV qualification; continues while a maintenance window is open (`SETUP-09`); stops once qualified **and** that window has closed | `RCV-17` |
| `MSP_FAILSAFE_CONFIG` | 75 | — | read | once per FC detection, condition B | `RCV-08` |
| `MSP_BOXIDS` | 119 | — | read | once at identification, **only** where a Maintenance Entry AUX is configured | `FC-12` |
| `MSP_MODE_RANGES` | 34 | 34 | read | setup-time only: overlap advisory (`CTRL-25`) and maintenance-entry refusal (`SETUP-07`) | `CTRL-25` |
| `MSP_MODE_RANGES_EXTRA` | 238 | — | read | setup-time diagnostic context only (`CTRL-26`) | `CTRL-26` |
| `MSP2_INAV_LOGIC_CONDITIONS_STATUS` | — | 0x2026 | read | where a Logic Condition trigger is configured | `CTRL-27` |
| `MSP2_INAV_GVAR_STATUS` | — | 0x2027 | read | where a GVAR trigger is configured | `CTRL-27` |
| `MSP2_INAV_CUSTOM_OSD_ELEMENTS` | — | 0x2100 | read | OSD capability probe | `FC-07` |
| `MSP_RTC` | **247** | **246** | read | only where time push is enabled — **see `SAFE-05`** | `CAM-13` |
| `MSP_RAW_GPS` | 106 | 106 | read | only where GPS push is enabled | `CAM-13` |
| `MSP_OSD_CANVAS` | 189 | — | read | optional; **never** 188 | `OSD-07` |
| **`MSP2_SET_TEXT`** | **0x3007** | — | **write** | Betaflight OSD injection | `OSD-07` |
| **`MSP2_INAV_SET_CUSTOM_OSD_ELEMENTS`** | — | **0x2102** | **write** | INAV OSD injection | `OSD-08` |

**The `When` column is exhaustive and normative.** A command sent outside its stated conditions violates
`SAFE-02` exactly as an unlisted command does. Where a condition stated here and the `MSP-01` polling table
appear to disagree, that is a defect in this specification to be repaired, never an implementation choice.

**SAFE-04.** There are exactly **two** permitted writes, and both are OSD text injection. An
implementation holding a third write path has left this specification.

**SAFE-05 — command IDs are not portable.** `MSP_RTC` is **247 on Betaflight and 246 on INAV**, and
each platform assigns the other's value to `MSP_SET_RTC` (`PF-BF-02`, `PF-INAV-01`). A shared constant
therefore *sets the flight controller's clock* on whichever platform it is wrong for. Command IDs
shall be resolved through the platform adapter (`ARCH-02`), never through a shared constant. A
constant used on both platforms shall be proven identical on both at the pinned tags before it is
shared.

**SAFE-07.** No command the bridge is permitted to send shall cause a flash write. This is a property
of `SAFE-03`, not of the MSP protocol; the general claim that only `MSP_EEPROM_WRITE` persists is
false on Betaflight (`PF-BF-03`).

### 2.2 Electrical isolation

**SAFE-06.** A bridge crash, reset, watchdog event, brownout, UART failure or camera failure shall
leave flight-controller behaviour unchanged. The FC shall continue operating normally with the bridge
electrically connected but completely non-functional, including with the bridge's TX line held at
either rail.

### 2.3 RF coexistence

This is the only path by which a correctly-isolated passenger device can still endanger a flight.
**Degraded bridge operation shall not become degraded control-link operation.**

**RF-01.** The bridge shall bound BLE transmit and scan duty cycle.

**RF-02.** Runtime reconnect to a paired camera shall be **directed**, and shall back off rather than
scan continuously.

**RF-03.** Broad/discovery scanning shall occur **only** after an explicit user Pair/Setup action
(`SETUP-01`), and shall never be entered autonomously because a paired camera is absent or
disconnected. Runtime camera loss shall never escalate into discovery scanning.

**RF-04.** BLE transmit power shall be treated as a coexistence parameter, shall be configurable and
stated, and the release configuration shall use the lowest validated level that maintains reliable
camera communication.

**RF-05 — release gate.** The bridge shall be shipped only after its effect on control-link
sensitivity/link margin has been **measured**, not asserted (`VAL-SPIKE-02`). Failing this gate
invalidates `SAFE-01`.

**RF-06.** The RF policy shall be independent of FC arm state. RF safety shall not depend on that
information being available or correct.

**RF-07.** Consecutive directed-reconnect failures shall increase the retry interval to a configured
ceiling of **≥ 5 s**. There shall never be a continuous reconnect loop.

**RF-08 — duty-cycle ceiling.** The bridge shall have a stated maximum BLE runtime reconnect/scan duty
cycle and shall meet it in the reconnect-storm case as well as in nominal use. *The concrete value is
delegated to `VAL-SPIKE-02`, which writes it back into this requirement.*

**RF-09.** Discovery scanning occurs in the `PAIR` boot mode only (`BOOT-02`), never in `RUN`.

---

## 3. FC interface and platform contract

**FC-01.** Four-wire interface — 5 V, GND, TX, RX — using one free full-duplex UART configured for
MSP. No additional FC wiring. No connection to RC receiver, VTX, or camera other than Bluetooth.
Default MSP baud rate 115200; higher rates may be supported where validated.

**FC-02.** The bridge shall document its **peak** current draw, not only its average, and the required
local bulk capacitance. It shall tolerate brownout and reset cleanly.

**FC-03.** At startup the bridge shall identify the FC using `MSP_API_VERSION`, `MSP_FC_VARIANT`
(`"BTFL"` → Betaflight backend, `"INAV"` → INAV backend) and `MSP_FC_VERSION`.

**FC-04 — ordering is fixed.**

```
identify FC → supported variant? → version floor? → capability probe → normal backend
```

The version floor shall precede capability probing. A capability probe answered by an out-of-contract
build is not evidence that the build behaves as this document describes.

**FC-05 — version floor.** Betaflight below **2025.12.5** and INAV below **8.0.1** shall enter
`UNSUPPORTED_FC_VERSION`. In that state **no camera-control input shall be acted on and no
platform-specific OSD write shall be issued**. Identification, setup, diagnostic and firmware-update
functions remain available, and the state shall be surfaced to the user with the detected and required
versions. Every OSD-write obligation in this document is subject to `OSD-19`, which is where this
prohibition is made to bind.

**FC-06 — what is compared.** Both platforms answer `MSP_FC_VERSION` with three unsigned bytes. The
bridge shall compare that byte triple lexicographically against the floor and shall **not** parse the
version string. The parser shall tolerate a payload longer than three bytes, because Betaflight
appends a Pascal string that INAV does not send (`PF-BF-04`).

```
Betaflight 2025.12.5  →  25, 12,  5      ← 25, not 2025
INAV       8.0.1      →   8,  0,  1
```

**FC-07 — capability probing.** Capability probing shall run only on a build that has passed `FC-05`,
and OSD capability shall not be inferred from the version alone.

- **[INAV]** issue `MSP2_INAV_CUSTOM_OSD_ELEMENTS`. A missing or error reply means the target was
  built without `USE_PROGRAMMING_FRAMEWORK` and is the correct trigger for telemetry-disabled
  compatibility mode.
- **[BF]** there is no runtime capability probe and none is required. Acceptance of an OSD write is
  **not** evidence that the target renders an OSD (`PF-BF-05`); that is confirmed once on the bench
  (`VAL-SPIKE-01`). The version floor is the whole programmatic test on this platform.

**FC-08 — the two degraded states are distinct and shall not be conflated.**

| State | Trigger | Camera control | Platform OSD writes |
| --- | --- | --- | --- |
| `UNSUPPORTED_FC_VERSION` | `FC-05` floor not met | **suppressed** | none |
| telemetry-disabled compatibility | supported version, no OSD capability | active | none |

**FC-09.** Unsupported FC variants shall enter telemetry-disabled compatibility mode and shall not
send platform-specific OSD writes.

**FC-10.** The bridge shall track FC family, FC firmware version, MSP/API version and available OSD
capabilities, **and shall expose all four through the setup and host interfaces** — a field tracked but never
readable cannot be used to diagnose an installation.

### 3.1 FC operational state is not a control input

**FC-11.** **Actual FC armed state is never a camera-control or camera-policy input.** The bridge
shall not query or track armed/disarmed state for camera control, BLE reconnect policy or setup
availability. Two exceptions are permitted and no others:

```
forbidden:  armed state changes what a control means, or what the camera does with it
permitted:  armed state qualifies whether another reading is still being maintained     (RCV-17)
permitted:  armed state gates entry to a maintenance mode with no camera actuation     (SETUP-09)
```

**[INAV]** the `ARMED` bit may be inspected only while control input is `UNQUALIFIED`, and solely to
determine whether `ARMING_DISABLED_RC_LINK` is currently being maintained (`PF-INAV-02`).

**FC-12.** No `MSP_BOXIDS` ARM lookup is part of the V1 control contract. **[BF]** where a Maintenance
Entry AUX is configured (`SETUP-05`), that lookup is required once during identification to locate the
ARM box bit, and is absent from every build in which no Maintenance Entry AUX has been selected.

**FC-13 — the invariant.** A given control in a given range shall produce the same camera action
regardless of armed state. Armed state may delay the bridge starting to act (`RCV-19`); it shall never
change what acting means.

**FC-14 — an FC restart is detected and the backend reinstated.** The bridge shall detect that the FC has
restarted while the bridge itself remained powered, and shall re-run identification (`FC-03`), the version
floor (`FC-05`), the capability probe (`FC-07`), owned-slot initialisation (`OSD-09`) and startup
qualification (`RCV-08`, `RCV-17`), returning to normal OSD and control polling **without requiring any
FC-side remap of ARM or box IDs, and without user action**.

`FC-03`'s *"at startup"* is the **bridge's** startup; this requirement governs the **FC's**. Re-detection is
the event `RCV-15` restarts the condition-B grace clock on, and `RCV-14` governs the requalification that
follows — so an FC reboot mid-flight reconciles level controls and emits no edge.

---

## 4. Control semantics

**CTRL-01.** Each configured control consists of an AUX channel plus one or more non-overlapping value
ranges, held in **bridge** configuration. The stored representation shall be **explicit ranges**, so
unusual radio endpoints remain supported; configuration may expose 2-state and 3-state presets over
that representation. The control model is independent of FC mode configuration and of actual FC armed
state.

**CTRL-02.** `USER1..USER4`, PINIO and other FC box modes are never bound automatically to a camera
control. Existing FC mode/PINIO configuration is never modified and is read only for `CTRL-25`.

**CTRL-03.** Betaflight and INAV shall share one bridge-side AUX/range evaluation implementation.

**CTRL-04 — control primitives. There are three, and an implementation shall provide no others.**

1. **Level** (presented to the user as *AUX Toggle*) — one configured range active → the control's
   intent is asserted; outside that range → released.
2. **Push Button** — entry into a configured active range is a momentary event, subject to
   `CTRL-18`…`CTRL-23`.
3. **Combi** — a Level control provides base intent, a Push Button provides manual override
   (`CTRL-06`).

**CTRL-29 — Arm Record is a preset over primitive 1, not a fourth primitive.** It is a **Level** Record
mapping whose AUX channel and range are learned from, or selected as, the control the pilot uses to arm.
Entering the range requests start; leaving it requests stop. **The bridge does not inspect whether the FC
actually armed**, and a rejected FC arm may still start the camera, by design (`FC-13`).

It shall require **no separate evaluator**, and shall be **behaviourally indistinguishable from the same
Level mapping configured by hand** (`LEARN-12`). The interface may present it prominently as the default
choice (`INST-01`); that is a presentation decision and creates no runtime distinction.

**CTRL-05.** In all modes, displayed state follows camera acknowledgement (`CAM-04`), never the input.

**CTRL-11.** **Level control is the specified default for the Record function**, because a level
control reconciles to the correct state after any interruption and an edge-triggered control cannot.
Push Button remains available and supported but shall not be the default for Record.

### 4.1 Combi arbitration

**CTRL-06.**

```
baseIntent      = level control evaluation (RECORD | STOP)
manualStop      = latching boolean, cleared at boot
effectiveIntent = baseIntent == RECORD && !manualStop ? RECORD : STOP
```

| Event | Effect |
| --- | --- |
| Push Button while `baseIntent == RECORD` | toggle `manualStop` |
| Push Button while `baseIntent == STOP` | no effect on `manualStop` |
| `baseIntent` transitions to `STOP` | clear `manualStop` |

**CTRL-07.** `manualStop` shall not restart recording by itself; it can only subtract from
`baseIntent`. A Combi button cannot start a recording the level control is not asking for.

**CTRL-08.** `manualStop` is **camera-control state, not input state**. It is part of what `RCV-02`
freezes, survives an RC-validity freeze intact, and shall not be cleared by entering or leaving a
freeze.

**CTRL-09.** `manualStop` shall be cleared on bridge reboot.

**CTRL-10.** A Combi button mapped to an action other than stop/restart is stateless and does not
participate in this arbitration.

### 4.2 Reading RC values

**CTRL-12.** The bridge reads FC-effective RC values using `MSP_RC` on both platforms. **Only AUX
channels are accepted as camera-control inputs; channels 1–4 are out of scope for control mapping.**

**CTRL-13.** The **response length**, not a hard-coded channel count, determines how many channels are
available.

**CTRL-14.** No operational meaning shall be placed on a single-microsecond boundary (`PF-BF-06`).

**CTRL-24.** Documentation shall describe the control source as the **FC-effective AUX value**, not
necessarily the physical transmitter switch position (`PF-INAV-03`).

### 4.3 Range evaluator

**CTRL-15.** **Hysteresis is ±10 µs** around each configured range boundary, applied as a Schmitt
trigger: a channel *enters* a range at `min + 10` / `max - 10` and *leaves* it at `min - 10` /
`max + 10`. The value is configurable; 10 µs is the default and the tested value (`PF-BF-07`).

**CTRL-16 — minimum range width.** Setup shall reject any configured range narrower than
`4 × hysteresis` (40 µs at the default), and shall reject two ranges on the same channel whose
boundaries are closer than `2 × hysteresis`.

**CTRL-17.** Hysteresis applies to the evaluator only. The stored configuration remains the explicit
range the user entered.

**CTRL-30 — latch initialisation.** `CTRL-15`'s entry and exit thresholds govern *subsequent* samples; they
do not define the latch's initial state. On first qualification and on **every** re-baseline (`RCV-14`), each
range's latch shall be established from the **nominal stored range** — active where `min ≤ value ≤ max`,
released otherwise — after which `CTRL-15` applies normally.

Without this rule a first sample landing inside the hysteresis band, between `min - 10` and `min + 10`, has
no defined result, and two conforming implementations reconcile the same switch position differently. Because
re-baselining is a first-class semantic throughout this document, that ambiguity would recur at every startup,
every RX-loss recovery and every confirmed learned mapping.

### 4.4 Push Button timing

**CTRL-18.** A configured AUX range shall be observed active on **at least two consecutive samples**
before a Push Button event is emitted.

**CTRL-19.** The documented minimum hold time shall be **250 ms**, and setup shall state it to the
user.

**CTRL-20.** Consecutive Push Button events shall be separated by a minimum interval, **default
400 ms**, so a single press cannot double-toggle.

**CTRL-21.** When any Push Button control is enabled, `MSP_RC` shall be polled at **20 Hz** rather than
10 Hz (`MSP-01`), subject to the `MSP-08` fallback and its floor.

**CTRL-22.** A Push Button press occurring while camera-control transitions are frozen **is lost**.
**[BF]** this includes the recovery window of `RCV-05`. Setup documentation shall state this limitation
where a Push Button control is configured.

**CTRL-23.** **[BF]** Both qualifying samples of a Push Button event shall be individually bracketed
per `RCV-03`. A press detected across a sample pair in which either member failed its validity bracket
shall be **discarded**, not committed.

### 4.5 Setup-time overlap advisory

**CTRL-25.** At setup, and never as part of runtime control evaluation, the bridge may read
`MSP_MODE_RANGES` for the sole purpose of warning that a proposed camera AUX range overlaps a
configured FC mode. The advisory is **non-blocking** — the user may proceed. A failed or unsupported
query shall suppress the advisory, not block setup. The advisory reports overlap only; it shall not
interpret the current active mode or the FC's armed state. An ARM-range overlap is an **expected**
result for the Arm Record preset.

**CTRL-26.** **[BF]** `MSP_MODE_RANGES_EXTRA` may additionally be queried for richer diagnostic
context, but **no advisory behaviour shall depend on it** — it does not exist in INAV 8.0.1
(`PF-BF-08`).

### 4.6 Optional INAV Logic Condition / GVAR trigger

**CTRL-27.** **[INAV]** Optional and non-blocking; AUX ranges remain the required default on both
platforms. A camera control may instead be bound to a Logic Condition or Global Variable, read
read-only through `MSP2_INAV_LOGIC_CONDITIONS_STATUS` / `MSP2_INAV_GVAR_STATUS`.
**`MSP2_INAV_LOGIC_CONDITIONS_CONFIGURED` shall not be used** — it does not exist in INAV 8.0.1
(`PF-INAV-04`). Which Logic Conditions are meaningful shall be inferred from the status reply and the
user's explicit selection, never from a capability query the supported baseline cannot answer. Where a
user encodes FC state, including armed state, inside a Logic Condition, the bridge observes only the
selected result and interprets no arming semantics.

**CTRL-28.** The bridge shall never repurpose a functional FC mode as a generic camera trigger.

---

## 5. RC validity and startup qualification

**RCV-01 — the common semantic.** **RX loss shall not create a camera-control transition.** The
mechanism differs per platform; the semantic does not.

### 5.1 Betaflight — runtime validity

**RCV-02.** While Betaflight reports RX data invalid, the bridge shall freeze the last accepted
camera-control state and shall not act on AUX transitions. The guard is
`ARMING_DISABLED_RX_FAILSAFE` from the `armingDisableFlags` word of `MSP_STATUS`, and it is valid while
armed (`PF-BF-09`).

**RCV-03 — guarded commit.** `MSP_STATUS` and `MSP_RC` are separate transactions and do not represent
one FC instant (`PF-BF-10`). A Betaflight RC sample that would produce a camera-control transition
shall be committed only under a validity bracket:

```
STATUS₁  → must be clear
MSP_RC   → candidate sample
STATUS₂  → must be clear
         → commit
```

If either status read reports the guard asserted, the candidate is **discarded** and the last accepted
camera-control state is frozen. `STATUS₂` is the load-bearing leg.

**RCV-04 — permitted optimisation.** Three transactions per poll are not required:

```
STATUS → MSP_RC
    sample implies no control transition:  accept, done
    sample implies a transition:           issue STATUS₂, commit only if clear
```

**RCV-05 — the delay's value shall not be encoded.** The guard's assert is immediate and its clear is
delayed (`PF-BF-11`). The bridge shall wait for the flag to clear, **whatever the reason and whatever
the latency**, and shall **not special-case `BOXFAILSAFE`** (`PF-BF-12`).

**RCV-06.** After real RX loss the guard holds camera state **past** the return of valid pilot input.
This is deliberate and conservative; it is not a defect. Switch-induced and transmitter-loss recovery
may produce visibly different latencies and both are correct.

**RCV-07.** Level controls reconcile to the correct camera state once the guard clears. Edge controls
do not (`CTRL-22`).

### 5.2 Betaflight — startup qualification

**RCV-08.** Control input has an explicit qualification state and **starts `UNQUALIFIED`**. A clear
guard is not proof that valid RC data was ever received (`PF-BF-13`), so the FC's initial no-RX
detection window shall be excluded before a clear guard is treated as meaningful:

```
FC detected
    ↓
read failsafe_delay  (MSP_FAILSAFE_CONFIG)
    ↓
control input = UNQUALIFIED          ← no camera-control transitions of any kind
    ↓
qualify on either:

  A. the guard has been observed ASSERTED, and subsequently observed CLEAR,
     under a passing bracket (RCV-03)

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

Both the `grace` and the `margin` formulae are normative, not advisory. `margin` exists to guarantee
the assertion is *observed*, not that it has occurred (`PF-BF-14`).

**RCV-09.** `failsafe_delay` is a U8 in tenths of a second. The bridge shall apply the same **100 ms
floor** the FC applies (`PF-BF-15`): a pilot who sets `failsafe_delay = 0` gets 100 ms, not zero.

**RCV-10 — continuity is measured in observations, not in wall time.** Each unanswered or failed status
transaction during a condition-B interval shall extend that interval by one poll period. **An interval
containing no successful observation shall not qualify at all, whatever its duration.**

**RCV-11.** If `MSP_FAILSAFE_CONFIG` fails or is unsupported, condition B is unavailable and the bridge
shall qualify on **condition A only**. There is no safe fixed fallback grace.

**RCV-12.** `UNQUALIFIED` shall be treated as **normal, not a fault**, and shall raise no RC-loss
warning (`OSD-16`).

**RCV-13.** Qualification shall not be inferred from elapsed time alone, from the FC being detected,
from a guard state observed before any RC sample was bracketed, or from a clear guard before the FC's
no-RX window has been excluded.

**RCV-15.** The condition-B grace clock shall restart on any FC re-detection.

### 5.3 The re-baseline rule — both platforms

**RCV-14.** **The first qualified sample may reconcile a level control. It shall never generate an edge
event.** This applies identically to **every** qualification and re-qualification — initial startup,
recovery from an RX-loss freeze, bridge reboot, FC reboot, and confirmation of a newly learned mapping
(`LEARN-11`). It is one rule; it shall not be implemented as several.

### 5.4 INAV — runtime validity

**RCV-16.** INAV freezes *values* rather than substituting them (`PF-INAV-05`), so **no separate INAV
RC-validity runtime gate and no bracket are required.** There is no INAV recovery window; recovery is
immediate and continuous.

### 5.5 INAV — startup qualification

**RCV-17.** INAV has the same boot exposure as Betaflight — every channel initialises to mid-stick
(`PF-INAV-06`) — but needs no timer and no configuration read, because `ARMING_DISABLED_RC_LINK` is
driven directly from link state and a clear guard genuinely proves valid RC data was received
(`PF-INAV-07`).

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

**RCV-18.** No bracket is required: a single `MSP2_INAV_STATUS` followed by `MSP_RC` qualifies the
input. INAV's flag and its RC data cannot disagree the way Betaflight's can, because nothing substitutes
values.

**RCV-19 — known consequence.** `ARMING_DISABLED_RC_LINK` is **not maintained while armed**
(`PF-INAV-02`), so a bridge that reboots while the aircraft is armed **cannot qualify until the
aircraft disarms**. It shall therefore perform no camera-control transitions for the remainder of that
flight, holding whatever the camera was doing. This shall be documented in release notes rather than
worked around, and the flag shall **not** be promoted to a runtime validity gate.

---

## 6. Camera model

**CAM-01.** Camera-specific protocols shall not leak into the FC interface.

**CAM-02.** Common camera state: `connected, mode, recording, recordingTime, batteryPercent,
recordTimeRemaining, storageRemaining, resolution, frameRate, errorState`. Optional capabilities —
`photoCapture, modeSwitch, presetSwitch, highlight, cameraPower, clockSet, gpsPush, remainingStorage,
temperature` and other camera-specific telemetry — shall each sit behind an explicit capability flag.

**CAM-03.** Unsupported information shall remain **unknown**, not fabricated. Capability flags shall
gate both behaviour and UI.

**CAM-04 — truth model.** OSD information shall represent **camera-confirmed** state. The bridge shall
not display `REC` solely because a control input was issued. States: `CAM OFF, CONNECTING, READY,
PHOTO, VIDEO, REC, CAM STALE, CAM LOST, CAM ERR`.

**CAM-05 — freshness.** Every telemetry field shall track `value, valid, lastUpdated`. Values the
camera does not report remain `valid = false`. Estimated values are never marked valid.

**CAM-06 — thresholds.**

| Condition | Result |
| --- | --- |
| Status update received | fields valid, `lastUpdated` refreshed |
| No status update for **> 3 s** | live fields (timer, battery, storage) invalidated; state shows `CAM STALE` |
| No status update for **> 10 s**, or BLE disconnected | all camera fields invalid; state shows `CAM LOST` |

**CAM-07.** Stale recording state shall never remain displayed indefinitely as confirmed `REC`.

**CAM-08 — compatibility lifecycle.** Three states, and promotion between them is by **named evidence**:

```
Unsupported   known incompatibility, or protocol support not demonstrated
     │
     │  protocol probe passes on physical hardware
     ▼
Expected      protocol-compatible; the finished driver is not qualified
     │
     │  the full camera-driver qualification suite passes on physical
     │  hardware, at a stated firmware version
     ▼
Verified
```

**A protocol probe shall never promote a model to Verified.** Demonstrating that recording starts and stops
proves the *protocol*; it says nothing about the *driver*.

**Verified** additionally requires, on physical hardware: pairing and directed bind (`PAIR-*`); reconnection
after loss (`RF-02`, `RF-07`); every capability flag the driver advertises (`CAM-03`); the `CAM-02` telemetry
set; `CAM-06` staleness and loss behaviour; command acknowledgement and one-shot semantics (`CAM-16`); OSD
integration; and the `RES-05` soak. **The same suite applies to every claimed driver**, so that *Verified*
means one thing across camera families rather than whatever each spike happened to measure.

Documentation and UI shall distinguish the three states, and **a camera shall not be promoted on protocol
similarity alone.**

**CAM-09 — stored identity.** The bridge stores one active camera identity, bound by an explicit
pairing action, remembered across power cycles, reconnected to automatically on subsequent boots, and
replaced **only** by an explicit pairing action.

**CAM-10.** **[Osmo Nano]** The driver shall write only characteristic `fff5`, never `fff3` —
`fff3` accepts writes with response and returns success while the firmware discards the payload
(`PF-EXT-02`).

**CAM-11.** The Osmo Nano is **Unsupported** under `CAM-08` until `VAL-SPIKE-04` passes, and shall not
be advertised as anything else.

**CAM-12.** DJI's EULA-encumbered R SDK protocol documents shall not be redistributed (`PF-EXT-03`).

**CAM-13 — GPS and time push.** *(V1.2, `SCOPE-05`.)* Where the camera protocol allows it, the bridge
may push UTC time (source `MSP_RTC`, subject to `SAFE-05`) and live GPS position (source
`MSP_RAW_GPS`). Neither shall affect recording or flight operation when unavailable. GPS push rate
counts against the `MSP-01` budget and shall not exceed the camera's documented rate.

**CAM-14 — the supported-model list is an input, not an assumption.** The camera vendor's published
supported-model list shall be **re-read at each bridge release** and the compatibility matrix updated from it.
Newer models within an otherwise supported family are **not** automatically covered, and a model absent from
that list when a release is cut shall not be claimed by that release (`PF-EXT-01`, `UPD-04`).

**CAM-15 — malformed camera data is rejected atomically.** A malformed, truncated or integrity-check-failing
camera frame shall be **discarded without updating any camera state**. It shall not leave a partially applied
telemetry update, shall not destabilise the BLE or MSP tasks, and shall not restart the bridge.

A discarded frame does not refresh `lastUpdated` (`CAM-05`), so a camera emitting nothing but malformed frames
ages into `CAM STALE` and then `CAM LOST` (`CAM-06`) exactly as a silent one does. **A talking camera that
makes no sense is not a fresh camera.**

**CAM-16 — delivery semantics differ by control primitive, and shall not be unified.**

| Intent | While the camera is unavailable | After an ambiguous outcome |
| --- | --- | --- |
| **Level / desired-state** (`CTRL-04` primitive 1) | persists as intent; reconciles on reconnection | re-asserted from camera-confirmed state |
| **One-shot / edge** (`CTRL-04` primitive 2, and every `LEARN-14` action) | **lost** — never queued across unavailability | **never blindly retried** |

*Unavailable* is any of: disconnected, `CAM STALE`, `CAM LOST`, reconnecting, or awaiting acknowledgement of a
prior command.

A one-shot action may be retried **only** where the camera-protocol operation is idempotent, or where
camera-confirmed state (`CAM-04`) proves the action did not take effect. **Blind retry of a non-idempotent
operation is prohibited**: a toggle retried after a lost acknowledgement undoes the command that actually
succeeded, and the pilot sees recording stop for no reason.

This is `CTRL-22` extended to the camera side of the same event — that requirement already discards a Push
Button press occurring during an `RCV-02` freeze. **One policy across two axes; it shall not be implemented as
two.**

---

## 7. OSD

**OSD-01.** The bridge shall compose all OSD strings in **uppercase on every platform**, so Betaflight
and INAV render identically (`PF-INAV-08`).

**OSD-02 — minimum common dataset.** Four fields, each **≤ 15 characters**, so one layout satisfies
both platforms.

| # | Field | Examples |
| --- | --- | --- |
| 1 | Camera state + timer | `REC 01:23`, `VID READY`, `CAM LOST`, `CAM STALE` |
| 2 | Camera battery | `CAM BAT 72%` |
| 3 | Remaining record capacity | `REC LEFT 38M` |
| 4 | Capture configuration | `4K100`, `1080P240`, `5.3K25` |

Where a platform offers more slots (`OSD-08`), additional optional fields may be presented.

**OSD-03.** If a camera does not report a value, that field shall be blank or omitted. No estimated
value may be presented as confirmed camera telemetry.

**OSD-04 — slot ownership.** OSD slots are a shared global resource. The bridge shall expose which slot
indices it owns as configuration (**default: Betaflight all four; INAV elements 1–4**), shall write
only owned slots, and shall leave unowned slots untouched.

**OSD-05 — update policy.** State changes immediate; recording timer ≤ 1 Hz; battery/storage on change
or ≤ 1 Hz periodic; resolution/FPS on change.

**OSD-06.** The bridge shall not rewrite a slot whose rendered content is unchanged.

**OSD-18 — state-change latency.** With a healthy MSP link, after the bridge receives a camera-confirmed
state transition (`CAM-04`) or locally determines a bridge warning or mode/state transition (`OSD-12`,
`BOOT-14`), the **changed** OSD content shall be submitted to the FC within **100 ms**.

This bounds **the bridge's contribution only.** Latency beyond the MSP write belongs to the FC, the VTX and
the display system, and is not the bridge's to bound. 100 ms is one poll period at the 10 Hz control rate,
so the MSP task shall be able to interleave an OSD write between control polls rather than queueing it
behind a full poll cycle (`MSP-07`).

Unchanged periodic fields remain subject to `OSD-05`'s rate limits, and **the recording timer's ≤ 1 Hz rate
is deliberately not a state-transition latency** — a ticking timer is not a state change.

**OSD-07 — Betaflight backend.** Transport `MSP2_SET_TEXT` → subtype `MSP2TEXT_CUSTOM_MSG_0..+3` →
`pilotConfig.message[0..3]`. Payload `[subtype u8][length u8][chars...]`. Four slots of 16 characters
(`PF-BF-16`). Writes are accepted while armed and this handler touches no flash (`PF-BF-03`).

**OSD-08 — INAV backend.** Transport `MSP2_INAV_SET_CUSTOM_OSD_ELEMENTS` → `osdCustomElements[n]`.
Payload is **exactly 28 bytes** and short writes are rejected (`PF-INAV-09`):

```
[element index      u8 ]   < MAX_CUSTOM_ELEMENTS (8)
[part0.type         u8 ]   [part0.value u16]
[part1.type         u8 ]   [part1.value u16]
[part2.type         u8 ]   [part2.value u16]
[visibility.type    u8 ]   [visibility.value u16]
[text              15 B]   NUL is appended by the FC
```

For a plain text field: `part0.type = CUSTOM_ELEMENT_TYPE_TEXT (1)`, remaining part types
`CUSTOM_ELEMENT_TYPE_NONE (0)`, `visibility.type = CUSTOM_ELEMENT_VISIBILITY_ALWAYS (0)`, all values
`0`, text 15 uppercase characters space-padded.

**Whole-element rewrite.** Every SET replaces parts, visibility and text together. The bridge shall
cache complete element state and resend it intact on each update; it cannot patch one field.

**OSD-09 — owned-slot initialisation.** As soon as the FC backend is up, and **before any camera
connection is attempted**, the bridge shall write **every owned slot** to non-authoritative content
(blank, or a connection-state string such as `CONNECTING`). This runs on every bridge start, whether or
not a camera is expected, and it is the only write the bridge makes before it has camera state to
report. It exists because a user who saves FC configuration persists whatever the bridge last wrote, on
both platforms (`PF-BF-17`, `PF-INAV-10`), which would otherwise leave the OSD asserting a
camera-confirmed state that nothing confirmed.

**OSD-10.** The bridge shall inject into the FC's OSD model rather than communicate with the VTX. No
VTX-specific bridge firmware shall be required.

**OSD-11 — INAV hard constraint.** INAV support shall require stock INAV 8.0.1 or a later validated
8.x release; no custom target, no firmware patch, no special FC hardware, and only the same MSP UART
the bridge already uses. It shall not abuse RC channels, RSSI, aircraft battery, altitude or navigation
values.

**OSD-17.** The user shall enable and position the required OSD elements. No custom FC build shall be
required.

### 7.1 Warnings

**OSD-12.** Configurable warnings: `CAM LOST, CAM BAT LOW, CARD LOW, CARD FULL, REC STOPPED,
CAM ERROR`.

**OSD-13.** Recording unexpectedly stopping **while the configured Record control remains active**
shall be treated as a high-priority camera warning. The comparison is camera-confirmed state versus
camera-control intent, and does not depend on actual FC armed state.

**OSD-14.** **`REC STOPPED` shall remain active during an `RCV-02` freeze.** A freeze freezes the last
accepted intent, so:

```
effective control intent during a freeze = last accepted intent
```

and camera-confirmed state continues to be compared against it. RX loss prevents **new RC
transitions**; it does not erase what the pilot last asked for.

**OSD-15.** **`REC STOPPED` requires a camera that said so.** It may be raised only while the camera is
connected and reporting `recording = false`. A camera that has gone away produces `CAM LOST`
(`CAM-06`), and `CAM LOST` is **never** `REC STOPPED`.

```
camera connected, reports recording = false   →  REC STOPPED
camera gone, last known state was recording   →  CAM LOST      ← never REC STOPPED
```

**OSD-16.** The `UNQUALIFIED` state raises **no** RC-loss warning: there is no accepted intent yet to
compare against.

**OSD-19 — one prerequisite governs every FC OSD write.** No requirement in this document that mandates OSD
content — `OSD-09` owned-slot initialisation, `OSD-05` and `OSD-18` updates, `BOOT-14` mode content,
`SETUP-14` gesture progress, `OSD-12` warnings — shall cause an FC OSD write unless the OSD backend is
**enabled**:

```
osdBackendEnabled  =  FC variant supported            (FC-09)
                 AND  version floor passed            (FC-05)
                 AND  platform OSD capability present (FC-07)
```

Where it is false the bridge shall present the same information through the setup or host interface and shall
issue **no** platform OSD write. This is the single place that resolves `FC-05`'s and `FC-09`'s prohibitions
against the otherwise unconditional obligations above: **the prohibitions win**, and an implementation needs
one flag rather than a per-requirement judgement call.

**OSD-20 — warning policy.**

| Warning | Source of truth | Threshold |
| --- | --- | --- |
| `CAM LOST` | `CAM-06` | specification-fixed |
| `REC STOPPED` | `OSD-13`…`OSD-15` | specification-fixed |
| `CAM ERROR` | camera-reported error state (`CAM-02`) | camera-native |
| `CARD FULL` | camera-reported | camera-native |
| `CAM BAT LOW` | camera-reported battery percent | **user-configured** |
| `CARD LOW` | camera-reported remaining capacity | **user-configured** |

**The last two carry no specification default and no bridge-chosen value.** Where the camera reports a native
low-battery or low-capacity flag, that flag is the trigger. Otherwise the threshold is user configuration, and
**the warning is disabled until the user sets one** — an unset threshold shall not become a built-in guess.

*Decided 2026-08-26 (Ledger CR-13): the bridge provides the warning mechanism; the user owns the number. The
alternative considered and rejected was removing threshold-based warnings altogether, which would have deleted
two of `OSD-12`'s inherited warnings and required an `X` disposition. **`CAM BAT LOW` and `CARD LOW` remain
supported requirements.***

**A warning whose underlying field is `valid = false` (`CAM-05`) shall not be raised.** An unknown battery
level is not a low one.

**Arbitration.** Where more warnings are active than there are owned slots to show them in, the bridge shall
display them in this table's order, **most severe first**, and shall **not** cycle or interleave warnings
within a single slot. Two conforming implementations shall not disagree about which warning the pilot sees.

---

## 8. Boot modes

**BOOT-01.** The bridge has **four operating modes, one per boot**. A mode is selected during startup
and does not change while the bridge is running; **every transition between modes is a restart.**

**BOOT-02 — ownership is fixed for the boot.**

| Mode | Wi-Fi AP | BLE | FC MSP link | Camera control |
| --- | --- | --- | --- | --- |
| `RUN` | never initialised | camera central | polled (`MSP-01`) | live `ControlPolicy` |
| `SETUP` | AP up | never initialised | polled | none — monitor (`SETUP-20`) and learning (`LEARN-01`) |
| `DIAG` | AP up | camera central | polled | live `ControlPolicy`, instrumented (`DIAG-*`) |
| `PAIR` | never initialised | scan / directed bind | not initialised | none |

No mode shall tear a radio subsystem down to reach another. Three of the four hold at most one radio
stack, so for them the peak-resource state is unreachable by any code, error or timeout path rather
than merely forbidden. `DIAG` is the exception (`RES-07`).

**BOOT-03 — boot selection.**

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

**BOOT-04.** The held-button path has **absolute priority and depends on nothing persistent**. It shall
not consult the retained request, the configuration store, stored camera identity or BLE bonding state.
Setup Mode is the only route to firmware update (`UPD-02`) and to recovering a corrupted configuration,
so an entry mechanism depending on healthy persistent storage would make those unreachable in exactly
the case they exist for.

**BOOT-05.** The boot request shall be held in **retained RAM that survives a deliberate software
restart, not in flash**. It therefore causes no flash write, no wear, and has no durable-clear step and
no clear-failure path. *(Fallback if `VAL-SPIKE-05` check 1 fails: an NVS-backed request that is
written, cleared and **verified cleared** before the requested mode is entered, with a durable-clear
failure falling back to `RUN`.)*

**BOOT-06 — consume before enter.** The retained request shall be invalidated **before** the requested
mode is executed, not after. `ESP_RST_SW` is not exclusive to the bridge's own mode transitions —
completing a firmware update restarts the same way (`UPD-05`) — and a request left valid would be
replayed into an unintended maintenance boot with nobody present.

**BOOT-07.** **Every other reset reason selects `RUN`, whatever the retained memory contains.**
Power-on, external reset, brownout, watchdog and panic all decline the request **without inspecting
it**. This makes *an interrupted maintenance boot never resumes itself* a property of the reset path
rather than of a write ordering an implementer must get right.

**BOOT-08 — who may create a boot request is a closed list.**

| Request | May be created by, and by nothing else |
| --- | --- |
| `SETUP` | entry authorities 1, 3 and 4 (`SETUP-01`); `PAIR` on completion, returning its result; `DIAG` on an explicit exit to `SETUP` |
| `DIAG` | an authorised Setup session only |
| `PAIR` | an authorised Setup session only |

The last two are **returns to an already-authorised session, not entries.**

**BOOT-09.** Entry authority 2 creates **no request at all** — it is `BOOT-03` rule 1. An
implementation routing it through a retained request reintroduces the dependency `BOOT-04` exists to
avoid.

**BOOT-10.** **No network-accessible interface shall exist in `RUN` or in `PAIR` from which any boot
request could be created.** A network request issued *inside* an already physically authorised Setup
session may schedule a single-shot `DIAG` or `PAIR` boot; that is not remote entry.

**BOOT-11 — operational failure is not abnormal termination.** A `PAIR` boot that finds no camera,
times out or fails to bind has **completed normally**: it retains an intelligible result and returns to
`SETUP`. **Only an uncontrolled reset lands in `RUN`.** This is a user-visible requirement, not an
implementation detail.

**BOOT-12 — exiting to `RUN` is a fresh start, not a resumption.** It re-runs FC detection (`FC-03`),
the version floor and capability probe (`FC-05`, `FC-07`), startup qualification (`RCV-08`, `RCV-17`),
owned-slot initialisation (`OSD-09`) and directed camera reconnect (`RF-02`). **No camera-control edge
shall be synthesised from switch activity that occurred during Setup Mode.** Because Setup Mode both
begins and ends with a restart, and `CTRL-09` clears `manualStop` at boot, Setup isolation is
structural: there is no state to preserve and no isolation discipline for an implementation to enforce.

**BOOT-13 — reboot is a maintenance mechanism only.** Transitions between mutually exclusive
radio-ownership modes shall use reboot boundaries rather than live Wi-Fi/BLE teardown and
reinitialisation. **Reboot shall never be used as an autonomous runtime recovery mechanism** for camera
loss, BLE failure, MSP failure or any other normal operating fault. The prohibition is the load-bearing
half: a "recover by restarting" policy would convert a recoverable camera fault into an unrecoverable
loss of camera control mid-flight (`RCV-19`).

**BOOT-14 — OSD content is mode-specific.** In `SETUP` the bridge shall write its owned primary state
slot to `SETUP` — or to the `SETUP-14` gesture indications while a maintenance window is open in `RUN`
— blank the remaining owned slots, and **shall not raise `CAM LOST` for a disconnection Setup Mode
deliberately caused**. `DIAG` presents normal camera truth exactly as `RUN` does. `PAIR` writes nothing;
the FC retains whatever the preceding `SETUP` boot left.

**BOOT-15.** The FC MSP link is **not arbitrated** in `SETUP` or `DIAG` and continues throughout.
`PAIR` does not initialise the MSP link at all.

**BOOT-16.** Setup Mode shall **never be entered autonomously** because of a camera or BLE failure or
any other runtime fault.

---

## 9. Setup interface and entry authorities

**SETUP-01 — four entry authorities, and no others.**

| # | Authority | The physical act | Available when |
| --- | --- | --- | --- |
| 1 | Setup button pressed while running | hands on the bridge | the bridge is reachable |
| 2 | Setup button held through a power-on or reset | hands on the bridge | always — depends on nothing persistent |
| 3 | Maintenance Entry AUX gesture in the startup window | transmitter in hand, aircraft in front of the operator | configured, disarmed, power-on boot |
| 4 | `BOOT_SETUP` over Betaflight USB serial passthrough | a USB cable into the flight controller | a host is attached; Betaflight |

**SETUP-02 — the guarantee.** **No authority shall be reachable over a network, over runtime MSP, over
BLE, or from any autonomous path.** Setup Mode shall not be enterable by any network-initiated action,
by runtime MSP, by a bonded or previously authorised BLE peer, or by any other remote request.

**Each authority shall be described as what it actually proves, and no more:**

| # | What it proves |
| --- | --- |
| 1, 2 | local physical interaction with the bridge itself |
| 3 | an **intentional transmitter sequence** inside the bounded eligible window of `SETUP-08`…`SETUP-10`, with the FC disarmed — **not** that the operator is standing beside the aircraft. A transmitter has range |
| 4 | **wired host access** through the FC. An attached host may issue `BOOT_SETUP` repeatedly without a further physical act; its reach is bounded by a cable |

None of the four is reachable from a bridge network service, from BLE, from runtime MSP, or from any
autonomous path — which is the property `SETUP-02` actually guarantees.

**SETUP-03 — per entry, not per enrolment.** A remote peer authorised once by a physical press shall
**not** thereby be authorised to cause later entries. Entering Setup Mode is a reboot (`BOOT-01`), and a
remotely triggerable entry would be a remote path to `RCV-19` and to raising a Wi-Fi AP at a moment
nobody at the aircraft chose (`RF-06`).

**SETUP-04.** **All four authorities target `SETUP`. `DIAG` and `PAIR` have no entry authority of their
own** and are launched only from an already-authorised Setup session (`BOOT-08`). Every route into the
maintenance surface passes through the one mode that has no live camera actuation, so an entry path
that misfires costs an unexpected Wi-Fi AP rather than an unexpected recording.

**SETUP-25.** The setup interface shall expose: recording control mode, AUX channel/range assignments,
Push Button timing, OSD slot ownership, camera preset assignments, OSD layout, warnings, clock/GPS
sync, reconnect policy, optional camera capabilities, camera pairing (`PAIR-*`), the live trigger
monitor (`SETUP-20`) and trigger learning (`LEARN-*`). It shall surface the `CTRL-25` overlap advisory
when a range is assigned, and shall state the `CTRL-22` Push Button limitation where such a control is
configured.

**SETUP-26.** Documentation shall state that Setup Mode is not to be entered while the aircraft is being
operated. Beyond `SETUP-09`, the bridge shall not consult FC armed state to decide whether Setup Mode is
available.

### 9.1 Maintenance Entry AUX gesture — authority 3

**SETUP-05.** **Unconfigured by default.** With no Maintenance Entry AUX selected there is no gesture,
no window, no added surface, and no arm-state lookup. The physical button remains available in every
configuration.

**SETUP-06.** The Maintenance Entry AUX **shall not be derived automatically** from the Record mapping
or from any other camera-control mapping. `INST-01` step 4 puts the default Arm Record preset on the
aircraft's own ARM AUX, so an inherited gesture would make rapid toggling of the arm switch the
documented way into Setup Mode, with props fitted.

**SETUP-07.** Where the chosen AUX and range overlap ARM, PREARM, failsafe, rescue/return-to-home or
another safety-relevant FC mode, setup shall **refuse** it as a maintenance-entry source. This reuses
`CTRL-25`'s read-only overlap machinery with the **opposite verdict**, and **user confirmation shall not
override the refusal.**

**SETUP-08.** Eligibility shall be decided from **`ESP_RST_POWERON` alone**. A software restart,
watchdog, panic or brownout shall never open the window — Betaflight can requalify a control input while
airborne (`RCV-08` condition B), so a window tied to qualification alone would reopen mid-flight after
any in-flight bridge reset.

**SETUP-09 — arming interlock.** The gesture shall not be recognised while the FC reports armed, and the
interlock shall be **re-checked continuously**. `SETUP-08` does not close the in-flight case by itself:
the bridge is powered from the FC's 5 V rail (`FC-01`), so a rail collapse and recovery presents as a
power-on reset. This is a maintenance interlock, not a control input, and is one of the two exceptions
`FC-11` permits.

**SETUP-10 — the window's start point and its duration are both normative.** The window shall open at
the **first qualified control sample** (`RCV-08`, `RCV-17`) — not at power-on, not at FC detection, and not
at the qualification state machine's transition considered separately from the sample that caused it — and
shall close **15.0 s** after that instant.

The start point is the same instant `SETUP-12` captures the baseline from, so **the two cannot disagree.**
It is not power-on because Betaflight's condition-B grace is configuration-dependent and can run to
seconds, and a window measured from power-on would be partly consumed before the input is trustworthy.

**A window opened while the FC reports armed still opens, runs and expires.** The window is consumed with
nothing recognisable. **That is the interlock working, not a defect.** On INAV the case cannot arise at all,
because qualification itself cannot complete while armed (`RCV-19`).

**Observing `ARMED` at any point inhibits recognition for the remainder of that window**, and does not merely
suspend it. Recognition state is cleared (`SETUP-13`) and a subsequent disarm does **not** reopen the window;
a new eligible window requires the next eligible power-on (`SETUP-08`). This is what `SETUP-09`'s continuous
re-check is *for* — it decides whether the window is still eligible, not whether the gesture is momentarily
allowed. Without this rule, an aircraft that arms and disarms inside the window leaves recognition in a state
two implementations would resolve differently.

**SETUP-11.** **Sequence, never static state.** A static AUX position present at startup shall never be
be recognised as a gesture. **Only qualified samples shall count toward recognition.**

**SETUP-12 — the recogniser.**

| Term | Definition |
| --- | --- |
| Position | `IN` (inside the configured maintenance range) or `OUT`, evaluated with `CTRL-15` hysteresis, from qualified samples only |
| Baseline | the position held when the window opens |
| Transition | a change of position **sustained for at least two consecutive qualified samples**; a single-sample excursion is neither a transition nor a reset |
| Pattern | exactly **four** transitions, alternating, **beginning and ending at the baseline** — `OUT→IN→OUT→IN→OUT`, or `IN→OUT→IN→OUT→IN` where the baseline is `IN` |
| Pattern timer | starts at the **first** transition, not at window open; the fourth shall complete within **3.0 s** of the first, after which recognition resets and the window continues to run |

The pattern ends where it began, so a control left displaced cannot complete it. The dwell is defined in
**samples, not milliseconds**, so it scales with whatever rate `MSP-01` is running.

**SETUP-13.** Recognition shall reset on RC-validity loss, on an out-of-pattern transition, on the FC
arming, on pattern-timer expiry, and on window expiry.

**SETUP-14.** **The window shall be visible.** While it is open the bridge shall display `SETUP GESTURE`
in its owned primary OSD slot, and `SETUP n/4` as transitions register, returning to normal camera
content when the window closes.

### 9.2 Host-assisted entry — authority 4

**SETUP-15.** The bridge-side maintenance protocol shall be minimal and platform-independent, and shall
consist of exactly these verbs:

```
PING        GET_INFO        BOOT_SETUP        REBOOT
```

**There shall be no firmware-transfer verb**, and **no `BOOT_DIAG` or `BOOT_PAIR` verb** — `BOOT_SETUP`
targets `SETUP` only, and authority 4 enters the hub like every other authority. `BOOT_SETUP` reaches
the Wi-Fi AP and `UPD-02`'s existing update path takes it from there.

**SETUP-16.** Maintenance frames shall be unambiguously distinguishable from MSP traffic on the same
UART. The bridge is an MSP **client** on that link and shall neither mistake a response for a command
nor emit a frame an FC could interpret as MSP.

**SETUP-17 — the threat boundary, stated as something observable.** The protocol shall not be reachable
through ordinary runtime MSP, RC input, BLE, or any autonomous recovery path. Two properties carry its
authority, and unlike the assumption they replace, **both are testable**:

1. **Ordinary FC-generated MSP traffic shall never be parsed as a maintenance command** — including a response
   whose payload bytes happen to resemble the maintenance protocol (`SETUP-16`, `SETUP-28`).
2. **No bridge network, BLE, RC or runtime-MSP path shall be able to emit a maintenance frame.**

The earlier formulation — that nothing but a passthrough host can place bytes on the bridge's UART — is
**false and shall not be relied on.** The FC itself sends MSP responses on that link, and the bridge cannot
observe whether a passthrough session is active: identical bytes are identical bytes. Direct electrical
injection onto the UART is already possession of `SETUP-01` authority 4's wired access, and is outside what
this boundary protects against.

**SETUP-28 — maintenance framing.** A maintenance frame shall carry a **magic value, a protocol version, an
explicit payload length, the verb, and an integrity check over the whole frame.** A frame failing any of the
five shall be discarded with no side effect and no state change. This is what makes `SETUP-16` mechanical
rather than aspirational: ordinary MSP response bytes cannot accidentally satisfy all five.

**SETUP-18.** **Host tooling shall invoke passthrough with the `reset` DTR action**, and shall document
a power cycle as the fallback exit. This specification shall not assume that a bare `serialpassthrough`
invocation returns control — for every default configuration it does not (`PF-BF-18`).

**SETUP-19.** Seamless host-side recovery is a **Betaflight property and shall not be claimed for
INAV**, which offers no equivalent passthrough exit (`PF-INAV-11`). INAV may reuse the same protocol.

**SETUP-27 — the bridge shall not attempt to recover the FC.** Where a host invokes passthrough without the
`SETUP-18` `reset` DTR action, the FC remains in its byte-copy loop and serves no MSP until it is power-cycled
(`PF-BF-18`). The bridge shall restart into the requested mode normally and shall **not** attempt to restore
FC MSP service — not by reset signalling, not by escape sequences, not by rebooting the FC.

**This is a host-invocation defect, not a bridge fault.** A bridge that tried to repair it would be writing to
a flight controller outside `SAFE-03`, in a situation it cannot observe correctly.

**Passthrough is a command-delivery channel, not a diagnostic one.** While it is active the FC copies
bytes rather than serving MSP, so no bridge mode needing live FC input can run underneath it
(`PF-BF-19`). The requested mode begins only after both ends have restarted.

### 9.3 Live trigger monitor

**SETUP-20.** While Setup Mode is active and a client is attached, the setup interface shall provide a
live dry-run view of every configured camera-control input, exposing as applicable:

- the FC-effective AUX channel value currently observed (`CTRL-24`);
- the configured range or ranges, as stored;
- **the hysteresis latch** — which side of the `CTRL-15` Schmitt trigger each range is currently latched
  to, and the effective entry/exit thresholds;
- whether each configured range is currently active;
- the resulting control intent or action — `RECORD`, `STOP`, `BUTTON EVENT`, or the configured action;
- whether the control input is `QUALIFIED`, `UNQUALIFIED`, or frozen by the applicable platform validity
  mechanism;
- for Push Button controls, whether a qualified button event has been recognised.

**SETUP-21 — same implementation, not a re-approximation.** The displayed result shall be produced by
the **same** platform input path, range evaluator, Push Button logic, Combi arbitration, qualification
logic and `ControlPolicy` implementation used during normal operation. The setup interface shall not
implement a separate approximation of runtime trigger behaviour.

**SETUP-22.** Dry-run monitoring shall issue **no command to the camera**.

**SETUP-23.** The monitor shall be **explicit about an unqualified input**, displaying `UNQUALIFIED`
together with the reason rather than presenting an inert display.

**SETUP-24.** **No live camera-control policy exists in `SETUP`.** The monitor's `ControlPolicy` instance
is the only one in that boot and is destroyed with the boot. Combined with `BOOT-12` and `CTRL-09`, this
is what makes the `manualStop` hazard unreachable rather than merely forbidden: under an architecture
where a live policy ran alongside the monitor, the user's own switch movements — made in order to
exercise the monitor — would reach the live instance through the ordinary input path.

---

## 10. Trigger learning

Trigger learning lets a user configure a bridge action by exercising the intended transmitter control,
without knowing its AUX channel number or numerical endpoints in advance. It is the primary route by
which a user configures a control mapping (`INST-02`).

**LEARN-01.** Trigger learning is **required in `SETUP`**. Where `DIAG` is implemented, the **identical**
learner shall be available there on the same terms, so a mapping can be learned and validated against the
real camera without an intervening restart. **Learning shall not be implemented twice**, and `DIAG`'s
availability shall not affect it in `SETUP`.

**LEARN-02.** Learning **needs no camera and no BLE.** It consumes qualified FC-effective `ControlSample`s
and writes bridge-side configuration. It therefore sits at `RES-02` peak 1 and is **not conditional on the
peak-2 gate** — the primary mechanism for configuring control mappings shall not depend on a resource
measurement that may not pass.

**LEARN-03.** Learning shall start **only on an explicit user action**. Normal RC activity outside an
active learning operation shall never alter persistent bridge configuration.

**LEARN-04 — polling.** While a learning operation is active, `MSP_RC` shall be polled at **20 Hz
regardless of the currently configured control modes**, and normal polling policy (`MSP-01`) shall resume
when learning ends. Without this the operation runs at 10 Hz, because the 20 Hz rate is otherwise enabled
only when a Push Button control already exists — the very thing being learned.

**LEARN-05 — camera actuation is suspended during learning.** Qualified samples continue to feed the
learner and the diagnostic display but **shall not produce camera commands**, and runtime control state
shall not be modified by movements performed solely during learning. When learning ends the live policy
re-baselines per `RCV-14`.

**LEARN-06 — candidate detection predicates.** A candidate shall be proposed only where the input is
qualified, exactly one AUX channel *moved*, and its resulting position is *stable*. Each term is a
mechanical predicate over a sample buffer:

```
learning rate        = 20 Hz                       (LEARN-04)

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

*Stable* is a **spread**, not a ± band, because a ± band requires first deciding what the centre is.
These thresholds are **not** `CTRL-15`'s hysteresis and shall not be conflated with it: `CTRL-15` decides
whether a *configured* range is currently active at runtime; 100 µs decides whether a channel was
*deliberately exercised* during setup.

**LEARN-07 — three distinct rejections.** They tell the user to do different things and **shall not be
collapsed into a single failure**:

| Outcome | Condition | What the user is told |
| --- | --- | --- |
| *ambiguous* | ≥ 2 AUX channels satisfy *moved* | move only one control |
| *no movement detected* | no channel satisfies *moved* before the acquisition timeout | move a control at all |
| *unstable* | a channel moved but never reached a *stable position* | hold the position |

Where the operation is ambiguous **the bridge shall not guess** — not by largest movement, not by lowest
channel index, not by first to cross the threshold.

**LEARN-17.** On acquisition-timeout expiry the operation shall return to idle, **change no
configuration**, and revert polling to `MSP-01` policy.

**LEARN-18 — abandonment is the same outcome as timeout.** A learning operation ends without changing
configuration when it times out (`LEARN-17`), **and equally when the client disconnects, the page is closed,
or the user cancels.** In every one of those cases: no persistent configuration change; polling reverts to
`MSP-01` policy; and live actuation resumes with a re-baseline that emits no edge (`RCV-14`, `LEARN-05`).

**The bridge shall not depend on a client's cooperation to end a learning operation.** A closed browser leaves
nobody to send a cancel, and a learner left active would hold `MSP_RC` at 20 Hz and camera actuation suspended
indefinitely.

**LEARN-08.** **Nominal transmitter values of exactly 1000, 1500 or 2000 µs shall not be assumed.** For a
multi-position switch the interface may guide the user through each position, record the observed stable
values, and derive proposed explicit ranges. Where a control moves from a baseline into another stable
position and returns to the baseline, the interface may propose `Push Button` semantics.

**LEARN-09 — inference is a proposal, never authoritative.** The interface shall state the minimum
demonstrated hold time (`CTRL-19`), any inferred trigger type shall remain a proposal the user may
override, and **every proposed control type, range and action requires user confirmation before it becomes
active configuration.**

**LEARN-10 — admissibility; learning weakens nothing.** Every proposed mapping shall pass the same checks
as a manually entered one: `CTRL-16`'s minimum range width and boundary separation, hysteresis validity,
the `CTRL-25` overlap advisory, and every action-specific restriction including `SETUP-07`. **User
confirmation is not an override for a rejected safety constraint.** A proposal that cannot yield a valid
`CTRL-01` representation shall be rejected with the reason shown, and learning **shall not silently widen,
merge or otherwise alter observed positions to make them valid.**

**LEARN-11 — confirmation re-baselines and never emits an edge.** Activating a newly confirmed mapping
shall establish a baseline from the current qualified control state and shall emit no edge event as a
consequence of confirmation. A level mapping takes the current level as its intent; an edge-triggered
mapping requires a subsequent qualifying transition. This is `RCV-14` and shall use the same mechanism.

**LEARN-12 — persisted representation.** A confirmed learned trigger shall be stored using the explicit
model of `CTRL-01`, so that a learned mapping and an equivalent manually entered mapping are
**behaviourally identical** in `RUN` and in `DIAG`. Trigger learning is a configuration mechanism and
**shall not create an alternative runtime control representation.**

**LEARN-13.** Learning shall never modify FC mode ranges, USER modes, PINIO configuration, Programming
Framework configuration or transmitter configuration, shall never inject or override RC values, and shall
require no FC configuration write (`SAFE-02`).

**LEARN-14.** Learnable actions are generic to bridge actions rather than specific to recording:
Record/Stop, photo capture, preset selection, camera mode selection, HiLight/tag, camera power, and any
other explicitly supported action.

**LEARN-15 — capability gating shall not depend on a live camera connection.** `SETUP` initialises no BLE,
so the absence of a connected camera carries no information whatever. The offered set shall be derived from
the **stored paired-camera identity and its known capability profile**, together with the product-version
scope of `SCOPE-03`…`SCOPE-05`. Where no camera is paired, or no trustworthy profile exists, the interface
shall offer the product-version set and state that capability is unconfirmed. It shall **never infer that a
capability is absent from the absence of BLE.**

**LEARN-16 — the learner shall never create an entry authority.** `SETUP-01`'s authorities are fixed, and
the camera-action binding system shall not produce one whatever the user confirms. Selecting the
Maintenance Entry AUX is a **separate and explicitly labelled workflow**; it may reuse the same
channel-discovery mechanism but shall run every `SETUP-05`…`SETUP-07` prohibition unchanged. Discovery is
shared; authority is not.

---

## 11. Pairing

**PAIR-01.** Pairing runs in the `PAIR` boot mode, parameterised by an optional target: with no target it
scans; with a target it performs a directed bind. Scan and bind are not separate operating modes.

```
SETUP ── request = PAIR ── restart ──► PAIR ── result retained,
                                               request = SETUP ── restart ──► SETUP
```

**PAIR-02.** **No second authorisation shall be required** to authorise pairing from within an
already-authorised Setup session. Scheduling a `PAIR` boot consumes no additional press, and the restart
between them does not revoke the authorisation the session already carries.

**PAIR-03 — bounds. These are maxima, not durations.** Each operation returns the instant it has its
answer. A **directed bind** has its answer when the connection succeeds or is refused. A **discovery scan** has
its answer only when it reaches `PAIR-11`'s completion criterion — **not** when its first candidate appears.
Nothing waits out its bound once it genuinely has an answer.

| Bound | Maximum | Expiry outcome |
| --- | --- | --- |
| Discovery scan | **30 s** | `SCAN_TIMEOUT`, or `NO_CAMERAS` where the scan completed early with nothing supported found |
| Directed bind attempt | **15 s** | `BIND_TIMEOUT` |
| Whole `PAIR` boot lifetime | **120 s** | boot terminates; next boot is `RUN` |

**PAIR-04.** The first two are **normal outcomes and return to `SETUP`** with a result to display
(`BOOT-11`). The third is abnormal and is the only one that lands in `RUN`. The lifetime cap shall be
enforced **independently of every operation inside the boot**, so that a fault in a BLE library cannot
leave the device indefinitely in a maintenance boot. It sits far above the worst legitimate path
precisely so that reaching it means a stuck stack and nothing else: it is a watchdog, not a schedule.

**PAIR-05 — outcomes.**

| Outcome | Meaning |
| --- | --- |
| `PAIRED` | directed bind succeeded; stored identity replaced |
| `MULTIPLE_CANDIDATES` | scan completed; user selection required |
| `NO_CAMERAS` | scan completed; nothing supported found |
| `SCAN_TIMEOUT` | scan reached its bound without completing |
| `BIND_FAILED` | camera rejected or refused the directed connection |
| `BIND_TIMEOUT` | directed connection did not complete within its bound |

**PAIR-06 — candidate cache.** Scan results are retained data, not live connection state, and are held in
the same retained-memory context as the boot request. The cache shall hold **at most 8 entries** — a scan
finding more shall retain the first 8 supported candidates and **report that the list was truncated**,
rather than dropping the excess silently or growing to fit. It shall never be treated as authoritative
camera identity, and **a cache that fails its validity check is empty.**

**PAIR-10 — cache contents are a closed list.** A candidate-cache entry may contain **only**:

- the camera-family / driver identifier;
- the candidate identity required for later selection and directed bind;
- bounded user-visible identification text;
- bounded driver-specific opaque bind context.

**Raw advertisement history, telemetry, GATT enumeration results and any other unbounded scan data shall
not be retained.** Storage limits for the opaque context and for the cache as a whole are `RES-09`.

**PAIR-07 — scan token.** Each completed scan shall stamp the cache with a fresh **opaque scan token**. A
selection shall be accepted only when it carries the current cache's token **and** names an entry that
exists in that cache. The token shall not be predictably reused while a page from an earlier scan could
plausibly still submit a selection — a monotonic counter restarting from a fixed value does not satisfy
this. **Entering `RUN` shall unconditionally empty the candidate cache and invalidate its token**, and that
unconditional clearing shall not be made conditional.

**PAIR-08 — selection rules.**

| Supported candidates discovered | Behaviour |
| --- | --- |
| Exactly one | **shall** be selected automatically and directed-bound, without further user confirmation |
| Two or more | explicit user selection is **required** |
| Two or more, no selection made | **no change** — the stored identity is left exactly as it was |

**PAIR-09.** The bridge shall **never bind to the strongest advertiser** as a default. "The closest one"
is not a statement of user intent, and pairing to a stranger's camera is a silent failure that survives
power cycles.

**PAIR-11 — discovery completion.** A discovery scan shall run to a defined completion criterion — a stated
minimum scan duration, or the `PAIR-03` bound, whichever comes first — and **shall not terminate merely
because its first supported candidate has been observed.** Candidate-set evaluation and `PAIR-08` selection
occur **only after completion**.

Observing camera A at 2 s is no evidence that camera B would not have advertised at 3 s, so a scan that stops
at the first hit can never establish the *exactly one* case `PAIR-08` turns on. Worse, **selecting the first
responder is `PAIR-09`'s prohibited behaviour under another name** — the nearest camera is generally the one
that answers first, which is precisely how a bridge binds a stranger's camera at a flying field.

---

## 12. `DIAG` — live end-to-end diagnostics

**DIAG-01.** **This whole section is V1.2 and conditional on the `RES-07` peak-2 validation.** What `DIAG`
adds over `SETUP` is exactly one thing — the camera — and holding the AP and a camera BLE connection at
once *is* peak 2, so no part of the mode survives that measurement failing. Every requirement below is of
the form *where `DIAG` is implemented*, and none gates V1.

**DIAG-02.** `DIAG` shall be entered only by explicit action from an authorised Setup session, and never
autonomously.

**DIAG-03.** The interface **observes** the real control path; `SETUP-21` applies here unchanged.

**DIAG-04.** At minimum the interface shall distinguish: observed RC/AUX input; range and hysteresis
state; qualification state; resulting intent; camera command generated/sent; command failure or timeout;
camera acknowledgement; camera-confirmed resulting state. Watching the OSD cannot separate *command never
generated* from *command sent and lost* from *capability unavailable*, which are three different
configuration errors with three different fixes.

**DIAG-05.** Camera-confirmed state remains authoritative exactly as in normal operation (`CAM-04`). The
interface **shall not report success merely because a command was issued.**

**DIAG-06.** Live `ControlPolicy` state advances in `DIAG`, `manualStop` included. **No "reset diagnostics
state" path shall be added** — the boot boundary is that path (`DIAG-07`, `CTRL-09`).

**DIAG-07.** `DIAG` exits by restart, either back to `SETUP` or to `RUN`.

**DIAG-08.** Where the `RES-07` gate does not pass, `DIAG` shall be **unavailable**, and its absence shall
not affect `RUN`, `SETUP`, `PAIR`, the `SETUP-20` monitor, `PAIR-*` or `LEARN-*`.

**DIAG-09.** **`DIAG` is a configuration diagnostic, not evidence about `RUN`.** Its observations are taken
with an AP resident and a client attached, which changes heap and radio scheduling. Timing or reliability
figures gathered in `DIAG` shall not be quoted as `RUN` behaviour.

**DIAG-10.** **The interface shall make the two modes unmistakable**, in either direction. `SETUP` shall
state that camera commands are disabled and `DIAG` shall state that controls operate the real camera:

```
SETUP                                    DIAG
DRY RUN — CAMERA COMMANDS DISABLED       LIVE CAMERA TEST — CONTROLS OPERATE CAMERA
```

---

## 13. MSP citizenship

**MSP-01 — polling.**

| Data | Rate | Command |
| --- | --- | --- |
| Configured AUX camera controls | 10 Hz, or 20 Hz when a Push Button control is enabled (`CTRL-21`) — **only when an AUX control is enabled** | `MSP_RC` |
| Trigger learning, while active | **20 Hz regardless of configured control modes** (`LEARN-04`) | `MSP_RC` |
| **[BF]** RX-validity guard | 10/20 Hz, bracketing the `MSP_RC` poll (`RCV-03`); plus one extra read on a transition-bearing sample | `MSP_STATUS` |
| **[BF]** qualification grace | **once per FC detection**, never in steady state (`RCV-08`) | `MSP_FAILSAFE_CONFIG` |
| **[INAV]** startup qualification | **until qualified only**, at the control poll rate; never polled again once qualified and once any maintenance window has closed (`RCV-17`) | `MSP2_INAV_STATUS` |
| Maintenance-entry window, while open | ≥ 10 Hz, **regardless of whether any AUX camera control is enabled**; stops permanently when the window closes | `MSP_RC` |
| Maintenance-entry armed interlock, while the window is open | at the same rate as that window's `MSP_RC` poll; stops permanently when the window closes (`SETUP-09`) | **[BF]** `MSP_STATUS` flight-mode flags via the `MSP_BOXIDS` mapping read once at identification; **[INAV]** the `armingFlags` word already read for `RCV-17` |
| Mode ranges (overlap advisory) | setup only, never runtime control (`CTRL-25`) | `MSP_MODE_RANGES`; **[BF]** `MSP_MODE_RANGES_EXTRA` |
| **[INAV]** Logic condition / GVAR trigger, if bound | 10 Hz | `MSP2_INAV_LOGIC_CONDITIONS_STATUS` / `..._GVAR_STATUS` |
| Camera state | event driven | — |
| Record timer | 1 Hz | — |
| Camera battery / storage | ≤ 1 Hz | — |
| Resolution / FPS | on change | — |
| GPS, only if `CAM-13` push enabled | ≤ 10 Hz | `MSP_RAW_GPS` |

**MSP-02.** **There is no steady-state arm-state poll after qualification.** This is the accurate form of
the rule; "no runtime arm-state poll" would not be, because a bridge that reboots at altitude on INAV must
keep issuing `MSP2_INAV_STATUS` during flight until it sees a trustworthy disarmed state (`RCV-17`).

**MSP-03.** The two maintenance-window rows exist **only while a window is open** and stop permanently when
it closes, whether or not the gesture was recognised. With no Maintenance Entry AUX configured, neither row
exists at all.

**MSP-04.** The window's `MSP_RC` row is **not** conditional on an AUX camera control being enabled — a
bridge with no camera control configured yet is exactly the state a bench user reaching for the gesture is
most likely to be in.

**MSP-05.** If no AUX or Logic-Condition camera control is enabled, the bridge need not poll RC/control
state at all.

**MSP-06 — quiet bridge.** The bridge shall: never hold the port busy waiting on a camera or BLE operation;
tolerate a missing or late reply without blocking; never retry-storm — failed requests back off; never
write an OSD slot whose rendered content is unchanged (`OSD-06`); and in telemetry-disabled compatibility
mode issue no OSD writes, continuing only the read-only queries required by enabled camera-control features.

**MSP-07.** The bridge shall cache the last accepted RC sample. A missing or late reply shall not block the
MSP task and **shall not synthesize a control transition**; an **incomplete bracket is treated exactly as a
failed bracket** — the candidate is discarded, not committed.

**MSP-08 — what gives way under budget pressure, and how far.** If a target cannot meet `MSP-06` at the
worst-case rate, **the Push Button poll rate gives way before the bracket does.** The bracket is a correctness
requirement; the 20 Hz rate is a latency preference. The permitted range is closed:

```
20 Hz    CTRL-21's normal rate wherever a Push Button control is enabled
10 Hz    permitted fallback. CTRL-19's documented 250 ms hold remains satisfiable,
         because CTRL-18's two consecutive samples span 200 ms at this rate
< 10 Hz  prohibited
```

A target that cannot satisfy `CTRL-18` and `CTRL-23` at 10 Hz **shall not offer Push Button controls at all**,
and setup shall state their unavailability, rather than offering a control that misses presses. **The validity
bracket shall never be weakened, at any rate.**

---

## 14. Resource requirements

**RES-01.** Every heap figure shall be measured on **production ESP32-C3 hardware**, in the configuration
it is quoted for, with **each radio stack the mode actually holds already resident**. A free-heap figure
logged before a stack is up overstates the true floor, and figures measured on other boards or other
firmware do not transfer.

**RES-02 — two peaks, and they are not interchangeable.**

| Peak | State | Gates |
| --- | --- | --- |
| 1 | AP up, client attached, BLE down (`SETUP`) | `SETUP`, including `LEARN-*` |
| 2 | AP up, client attached, BLE connected to the camera (`DIAG`) | `DIAG` only |

**Neither peak gates `RUN` or `PAIR`, and neither shall be quoted for them.** `RUN` holds a BLE central
with no AP; `PAIR` holds a BLE scan or directed bind with no AP. The stacks draw on different pools and
the measured case does not dominate the unmeasured ones. `RUN` and `PAIR` shall each be measured in their
own configuration.

**RES-03 — the pass criterion is two figures, not one.** *Provisional; closed by `VAL-SPIKE-03`, which
writes its result back into this requirement.*

| Figure | Floor |
| --- | --- |
| Minimum free internal heap | **≥ 32 KiB** |
| Largest allocatable internal block | **≥ 16 KiB** |

Free bytes alone are not a criterion on this part: ESP-IDF's allocator fragments under repeated Wi-Fi and
NimBLE connect/disconnect cycles, so a mode can report ample total free heap and still fail the one
contiguous allocation a reconnect needs. **Both floors shall hold simultaneously, in each mode, after every
stack that mode holds is resident and while the mode is under its specified worst-case workload — sampled
at the mode's own worst moment, never at entry.**

**RES-04 — no monotonic degradation.** Neither figure shall show monotonic degradation across repeated
mode-specific operations. A floor that holds for the duration of one observation says nothing about a leak,
and `RUN` is the mode that runs for hours and reconnects on every camera dropout.

**RES-05 — soak durations and workloads.**

| Mode | Soak | Workload it shall be held under |
| --- | --- | --- |
| `RUN` | **≥ 30 min** | ≥ 10 camera disconnect/reconnect cycles |
| `SETUP` peak 1 | ≥ 60 s | AP up with a client attached and a page actively polling |
| `PAIR` | ≥ 60 s | a completed scan followed by a directed bind attempt |
| `DIAG` peak 2 | ≥ 60 s | AP, client and a live camera link held together |

**RES-06.** Peak 2 is strictly higher than peak 1 and shall be measured separately. It is measured only
when qualifying V1.2 and shall not gate V1 (`SCOPE-07`).

**RES-07.** Where peak 2 does not pass, `DIAG` shall be unavailable (`DIAG-08`) and that outcome shall not
affect `RUN`, `SETUP`, `PAIR`, `SETUP-20`, `PAIR-*` or `LEARN-*` — none of which requires a camera
connection.

**RES-08.** Flash fit is a property of the image and may be established **once** for all modes.

**RES-09 — candidate-cache storage budget.** The retained candidate cache shall have a stated total storage
bound and a stated per-entry bound on `PAIR-10`'s opaque bind context, and shall enforce both.

**The bound closes per release, from the camera drivers that release actually claims.** V1 claims the GoPro
driver alone, so `VAL-SPIKE-03` closes it for V1. A release that adds a camera family may widen or revalidate
the bound **as part of that release** — `VAL-SPIKE-04` closes it for the Osmo Nano in V1.1 — and shall not
hold an earlier release open. *A bound invented before any driver exists would be a guess presented as a
limit.*

**RES-10 — configuration integrity.** Stored configuration shall be integrity-checked before use.
Configuration failing that check shall be treated as **absent, not as partially valid**: the bridge shall fall
back to safe defaults in which **no camera-control mapping is active and no Maintenance Entry AUX is
configured**, shall surface the fallback to the user rather than running silently on defaults, and shall issue
**no FC write outside `SAFE-03`** while recovering.

Recovery is through Setup Mode, which `BOOT-04` guarantees reachable without any healthy persistent storage —
which is the whole reason the held-button authority depends on nothing persistent. `PAIR-06`'s *"a cache that
fails its validity check is empty"* is this same rule applied to the candidate cache.

---

## 15. Firmware update

**UPD-01.** Bridge firmware shall be field-updatable, to allow new camera models, camera BLE protocol
changes, new FC versions and compatibility fixes.

**UPD-02.** Firmware update is an **explicit maintenance procedure, reachable only through Setup Mode** and
therefore only through one of `SETUP-01`'s authorities. It inherits exactly that bound.

**UPD-03.** The bridge shall **not** query FC armed state or implement an armed-state firmware-update
interlock. The operator, not the bridge, is responsible for not initiating maintenance during flight, and
documentation shall state that bridge, camera and FC firmware updates must not be performed while the
aircraft is being operated.

**UPD-04.** Camera support claims shall be tied to tested bridge firmware versions.

**UPD-05.** Completing an update restarts the bridge, and that restart carries `ESP_RST_SW` like any other
deliberate one — which is why `BOOT-06` requires the retained boot request to be invalidated *before* a mode
is entered.

---

## 16. Installation

**INST-01.** Target installation is four solder joints and one spare UART:

1. Connect 5 V, ground, TX and RX (observing `FC-02`).
2. Enable MSP on the selected FC UART.
3. Enable and position the required OSD elements (`OSD-17`).
4. Enter `SETUP` and configure camera controls by **learning** them (`LEARN-*`); verify against the
   dry-run monitor (`SETUP-20`) before enabling actuation.
5. Power camera.
6. Pair (`PAIR-*`).
7. Verify end to end — in `DIAG` where available, otherwise by returning to `RUN` and watching normal
   camera behaviour and the OSD.
8. Fly.

**INST-02.** **Manual AUX/range entry remains available as the advanced and fallback path**, not the primary
installation route.

**INST-03.** No CLI commands shall be required for the default supported configuration where the FC
configurator exposes all required settings.

**INST-04.** Documentation shall note that saving FC configuration while the OSD is showing live camera text
persists that text into the FC's stored configuration on both platforms. `OSD-09` clears owned slots at every
startup, so this shows up only if the bridge is later absent or slow to start.

**INST-05 — setups without an arm switch.** Both platforms have a path that arms with **no arm-switch AUX
channel configured** (`PF-BF-20`, `PF-INAV-12`); both are **non-default on both platforms**. A pilot with no
`ARM` mode range configured has no arm-switch AUX to map, so the Arm Record preset does not apply to them and
they shall configure a Record control on any other AUX channel. **Setup documentation shall state the
exception rather than presenting Arm Record as universally available, and shall not attribute it to the
wrong platform.**

---

## 17. Architecture — the seam

**ARCH-01.** The camera core shall not depend on MSP UART implementation details.

**ARCH-02 — where the seam goes.**

```
PlatformAdapter  →  ControlSample { values, valid, epoch }

ControlPolicy    →  RCV-14   first valid sample = baseline, never an edge
                    CTRL-06  Combi arbitration
                    CTRL-15  range evaluator + hysteresis
                    CTRL-18  Push Button timing
```

Standalone Betaflight, standalone INAV and a future integrated ELRS build differ in **exactly one thing:
how they produce `valid`.** Everything downstream of `ControlSample` is written once. `valid` is
**per sample**, not a mode flag, because `CTRL-23` requires both qualifying samples of a Push Button event
to be individually guarded.

**ARCH-03.** **The boot-mode architecture of §8 is standalone-only.** `RUN`/`SETUP`/`DIAG`/`PAIR`, the
retained boot request, the reset-reason gate and mode-specific OSD behaviour are properties of the standalone
bridge and **shall not be promoted into the shared core**, nor assumed by an integrated build. §9's
configuration diagnostics are scoped the same way.

**ARCH-04.** **Startup qualification does not disappear** in an integrated build; only its *mechanism* does.
`RCV-14`'s semantics survive intact:

```
before the first valid sample:   UNQUALIFIED, no camera-control transitions
the first valid sample:          becomes the baseline; reconcile level controls; emit NO edge
subsequent samples:              normal processing
```

**ARCH-05.** The ELRS-integrated implementation is **not** in the standalone validation envelope. Because
ELRS is flight-critical, it requires a separate acceptance process proving that enabling the camera subsystem
cannot materially affect ELRS timing, RF reception, LQ, packet loss, reconnect, watchdog behaviour, memory
integrity or receiver stability. `RF-*` applies doubly there: an integrated build shares not only the band but
the same MCU, scheduler and heap as the control link.

---

## 18. Release scope

### 18.1 V1 — required

**SCOPE-03.** V1 comprises: the standalone ESP32-C3 bridge; Betaflight and INAV backends at `SCOPE-01`; the
GoPro BLE driver; `RF-02`/`RF-07` directed reconnect with state-independent backoff; `CTRL-*` AUX-controlled
recording including the Arm Record preset; `RCV-*` in full; `CAM-04`/`CAM-05`/`CAM-06` camera-state feedback
with staleness handling; `OSD-02`'s four fields; `OSD-12`…`OSD-16` warnings; `OSD-04` slot ownership;
`CTRL-25` overlap advisory; `PAIR-*` one-button pairing with its selection rules; `SETUP-*` Setup Mode under
its four entry authorities including the live trigger monitor and the optional Maintenance Entry AUX gesture
and the four-verb passthrough protocol; `BOOT-*` reboot-separated `RUN`/`SETUP`/`PAIR` modes; **`LEARN-*`
trigger learning in `SETUP`, unconditionally**; `UPD-*` firmware update; optional Wi-Fi configuration through
explicit Setup Mode; and `VAL-FUNC-*` and `VAL-FAIL-*` passing.

### 18.2 V1.1 — conditional on `VAL-SPIKE-04`

**SCOPE-04.** DJI Osmo Nano driver over DUML. The probe runs independently of V1 and may return "not viable",
in which case V1.1 either escalates to a Mimo-app BLE capture or substitutes an officially supported model
(Osmo Action 6 / Osmo 360).

**The Osmo Nano / DUML investigation is a development priority but is not a V1 release criterion. Failure of
the Nano probe shall follow the fallback path defined here and shall not block V1 release.** V1's
release-gating camera implementation is the GoPro driver (`SCOPE-03`); `VAL-SPIKE-04` may run at any time and
its hardware is on hand, so priority costs V1 nothing. **Priority and gating are separate**, and conflating
them would turn an outcome this requirement already plans for into a release blocker.

### 18.3 V1.2 — conditional by camera capability

**SCOPE-05.** Photo capture; preset switching; HiLight/tag; `CAM-13` camera clock and GPS push; additional
telemetry; `CTRL-27` optional INAV Logic Condition trigger if not delivered in V1; and **the `DIAG` boot mode
in its entirety**, conditional on `RES-06`.

**SCOPE-06.** *(V1.2 camera mode/preset control.)* Where supported by the camera and positively advertised by
the driver, photo/video mode may be assigned to a configured AUX control; up to three camera presets may be
mapped to one AUX channel using three configured value ranges; and switching shall be possible while armed.

**SCOPE-07.** **Failure of the peak-2 validation shall not block V1 release**, and shall not affect trigger
learning or any other configuration capability. A capability that may be absent is not a release criterion.
Simultaneous Wi-Fi AP and BLE operation is required by `DIAG` and by nothing else.

### 18.4 Explicitly out of scope

**SCOPE-08.** Flight-controller firmware modification; primary-stick maintenance gestures (`VAL-SPIKE-06`);
a serial firmware flasher over the `SETUP-15` maintenance protocol; VTX-specific integration; RC override,
motor or servo control; a required mobile app; interpreting or enforcing FC armed/disarmed state; and the
ELRS-integrated production implementation.

---

## 19. Failure requirements

**FAIL-01.** None of the following shall affect aircraft control, and each has a corresponding acceptance
test in `VAL-FAIL-*`. Per `RF-05`, "not affecting aircraft control" includes not degrading control-link
quality.

Camera power-off · camera reboot · camera absent at boot · BLE disconnect · repeated BLE reconnect failures ·
unsupported camera · malformed camera response · bridge reboot · bridge watchdog · bridge held in reset ·
bridge UART failure including TX held at either rail · bridge Wi-Fi failure · configuration corruption · FC
reboot while the bridge is powered · RC link loss and failsafe · bridge powered up while the FC has not yet
validated RC data · bridge attached to an already-running FC · `MSP_FAILSAFE_CONFIG` unreadable · status polls
timing out through a qualification interval · bridge rebooted while the aircraft is armed · a validity bracket
straddling an FC failsafe transition · interruption of a `PAIR` boot · a retained boot request surviving into
an unrelated software restart · a stale setup page submitting a candidate selection · an in-flight 5 V
interruption presenting as a power-on reset · a maintenance-protocol frame arriving outside a passthrough
session · `DIAG` requested on a build whose peak-2 gate did not pass.

**FAIL-02.** The FC shall continue operating normally with the bridge electrically connected but completely
non-functional (`SAFE-06`).

---

## 20. Release gate

**REL-01 — the applicable validation set.** A release shall pass **every applicable case** in
`VAL-FUNC-*`, `VAL-FAIL-*` **and `VAL-REV-*`**, plus every `VAL-SPIKE-*` named by an included requirement as a
release gate or as a `REL-02` parameter closer. It shall not ship until `VAL-SPIKE-02` passes (`RF-05`).

**Applicable** means: in scope for the claimed FC platforms, the claimed camera models and the claimed release
scope (`SCOPE-03`…`SCOPE-05`). A case belonging to an unimplemented conditional feature — `DIAG-*`, `CAM-13`,
`SCOPE-06` — is **not applicable and is not waived**: it is simply not claimed.

**The set is defined by applicability, not by ID prefix.** `VAL-REV-*` is the only verification that 22
requirements have — architecture isolation, the shared-core boundary, installation, documentation and camera
policy among them — so a gate naming only `VAL-FUNC-*` and `VAL-FAIL-*` would let every one of those reviews
fail while the release still passed its gate.

**REL-02 — delegated parameter registry.** Every normative parameter whose value is delegated to a
measurement appears here with its status, its closing validation and its write-back destination. **This
table is the complete list; a parameter delegated anywhere else in this document and absent here is a
defect.**

| Parameter | Requirement | Value | Status | Closed by |
| --- | --- | --- | --- | --- |
| BLE transmit power | `RF-04` | — | **OPEN** | `VAL-SPIKE-02` |
| BLE duty-cycle ceiling | `RF-08` | — | **OPEN** | `VAL-SPIKE-02` |
| Candidate-cache storage bounds | `RES-09` | — | **OPEN**, per release | `VAL-SPIKE-03` closes V1; `VAL-SPIKE-04` extends it for V1.1 |
| Minimum free internal heap | `RES-03` | ≥ 32 KiB | **PROVISIONAL** | `VAL-SPIKE-03` |
| Largest allocatable block | `RES-03` | ≥ 16 KiB | **PROVISIONAL** | `VAL-SPIKE-03` |
| Retained-request mechanism | `BOOT-05` | retained RAM | **CONDITIONAL** | `VAL-SPIKE-05` |

**The rule.** **No release may claim a feature whose applicable parameter is unresolved.** Specifically, no
claimed feature shall ship with an applicable parameter that is `OPEN`; with an applicable `PROVISIONAL` value
its named validation has not yet closed and written back; or with an applicable `CONDITIONAL` choice its named
check has not yet resolved. **Applicable** carries `REL-01`'s meaning — a parameter belonging only to an
unclaimed feature does not gate the release.

A `PROVISIONAL` value is an implementation input and a test target immediately, but becomes a normative
release value only after its named validation has run and written back. A `CONDITIONAL` entry names the
check that selects between a specified design and a specified fallback.

**`OPEN` does not license an implementer to choose something sensible.** It records that the value is not
yet knowable, names who will know it, and blocks the release until they do.
