# Action Camera Bridge — Platform Evidence

**Companion to** `action-camera-bridge-prs.md`. **This document contains no requirements.** It
establishes *why* each normative requirement is correct for the pinned baselines, and records the
rationale for decisions whose reasoning would otherwise be lost.

**Nothing here is a condition of conformance.** Where a platform fact is itself an input to
conformance — a command ID, a byte encoding, a bit position, an ordering an implementation must
respect — that fact is stated in the PRS, and this document carries only its citation and
derivation.

**A `shall` appearing in this document is one of exactly two things**, never a third: a **restatement of a
cited PRS requirement**, or a statement about *this document or the project's own process*. **It is never a
new obligation on the implementation.** Compression-rule audit 5b enforces this mechanically, because CR-01
recurred twice under an audit that only checked the other direction.

---

## 0. Citation rule

```
Betaflight   2025.12.5      ← all [BF] citations
INAV         8.0.1          ← all [INAV] citations
```

Citations marked **[BF]** are `betaflight/betaflight` at tag `2025.12.5`; **[INAV]** are
`iNavFlight/inav` at tag `8.0.1`. Line numbers were read from those tags on 2026-08-25, and the
control-path and version-floor citations re-read on 2026-08-26.

**Standing rule.** Line numbers are the part of this document set most likely to rot, and the part
most likely to be regenerated from memory rather than read. Re-verification before each release means
**opening the file at the pinned tag**, not recalling it — and a citation that is right on `master` is
still wrong here. Reading source also means reading the **code, not the comments**: Betaflight's
`failsafe.c` contains comments that its own reset template contradicts (`PF-BF-11`).

Forward compatibility against `master` is a separate audit, and its findings are recorded as version notes
rather than by silently re-pointing a citation. (`SCOPE-02` is the requirement; this is how it is worked.)

**Three claims that hold on `master` and fail on the supported baseline**, each recorded below:
`MSP_MODE_RANGES_EXTRA` is Betaflight-only (`PF-BF-08`), `MSP2_INAV_LOGIC_CONDITIONS_CONFIGURED` does
not exist in INAV 8.0.1 (`PF-INAV-04`), and INAV 8.0.1 carries armed guards on several MSP commands
(`PF-INAV-10`).

---

## 1. Betaflight platform facts

### PF-BF-01 — the 2025.12 release series
*Supports `SCOPE-01`.*

**There is no Betaflight `2025.12.0` release.** The GA series is 2025.12.1 … 2025.12.5, preceded only
by `2025.12.0-RC1` … `-RC4`. A version floor is a comparison against real releases, so the
specification shall not name one that was never tagged.

2025.12.1 through .4 carry the custom-message OSD feature and are nonetheless out of contract —
verified present in `msp/msp.c` at each of those tags. The custom-message feature merged 2024-12-22
(PR #14097, *"support for custom OSD messages from external device"*), but **feature availability is
not the contract**: the supported floor is the .5 patch release, which is also the citation baseline.

Writing the range as `2025.12+` is what permits a graceful-degrade test to be written against "a build
older than 2025.12", which is a different question from the one this product needs answered.

### PF-BF-02 — `MSP_RTC` and `MSP_SET_RTC` command IDs
*Supports `SAFE-05`. **This fact is normative and is stated in the PRS.***

[BF] `msp/msp_protocol.h:269-270`; [INAV] `msp/msp_protocol.h:301-302`.

| ID | Betaflight `2025.12.5` | INAV `8.0.1` |
| --- | --- | --- |
| 246 | `MSP_SET_RTC` — **in message, sets the FC clock** | `MSP_RTC` — out message |
| 247 | `MSP_RTC` — out message | `MSP_SET_RTC` — **in message, sets the FC clock** |

**Why this is the more serious of the two allowlist findings.** `MSP_SET_OSD_CANVAS` (`PF-BF-21`) is a
command nobody would deliberately send; it is reached by mistake. `MSP_RTC` is a command `CAM-13`
*requires* sending, where the wrong constant is a **configuration write issued by a product that
promises to issue none**. It is not a decode failure surfacing as a bad timestamp.

The *response* formats also differ — Betaflight returns year/month/day/h/m/s/ms ([BF]
`msp/msp.c:2179`), INAV returns seconds-since-epoch as U32 plus millis U16 ([INAV]
`fc/fc_msp.c:1459`) — which makes the request look shared. It is not. **The format difference is the
visible one and the ID difference is the dangerous one.**

### PF-BF-03 — flash persistence of the permitted OSD write
*Supports `SAFE-07`, `OSD-07`.*

The `MSP2_SET_TEXT` handler writes into the `pilotConfig` parameter group in RAM and **never touches
flash** ([BF] `msp/msp.c:4052` ff.). The handler carries no `ARMING_FLAG(ARMED)` guard; the armed
checks in `msp.c` apply to unrelated commands, so writes are accepted while armed.

**The general rule that only `MSP_EEPROM_WRITE` persists is false on Betaflight and shall not be relied
on** — see `PF-BF-21`. What holds is the narrower and sufficient statement in `SAFE-07`: *no command
the bridge is permitted to send causes a flash write*. That is a property of the allowlist, not of the
MSP protocol.

### PF-BF-04 — `MSP_FC_VERSION` encoding
*Supports `FC-06`. **The encoding is normative and is stated in the PRS.***

[BF] `msp/msp.c:642-647`, `build/version.h:28-35`; [INAV] `fc/fc_msp.c:369-373`.

Three properties are load-bearing, and each is a way to get the gate wrong:

- Betaflight sends `FC_VERSION_YEAR - FC_CALVER_BASE_YEAR`, base year 2000 ([BF]
  `build/version.h:28`). The first byte of a 2025 release is **25, not 2025** — a gate comparing it
  against a literal `2025` refuses every supported build, and one comparing against `20` accepts a
  hypothetical 2020 release.
- Betaflight 4.5 and earlier were semantically versioned and placed `4` in that byte, so the triple is
  **well-ordered across the calendar-versioning transition** (4 < 25). A single lexicographic compare
  against `(25, 12, 5)` is therefore correct for every Betaflight the bridge can meet, with **no
  special case for the version scheme**.
- Betaflight appends a Pascal-string version after the three bytes ([BF] `msp/msp.c:646`); INAV does
  not. **A parser that requires a fixed payload length fails on one platform or the other.**

### PF-BF-05 — no runtime OSD capability probe exists
*Supports `FC-07`.*

The `MSP2_SET_TEXT` handler writes `pilotConfig.message[]` with **no `USE_OSD` guard on that case**
([BF] `msp/msp.c:4080-4090`; only the craft/pilot-name re-analysis at `:4101-4104` is guarded).
Acceptance of the write is therefore **not evidence that the target renders an OSD at all**. That is
confirmed once on the bench, not at runtime, and the version floor is the whole programmatic test on
this platform.

### PF-BF-06 — `rcData` is float, serialized truncated
*Supports `CTRL-14`.*

`rcData` is `float` ([BF] `rx/rx.c:128`) serialized through `sbufWriteU16`, i.e. truncated. Bridge
range comparisons may therefore differ by 1 µs from the FC's own arithmetic at an exact boundary.

### PF-BF-07 — the 25 µs mode-range grid
*Supports `CTRL-15`, `CTRL-16`.*

Betaflight quantises mode ranges to a 25 µs grid —
`MODE_STEP_TO_CHANNEL_VALUE(step) = 900 + 25 * step`, [BF] `fc/rc_modes.h:101`.

**Why ±10 µs and not ±25 µs.** A hysteresis of ±25 µs would span a full step and make two adjacent
ranges mutually ambiguous; ±10 µs stays safely under half a step, so a user who lays bridge ranges out
on the same grid as their FC modes **cannot create an overlap by enabling hysteresis**. The minimum
range width of `4 × hysteresis` and the `2 × hysteresis` boundary separation follow from the same
argument.

### PF-BF-08 — `MSP_MODE_RANGES_EXTRA` is Betaflight-only
*Supports `CTRL-26`.*

`MSP_MODE_RANGES_EXTRA` (238) exists at [BF] `msp/msp_protocol.h:263` and **does not exist in INAV
8.0.1**. `MSP_MODE_RANGES` (34) is the only command available on both — [BF]
`msp/msp_protocol.h:106`, [INAV] `msp/msp_protocol.h:127` — and carries AUX index and active range,
which is all the advisory needs. Advisory behaviour depending on the EXTRA command **silently degrades
to nothing on INAV**.

### PF-BF-09 — the RX-validity guard is valid while armed
*Supports `RCV-02`.*

`ARMING_DISABLED_RX_FAILSAFE` is carried in the `armingDisableFlags` U32 appended to `MSP_STATUS`
([BF] `msp/msp.c:1128`, within the `MSP_STATUS` case at `:1094-1095`).

`failsafeOnValidDataFailed()` sets this flag **unconditionally** when RX data fails, including
`BOXFAILSAFE`-induced loss ([BF] `flight/failsafe.c:196-209`, set at `:202`), and
`setArmingDisabled()` is an unconditional bitwise OR **with no armed-state check** ([BF]
`fc/runtime_config.c:73-76`).

**The guard is therefore valid while armed, and its use does not constitute FC armed-state decoding.**
Note that Betaflight implements input validity *as* an arming permission flag, which is why the PRS
must specify its edges (`RCV-05`) rather than treat it as a clean boolean.

### PF-BF-10 — the data leads the flag
*Supports `RCV-03`. This is the ordering that makes the bracket both necessary and sufficient.*

The RX failsafe path can replace an AUX channel with its configured HOLD/SET failsafe value before
`MSP_RC` is generated — `getRxfailValue()` at [BF] `rx/rx.c:619`, applied at `:724`, `:730`, `:744`,
written to `rcData` at `:762`. AUX channels default to `RX_FAILSAFE_MODE_HOLD` ([BF] `rx/rx.c:158`),
which is benign; `RX_FAILSAFE_MODE_SET` is the hazard.

In `calculateRxChannelsAndUpdateFailsafe`, substituted values are written to `rcData[]` in the
per-channel loop (`:762`) and the failsafe flag is updated only **afterwards**, at `:767,770`:

```
bridge: MSP_STATUS  →  guard clear
                       FC: rcData ← failsafe SET value   (rx.c:762)
                       FC: guard asserted                (rx.c:770)
bridge: MSP_RC      →  substituted value, guard looked clear
```

Because the FC's internal data→flag gap is **microseconds** while the bridge's inter-transaction gap is
**milliseconds**, a **trailing** status read is a reliable detector. `STATUS₂` is the load-bearing leg.

**Scope of the hazard, stated precisely.** Under the stock `HOLD` default the substituted value *is*
the last accepted value, so no transition can be manufactured and the guard has nothing to catch. The
guard exists for `RX_FAILSAFE_MODE_SET`, which a pilot may legitimately configure on the bridge's AUX
channel. Implementers shall not read routine RX glitches as hazards — but a spec that works only under
default config is not a spec.

### PF-BF-11 — assert is immediate, clear is delayed
*Supports `RCV-05`, `RCV-06`.*

The flag clears only after `receivingRxDataPeriodPreset` of continuously valid data ([BF]
`flight/failsafe.c:186-192`), which is `rxDataRecoveryPeriod` — `failsafe_recovery_delay`, floored at
`PERIOD_RXDATA_RECOVERY` = 100 ms ([BF] `flight/failsafe.c:101-105,111`).

For documentation only: the stock 2025.12.5 default is **500 ms**
(`DEFAULT_FAILSAFE_RECOVERY_DELAY = 5`, or `1` = 100 ms under `USE_RACE_PRO` — [BF]
`flight/failsafe.c:66-70`), and the setting is user-configurable.

**Comments elsewhere in that file say "1.0s"; the reset template is authoritative.** This is the
concrete instance behind the standing rule in §0 about reading code rather than comments.

### PF-BF-12 — `BOXFAILSAFE` recovery may be immediate, but not universally
*Supports `RCV-05`'s no-special-case rule.*

Where Betaflight has entered the switch-induced failsafe path it sets
`receivingRxDataPeriodPreset = 0` ([BF] `flight/failsafe.c:339`), removing the recovery delay **for
that recovery**. This is a property of **one branch** of `failsafeUpdateState`, **not** a guarantee
attaching to every state in which `BOXFAILSAFE` has been touched.

Hence `RCV-05`: wait for the guard to clear, regardless of why or how quickly. Switch-induced and
transmitter-loss recovery produce visibly different latencies; both are correct, and a tester who sees
only one will report the other as a bug.

### PF-BF-13 — a clear guard is not proof that valid RC data was ever received
*Supports `RCV-08`. This is the whole reason startup qualification exists.*

A clear guard is only proof that the FC has **not yet concluded otherwise** — and immediately after
boot it has not yet had time to conclude anything:

- `failsafeReset()` does **not** set the flag ([BF] `flight/failsafe.c:94-115`). It sets
  `rxLinkState = FAILSAFE_RXLINK_DOWN` and nothing more.
- The flag is asserted later by one of: `failsafeOnValidDataReceived()` on the **first** valid data,
  via the `validRxDataFailedAt == 0` branch (`:176-183`); `failsafeOnValidDataFailed()` on invalid data
  (`:202`); or `failsafeCheckDataFailurePeriod()` (`:212-221`, set at `:219`), which the scheduler runs
  every 10 ms with **no `failsafeIsMonitoring()` gate** — that gate applies only to
  `failsafeUpdateState` at `:231` — from [BF] `scheduler/scheduler.c:569-571`.
- With no receiver present at all, that third path asserts only after `rxDataFailurePeriod` elapses,
  i.e. `failsafe_delay`, whose **stock default is 15 = 1.5 s** ([BF] `flight/failsafe.c:75`).

So there is a window of up to **~1.5 s** in which the guard reads clear and `rcData[]` holds
initialisation values: `midrc` for every channel ([BF] `rx/rx.c:308`). A criterion of "observe one
valid guarded RC snapshot" is satisfied by that window — `STATUS₁` clear, `MSP_RC` returning `midrc`,
`STATUS₂` clear — and the baseline it captures is an initialisation value.

**The concrete failure this prevents:**

| Control | Init value | Boot-window behaviour without qualification |
| --- | --- | --- |
| **Arm Record** | ARM AUX is explicitly initialised to an **OFF** value ([BF] `rx/rx.c:314` ff.) | safe — **by luck, not by design** |
| **AUX Toggle / Push Button** on any other AUX | `midrc` (1500) | a configured range spanning mid-stick reads **ACTIVE at boot** — the camera starts recording on the bench with the transmitter switched off |

**Why the two conditions together are sufficient.** The guard is asserted on **every** boot path,
either by `failsafeOnValidDataReceived()` on first valid data (`:180-183`) or by the timeout (`:219`).
There is no sequence in which it stays clear indefinitely:

| Case | Path |
| --- | --- |
| Fresh FC, transmitter **on**, bridge up within the window | first RC asserts the guard, which then clears → **A** |
| Fresh FC, transmitter **off** | guard asserts at `failsafe_delay` before B's grace expires → stays **UNQUALIFIED**, correctly |
| Fresh FC, transmitter **on**, bridge slower to boot than ~1.5 s | assert→clear already happened unobserved → **B**, after the grace |
| Bridge attached to an FC running for minutes | guard long since clear → **B** |

**Row three is documented rather than fixed.** A bridge that finishes MSP sync more than
`failsafe_delay` after FC boot misses the edge and waits out condition B, so normal power-up
qualification can take roughly `failsafe_delay + grace` ≈ 3 s at stock settings. This is slower than
camera BLE connect and therefore invisible in use — but it **will** be filed as a bug if it is not
written down.

### PF-BF-14 — why `margin` has the form it does
*Supports `RCV-08`, `RCV-10`.*

`failsafeReset()` sets `validRxDataReceivedAt = 0` ([BF] `flight/failsafe.c:106`) and
`failsafeCheckDataFailurePeriod()` compares it against raw `millis()` (`:215`), so the FC's deadline
runs from **MCU boot**. Bridge sync is necessarily at or after FC boot, which makes condition B's expiry
unconditionally later than the FC's own assertion — **B cannot fire first.**

**`margin` therefore exists to guarantee the assertion is *observed*, not to guarantee it has
occurred.** The derivation of `margin = max(2 × qualification poll period, 100 ms)`:

- the assertion is **sticky**, re-asserted every 10 ms while data is absent (`:212-221`), so it does
  not have to be caught in a narrow window — it only has to be sampled once while it stands;
- the interval in which it stands but B has not yet expired is at least `margin` long, so a margin of
  two poll periods yields **at least two** sampling opportunities, tolerating one lost or failed
  transaction;
- the formula **scales automatically with the poll rate**: 10 Hz → 200 ms, 20 Hz → 100 ms;
- the **100 ms floor** exists because MSP round-trip latency and scheduler jitter are not zero. At a
  hypothetical 50 Hz qualification poll, `2 × 20 ms` would be comparable to a single transaction's own
  latency — precisely the kind of unstated timing assumption `RCV-08` exists to remove.

**Why continuity is counted in observations** (`RCV-10`): a bridge whose status polls are timing out is
not observing anything, and shall not qualify by letting the clock run.

**Why there is no fixed fallback grace** (`RCV-11`): `failsafe_delay` is user-settable well beyond its
default, so no hardcoded grace bounds it. Falling back to condition A only costs late-attach
convenience and costs nothing in safety.

### PF-BF-15 — the `failsafe_delay` floor
*Supports `RCV-09`.*

`MSP_FAILSAFE_CONFIG` returns `failsafe_delay` as its first byte ([BF] `msp/msp.c:1657`), a **U8 in
tenths of a second**. The FC floors the derived period:

```c
failsafeState.rxDataFailurePeriod = failsafeConfig()->failsafe_delay * MILLIS_PER_TENTH_SECOND;
if (failsafeState.rxDataFailurePeriod < PERIOD_RXDATA_RECOVERY) {
    failsafeState.rxDataFailurePeriod = PERIOD_RXDATA_RECOVERY;   // 100 ms
}
```

[BF] `flight/failsafe.c:96-100`. A pilot who sets `failsafe_delay = 0` gets **100 ms, not zero**.

### PF-BF-16 — Betaflight OSD constants
*Supports `OSD-07`. **These constants are normative and are stated in the PRS.***

| Item | Value | Source |
| --- | --- | --- |
| Command | `MSP2_SET_TEXT = 0x3007` | [BF] `msp/msp_protocol_v2_betaflight.h:28` |
| Subtype base | `MSP2TEXT_CUSTOM_MSG_0 = 7` (so 7, 8, 9, 10) | [BF] `msp_protocol_v2_betaflight.h:43` |
| Slot count | `OSD_CUSTOM_MSG_COUNT = 4` | [BF] `pg/pilot.h:27` |
| Chars per slot | `MAX_NAME_LENGTH = 16` | [BF] `pg/pilot.h:26,32` |
| Handler | `mspProcessInCommand` (`msp.c:2647`), `case MSP2_SET_TEXT` | [BF] `msp/msp.c:4052` |

### PF-BF-17 — Betaflight is not the safer platform for persisted OSD text
*Supports `OSD-09`, `INST-04`.*

`pilotConfig_t` carries `char message[OSD_CUSTOM_MSG_COUNT][MAX_NAME_LENGTH + 1]` and **is a parameter
group** ([BF] `pg/pilot.h:29-35`), so a Configurator or CLI `save` performed by the user persists
whatever the bridge last wrote — exactly as on INAV.

The exposure is a **telemetry-truth problem, not a flight-safety one**:

```
runtime          bridge writes  REC 04:22
user saves FC configuration
power cycle      FC loads       REC 04:22        ← no camera, no bridge, no recording
bridge dead / absent / slow
                 OSD still says REC 04:22        ← indefinitely
```

That is the OSD asserting a camera-confirmed state that nothing confirmed. `OSD-09` closes the case
where the bridge starts; **a bridge that never comes up cannot clear anything**, so the residual case —
persisted text plus a dead bridge — remains, and is bounded only by `INST-04`'s documentation note.

### PF-BF-18 — passthrough exit is opt-in
*Supports `SETUP-18`.*

**By default the only exit is a power cycle.** `cliSerialPassthrough()` prints
`"Forwarding, power cycle %sto exit."` ([BF] `cli/cli.c:1629`) before entering the non-returning loop
([BF] `cli/cli.c:1671`).

A DTR-driven exit exists **only** when the `reset` DTR action was given on the command line —
`strcasecmp(tok, "reset") == 0` sets `port1ResetOnDtr` ([BF] `cli/cli.c:1510`) — **and** only when the
second port is the USB VCP ([BF] `cli/cli.c:1625`). Under those two conditions the CLI registers
`cbCtrlLine_reset` ([BF] `cli/cli.c:1633`), which calls `systemReset()` when the host de-asserts DTR
([BF] `cli/cli.c:1398`).

Syntax:
`serialpassthrough <id1> [<baud1>] [<mode1>] [none|<dtr pinio>|reset] [<id2>] [<baud2>] [<mode2>]`
([BF] `cli/cli.c:6681`; a build without `USE_PINIO` offers `[none|reset]`, `:6683`).

### PF-BF-19 — passthrough serves no MSP
*Supports `SETUP-15`, `SETUP-17`.*

`serialPassthrough()` is an unconditional `while (1)` byte-copy loop that **never returns** ([BF]
`io/serial.c:692`, loop at `:708`; [INAV] `io/serial.c:526`). While it is active the FC copies bytes
rather than serving MSP, so `MSP_RC` is unavailable and no bridge mode needing live FC input can run
underneath it.

### PF-BF-20 — Betaflight has **not** removed stick arming
*Supports `INST-05`. Corrected 2026-08-26; see §4.1.*

| Path that needs no arm switch | Enabled when | Default |
| --- | --- | --- |
| **yaw-stick arming** — throttle low, yaw high, pitch and roll centred, held for `ARM_DELAY_MS`, then `tryArm()` ([BF] `fc/rc_controls.c:225`, `:226`, `:229-230`) | `enable_stick_arming` is `ON` **and** no `ARM` mode range is configured ([BF] `fc/rc_controls.c:430`, `cli/settings.c:1730`); additionally suppressed while the `STICK COMMAND DISABLE` mode is active ([BF] `fc/rc_controls.c:225`) | `OFF` ([BF] `config/config.c:121`) |

`isUsingSticksToArm` initialises `true` ([BF] `fc/rc_controls.c:71-72`) and is resolved once at
`rcControlsInit()` ([BF] `fc/rc_controls.c:430`). Betaflight's own code treats stick arming as a live,
less-safe configuration rather than a removed one — for example launch control requires switch arming
explicitly ([BF] `fc/core.c:201`).

### PF-BF-21 — `MSP_SET_OSD_CANVAS` writes flash and resets the FC
*Supports `SAFE-02`, `SAFE-07`. Not permitted by `SAFE-03`; recorded because it is the empirical basis
for the closed policy.*

`MSP_SET_OSD_CANVAS` (188, [BF] `msp/msp_protocol.h:231`) writes `osdConfig` and `vcdProfile`, then
calls `writeEEPROM()` **and `systemReset()`** where the FC was not already in HD + MSP-DisplayPort mode
— with **no `MSP_EEPROM_WRITE` and no armed guard** ([BF] `msp/msp.c:4391`, `:4404-4405`).

It is **one digit** from the permitted read `MSP_OSD_CANVAS` (189) in both name and ID. An implementer
fetching canvas dimensions to format `OSD-02` fields is one keystroke from an in-flight FC reset.

### PF-BF-22 — ARM's permanent ID, and its correspondence to the status bit
*Supports `FC-12`. **Both halves are normative and are stated in the PRS.** Added 2026-08-27, closing CR-24 —
a conformance input that was absent from both documents.*

[BF] `msp/msp_box.c:43-46`, `:161-168`, `:171-186`, `:377-395` (the correspondence at `:381-390`);
`msp/msp.c:2171-2175`.

| Fact | Where |
| --- | --- |
| `BOXARM` is declared in `boxes[]` with name `"ARM"` and **`permanentId = 0`** | `msp_box.c:43-46` |
| `serializeBoxPermanentIdFn()` writes `box->permanentId` directly — the response carries permanent IDs, not `boxId` values | `msp_box.c:161-168` |
| `serializeBoxReply()` iterates `boxId` ascending and emits **only active boxes** | `msp_box.c:171-186` |
| `MSP_BOXIDS` dispatches to that serializer | `msp.c:2171-2175` |
| `packFlightModeFlags()` traverses the **same active-box iteration**, incrementing `mspBoxIdx` as the status-bit index, documented as matching the sent permanent IDs and box names — *"in the order we delivered them"* | `msp_box.c:377-395`, load-bearing at `:381-390` |

**The consequence, and it is the whole point of the lookup:** the **position of permanent ID 0 in the
`MSP_BOXIDS` response** is ARM's bit position in `MSP_STATUS`'s flight-mode flags.

**The permanent ID is fixed; the bit position is not.** `serializeBoxReply()` emits only *active* boxes, so
the packed index of any box depends on which modes the pilot has configured. **An implementation that
hardcodes ARM to bit 0 is correct only on a craft where ARM is the sole active box** — and it fails silently
elsewhere, reading some other mode's bit as the arming interlock. That is the failure `FC-12`'s lookup
exists to prevent, and recording only `permanentId = 0` without this correspondence would have invited it.

### PF-INAV-01 — `MSP_RTC` / `MSP_SET_RTC`
See `PF-BF-02`; the two facts are one table.

### PF-INAV-02 — `ARMING_DISABLED_RC_LINK` is not maintained while armed
*Supports `FC-11`, `RCV-19`. This is the single most consequential INAV fact in the specification.*

The entire arming-check block sits inside the `else` of:

```c
static void updateArmingStatus(void)
{
    if (ARMING_FLAG(ARMED)) {
        LED0_ON;
    } else {
        /* ... every arming check, including RC_LINK ... */
```

[INAV] `fc/fc_core.c:200-225`.

**While armed, `ARMING_DISABLED_RC_LINK` holds whatever value it had at the arming instant.** Two
consequences follow, and both are normative elsewhere:

1. Reading `ARMED` is a **staleness qualifier on another reading**, in the same category as a
   timestamp — which is what makes `FC-11`'s narrow permission coherent rather than a hole in it.
2. The flag is **startup-only**, and the reason must be stated wherever the requirement is: it is not a
   design preference a later implementer may revisit for a cleaner runtime gate than Betaflight's.
   **Promoting it to one yields a validity signal that stops updating at exactly the moment flight
   begins.**

### PF-INAV-03 — `rxGetChannelValue` may return overridden values
*Supports `CTRL-24`.*

```c
int16_t rxGetChannelValue(unsigned channelNumber) {
    if (LOGIC_CONDITION_GLOBAL_FLAG(LOGIC_CONDITION_GLOBAL_FLAG_OVERRIDE_RC_CHANNEL)) {
        return getRcChannelOverride(channelNumber, rcChannels[channelNumber].data);
    } else {
        return rcChannels[channelNumber].data;
    }
}
```

[INAV] `rx/rx.c:657`. `MSP_RC` is the FC's **effective channel state**, not a raw tap of the receiver
protocol — on Betaflight it includes scaling and may include failsafe substitution; on INAV it may
include Logic-Condition overrides. The bridge overrides nothing, so `SAFE-02` is not implicated.

INAV versions **later than 8.0.1** additionally expose an MSP AUX overlay for CH9–CH32
(`MSP2_INAV_SET_AUX_RC`), settable by any MSP client. This is **not present in 8.0.1** and is noted only
so that a future retarget re-examines it.

### PF-INAV-04 — `MSP2_INAV_LOGIC_CONDITIONS_CONFIGURED` does not exist in 8.0.1
*Supports `CTRL-27`.*

`MSP2_INAV_LOGIC_CONDITIONS_CONFIGURED` (0x203C) appears in **neither** `msp/msp_protocol_v2_inav.h`
**nor** `fc/fc_msp.c` at tag `8.0.1`; it is a later addition. A bridge that requires it would fail
against the minimum supported release.

The two commands that do exist:

| Purpose | Command | Source |
| --- | --- | --- |
| Logic condition results | `MSP2_INAV_LOGIC_CONDITIONS_STATUS` (0x2026) | [INAV] `fc/fc_msp.c:570`; `msp_protocol_v2_inav.h:68` |
| Global variable values | `MSP2_INAV_GVAR_STATUS` (0x2027) | [INAV] `fc/fc_msp.c:575`; `msp_protocol_v2_inav.h:69` |

Both are read-only and sit behind the same `USE_PROGRAMMING_FRAMEWORK` gate as the custom OSD elements.

### PF-INAV-05 — INAV freezes values through two independent mechanisms
*Supports `RCV-16`.*

Per-channel, an invalid pulse is replaced by the last good value:

```c
// Apply invalid pulse value logic
if (!isRxPulseValid(sample)) {
    sample = rcChannels[channel].data;   // hold channel, replace with old value
```
[INAV] `rx/rx.c:489`

And globally, no channel's data updates at all unless both the flight channels and the RX signal are
valid:

```c
if (rxFlightChannelsValid && rxSignalReceived) {
    for (int channel = 0; channel < rxChannelCount; channel++) {
        rcChannels[channel].data = rcStaging[channel];
    }
}
```
[INAV] `rx/rx.c:500-506`

**There is consequently no path by which an AUX value changes during RX loss**, and INAV has no
per-channel failsafe SET equivalent to Betaflight's. `MSP_RC` naturally freezes.

Because INAV freezes *values* rather than *transitions*, there is **no INAV recovery window** and no
INAV equivalent of the swallowed-Push-Button case. Recovery is immediate and continuous.

### PF-INAV-06 — mid-stick channel initialisation
*Supports `RCV-17`.*

```c
rcChannels[i].raw  = PWM_RANGE_MIDDLE;
rcChannels[i].data = PWM_RANGE_MIDDLE;
```
[INAV] `rx/rx.c:271-272`, with the ARM AUX specially initialised to an OFF value at `:279-289` — the
identical safe-by-luck asymmetry documented in `PF-BF-13`'s table.

A fresh INAV FC with no transmitter therefore returns 1500 from `MSP_RC`, and a Record range spanning
mid-stick reads ACTIVE **before any real RC packet has existed**.

### PF-INAV-07 — `ARMING_DISABLED_RC_LINK` is driven directly from link state
*Supports `RCV-17`, `RCV-18`.*

```c
/* CHECK: RX signal */
if (!failsafeIsReceivingRxData()) {
    ENABLE_ARMING_FLAG(ARMING_DISABLED_RC_LINK);
} else {
    DISABLE_ARMING_FLAG(ARMING_DISABLED_RC_LINK);
}
```
[INAV] `fc/fc_core.c:220-225`

`failsafeIsReceivingRxData()` is simply `rxLinkState == FAILSAFE_RXLINK_UP` ([INAV]
`flight/failsafe.c:289`), with `failsafeReset()` setting `rxLinkState = FAILSAFE_RXLINK_DOWN` (`:165`).

**There is no timeout path.** The flag is asserted from the first `updateArmingStatus()` pass and clears
only when data actually arrives. **On INAV, unlike Betaflight, a clear guard genuinely proves valid RC
data was received** — which is why INAV needs no grace, no `MSP_FAILSAFE_CONFIG` read and no bracket.

The flag is bit 18 ([INAV] `fc/runtime_config.h:38`), exposed as part of the full U32 `armingFlags` in
`MSP2_INAV_STATUS` (`sbufWriteU32(dst, armingFlags)`, [INAV] `fc/fc_msp.c:459`).

### PF-INAV-08 — INAV upper-cases custom element text at render
*Supports `OSD-01`.*

Text is upper-cased by `sl_toupper` ([INAV] `io/osd/custom_elements.c:215`). Composing all strings in
uppercase on **every** platform is what makes Betaflight and INAV render identically.

### PF-INAV-09 — INAV OSD constants and payload
*Supports `OSD-08`. **These constants are normative and are stated in the PRS.***

| Item | Value | Source |
| --- | --- | --- |
| Command | `MSP2_INAV_SET_CUSTOM_OSD_ELEMENTS = 0x2102` | [INAV] `msp/msp_protocol_v2_inav.h:114` |
| Capability query | `MSP2_INAV_CUSTOM_OSD_ELEMENTS = 0x2100` | [INAV] `msp_protocol_v2_inav.h:112` |
| Element count | `MAX_CUSTOM_ELEMENTS = 8` | [INAV] `io/osd/custom_elements.h:24` |
| Parts per element | `CUSTOM_ELEMENTS_PARTS = 3` | [INAV] `io/osd/custom_elements.h:23` |
| Text size | `OSD_CUSTOM_ELEMENT_TEXT_SIZE = 16` (15 usable + NUL) | [INAV] `io/osd/custom_elements.h:22` |
| Handler | `mspFcProcessInCommand` | [INAV] `fc/fc_msp.c:3519` |
| Build gate | `USE_PROGRAMMING_FRAMEWORK` (default on) | [INAV] `target/common.h:172` |

The handler enforces
`dataSize == (OSD_CUSTOM_ELEMENT_TEXT_SIZE - 1) + (CUSTOM_ELEMENTS_PARTS * 3) + 4` = **28**
([INAV] `fc/fc_msp.c:3521`) — short writes are rejected with `MSP_RESULT_ERROR`.

Type and visibility enums: [INAV] `io/osd/custom_elements.h:26-63`; `CUSTOM_ELEMENT_TYPE_TEXT` at `:28`,
`CUSTOM_ELEMENT_VISIBILITY_ALWAYS` at `:60`.

The capability probe reply carries `MAX_CUSTOM_ELEMENTS`, `OSD_CUSTOM_ELEMENT_TEXT_SIZE - 1` and
`CUSTOM_ELEMENTS_PARTS` ([INAV] `fc/fc_msp.c:1732-1737`). A missing or error reply means the target was
built without `USE_PROGRAMMING_FRAMEWORK` and has **no custom elements at all**.

### PF-INAV-10 — the custom-element setter is unguarded, but INAV does guard other commands
*Supports `OSD-08`, `SAFE-07`.*

The custom-element setter carries **no** `ARMING_FLAG(ARMED)` guard — [INAV] `fc/fc_msp.c:3519-3540` —
and is therefore reachable while armed. It writes `osdCustomElementsMutable(n)`, the parameter group in
RAM. **No flash write.**

INAV 8.0.1 **does** carry armed guards on other MSP commands, at [INAV]
`fc/fc_msp.c:1434,1894,2617,2628,2635,2643,2651,3316,3324`. None covers
`MSP2_INAV_SET_CUSTOM_OSD_ELEMENTS`, so the conclusion holds — **but the claim shall be stated as "this
handler is unguarded", never as "INAV guards only reboot".** The latter would license writes that are in
fact blocked.

INAV offers 8 × 15 characters against Betaflight's 4 × 16, so `OSD-02` parity is comfortably met with
room for optional fields. Exact visual layout need not match Betaflight. Custom elements are equally a
shared resource with the Programming Framework, which is why `OSD-04` applies identically.

### PF-INAV-11 — INAV has no passthrough DTR exit
*Supports `SETUP-19`.*

`cliSerialPassthrough()` accepts **no DTR-action argument**
(`serialpassthrough <id> [baud] [mode] [options]`, [INAV] `fc/cli.c:4889`), prints
`"Forwarding data to %d, power cycle to exit."` ([INAV] `fc/cli.c:1032`), and enters the same
non-returning loop ([INAV] `fc/cli.c:1034` → `io/serial.c:526`). That file contains **no
`CTRL_LINE_STATE_DTR` handling and no `serialSetCtrlLineStateCb` registration of any kind.**

### PF-INAV-12 — INAV has **no** stick arming
*Supports `INST-05`. Corrected 2026-08-26; see §4.1.*

| Path that needs no arm switch | Enabled when | Default |
| --- | --- | --- |
| **fixed-wing auto-arm on throttle** above `min_check` ([INAV] `fc/rc_controls.c:215-219`) | `fixed_wing_auto_arm` is `ON`, the model is an airplane, and motorstop is enabled — INAV's own description adds *"Requires enabled motorstop and no arm switch configured"* | `OFF` ([INAV] `fc/settings.yaml:1545-1548`) |

INAV `8.0.1` arms only from `BOXARM` ([INAV] `fc/rc_controls.c:213`, `:223-225`) or the fixed-wing path
above. Its stick-command block is reached only when disarmed ([INAV] `fc/rc_controls.c:249-250`) and,
past the comment that introduces it (`:258`), handles calibration, profile selection and trim only — **no
arming.**

### PF-INAV-13 — INAV OSD injection is confirmed available
*Supports `OSD-08`, `OSD-11`.*

Runtime injection into INAV Custom OSD Elements is confirmed in source: the mechanism exists, is
reachable while armed, and does not touch flash. **No `MSP_SET_NAME` → Craft Name fallback is required or
specified.** INAV 8.0.0 answers the capability probe successfully and is nonetheless
`UNSUPPORTED_FC_VERSION` — the probe reports what the target was **built with**, never whether the build
is inside the support contract.

---

## 3. External facts

### PF-EXT-01 — Open GoPro
*Supports `SCOPE-03`, `CAM-08`.*

Open GoPro is GoPro's official, actively maintained programme (API version 2.0; repository last updated
2026-06-08). BLE covers what `OSD-02` needs: start/stop capture, recording status, battery, SD capacity,
and resolution/frame-rate settings. The supported-model list is an input to the compatibility matrix, and newer HERO models are not
automatically covered by an earlier claim. **The obligation this establishes is `CAM-14`**; what lives here
is only the fact that the list moves.

### PF-EXT-02 — DJI Osmo Nano: two protocol families, and the write hazard
*Supports `CAM-10`, `CAM-11`.*

**1. The official "R SDK" protocol** — what DJI's own Osmo Action GPS Bluetooth Remote speaks, with an
MIT-licensed ESP32 reference at `dji-sdk/Osmo-GPS-Controller-Demo` (ESP-IDF v5.5; ships
`sdkconfig.defaults.esp32c3`). Its device-ID table:

| Model | `device_id` |
| --- | --- |
| Osmo Action 4 | 0xFF33 |
| Osmo Action 5 Pro | 0xFF44 |
| Osmo Action 6 | 0xFF55 |
| Osmo 360 | 0xFF66 |
| **Osmo Nano** | *"Not supported yet. Please wait for a future firmware update."* |

That quotation is DJI's own text, from `docs/protocol_data_segment.md` in the reference repository. It is
corroborated independently: DJI's published accessory compatibility list marks the Osmo Action GPS
Bluetooth Remote Controller as **not compatible** with the Osmo Nano. The reference repository has had no
commits since 2025-11-17, so the promised firmware update has been outstanding for the better part of a
year and **shall not be planned around**.

**2. DUML** — DJI's universal binary protocol, reverse-engineered by the community and carried over the
same GATT profile: service `0xFFF0`, notify `0xFFF4`, write `0xFFF5`. Published implementations exist for
the Pocket 3 (`lib-osmo-ble`, updated 2026-02, full DUML framing with CRC8/CRC16) and for Action 3/4/5
plus Pocket 3 (`node-osmo`). **Neither covers the Nano.**

**Working hypothesis.** The Nano is *expected* to speak DUML, because DUML is DJI-wide and the Nano is
architecturally closer to a Pocket 3 than to an Action. **This is an inference and has not been tested.**

**Hazard — silent write failure.** Characteristic `fff3` accepts writes *with response* and returns
success **while the firmware discards the payload**. Only `fff5` (write-without-response) actually
processes DUML. A driver that writes `fff3` looks entirely correct and does nothing. This is documented
behaviour that already caught an existing library — hence `CAM-10`.

DJI's own GPS push runs at 10 Hz to tag footage, but over the R SDK protocol, which the Nano does not
speak. **Whether the Nano accepts a GPS push over DUML is unknown**, is not part of `VAL-SPIKE-04`'s pass
criteria, and shall be treated as speculative until a driver exists.

### PF-EXT-03 — DJI reference repository licence
*Supports `CAM-12`.*

The `LICENSE` file is **layered**: the R SDK **protocol documents** are under DJI's End User License
Agreement, while the **demo software** is offered under the EULA *and* the MIT License (© 2025 SZ DJI
Technology Co., Ltd).

Because the Nano path is DUML rather than R SDK, DJI's MIT-licensed code is **not** the basis of the Nano
driver. The redistribution prohibition this establishes is `CAM-12`.

DJI could later add the Nano to the official R SDK protocol. **No requirement covers that case**: a preference
for the official path was recorded in source §20.2 and was **deliberately removed** rather than restored
(Ledger CR-12), because V1.1 is directed at the DUML path. Should it happen, that is a specification change,
not a decision an implementer makes.

---

## 4. Rationale

Non-normative. Retained because losing it would make a future engineer likely to "simplify" a
requirement incorrectly.

### 4.1 Why the MSP policy is an allowlist rather than a denylist

`SAFE-02` is deliberately the inverse of an enumerated-hazards rule, and the reason is empirical. **A
prohibition list can only forbid what its author already knows to be dangerous**, and the audit behind
this revision found two commands that no reasonable denylist would have contained: `MSP_SET_OSD_CANVAS`
(`PF-BF-21`) and `MSP_RTC` (`PF-BF-02`). Under a closed policy neither is reachable, because neither is
listed — **no foresight required.**

**Why an implementer reaches for the prohibited ones.** Knowing *why* each is tempting is what stops
someone proposing it in review. None of this narrows `SAFE-03`.

- **RC injection** — `MSP_SET_RAW_RC`, `MSP2_INAV_SET_AUX_RC`. The apparently clean way to make the
  camera "just follow a switch". It writes the pilot's control state.
- **Mode-range writes** — `MSP_SET_MODE_RANGE` (35, [BF] `msp/msp_protocol.h:107`). The apparent way to
  bind a camera action to a box mode.
- **Programming Framework writes** — `MSP2_INAV_SET_LOGIC_CONDITIONS` (0x2023), `MSP2_INAV_SET_GVAR`
  (0x2214). Not RC state, so **not covered by the RC-injection reasoning at all**; a GVAR write is the
  obvious "helpful" shortcut and reaches silently into the pilot's flight programming.
- **`MSP_SET_OSD_CANVAS`** — see `PF-BF-21`; one digit from a permitted read.
- **`MSP_SET_RTC`** — see `PF-BF-02`; reachable by getting one constant backwards rather than by
  intending anything.
- **`MSP_EEPROM_WRITE`, `MSP_REBOOT`**, motor, servo, arming, flight-mode and PID/configuration commands
  — dangerous for reasons that need no elaboration.

**Reads of configuration are permitted; writes are not.** That is why `MSP_FAILSAFE_CONFIG` and INAV's
`armingFlags` appear in the allowlist despite naming subsystems this specification otherwise stays clear
of. **Reading a pilot's failsafe timing so as not to depend on its default value is the opposite of
interfering with it.**

### 4.2 Why the version floor gates camera control and not only OSD writes

Every behavioural claim in `RCV-*` — the failsafe guard's assertion paths, `rxDataFailurePeriod`, the
recovery delay, the `armingFlags` bit position — is read from the pinned baselines and is verified for
those tags only. Acting on control input from an unverified build means **acting on a control path whose
failsafe semantics have not been checked**, which is the precise defect the citation rule exists to
prevent.

**A bridge that does nothing is a correct bridge; a bridge that records on an unverified failsafe path is
not.**

### 4.3 Why `FC-11` is the weaker rule, deliberately

It is **not** true that no camera behaviour anywhere differs by armed state: `RCV-19` makes an INAV
bridge that reboots while armed stay `UNQUALIFIED` until disarm, which is observably different behaviour.
What is invariant — and what `FC-13` states and `VAL-FUNC-*` tests — is **the mapping itself**. Armed
state can delay the bridge starting to act. It can never change what acting means.

"Arm Record" means the Record AUX control is configured to the same channel/range normally used by the
pilot's arm switch. If the switch enters that range but the FC remains disarmed, **the camera still
follows the configured Record control. This is intentional.**

`SETUP-09`'s interlock exists because the alternative is worse: the bridge is powered from the FC's 5 V
rail, so an in-flight rail interruption presents as a power-on reset, and without the interlock a
mid-flight switch pattern could restart the bridge into a mode that raises a Wi-Fi AP. Its cost is
bounded — on INAV `ARMED` is in the same word `RCV-17` already reads; on Betaflight `MSP_STATUS` is
already polled for the `RCV-03` bracket and only the box-index mapping is new, read once at
identification. **Neither adds steady-state traffic.**

### 4.4 Why level control is the default for Record

A level control **reconciles** to the correct state after any interruption — RC loss, bridge reboot, or
the Betaflight recovery window. An edge-triggered control cannot: an event that occurs while the bridge is
not accepting transitions is simply gone.

### 4.5 Why the learning predicates have the values they do

*Supports `LEARN-06`.*

- **Median of 5, not mean.** A median is unmoved by one outlier sample; a mean is not, and the one outlier
  is exactly what a switch bounce or a dropped frame produces at the moment a baseline is captured.
- **100 µs sits in a wide gap.** Jitter on a settled channel is a few microseconds; the smallest step
  `CTRL-16` permits a runtime range to resolve is 40 µs; a physical two- or three-position switch moves
  400–500 µs. It rejects noise and a nudged gimbal while accepting every real switch throw, including a
  transmitter with substantially non-standard endpoints.
- **Two consecutive samples to count as moved.** A single sample over threshold is a glitch, not a
  movement — the same reasoning `CTRL-18` applies to Push Button detection and `SETUP-12` to gesture
  transitions.
- **Spread, not a ± band.** `max - min <= 20 µs` rather than "within ±10 µs", because a ± band requires
  first deciding what the centre is, and two implementations may reasonably pick the mean, the median or
  the first sample and **disagree about identical input**. A spread has no centre and no ambiguity. The
  width is the same; the testability is not.
- **5 samples = 250 ms at 20 Hz**, which is exactly `CTRL-19`'s documented minimum Push Button hold. **A
  momentary control a user is able to demonstrate is therefore, by construction, a control the runtime can
  detect** — the learner cannot accept something the live path would miss.
- **15 s acquisition timeout.** Without it the ambiguity rule is **unbounded in time**: two channels moved
  thirty seconds apart are indistinguishable from two moved together, and a learner left open on a
  forgotten browser tab accumulates state forever.

### 4.6 Why the `PAIR` lifetime cap is 120 s

The cap sits far above the worst legitimate path — a 30 s scan, a 15 s bind attempt and bookkeeping, well
under half the cap — **precisely so that reaching it means a stuck BLE stack and nothing else.** It is a
watchdog, not a schedule. A `PAIR` boot that routinely approaches it is reporting a defect rather than
working slowly.

An implementation that returns `SCAN_TIMEOUT` to `SETUP` but *also* lets the boot run to 120 s has
confused the two.

### 4.7 Why heap needs two figures and a leak condition

*Supports `RES-03`, `RES-04`.*

ESP-IDF's allocator fragments under repeated Wi-Fi and NimBLE connect/disconnect cycles, so a mode can
report ample **total** free heap and still fail the one **contiguous** allocation a reconnect needs. Free
bytes alone are therefore not a criterion on this part.

A floor that holds for the duration of one observation says nothing about a **leak**, which is the failure
this product is most exposed to: `RUN` is the mode that runs for hours and reconnects on every camera
dropout. Hence `RES-04`, and hence `RES-05`'s `RUN` soak being an order of magnitude longer than the
others.

`RES-03`'s floors are provisional in exactly the pattern `RF-08` uses: **a stated value now so tests can
be written against it, a named measurement that closes it, and a named destination to write the result
back to.** What shall not happen is the criterion remaining the word *acceptable* — **a gate whose
threshold is unstated cannot be failed.**

### 4.8 Why `DIAG` is V1.2 whole

The mode is **defined by** holding the Wi-Fi AP and a camera BLE connection at once, which *is* peak 2.
There is therefore no residue of `DIAG` that could survive the measurement failing, and listing the mode
in V1 while its only content is V1.2 would be a requirement that peak-2 failure falsifies. **What `DIAG`
adds over `SETUP` is the camera; without it the mode would be `SETUP` under another name.**

Simultaneous AP + BLE is a property of one **boot mode** rather than an exception granted inside a running
one, which is why it costs a **measurement** rather than an **invariant**.

**`DIAG`'s resource and coexistence validation is functional, not a `RF-*` safety condition.** `SETUP-26`
requires that Setup Mode not be entered while the aircraft is being operated, and `DIAG` is reached only
through Setup Mode, so no flight is in progress and the control-link concern does not arise. The question
is narrower: whether the C3's single 2.4 GHz radio serves an AP and a BLE connection reliably enough for
the mode's output to be trustworthy. **No AP+BLE condition is therefore added to the `VAL-SPIKE-02` sweep**,
whose conditions are flight conditions.

### 4.9 Why a bonded-phone BLE path into Setup Mode was rejected

`SETUP-03`'s guarantee is **per-entry physical presence, not one-time enrolment**. Since entry is a reboot,
a remote trigger would be a remote way to leave INAV `UNQUALIFIED` for the rest of a flight (`RCV-19`) and
to raise an AP mid-flight. It would also require a GATT server resident in `RUN`.

Authority 4 is not a counter-example: **a cable's reach is bounded by its length and by physical access to
the airframe, whereas a bond's reach is a radio's.** The bridge cannot close authority 4's gap by demanding
a fresh attachment — it observes UART bytes and has no visibility of USB attachment at the FC — and
demanding one would defeat the purpose, since a bench host's value is repeated cycles without recabling.

### 4.10 Why Setup isolation is structural rather than disciplinary

An earlier revision addressed the `manualStop` hazard with a "separate `ControlPolicy` instance" rule. That
was insufficient, and the defect was worse than the fix addressed: with a live policy running during Setup
Mode, **the user's own switch flips — made in order to exercise the monitor — reach the live instance
through the ordinary input path** and set `manualStop`. A separate monitor instance does not prevent that.

`BOOT-01` does. `SETUP` runs no live policy at all (`SETUP-24`), and `CTRL-09`'s clear-at-boot rule does
the rest. **There is no state to preserve and no discipline to enforce.**

### 4.11 Why the maintenance gesture's AUX is chosen rather than inherited

`INST-01` step 4 puts Arm Record on the aircraft's ARM AUX, so deriving the gesture from the Record mapping
would make **rapid toggling of the arm switch the documented way into Setup Mode, props fitted**. Hence
`SETUP-06`, and hence `SETUP-07` reusing `CTRL-25`'s machinery with the opposite verdict: for a
camera-control mapping the ARM overlap is an expected advisory; for a maintenance-entry source it is a
prohibition. **A spare AUX is the clean choice.**

**Sample budget for the gesture** (`SETUP-12`): the two-sample dwell costs at least 100 ms per transition at
20 Hz and 200 ms at 10 Hz, so the pattern's floor is 400 ms or 800 ms respectively against a 3.0 s budget —
comfortable at 20 Hz, workable at 10 Hz. The dwell is defined in samples rather than milliseconds for
exactly that reason: **a millisecond dwell would silently become a one-sample dwell at the lower rate.**

### 4.12 Why the boot-mode architecture stays out of the shared core

*Supports `ARCH-03`.*

An integrated receiver **cannot treat a reboot as a cheap maintenance transition, because the radio it would
restart is the control link**; and it has no Wi-Fi/BLE ownership conflict of this shape to resolve.

An integrated build would also have direct access to RC channel data and link validity in the **same address
space and the same instant**, removing the Betaflight `MSP_RC` failsafe ambiguity at source — and with it
the guarded-commit bracket and the recovery-window semantics. Both exist only because an arming-permission
flag is being borrowed as an input-validity signal and read through a transport that cannot deliver the two
together. **That is a property of that architecture, not a reason to weaken `RCV-*` in the standalone
product.** Neither requires actual FC armed-state knowledge.

What survives into an integrated build is `RCV-14`'s semantics (`ARCH-04`). The question *"has real control
input ever arrived?"* is not a transport artifact — **it is a property of every system that reads a control
input.** What disappears is only the *mechanism*.

### 4.13 Betaflight worst-case MSP budget

*Supports `MSP-08`.*

3 transactions × 20 Hz = **60 MSP round-trips per second** at 115200 baud, and only while a Push Button
control is enabled and actively transitioning. **Steady state is 40.** On INAV no equivalent status poll and
no bracket are required.
