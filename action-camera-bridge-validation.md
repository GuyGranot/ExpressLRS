# Action Camera Bridge — Validation Specification

**Companion to** `action-camera-bridge-prs.md`. **This document contains no requirements.** It
specifies how conformity to the PRS is measured and demonstrated.

Every case carries a **Verifies** column naming the requirement IDs it exercises. The relation is
**many-to-many**: one case may verify several requirements and one requirement may need several cases.
The traceability audit (`action-camera-bridge-traceability.md` §4) asserts only that **no requirement
has zero cases and no case has zero requirements**.

**Baseline.** Every run is against stock **Betaflight 2025.12.5** and **INAV 8.0.1** specifically. A
pass on a newer build is evidence about that build, not about the supported minimum (`SCOPE-02`).

**Release applicability and gating are defined solely by `REL-01`; this document does not restate that
contract.** The rule previously restated here named only `VAL-FUNC-*` and `VAL-FAIL-*` — the exact
formulation `REL-01` identifies as defective, because it lets every `VAL-REV-*` review fail while the release
still passes its gate. **One authoritative home, and it is not this document** (v1.3, CR-16).

---

## 1. Functional acceptance — `VAL-FUNC-*`

| ID | Test | Verifies | BF | INAV |
| --- | --- | --- | --- | --- |
| `VAL-FUNC-01` | FC auto-detection | `FC-03` | Req | Req |
| `VAL-FUNC-02` | A build below the minimum enters `UNSUPPORTED_FC_VERSION` — no camera-control input acted on, no platform OSD write issued — while identification and setup remain available | `FC-05`, `FC-08` | Req | Req |
| `VAL-FUNC-03` | Version floor precedes capability probing: on an out-of-contract build that would answer the probe successfully, the probe result does not enable the backend | `FC-04`, `FC-07` | n/a | Req |
| `VAL-FUNC-04` | `MSP_FC_VERSION` byte triple decoded correctly across the calendar-versioning transition — a 4.5-era build and a 2025.12-era build are both ordered against the floor by the same comparison | `FC-06` | Req | n/a |
| `VAL-FUNC-05` | OSD capability probe / graceful degrade: with the probe denied, `osdBackendEnabled` is false, **no platform OSD write is issued at all**, and the same information appears on the setup interface | `FC-07`, `OSD-19` | Req | Req |
| `VAL-FUNC-06` | The control mapping is invariant under armed state: a given control in a given range produces the same camera action whether the FC is armed or disarmed | `FC-13`, `FC-11` | Req | Req |
| `VAL-FUNC-07` | No arm-state poll in steady state after qualification, verified by MSP capture across a full flight | `MSP-02`, `FC-11` | Req | Req |
| `VAL-FUNC-08` | Arm Record follows configured arm-switch AUX even when FC arming is deliberately blocked, **and is behaviourally indistinguishable from the same Level mapping entered by hand** | `CTRL-29`, `FC-13`, `CTRL-04` | Req | Req |
| `VAL-FUNC-09` | MSP writes accepted while FC is armed, **on both backends**, and the bridge issues them: with the FC armed, force a camera-state change and confirm the owned slot updates. Not an arm-state dependency — the bridge never reads armed state to decide this | `OSD-19`, `OSD-07`, `OSD-08` | Req | Req |
| `VAL-FUNC-10` | No flash write across a long operating session | `SAFE-07` | Req | Req |
| `VAL-FUNC-11` | `MSP_RC` returns live AUX values while FC is armed — the platform property `FC-13`'s armed-invariance depends on, confirmed rather than assumed | `FC-13`, `CTRL-12` | Req | Req |
| `VAL-FUNC-12` | Direct AUX record control via `MSP_RC`. **Run additionally on a radio with non-standard endpoints** (a channel whose configured range sits outside 1000–2000 µs): the stored mapping is explicit ranges, so the control behaves identically to one on a standard radio and no endpoint is assumed anywhere in the path | `CTRL-01`, `CTRL-12` | Req | Req |
| `VAL-FUNC-13` | Push Button: 250 ms press detected; 100 ms press either detected or ignored, never double-fired. **The event-separation interval is measured, not inferred from the absence of double-firing:** two deliberate qualifying presses separated by **less than** `CTRL-20`'s configured minimum yield **one** event, and two separated by more than it yield two. At the 400 ms default that means a pair at ~300 ms is coalesced and a pair at ~500 ms is not — an implementation with a 200 ms floor passes every other clause of this case and fails here. **The suppressed press is discarded, not deferred** (`CTRL-20`): after the coalesced pair, the control is left at rest for 2 s and **no second event appears** at any point | `CTRL-18`, `CTRL-19`, `CTRL-20` | Req | Req |
| `VAL-FUNC-14` | RX loss creates no camera-control transition | `RCV-01` | Req | Req |
| `VAL-FUNC-15` | `ARMING_DISABLED_RX_FAILSAFE` guard asserts during real RX loss and `BOXFAILSAFE` while FC is armed | `RCV-02` | Req | n/a |
| `VAL-FUNC-16` | Startup qualification: no camera-control transition of any kind before qualification completes; the first qualified sample reconciles a level control and emits no edge | `RCV-08`, `RCV-14`, `RCV-17` | Req | Req |
| `VAL-FUNC-17` | Startup, no-receiver case: with the transmitter off, no spurious record occurs during the ~1.5 s window before the guard asserts, for a control range spanning `midrc`; qualification does not complete | `RCV-08`, `RCV-13` | Req | n/a |
| `VAL-FUNC-18` | Betaflight qualification condition A: an observed assert→clear edge qualifies the input | `RCV-08` | Req | n/a |
| `VAL-FUNC-19` | Betaflight qualification condition B: a continuously clear guard qualifies only after the computed `grace` from first MSP exchange — measured, and **never earlier** than computed — and the grace tracks both a reconfigured `failsafe_delay` and a changed poll rate | `RCV-08` | Req | n/a |
| `VAL-FUNC-20` | Betaflight condition B continuity: status-poll timeouts during the interval extend it rather than being counted as clear observations | `RCV-10` | Req | n/a |
| `VAL-FUNC-21` | Betaflight fallback: with `MSP_FAILSAFE_CONFIG` denied, the bridge qualifies on condition A only and never on a timer | `RCV-11`, `RCV-13` | Req | n/a |
| `VAL-FUNC-22` | INAV startup, no-receiver case: with the transmitter off, `ARMING_DISABLED_RC_LINK` reads asserted and no spurious record occurs for a control range spanning `midrc` | `RCV-17` | n/a | Req |
| `VAL-FUNC-23` | INAV qualification stops reading `armingFlags` once qualified, and once any maintenance window has closed — no runtime arm-state poll thereafter | `RCV-17`, `MSP-02`, `MSP-03` | n/a | Req |
| `VAL-FUNC-24` | Guarded commit: a transition-bearing RC sample is committed only under a passing `STATUS₁ → RC → STATUS₂` bracket; a failed bracket discards the candidate. **`RCV-04` is a permitted optimisation and this case does not require it.** From the same MSP capture, classify the implementation once: either every poll carries three transactions (bracket-always), or polls whose sample implies no transition carry **two** and issue no `STATUS₂` while transition-bearing polls carry **three**. **Both pass.** What fails is a third pattern — a transition-bearing poll carrying only two — which is `RCV-03` violated under `RCV-04`'s cover. Record which of the two the build uses, because `MSP-08`'s budget argument assumes the optimised one | `RCV-03`, `RCV-04` | Req | n/a |
| `VAL-FUNC-25` | Recovery latency: guard clear is awaited rather than timed; level control reconciles after both TX-off recovery and `BOXFAILSAFE` revert, whatever the two latencies are | `RCV-05`, `RCV-06`, `RCV-07` | Req | n/a |
| `VAL-FUNC-26` | `Combi` arbitration matches the `CTRL-06` truth table, including override clearing when the level control leaves its range. **Run additionally with a Combi button mapped to a non-stop action** (photo capture): it fires on each press, sets no `manualStop`, and the Record level control's intent is unaffected throughout — **a stateless button that silently joined the arbitration would suppress recording on a photo press**, and nothing else in this suite would show it | `CTRL-06`, `CTRL-07`, `CTRL-10` | Req | Req |
| `VAL-FUNC-27` | Range evaluator: ±10 µs hysteresis observed; sub-40 µs range rejected at setup; **no behaviour depends on a single-microsecond boundary** — a channel parked exactly on a boundary produces a stable verdict, not chatter. **`CTRL-17` is checked at the store, not at the evaluator:** after the control has been driven repeatedly across both thresholds, the persisted range is re-read and is **byte-identical to what the user entered** — hysteresis has not been folded into it. A widened stored range evaluates correctly and fails silently the moment the hysteresis default changes | `CTRL-15`, `CTRL-16`, `CTRL-17`, `CTRL-14` | Req | Req |
| `VAL-FUNC-28` | INAV `MSP_RC` retains last-good AUX values through RX loss | `RCV-16` | n/a | Req |
| `VAL-FUNC-29` | No spurious Push Button event on Betaflight RX recovery re-baseline. **Run in two parts, and the second is the one that tests `CTRL-22`.** (a) Recovery with the control untouched throughout emits no event. (b) **With the validity guard asserted, a qualifying Push Button press is deliberately made and held long enough to satisfy `CTRL-18` and `CTRL-19`**, then the guard is allowed to clear: the press is **lost** — no event on recovery, none on the re-baseline that follows, and none at any later point. A swallowed press that surfaces late is the failure this catches, and it is indistinguishable from part (a) unless the press is actually made | `RCV-14`, `CTRL-22` | Req | n/a |
| `VAL-FUNC-30` | Overlap advisory reports a camera range overlapping an FC mode; ARM overlap remains allowable. **The INAV run is the test of `CTRL-26`'s prohibition**: `MSP_MODE_RANGES_EXTRA` does not exist there, so an advisory that depended on it fails on INAV and only on INAV | `CTRL-25`, `CTRL-26` | Req | Req |
| `VAL-FUNC-31` | Setup Mode is entered only through one of the four authorities; no network, runtime-MSP, BLE or remote path reaches it | `SETUP-01`, `SETUP-02`, `SETUP-03` | Req | Req |
| `VAL-FUNC-32` | `DIAG` and `PAIR` cannot be entered except from an authorised Setup session — no entry authority reaches either directly | `SETUP-04`, `BOOT-08` | Req | Req |
| `VAL-FUNC-33` | Boot with the Setup button held enters `SETUP` regardless of the retained boot request's contents, and without consulting the configuration store or stored camera identity | `BOOT-04`, `BOOT-09` | Req | Req |
| `VAL-FUNC-34` | A deliberate `RUN → SETUP` restart carries the retained boot request across `esp_restart()` on the pinned ESP32-C3 build | `BOOT-05`, `BOOT-03` | Req | Req |
| `VAL-FUNC-35` | The retained request is invalidated before the requested mode executes: a later unrelated `ESP_RST_SW` — including the restart that completes a firmware update — does not replay a consumed request | `BOOT-06`, `UPD-05` | Req | Req |
| `VAL-FUNC-36` | `SETUP` runs no live `ControlPolicy`: switch activity during Setup Mode leaves no `manualStop`, baseline or camera-control state to carry into the following `RUN` | `SETUP-24`, `BOOT-12`, `CTRL-09` | Req | Req |
| `VAL-FUNC-37` | Live trigger monitor reflects the runtime path: observed AUX value, hysteresis latch, range activity, resulting intent and qualification state all match what the live control path does with the same input | `SETUP-20`, `SETUP-21` | Req | Req |
| `VAL-FUNC-38` | Monitor with the transmitter off displays `UNQUALIFIED` with its reason, and issues no camera command | `SETUP-22`, `SETUP-23` | Req | Req |
| `VAL-FUNC-39` | Pairing from an already-authorised Setup session requires no second entry authorisation, and each scan is bounded and terminates on its own | `PAIR-02`, `PAIR-03` | Req | Req |
| `VAL-FUNC-40` | A `PAIR` boot that finds nothing, times out, or fails to bind returns to `SETUP` with `NO_CAMERAS` / `SCAN_TIMEOUT` / `BIND_FAILED` / `BIND_TIMEOUT` — not to `RUN` | `BOOT-11`, `PAIR-04`, `PAIR-05` | Req | Req |
| `VAL-FUNC-41` | A `PAIR` boot that reaches its 120 s maximum lifetime terminates rather than continuing to scan or retry, and the next boot is `RUN` — a lifetime expiry is a fault, not a normal outcome | `PAIR-03`, `PAIR-04` | Req | Req |
| `VAL-FUNC-42` | `PAIR` operation bounds are enforced individually and neither runs the boot to its lifetime cap. **The directed-bind half runs unconditionally:** a 15 s bind expiry yields `BIND_TIMEOUT` and returns to `SETUP`. **The discovery half is gated on `PAIR-12` closing below the `PAIR-03` bound** — while `PAIR-12` holds its interim value the dwell elapses at the bound, the scan completes, and `SCAN_TIMEOUT` is unreachable by construction (`PAIR-11`). It is not run and not waived; it is **deferred, and `REL-02` holds the dependency** | `PAIR-03`, `PAIR-04`, `PAIR-11`, `PAIR-12` | Req | Req |
| `VAL-FUNC-43` | A scan finding more than 8 supported candidates retains 8 and reports the list as truncated, rather than dropping the excess silently or growing the cache | `PAIR-06` | Req | Req |
| `VAL-FUNC-44` | A candidate selection carrying a stale scan token is rejected, and entering `RUN` empties the candidate cache and invalidates its token | `PAIR-07` | Req | Req |
| `VAL-FUNC-45` | Exit `SETUP` → `RUN` performs full FC detection, version/capability validation and startup qualification, and synthesises no camera-control edge from switch activity that occurred during Setup Mode | `BOOT-12` | Req | Req |
| `VAL-FUNC-46` | FC MSP polling continues throughout `SETUP`; `PAIR` initialises no MSP link | `BOOT-15`, `BOOT-02` | Req | Req |
| `VAL-FUNC-47` | Owned OSD slots read `SETUP` during Setup Mode, with no `CAM LOST` raised by the deliberate disconnection | `BOOT-14`, `CAM-06` | Req | Req |
| `VAL-FUNC-48` | Trigger learning identifies the exercised AUX channel and its stable positions, proposes explicit ranges without assuming 1000/1500/2000 µs, and stores a mapping behaviourally identical to the same mapping entered by hand | `LEARN-06`, `LEARN-08`, `LEARN-12` | Req | Req |
| `VAL-FUNC-49` | Learning predicates match `LEARN-06` exactly: baseline is the median of 5 qualified samples; 100 µs over 2 consecutive samples is *moved* while 40 µs and a single-sample 100 µs excursion are not; a 5-sample window of max−min ≤ 20 µs is *stable* while a 22 µs spread and a 4-sample window are not | `LEARN-06` | Req | Req |
| `VAL-FUNC-50` | Learning's three rejections are distinct and differently reported: *ambiguous* (≥2 channels moved), *no movement detected* (none moved before the 15 s acquisition timeout) and *unstable* (moved but never held a stable position) | `LEARN-07` | Req | Req |
| `VAL-FUNC-51` | A learning acquisition left open expires at 15 s, returns to idle, writes no configuration and reverts the poll rate | `LEARN-17`, `LEARN-04` | Req | Req |
| `VAL-FUNC-52` | Learning thresholds are not `CTRL-15`'s: a mapping learned at these thresholds and the same mapping entered by hand evaluate identically at runtime under ±10 µs hysteresis | `LEARN-06`, `LEARN-12`, `CTRL-15` | Req | Req |
| `VAL-FUNC-53` | Trigger learning is available in `SETUP` with no camera present and no BLE initialised — it does not depend on the peak-2 gate | `LEARN-02`, `RES-07` | Req | Req |
| `VAL-FUNC-54` | Resource floors: each mode meets ≥ 32 KiB free and ≥ 16 KiB largest allocatable block under its specified workload, and neither figure degrades monotonically across repeated operations — `RUN` over ≥ 30 min and ≥ 10 reconnect cycles | `RES-01`, `RES-02`, `RES-03`, `RES-04`, `RES-05` | Req | Req |
| `VAL-FUNC-55` | MSP command policy is closed: an MSP capture across a full session contains only allowlist commands, and exactly two write commands appear in the whole product — the platform's OSD injection command and nothing else | `SAFE-02`, `SAFE-03`, `SAFE-04` | Req | Req |
| `VAL-FUNC-56` | `MSP_RTC` is resolved per platform — 247 on Betaflight, 246 on INAV — and no shared constant is used; an MSP capture shows no `MSP_SET_RTC` on either platform | `SAFE-05` | Req | Req |
| `VAL-FUNC-57` | `MSP_RC` is polled at 20 Hz for the duration of a learning operation regardless of configured control modes, and reverts afterwards; a 250 ms momentary demonstration is characterised, not missed | `LEARN-04`, `MSP-01` | Req | Req |
| `VAL-FUNC-58` | Camera actuation is suspended while learning is active, and the live policy re-baselines when it ends — movements made solely to teach a control produce no camera command and change no runtime control state | `LEARN-05`, `RCV-14` | Req | Req |
| `VAL-FUNC-59` | Confirming a mapping whose control is already inside the learned range establishes a baseline and emits no edge; an edge-triggered mapping requires a later qualifying transition | `LEARN-11`, `RCV-14` | Req | Req |
| `VAL-FUNC-60` | A learned proposal that cannot yield a valid representation — positions closer than `CTRL-16` permits — is rejected with the reason shown, and is never silently widened or merged | `LEARN-10`, `CTRL-16` | Req | Req |
| `VAL-FUNC-61` | Learning is subject to every restriction a manual mapping is: user confirmation does not override a rejected safety constraint, and the generic camera-action learner never produces an entry authority | `LEARN-10`, `LEARN-16` | Req | Req |
| `VAL-FUNC-62` | Maintenance Entry AUX selection, where offered, runs the overlap prohibition unchanged: a channel or range overlapping ARM, PREARM, failsafe or rescue is refused, and user confirmation does not override the refusal | `SETUP-07`, `LEARN-16` | Req | Req |
| `VAL-FUNC-63` | Learnable actions are offered from the stored paired-camera capability profile and the product-version scope, never inferred from the absence of BLE; with no camera paired the product-version set is offered and marked unconfirmed | `LEARN-14`, `LEARN-15` | Req | Req |
| `VAL-FUNC-64` | Maintenance gesture pattern: four sustained transitions beginning and ending at the baseline, the fourth within 3.0 s of the first; a single-sample excursion is neither a transition nor a reset; a control left displaced never completes the pattern | `SETUP-12` | Req | Req |
| `VAL-FUNC-65` | With a Maintenance Entry AUX configured, the pattern in the startup window restarts the bridge into `SETUP`, and a static AUX position present at startup never does | `SETUP-10`, `SETUP-11` | Req | Req |
| `VAL-FUNC-66` | Maintenance gesture is absent by default: with no Maintenance Entry AUX configured, no window opens, no OSD gesture text appears, and no arm-state lookup is performed | `SETUP-05`, `FC-12`, `MSP-03` | Req | Req |
| `VAL-FUNC-124` | **[BF] ARM is located by permanent ID, not by position.** With a Maintenance Entry AUX configured **and several other modes active** (`ANGLE`, `BEEPER`, `FAILSAFE`) so that ARM is **not** at position 0 of the `MSP_BOXIDS` response, the bridge locates permanent ID 0, uses that entry's position as the ARM status bit, and the `SETUP-09` interlock refuses the gesture while armed. Repeated with the mode set changed between boots so the position moves. **An implementation hardcoding bit 0 passes `VAL-FUNC-112` and `VAL-FUNC-123` and fails here** — those cases can be run on a craft where ARM happens to sit at 0. Also run with permanent ID 0 absent from the response: the Maintenance Entry AUX is treated as unconfigured rather than the interlock proceeding without an input | `FC-12`, `SETUP-09`, `SETUP-05` | Req | n/a |
| `VAL-FUNC-67` | Setup refuses a Maintenance Entry AUX whose range overlaps ARM, PREARM, failsafe or rescue — a rejection, not an advisory — and never derives the selection from the Record mapping | `SETUP-06`, `SETUP-07` | Req | Req |
| `VAL-FUNC-68` | Gesture window is visible: `SETUP GESTURE` while open and `SETUP n/4` as transitions register, returning to normal camera content at expiry | `SETUP-14`, `BOOT-14` | Req | Req |
| `VAL-FUNC-69` | Betaflight USB passthrough `BOOT_SETUP` reaches `SETUP` after both ends restart, with passthrough invoked using the `reset` DTR action and a power cycle documented as the fallback exit; the protocol exposes no firmware-transfer verb and no `BOOT_DIAG` / `BOOT_PAIR` | `SETUP-15`, `SETUP-18`, `SETUP-19` | Req | n/a |
| `VAL-FUNC-70` | Camera control bound to a Logic Condition | `CTRL-27` | n/a | Opt |
| `VAL-FUNC-71` | Pilot's existing USER1..4 / PINIO / mode / Programming Framework configuration unmodified after a full session — verified by Configurator `diff` before/after | `CTRL-02`, `LEARN-13`, `SAFE-02` | Req | Req |
| `VAL-FUNC-72` | Actual `REC` confirmation | `CAM-04`, `CTRL-05` | Req | Req |
| `VAL-FUNC-73` | Recording timer OSD | `OSD-02`, `OSD-05` | Req | Req |
| `VAL-FUNC-74` | Camera battery OSD | `OSD-02` | Req | Req |
| `VAL-FUNC-75` | Remaining record time OSD | `OSD-02` | Req | Req |
| `VAL-FUNC-76` | Resolution/FPS OSD | `OSD-02` | Req | Req |
| `VAL-FUNC-77` | Staleness → `CAM STALE` → `CAM LOST` | `CAM-06`, `CAM-07` | Req | Req |
| `VAL-FUNC-78` | Slot ownership respected | `OSD-04` | Req | Req |
| `VAL-FUNC-79` | Owned OSD slots initialised to non-authoritative content at bridge startup, before camera connection is attempted — a persisted `REC nn:nn` from a prior session is cleared and never re-asserted | `OSD-09` | Req | Req |
| `VAL-FUNC-80` | No FC firmware modification | `OSD-11`, `OSD-17`, `SCOPE-08` | Req | Req |
| `VAL-FUNC-81` | Analog OSD renders correctly | `OSD-10` | Req | Req |
| `VAL-FUNC-82` | Digital OSD (MSP DisplayPort) renders correctly | `OSD-10` | Req | Req |
| `VAL-FUNC-83` | Unsupported FC variant enters telemetry-disabled compatibility and issues no platform OSD write | `FC-09`, `OSD-19` | Req | Req |
| `VAL-FUNC-84` | Uppercase composition: the same string renders identically on both platforms | `OSD-01` | Req | Req |
| `VAL-FUNC-85` | A value the camera does not report is blank or omitted, never estimated and never marked valid | `CAM-03`, `CAM-05`, `OSD-03` | Req | Req |
| `VAL-FUNC-86` | Where `DIAG` is implemented: `SETUP` and `DIAG` are distinguishable in the interface — dry-run cannot be mistaken for live camera operation | `DIAG-10` | Opt | Opt |
| `VAL-FUNC-87` | Where `DIAG` is implemented: input, intent, command-sent, acknowledgement and camera-confirmed state are distinguished, and success is never reported from command issue alone | `DIAG-04`, `DIAG-05` | Opt | Opt |
| `VAL-FUNC-88` | Where `DIAG` is implemented: a mapping is learned and then validated against the real camera within one session, with no restart between confirmation and validation | `LEARN-01` | Opt | Opt |
| `VAL-FUNC-89` | Where `DIAG` is implemented: `DIAG` exits by restart, and `manualStop` exercised there does not survive into the following `RUN` | `DIAG-06`, `DIAG-07`, `CTRL-09` | Opt | Opt |

### 1.1 Coverage cases

Added by the traceability audit to close requirements the acceptance matrix exercised only implicitly.
They are requirements-in-force in v1.0; none is new.

| ID | Test | Verifies | BF | INAV |
| --- | --- | --- | --- | --- |
| `VAL-FUNC-90` | Channel count is taken from the `MSP_RC` response length: a receiver presenting 8 channels and one presenting 16 both work with no configuration change and no hard-coded count | `CTRL-13` | Req | Req |
| `VAL-FUNC-91` | Poll rate by MSP capture: 10 Hz with no Push Button control, 20 Hz with one enabled, and **no RC poll at all** where no AUX or Logic-Condition camera control is enabled | `CTRL-21`, `MSP-01`, `MSP-05` | Req | Req |
| `VAL-FUNC-92` | Both qualifying samples of a Push Button event are individually bracketed; a press whose sample pair contains a failed bracket is discarded, not committed | `CTRL-23` | Req | n/a |
| `VAL-FUNC-93` | INAV issues no validity bracket around `MSP_RC`: MSP capture shows status reads before qualification and none after | `RCV-18`, `MSP-01` | n/a | Req |
| `VAL-FUNC-94` | An OSD slot whose rendered content is unchanged is not rewritten — MSP capture across a steady-state interval shows no redundant write | `OSD-06`, `MSP-06` | Req | Req |
| `VAL-FUNC-95` | Stored camera identity survives power cycles, reconnects automatically on subsequent boots, and is replaced only by an explicit pairing action. **The automatic reconnect is a directed bind from `RUN` and enters no `PAIR` boot** — verified from the boot-mode trace, since `PAIR-01` places both scan and directed bind in one mode and an implementation could satisfy the reconnect by restarting into it | `CAM-09`, `PAIR-01` | Req | Req |
| `VAL-FUNC-96` | **No listening network service exists in `RUN` or in `PAIR`** — verified by port scan against a running device in each mode, and by review that each mode's entry point initialises only its own stack | `BOOT-10`, `BOOT-01`, `BOOT-02` | Req | Req |
| `VAL-FUNC-97` | No reboot occurs autonomously: camera loss, BLE failure and MSP failure are each injected in `RUN` and none produces a restart or an entry to Setup Mode | `BOOT-13`, `BOOT-16` | Req | Req |
| `VAL-FUNC-98` | RF policy is independent of arm state: backoff intervals, duty cycle and discovery behaviour are identical with the FC armed and disarmed. **The measured transmit-and-scan duty cycle is also recorded against `RF-01`'s stated bound in both conditions** — equality between two unbounded figures would satisfy the first half of this case and prove nothing | `RF-06`, `RF-01` | Req | Req |
| `VAL-FUNC-99` | Broad discovery is never observed in `RUN` — BLE sniffer capture across a full session including sustained camera loss shows directed connection attempts only | `RF-03`, `RF-09` | Req | Req |
| `VAL-FUNC-100` | Learning starts only on explicit user action: RC activity with no learning operation open changes no stored configuration | `LEARN-03` | Req | Req |
| `VAL-FUNC-101` | No proposal becomes active configuration without explicit user confirmation, and the interface states the minimum demonstrated hold time | `LEARN-09`, `CTRL-19` | Req | Req |
| `VAL-FUNC-102` | A missing or late MSP reply neither blocks the MSP task nor synthesizes a control transition; an incomplete bracket is treated exactly as a failed bracket | `MSP-07`, `MSP-06` | Req | Req |
| `VAL-FUNC-103` | The `UNQUALIFIED` state raises no RC-loss warning in the OSD or in the setup interface | `OSD-16`, `RCV-12` | Req | Req |
| `VAL-FUNC-104` | FC family, FC firmware version, MSP/API version and available OSD capabilities are all tracked and exposed | `FC-10` | Req | Req |
| `VAL-FUNC-105` | Firmware update completes from Setup Mode, is unreachable from `RUN`, `PAIR` and `DIAG`, and imposes no armed-state interlock | `UPD-01`, `UPD-02`, `UPD-03` | Req | Req |
| `VAL-FUNC-106` | *(V1.2)* Time and GPS push, where enabled, do not affect recording or flight operation when the source is unavailable, and GPS push stays ≤ 10 Hz | `CAM-13`, `SCOPE-05` | Opt | Opt |
| `VAL-FUNC-107` | *(V1.2)* Camera mode/preset control: three presets on one AUX channel via three configured ranges, switchable while armed | `SCOPE-06` | Opt | Opt |
| `VAL-FUNC-108` | The setup interface exposes the full configuration surface, surfaces the overlap advisory when a range is assigned, and states the Push Button freeze limitation where such a control is configured | `SETUP-25`, `CTRL-22`, `CTRL-25` | Req | Req |
| `VAL-FUNC-109` | *(Where `DIAG` is implemented)* `DIAG` is reached only by explicit action from an authorised Setup session, observes rather than reimplements the runtime path, and its timing figures are not quoted as `RUN` behaviour | `DIAG-01`, `DIAG-02`, `DIAG-03`, `DIAG-09` | Opt | Opt |

### 1.2 Cases added by the v1.1 normative delta

| ID | Test | Verifies | BF | INAV |
| --- | --- | --- | --- | --- |
| `VAL-FUNC-110` | **Exactly three control primitives exist.** Source review plus behavioural check: Arm Record instantiates the Level evaluator and no other, and no code path evaluates a fourth primitive | `CTRL-04`, `CTRL-29` | Req | Req |
| `VAL-FUNC-111` | **Gesture window start point and duration.** The window opens at the first qualified control sample — not at power-on and not at FC detection — and closes **15.0 ± 0.5 s** later. Measured across a stock `failsafe_delay` and a `failsafe_delay` of 50, where the two power-on-to-qualification intervals differ by seconds: **the window duration shall be identical in both runs** | `SETUP-10` | Req | Req |
| `VAL-FUNC-112` | **Window opened while armed.** With the FC armed at qualification (Betaflight), the window opens, runs and expires with the gesture unrecognised throughout; no restart into `SETUP` occurs | `SETUP-10`, `SETUP-09` | Req | n/a |
| `VAL-FUNC-113` | **Candidate-cache contents are a closed list.** After a scan against advertisers carrying large manufacturer payloads, the retained cache holds only the four permitted classes — no advertisement history, telemetry or GATT enumeration — and its total size does not scale with advertisement size | `PAIR-10`, `PAIR-06` | Req | Req |
| `VAL-FUNC-114` | **Cache storage bounds are enforced**, not merely stated: an entry whose opaque bind context exceeds its per-entry bound is rejected, and the cache never exceeds its total bound. *(Bounds themselves are `OPEN` — this case verifies enforcement and is re-run against the written-back values.)* | `RES-09`, `REL-02` | Req | Req |
| `VAL-FUNC-115` | **OSD state-change latency.** With a healthy MSP link, the interval from a camera-confirmed state transition arriving at the bridge to the corresponding MSP OSD write leaving it is **≤ 100 ms**, measured at the worst-case MSP load of `MSP-01` (Push Button enabled, 20 Hz, bracket active). An unchanged periodic field is not accelerated by this rule, and the recording timer stays at ≤ 1 Hz | `OSD-18`, `OSD-05` | Req | Req |
| `VAL-FUNC-116` | **No parameter is delegated outside the registry.** Review: every `OPEN` / `PROVISIONAL` / `CONDITIONAL` value in the PRS appears in `REL-02`; no release is claimed against an applicable `OPEN` parameter, an unclosed applicable `PROVISIONAL` value, or an unresolved applicable `CONDITIONAL` choice | `REL-02`, `REL-01` | Req | Req |

### 1.3 Cases added by the v1.2 normative delta

| ID | Test | Verifies | BF | INAV |
| --- | --- | --- | --- | --- |
| `VAL-FUNC-117` | **One-shot actions are not queued across camera unavailability.** With the camera disconnected, `CAM STALE`, and reconnecting, a Push Button press is issued in each state: **no command is delivered on reconnection** in any of them. In the same three states a Level Record control is moved: the intent persists and **reconciles on reconnection** | `CAM-16` | Req | Req |
| `VAL-FUNC-118` | **Latch initialisation is deterministic in the hysteresis band.** With the AUX value parked at `min + 5` — inside the band, where `CTRL-15` alone gives no answer — the control is qualified from cold, then re-baselined after an RX-loss recovery, an FC reboot and a confirmed learned mapping. **The latch resolves active in all four runs**, per the nominal stored range, and no edge is emitted | `CTRL-30`, `RCV-14`, `CTRL-15` | Req | Req |
| `VAL-FUNC-119` | **No FC OSD write occurs while `osdBackendEnabled` is false.** Captured MSP trace across a full boot in each of: below the version floor (`UNSUPPORTED_FC_VERSION`), unsupported variant, and **[INAV]** a target built without `USE_PROGRAMMING_FRAMEWORK`. **Zero OSD writes in all three, including the `OSD-09` owned-slot initialisation, `BOOT-14`'s `SETUP` content and `SETUP-14` gesture progress** — each of which is otherwise unconditional. The same information appears in the setup interface | `OSD-19`, `FC-05`, `FC-07`, `FC-09`, `OSD-09` | Req | Req |
| `VAL-FUNC-120` | **Warning policy.** With no user threshold set, `CAM BAT LOW` and `CARD LOW` **never raise**, at any battery or capacity value. With a threshold set, each raises at it. With the underlying field `valid = false`, neither raises. With `CAM LOST`, `CARD FULL` and `CAM BAT LOW` simultaneously active and one owned slot, the display shows **`CAM LOST` only** and does not cycle | `OSD-20`, `OSD-12`, `CAM-05` | Req | Req |
| `VAL-FUNC-121` | **Discovery does not stop at the first candidate.** Two supported cameras powered, one placed markedly closer. The scan **runs the full `PAIR-12` dwell**, both are enumerated, and the outcome is `MULTIPLE_CANDIDATES` requiring explicit selection — **not an automatic bind to the nearer one**. Repeated ≥ 5 times with the closer camera advertising first. With one camera present, the scan still runs the full dwell and then auto-selects and directed-binds. **Run against whatever value `PAIR-12` currently holds, interim or closed** — the case tests that the dwell is observed, not what it is | `PAIR-11`, `PAIR-12`, `PAIR-08`, `PAIR-09`, `PAIR-03` | Req | Req |
| `VAL-FUNC-123` | **Armed inhibits the rest of the window, not part of it.** The maintenance window opens with the FC disarmed; the FC is armed at ~4 s and disarmed at ~7 s; a correct gesture is then performed at ~9 s, well inside 15 s. **It is not recognised.** Power-cycling and repeating without the arm/disarm recognises it. Also run with the FC armed for the whole window — the window still opens, runs and expires | `SETUP-10`, `SETUP-09`, `SETUP-13` | Req | n/a |
| `VAL-FUNC-122` | **The allowlist is closed on conditions, not only on commands.** Captured MSP trace over a full session — boot, qualification, steady state, a learning operation, an open maintenance window, and setup — is checked against `SAFE-03`: every command emitted appears in the table **and** was emitted only within its stated `When` condition. **No command appears outside its condition**, and the two permitted writes are the only writes | `SAFE-03`, `SAFE-02`, `SAFE-04` | Req | Req |

---

### 1.4 Review-verified requirements — `VAL-REV-*`

Requirements whose conformance is established by design or documentation review rather than by a
test on hardware. Each still names its verification.

| ID | Verification | Verifies |
| --- | --- | --- |
| `VAL-REV-01` | **CI-enforced:** the control core and camera core compile under a host target with no transport headers present. A build failure is the test. **The review additionally confirms there is exactly one AUX/range evaluation implementation** — a headerless build proves the seam, not that the two platforms share the evaluator behind it (`CTRL-03`) | `ARCH-01`, `ARCH-02`, `CTRL-03` |
| `VAL-REV-02` | Source review: no boot-mode, retained-request or reset-reason symbol appears in the shared core, and no shared radio-bring-up helper exists that a mode flag could be added to | `ARCH-03` |
| `VAL-REV-03` | Source review: `ControlSample.valid` is **per sample**, not a mode flag, and the re-baseline rule is implemented once rather than once per path | `ARCH-04`, `RCV-14` |
| `VAL-REV-04` | Scope review: no acceptance claim in this document references an ELRS-integrated build | `ARCH-05`, `SCOPE-08` |
| `VAL-REV-05` | **Documentation review.** The documentation set states: the control source is the FC-effective AUX value; the Push Button freeze limitation; the no-arm-switch exception **with the correct platform attribution**; the save-with-live-camera-text note; and that Setup Mode and firmware update are not to be entered while the aircraft is being operated | `CTRL-24`, `CTRL-22`, `INST-04`, `INST-05`, `SETUP-26`, `UPD-03`, `RCV-19` |
| `VAL-REV-06` | Installation review: four solder joints and one spare UART; default 115200; no CLI required for the default supported configuration; learning is the primary route and manual entry the fallback. **Documentation states the measured peak current draw — not only the average — and the required local bulk capacitance** (`FC-02`), and the brownout/reset behaviour is stated | `INST-01`, `INST-02`, `INST-03`, `FC-01`, `FC-02` |
| `VAL-REV-07` | Product-level acceptance: the bridge controls the camera over BLE, reads actual reported state, displays it through the FC OSD, works independently of the video system, needs only power and one UART, and requires no interaction in normal use. **`PROD-03` is gated, not re-tested, and the gate names all three of its members.** The review shall confirm that the following passed on the release build: **bridge failure** — `VAL-FAIL-01`, `VAL-FAIL-02`, `VAL-FAIL-14`, `VAL-FAIL-33`; **camera failure** — `VAL-FAIL-25`, `VAL-FAIL-29`; **Bluetooth-link failure** — `VAL-FAIL-26` and `VAL-FAIL-27`, the second carrying the no-measurable-RF-degradation criterion; and **control-link margin** — `VAL-SPIKE-02` (`RF-05`). **A failure in any of them is a `PROD-03` failure**, and the product claim is not made. The BLE-link member is not reachable through `SAFE-06`, which enumerates bridge and camera faults and not link loss (v1.6, CR-41) | `PROD-01`, `PROD-02`, `PROD-03` |
| `VAL-REV-08` | Camera-policy review: the Nano is not advertised above **Unsupported** until `VAL-SPIKE-04` passes; no EULA-encumbered protocol document is redistributed; no model is promoted to Verified on protocol similarity alone; **every camera support claim names the bridge firmware version it was tested at**, and the Open GoPro supported-model list is re-read per release rather than assumed forward (`PF-EXT-01`). **`Verified` is reached only through the full camera-driver qualification suite on physical hardware at a stated firmware version, never from a protocol probe**; the suite applied is the same one for every claimed driver | `CAM-08`, `CAM-11`, `CAM-12`, `UPD-04` |
| `VAL-REV-09` | Design review: the recorded reduction lever under MSP budget pressure is the Push Button poll rate, **never** the validity bracket; the fallback stops at **10 Hz** and a target that cannot meet `CTRL-18`/`CTRL-23` there **offers no Push Button control at all** rather than a lossy one | `MSP-08`, `CTRL-21` |
| `VAL-REV-10` | Source review: no code path binds a functional FC mode as a generic camera trigger, and no `MSP_SET_*` command appears in the built image outside the two permitted OSD writes | `CTRL-28`, `SAFE-02`, `SAFE-04` |
| `VAL-REV-11` | Coverage roll-up: every entry in the failure list has a `VAL-FAIL-*` case, and each carries both pass criteria | `FAIL-01` |
| `VAL-REV-13` | Compatibility-matrix review: **the camera vendor's published supported-model list was re-read for this release** and the matrix updated from it; no model is claimed that was absent from that list when the release was cut; no model within a supported family is claimed merely for being in it (`PF-EXT-01`) | `CAM-14`, `CAM-08` |
| `VAL-REV-12` | Release review: **the applicable set is enumerated before the gate is claimed** — every applicable `VAL-FUNC-*`, `VAL-FAIL-*` **and `VAL-REV-*`** case for the claimed platforms, camera models and release scope, plus every `VAL-SPIKE-*` named as a gate or `REL-02` closer. Cases excluded as inapplicable are **listed with the unclaimed feature that makes them so**; none ships with `VAL-SPIKE-02` outstanding | `REL-01`, `RF-05` |

---

## 2. Failure acceptance — `VAL-FAIL-*`

**Every case below has two pass criteria:** **(a)** FC behaviour unchanged, and **(b)** control-link
sensitivity/link margin not measurably degraded versus the bridge-absent baseline, where RF activity is
relevant (`SAFE-06`, `RF-05`).

| ID | Injected failure | Expected bridge behaviour | Verifies |
| --- | --- | --- | --- |
| `VAL-FAIL-01` | Bridge held in reset for the whole flight | FC unaffected | `SAFE-06`, `FAIL-01` |
| `VAL-FAIL-02` | Bridge TX line held high, then held low | FC unaffected; document any UART electrical limitation found | `SAFE-06` |
| `VAL-FAIL-03` | Bridge power cycled while Record AUX remains active | bridge re-detects FC/camera; no false edge event; level control reconciles to active Record state | `RCV-14`, `CTRL-11`, `BOOT-12` |
| `VAL-FAIL-04` | Bridge powered up simultaneously with the FC, transmitter ON | input stays `UNQUALIFIED` until condition A's assert→clear edge is observed; no RC-loss warning; first qualified sample reconciles level controls and emits no edge | `RCV-08`, `RCV-12`, `RCV-14` |
| `VAL-FAIL-05` | Bridge powered up simultaneously with the FC, transmitter OFF, Record range spanning `midrc` | no recording starts during the ~1.5 s pre-assertion window; the guard then asserts and qualification **never completes**; a passing bracket over init values is not sufficient | `RCV-08`, `RCV-13` |
| `VAL-FAIL-06` | Bridge attached to an FC that has been running for minutes | no assert→clear edge exists to observe; qualification completes via condition B after the grace, not before it | `RCV-08` |
| `VAL-FAIL-07` | Bridge start delayed past `failsafe_delay` with the transmitter ON | edge missed; qualification via condition B; total delay ≈ `failsafe_delay + grace` and no camera-control transition before it | `RCV-08` |
| `VAL-FAIL-08` | `MSP_FAILSAFE_CONFIG` request denied or timed out | condition B is disabled; the bridge qualifies only on an observed assert→clear edge, and never on elapsed time | `RCV-11`, `RCV-13` |
| `VAL-FAIL-09` | `failsafe_delay` reconfigured to 5 (0.5 s), to 50 (5.0 s) **and to 0** | condition B's grace tracks the configured value in both non-zero runs and no grace value is encoded in the bridge; **at `failsafe_delay = 0` the bridge applies a 100 ms grace, not zero** — the floor the FC itself applies (`PF-BF-15`). Until v1.4 no case exercised 0, so the floor was specified and untested | `RCV-08`, `RCV-09` |
| `VAL-FAIL-10` | INAV: bridge and FC powered together, transmitter OFF, Record range spanning `midrc` | `ARMING_DISABLED_RC_LINK` reads asserted from the first status reply; no recording starts; qualification does not complete | `RCV-17` |
| `VAL-FAIL-11` | INAV: bridge power-cycled while the aircraft is armed | input stays `UNQUALIFIED` for the remainder of the flight; camera continues doing whatever it was doing; no transition is committed from a flag the FC has stopped maintaining | `RCV-19` |
| `VAL-FAIL-12` | Betaflight: RX loss induced so that the FC substitutes between the bridge's `STATUS₁` and `MSP_RC` | the trailing `STATUS₂` fails the bracket; the substituted sample is discarded, not committed | `RCV-03` |
| `VAL-FAIL-13` | Betaflight: every `MSP_STATUS` reply suppressed during a condition-B interval | the interval never accumulates a successful observation; the bridge never qualifies, however long it is left | `RCV-10` |
| `VAL-FAIL-14` | Bridge watchdog forced; also run with the bridge held in reset and with its firmware erased, electrically connected throughout | FC unaffected — it continues operating normally with the bridge completely non-functional, on every one of the three | `FAIL-02`, `SAFE-06` |
| `VAL-FAIL-15` | FC rebooted while bridge is powered and recording | bridge **detects the restart**, re-runs identification, version floor, capability probe, owned-slot init and qualification, and reinstates OSD/control polling without an ARM/box remap and without user action; level controls reconcile and **no edge is emitted** | `FC-14`, `RCV-14`, `RCV-15`, `FC-03` |
| `VAL-FAIL-16` | Betaflight: transmitter powered off with camera AUX configured `RX_FAILSAFE_MODE_HOLD` | recording continues | `RCV-01`, `RCV-02` |
| `VAL-FAIL-17` | Betaflight: transmitter powered off with camera AUX configured `RX_FAILSAFE_MODE_SET` outside active range | recording still continues; the guard blocks the substituted transition | `RCV-02`, `RCV-03` |
| `VAL-FAIL-18` | Betaflight: transmitter powered back on after `VAL-FAIL-17` | guard clears after `failsafe_recovery_delay` of valid data (stock default 500 ms) — **not immediately**; level control reconciles; no Push Button event emitted | `RCV-05`, `RCV-06`, `RCV-07` |
| `VAL-FAIL-19` | Betaflight: `BOXFAILSAFE` actioned while recording | no camera-control transition | `RCV-02` |
| `VAL-FAIL-20` | Betaflight: `BOXFAILSAFE` reverted | guard clears; where the FC took the switch-induced path the recovery preset is zero and the clear is immediate. **Record the observed latency**; the bridge shall behave correctly for **any** latency and shall not special-case this row | `RCV-05`, `RCV-06` |
| `VAL-FAIL-21` | Betaflight: `failsafe_recovery_delay` reconfigured to an extreme (0.1 s and 2.0 s) | camera behaviour identical in both, only the reconcile latency differs — proves no delay value is encoded in the bridge | `RCV-05` |
| `VAL-FAIL-22` | INAV: transmitter powered off while recording | `MSP_RC` remains at last-good camera AUX value; recording continues | `RCV-16` |
| `VAL-FAIL-23` | Camera absent at boot | directed reconnect backs off; no autonomous broad discovery | `RF-02`, `RF-03`, `RF-07` |
| `VAL-FAIL-24` | Camera powered off mid-record | OSD transitions through stale/lost policy; reconnect backs off | `CAM-06`, `RF-07` |
| `VAL-FAIL-25` | Camera rebooted | reconnect/recovery without flight effect | `SAFE-06`, `RF-02` |
| `VAL-FAIL-26` | Camera carried out of BLE range and back | reconnect/recovery without flight effect | `RF-02`, `RF-07` |
| `VAL-FAIL-27` | Sustained runtime reconnect failures | bounded directed attempts; no discovery escalation; no measurable RF degradation | `RF-03`, `RF-07`, `RF-08` |
| `VAL-FAIL-28` | Unsupported camera advertised nearby | ignored outside explicit Pair/Setup discovery | `RF-03` |
| `VAL-FAIL-29` | Malformed, truncated and integrity-check-failing camera frames injected | each discarded **atomically** — no camera-state field updated, no partial telemetry applied, `lastUpdated` not refreshed; no BLE/MSP task instability and no bridge restart. A camera emitting only malformed frames ages to `CAM STALE` then `CAM LOST` on the `CAM-06` schedule | `CAM-15`, `CAM-06`, `SAFE-06`, `CAM-03` |
| `VAL-FAIL-30` | Corrupted stored configuration | integrity check fails; configuration treated as **absent**, not partial; safe defaults with **no control mapping active and no Maintenance Entry AUX configured**; fallback surfaced to the user; no FC write outside `SAFE-03`; Setup Mode reachable by held button | `RES-10`, `SAFE-02`, `BOOT-04` |
| `VAL-FAIL-31` | Camera stops recording on its own (card filled) while the guard is frozen, **BLE still connected** | `REC STOPPED` **is raised**, compared against the last accepted intent | `OSD-13`, `OSD-14` |
| `VAL-FAIL-32` | Camera powered off while the guard is frozen and last accepted intent is `RECORD` | `CAM LOST` — **not** `REC STOPPED`. A disconnect is not a camera-confirmed stop | `OSD-15`, `CAM-04`, `CAM-06` |
| `VAL-FAIL-33` | Wi-Fi stack forced to fail at Setup init | setup fails safely; normal FC operation unchanged | `SAFE-06` |
| `VAL-FAIL-34` | Two supported cameras advertising during a `PAIR` scan, no selection made | neither is bound; the stored paired identity is unchanged; no strongest-advertiser default | `PAIR-08`, `PAIR-09` |
| `VAL-FAIL-35` | Watchdog forced during a `PAIR` boot | next boot is `RUN`; the retained request is not honoured; `PAIR` does not resume itself | `BOOT-07` |
| `VAL-FAIL-36` | Brownout or external reset during a `PAIR` boot | next boot is `RUN` | `BOOT-07` |
| `VAL-FAIL-37` | Power removed during a `PAIR` boot | next power-on is `RUN`; no maintenance mode is entered unattended | `BOOT-07` |
| `VAL-FAIL-38` | BLE stack hung during a `PAIR` boot so that no operation bound ever fires | the 120 s lifetime cap terminates the boot independently of every operation inside it; next boot is `RUN`; the device does not sit in a maintenance boot | `PAIR-03`, `PAIR-04` |
| `VAL-FAIL-39` | Retained boot-request memory corrupted or filled with arbitrary content, then a deliberate `esp_restart()` | the request fails its validity check and the bridge enters `RUN`; no mode is entered from unvalidated retained state | `BOOT-03`, `BOOT-05` |
| `VAL-FAIL-40` | Setup button held during a boot whose retained memory holds a valid `PAIR` request | `SETUP` is entered; the held-button path takes priority and depends on nothing persistent | `BOOT-04` |
| `VAL-FAIL-41` | Configuration store corrupted, then Setup entered by held button | Setup Mode is reachable and firmware update remains available; the escape hatch does not depend on the damaged store | `BOOT-04`, `UPD-02` |
| `VAL-FAIL-42` | Candidate selection submitted from a setup page held open across a later scan | the stale scan token is rejected; no camera is bound from a superseded candidate list | `PAIR-07` |
| `VAL-FAIL-43` | Bridge 5 V supply interrupted and restored while the FC stays armed, then the gesture pattern is performed | the arming interlock refuses recognition; no restart into `SETUP` and no Wi-Fi AP is raised while armed | `SETUP-09` |
| `VAL-FAIL-44` | Gesture pattern performed after a watchdog, panic or software restart rather than a power-on | no window ever opens; eligibility is decided from `ESP_RST_POWERON` alone | `SETUP-08` |
| `VAL-FAIL-45` | Gesture partially performed, then RC validity lost / an out-of-pattern transition / the FC arms / the window expires | recognition resets in every case; a later resumption of the pattern does not complete it | `SETUP-13` |
| `VAL-FAIL-46` | **Ordinary FC MSP response traffic carrying payload bytes that resemble a maintenance frame** — including a crafted `MSP2_SET_TEXT`-shaped response and a byte-for-byte maintenance frame embedded in an MSP payload | never parsed as a maintenance command; the bridge does not enter `SETUP`; its MSP client state is unaffected. Framing is rejected on **each** of the five `SETUP-28` fields independently — bad magic, wrong version, wrong length, unknown verb, bad integrity check | `SETUP-28`, `SETUP-16`, `SETUP-17` |
| `VAL-FAIL-47` | `DIAG` requested on a build whose peak-2 resource gate did not pass | `DIAG` is unavailable; `RUN`, `SETUP`, `PAIR`, the monitor, pairing and trigger learning are unaffected, and every mapping remains configurable | `DIAG-08`, `RES-07`, `SCOPE-07` |
| `VAL-FAIL-48` | Passthrough invoked without the `reset` DTR action, then `BOOT_SETUP` sent | the bridge restarts into `SETUP` normally; the FC remains in its byte-copy loop and serves no MSP until power-cycled. **This is a host-invocation defect, not a bridge fault, and the bridge shall not attempt to recover the FC** — not by reset signalling, escape sequence or FC reboot | `SETUP-27`, `SETUP-18` |
| `VAL-FAIL-49` | Maintenance Entry AUX configured with no AUX camera control enabled | the window still polls `MSP_RC` and the gesture is recognisable; the ordinary control-poll row's "only when an AUX control is enabled" condition does not apply to the window | `MSP-04` |
| `VAL-FAIL-50` | Maintenance gesture partially performed, then the control left displaced | the pattern never completes — it must end at the baseline — recognition resets on pattern-timer expiry, and the window closes normally into `RUN` | `SETUP-12`, `SETUP-13` |
| `VAL-FAIL-51` | Two or more AUX channels moved ≥ 100 µs during a learning operation | rejected as *ambiguous* and a repeat requested; no channel is guessed — not by largest movement, not by lowest index, not by first to cross — and no configuration is written | `LEARN-07` |
| `VAL-FAIL-52` | A learning operation in which no channel moves ≥ 100 µs before the 15 s acquisition timeout | rejected with *no movement detected*, distinguished from the ambiguous case; a channel drifting or jittering below the threshold never becomes a candidate; the operation returns to idle and reverts the poll rate | `LEARN-06`, `LEARN-07`, `LEARN-17` |
| `VAL-FAIL-53` | A channel moved well past 100 µs but never held within a 20 µs spread for 5 samples | rejected as *unstable* — a third outcome distinct from ambiguous and from no-movement; no range is proposed from an unsettled value | `LEARN-06`, `LEARN-07` |
| `VAL-FAIL-54` | Two channels each moved ≥ 100 µs, 10 s apart, inside one acquisition window | rejected as ambiguous exactly as if they had moved together; the acquisition timeout is what makes "within one operation" a bounded claim | `LEARN-06`, `LEARN-07` |
| `VAL-FAIL-55` | Learning operation abandoned — **run three ways: client disconnected, page closed, and acquisition timeout** | identical outcome in all three: no persistent configuration change; polling reverts to `MSP-01`; actuation resumes with a re-baseline and no edge. **The bridge ends the operation itself** — it does not wait for a cancel that a closed browser cannot send | `LEARN-18`, `LEARN-17`, `LEARN-05`, `RCV-14` |
| `VAL-FAIL-56` | RC validity lost partway through a learning operation | unqualified samples are not characterised; the operation reaches acquisition timeout, returns to idle and **changes no configuration** — it does not complete from stale or substituted values | `LEARN-06`, `LEARN-17` |
| `VAL-FAIL-57` | Learning attempted with the transmitter off | the input never qualifies, no candidate is proposed, and the monitor states `UNQUALIFIED` with its reason | `LEARN-06`, `SETUP-23`, `RCV-08`, `RCV-17` |
| `VAL-FAIL-58` | Bridge Wi-Fi failure in `RUN` | no effect — Wi-Fi is never initialised in `RUN` | `BOOT-02` |
| `VAL-FAIL-59` | **Camera acknowledgement lost after a non-idempotent one-shot command.** A record-toggle is issued, the camera acts on it, and the acknowledgement is dropped in transit | the bridge **does not retry**. Camera-confirmed state (`CAM-04`) resolves the outcome on the next status update; recording is **not** toggled a second time. Repeated with an idempotent action, which **may** be retried | `CAM-16`, `CAM-04` |

### 2.1 Notes that change how these are run

- **The two transmitter-OFF cases (`VAL-FAIL-05`, `VAL-FAIL-10`) are the ones to run first.** A bridge
  that starts recording on a bench with no transmitter has failed the only test in this document that a
  user will discover by accident.
- **The `RX_FAILSAFE_MODE_SET` / `BOXFAILSAFE` cases shall not be waived** — they are the direct tests of
  the failsafe-substitution hazard.
- **The recovery cases exist because the guard's clear latency differs** between switch-induced and
  transmitter-loss failsafe, and differs again with configuration. A tester who sees only one of them will
  report the others as defects.
- **`VAL-FAIL-08`, `VAL-FAIL-09` and `VAL-FAIL-21` exist to prove a configurable FC value is being read
  rather than assumed.** A bridge that passes at stock settings and fails at 5.0 s has hardcoded
  something.

---

## 3. Engineering spikes — `VAL-SPIKE-*`

Seven spikes, each with pass criteria. Whether stock INAV 8 can accept runtime camera data for its Custom
OSD Elements is already answered in source (`PF-INAV-13`); `VAL-SPIKE-01` confirms it on hardware.

### `VAL-SPIKE-01` — FC interface, control input and OSD injection on hardware
**Verifies:** `FC-05`, `FC-07`, `FC-08`, `FC-13`, `CTRL-02`, `CTRL-04`, `CTRL-06`, `CTRL-08`, `CTRL-12`, `CTRL-15`, `CTRL-16`, `CTRL-18`, `CTRL-19`, `CTRL-20`, `CTRL-29`, `RCV-02`, `RCV-03`, `RCV-05`, `RCV-06`, `RCV-07`, `RCV-08`, `RCV-15`, `RCV-16`, `RCV-17`, `RCV-19`, `CAM-04`, `OSD-02`, `OSD-07`, `OSD-08`, `OSD-09`, `OSD-10`, `OSD-13`, `OSD-14`, `OSD-15`, `OSD-18`, `SAFE-07`, `SCOPE-01`, `SCOPE-02`

1. `MSP_RC` returns live AUX values while armed on both platforms, matching the FC Receiver tab for
   configured AUX channels. *(`CTRL-12`)*
2. **Arm Record follows the configured arm-switch AUX/range even when actual FC arming is deliberately
   prevented** (for example by an unmet prearm condition). The camera-control result shall be identical
   whether the FC accepts or rejects arming. **Repeat with the identical channel/range entered by hand as
   a plain Level mapping and confirm the two are indistinguishable** — the evidence that Arm Record is a
   preset and not a fourth evaluator. *(`CTRL-29`, `CTRL-04`, `FC-13`)*
3. Configured AUX thresholds and level changes produce the expected camera-control events without
   modifying FC mode, USER or PINIO configuration. *(`CTRL-02`)*
4. A 250 ms Push Button press is detected reliably; a 100 ms press is either detected or ignored but never
   double-fired. **Two qualifying presses closer together than `CTRL-20`'s minimum produce one event and two
   further apart produce two, and the suppressed press never appears later** — the interval is measured on
   hardware here, not assumed from the absence of double-firing. *(`CTRL-18`, `CTRL-19`, `CTRL-20`)*
5. **Betaflight guard — assert.** `ARMING_DISABLED_RX_FAILSAFE` asserts during real RX loss while armed and
   during `BOXFAILSAFE`-induced invalid data; `RX_FAILSAFE_MODE_SET` on the camera AUX produces no
   camera-control transition. *(The flag's validity while armed is established in source, `PF-BF-09`, so
   this is a confirmation run, not an open question.)* *(`RCV-02`)*
6. **Betaflight guard — clear.** Measure and record the clear latency after transmitter-loss recovery and
   after `BOXFAILSAFE` revert, then repeat with `failsafe_recovery_delay` set to 1 (100 ms) and to 20
   (2.0 s). **Camera behaviour shall be identical across all four runs and only the reconcile latency shall
   change** — the evidence that no delay value is encoded. Confirm level controls reconcile and no Push
   Button event is emitted by any re-baseline. *(`RCV-05`, `RCV-06`, `RCV-07`)*
7. **Betaflight guard — startup qualification. Seven runs.** In every one, confirm no camera-control
   transition before qualification completes, no RC-loss warning for the unqualified state, and that the
   first qualified sample reconciles level controls without emitting an edge. **Instrument the bridge to
   log which condition qualified it and when.** *(`RCV-08`…`RCV-15`)*
   - **(a)** Bridge and FC powered together, transmitter **on** — expect condition **A**.
   - **(b)** The same with the transmitter **off** and a Record range deliberately spanning `midrc`, held
     for at least 30 s. **No recording shall start at any point**, in particular not during the ~1.5 s
     before the guard asserts, and **qualification shall never complete.**
   - **(c)** Bridge attached to an FC running for several minutes — expect condition **B**. Timestamp the
     bridge's first MSP exchange and the qualification instant, and assert the measured interval is **≥ the
     computed `grace`**. Repeat at 10 Hz and 20 Hz qualification polling: the observed grace shall differ by
     the margin term. **A qualification that lands early is a failure even though nothing visibly
     misbehaves** — it means the interval is not being computed.
   - **(d)** Bridge start deliberately delayed past `failsafe_delay` with the transmitter on — expect the
     edge to be missed and condition **B** to qualify, total delay ≈ `failsafe_delay + grace`.
   - **(e)** `failsafe_delay` reconfigured to 5 and to 50, repeating (c). **The observed grace shall track
     the configured value; if it does not, a constant has been compiled in.**
   - **(f)** `MSP_FAILSAFE_CONFIG` denied at the FC or blocked at the bridge, repeating (a) and (c). (a)
     shall still qualify via condition A; **(c) shall not qualify at all.**
   - **(g)** Repeat (c) while dropping a defined fraction of `MSP_STATUS` replies. Qualification shall be
     delayed by one poll period per unanswered transaction, and **a run in which every status poll times
     out shall never qualify however long it is left.**
8. **INAV startup qualification. Three runs.** *(`RCV-17`, `RCV-19`)*
   - **(a)** bridge and FC powered together, transmitter **off**, Record range spanning `midrc` — confirm
     `ARMING_DISABLED_RC_LINK` (bit 18) reads asserted in the first `MSP2_INAV_STATUS` reply, that no
     recording starts, and that qualification does not complete;
   - **(b)** transmitter switched on — confirm the flag clears, the input qualifies, level controls
     reconcile and no edge is emitted;
   - **(c)** with the aircraft **armed**, confirm by MSP capture that the bridge issues no further
     `armingFlags` read once qualified. Separately, power-cycle the bridge while armed and confirm it
     remains `UNQUALIFIED` until disarm, committing no camera-control transition in between.
9. **Betaflight guarded commit.** Induce RX loss repeatedly while polling, with `RX_FAILSAFE_MODE_SET`
   configured outside the active range, and confirm no committed camera-control transition arises from a
   bracket that straddles the FC's substitution. **Instrument the bridge to count discarded candidates; a
   run that discards zero over many induced losses indicates the bracket is not actually being applied,
   and shall be treated as a failed test rather than a clean one.** *(`RCV-03`)*
10. **`Combi` arbitration.** Walk the `CTRL-06` truth table on hardware, including that a button-stop
    survives repeated level-control evaluations, that leaving the level range clears the override, and that
    **the override survives an RX-loss freeze unchanged**. *(`CTRL-06`…`CTRL-08`)*
11. **Range evaluator.** Confirm ±10 µs hysteresis at a boundary using a slowly swept channel — the control
    shall not chatter — and that setup rejects a 30 µs range and a pair of ranges 15 µs apart. *(`CTRL-15`,
    `CTRL-16`)*
12. **INAV:** RX loss leaves the camera AUX returned by `MSP_RC` at its last-good value, producing no
    camera-control transition without an additional validity flag and without a bracket. Confirm **there is
    no INAV recovery window** — recovery is immediate and continuous. *(`RCV-16`)*
13. **`REC STOPPED` during a freeze.** With the guard frozen and last accepted intent `RECORD`, stop the
    camera **while BLE remains connected** — fill the card, press the camera's own stop button, or inject a
    fault reporting `recording = false` — and confirm the warning **is** raised against the frozen intent.
    *(`OSD-13`, `OSD-14`)*
14. **Camera power-off during a freeze — a separate test, not a variant of item 13.** Under the same
    conditions, power the camera down. Expected result is `CAM LOST` and **no** `REC STOPPED`. **A build
    that reports `REC STOPPED` here has synthesized a camera-confirmed event from a disconnect and fails
    the truth model, however sensible the OSD looks.** *(`OSD-15`, `CAM-04`)*
15. **Persisted OSD content.** With owned slots holding live camera text, save FC configuration from the
    Configurator and power-cycle with the bridge disconnected: confirm the stale text is present, which is
    the exposure. Reconnect the bridge and confirm every owned slot is overwritten with non-authoritative
    content **before** any camera connection is attempted. **Run on both platforms** — Betaflight's
    `pilotConfig` is a parameter group like INAV's and is not the safer case. *(`OSD-09`, `PF-BF-17`)*
16. Custom message / custom element writes are accepted and rendered **while armed** on both platforms.
    This validates in-flight availability of the OSD path; it does not create an armed-state dependency.
    *(`OSD-07`, `OSD-08`)*
17. Correct rendering on analog and on MSP DisplayPort. *(`OSD-10`)*
18. Four fields independently updateable. *(`OSD-02`)*
19. **OSD state-change latency** is confirmed on hardware at worst-case MSP load: camera-confirmed
    transition in, MSP write out, **≤ 100 ms**. Where contention makes 100 ms unachievable on the pinned
    build, that is a **specification change requiring a single replacement value**, not a licence to leave
    the bound unstated. *(`OSD-18`)*
20. **No flash write** observed across a long operating session — verify by configuration persistence check
    across power cycle, **not by inference**. *(`SAFE-07`)*
21. **Degrade paths, both platforms and both kinds.** Confirm the two degraded states are reached
    independently and behave differently. *(`FC-05`, `FC-07`, `FC-08`)*
    - **a.** *Capability degrade.* An INAV target built without `USE_PROGRAMMING_FRAMEWORK` enters
      telemetry-disabled compatibility mode: no OSD writes, camera control still works.
    - **b.** *Version degrade, Betaflight.* **2025.12.4** — a build that carries the custom-message feature
      and would otherwise half-work — enters `UNSUPPORTED_FC_VERSION`: no OSD writes **and** no camera
      control, with the reason shown to the user.
    - **c.** *Version degrade, INAV.* **8.0.0** — a build whose capability probe succeeds — enters
      `UNSUPPORTED_FC_VERSION` on the same terms. **This is the case that fails if the implementation probes
      before it gates.**
    - **d.** *Ordering.* In both (b) and (c), confirm by MSP capture that no camera-control input was acted
      on at any point, **including during the interval between identification and the gate decision**.
22. **Baseline conformance.** Run the full set against stock Betaflight **2025.12.5** and INAV **8.0.1**
    specifically. Forward-compatibility runs against later releases are recorded as version notes and
    **never substituted for this one**. *(`SCOPE-01`, `SCOPE-02`)*

### `VAL-SPIKE-02` — RF coexistence: the `RF-05` release gate

**Verifies:** `RF-05`, `RF-03`, `RF-07`, `RF-08`

Measure the bridge's effect on a 2.4 GHz ELRS control link.

**Two instruments, two jobs.** The existing passive RF survey and Blackbox analysis tooling slices noise
floor by condition and is the right tool for *detecting* interference and for in-flight realism. **It does
not measure receiver sensitivity in dBm and cannot on its own support a sensitivity claim.** The release
gate therefore requires a calibrated bench measurement **in addition to** the survey, not instead of it.

**Required method — calibrated link budget.**

```
ELRS TX  →  fixed-attenuator pad  →  step attenuator  →  ELRS RX + bridge
                                     (calibrated, known step size)
```

- Conducted where the hardware allows it; where an antenna port is not available, a fixed
  shielded-enclosure geometry with a documented and re-verified path loss.
- Sweep the step attenuator down through the link cliff, recording PER and LQ at each step.
- **The measurement is the attenuation at which a fixed PER threshold is crossed** — stated as a PER figure
  (for example 1 % or 5 % packet loss at the configured packet rate), **not** as an LQ or RSSI reading.
  **LQ and RSSI saturate and report "no change" right up to the cliff**, which is precisely the failure this
  gate exists to catch.
- Repeat the sweep **≥ 5 times per condition** and report mean and spread.

Conditions, all else held constant:

```
bridge absent → bridge idle (paired, connected)
              → bridge active (recording, telemetry flowing)
              → bridge reconnecting (sustained directed reconnect failures, camera off)
```

swept against configured BLE transmit power and against ELRS packet rate if more than one is supported.

**Defining "no measurable degradation".** The phrase is meaningless without the rig's own resolution, so it
is bound to it:

```
degradation = Δ (attenuation at fixed PER) between condition and bridge-absent baseline

PASS  if  |Δ| ≤ max(attenuator step size, 2σ of the baseline repeats)
```

Both quantities shall be reported with the result. **A rig whose uncertainty is 3 dB cannot demonstrate a
1 dB claim and shall not be used to make one** — if the measured uncertainty exceeds the degradation the
gate needs to exclude, the correct outcome is to improve the rig, not to widen the criterion.

**Pass criteria.**

1. **No measurable loss of receiver sensitivity or link margin** as defined above, in the idle and active
   conditions. *(`RF-05`)*
2. Sustained directed reconnect failures remain bounded by backoff/duty-cycle limits, never escalate into
   broad discovery, and meet the same criterion. *(`RF-03`, `RF-07`, `RF-08`)*
3. The in-flight passive survey shows no new noise-floor feature attributable to the bridge across the same
   conditions — the realism cross-check on the bench result. **A disagreement between the two is itself a
   finding.**
4. **The resulting numbers are written back into `RF-04` and `RF-08`** as concrete configured values,
   together with the attenuator step size and baseline spread that qualify them.

**Failing this gate does not merely delay a feature — it invalidates `SAFE-01`.**

### `VAL-SPIKE-03` — Camera protocol re-host: GoPro, V1

**Verifies:** `SCOPE-03`, `CAM-02`, `CAM-03`

Port Open GoPro onto NimBLE-Arduino on ESP32-C3, behind the `CAM-01` driver interface.

**Pass criteria.**

1. Start/stop recording and full `OSD-02` telemetry from a physical camera. *(`SCOPE-03`)*
2. Driver capability flags correctly gate unsupported features. *(`CAM-02`, `CAM-03`)*
3. Combined firmware **fits flash** — one figure for the image, established once (`RES-08`) — **and meets
   `RES-03`'s two heap floors (≥ 32 KiB minimum free, ≥ 16 KiB largest allocatable block), including
   `RES-04`'s no-monotonic-degradation condition and `RES-05`'s per-mode soak durations, measured separately
   in each mode it must run in**: `RUN` (BLE central, no AP), `SETUP` peak 1 (AP up, client attached, BLE
   down) and `PAIR` (BLE scan, no AP). **These are three measurements, not one**: peak 1 does not bound `RUN`
   or `PAIR` (`RES-02`). `DIAG` peak 2 is measured only when qualifying V1.2 and shall not gate this
   criterion (`RES-06`).

   **This spike is where `RES-03`'s provisional floors are confirmed or replaced.** The measured per-mode
   figures shall be written back into `RES-03` whatever they show, **including where they show the
   provisional floors were too generous to be met.**
4. **The GoPro bind representation is fixed here**, so its contribution to `RES-09`'s per-entry opaque
   context bound is measured and written back into `RES-09` and `REL-02`. **This closes `RES-09` for V1**,
   which claims the GoPro driver alone (`SCOPE-03`); it does not wait on `VAL-SPIKE-04`, which belongs to the
   release that adds the second driver.

### `VAL-SPIKE-04` — Osmo Nano protocol probe: gates V1.1 only

**Verifies:** `CAM-08`, `CAM-10`, `CAM-11`, `SCOPE-04`

Independent of V1 and of the other spikes; may run immediately, and hardware is on hand. The purpose is to
answer one question cheaply — *does the Osmo Nano answer DUML at all?* — before any driver is written.

1. **Advertisement.** Scan for the Nano and dump its manufacturer-specific data. DJI's reference identifies
   one of its cameras when manufacturer bytes 0, 1 and 4 are `0xAA`, `0x08`, `0xFA` (see
   `bsp_link_is_dji_camera_adv` in the reference's `ble.c`). Record whether the Nano matches.
2. **GATT enumeration.** Connect and enumerate services. Confirm service `0xFFF0` with `0xFFF4` (notify) and
   `0xFFF5` (write-without-response).
3. **DUML liveness.** Subscribe to `fff4`; send a known-safe DUML query from the Pocket 3 corpus over
   `fff5` — **never `fff3`** (`CAM-10`, `PF-EXT-02`) — and check for a CRC-valid response.
4. **Control.** Attempt start/stop recording and a camera-status subscription.
5. **Official cross-check.** Send the R SDK connection-request frame and record the reply. A rejection
   carrying a `device_id` would both reveal the Nano's ID and indicate official support is nearer than the
   documentation suggests.

| Outcome | Consequence |
| --- | --- |
| Steps 1–3 yield a CRC-valid response | DUML driver viable; Nano proceeds to V1.1 as **Expected** (`CAM-08`) |
| Step 4 works on physical hardware | **still `Expected`.** The probe has proven the protocol, not the driver — promotion to `Verified` requires the full camera-driver qualification suite of `CAM-08` on physical hardware at a stated firmware version, which this spike does not run |
| Fails at step 2 or 3 | Nano is **Unsupported**; escalate to a DJI Mimo app BLE capture, or substitute Osmo Action 6 / Osmo 360 (`SCOPE-04`) |

**The DJI bind representation, where the probe succeeds, is measured and written back into `RES-09` and
`REL-02` as part of V1.1** — the release that adds the driver is the release that widens or revalidates the
bound. Where the probe fails, `RES-09` keeps its V1 value and no DJI allowance is reserved.

**Record the result — including a negative result — against the Nano's firmware version.** `CAM-08` forbids
promoting the model on protocol similarity alone, and **the whole premise of the DUML path is a similarity
argument.**

### `VAL-SPIKE-05` — Retained boot request and reset-reason gate: gates `BOOT-*`

**Verifies:** `BOOT-03`, `BOOT-05`, `BOOT-07`

The cheapest spike, and **the one to run first**: `BOOT-03`'s entire failure story rests on two toolchain
behaviours, and neither should be assumed from documentation written against a different ESP-IDF release
than the one this product pins.

**The verification target is deliberately narrow.** Because the reset reason is checked *before* the
retained request is inspected (`BOOT-07`), it does not matter whether retained memory survives a watchdog, a
panic or a brownout — its contents are declined on those paths regardless. Only two things are load-bearing:

1. a value placed in the selected retained region survives a deliberate `esp_restart()`;
2. `esp_reset_reason()` reliably distinguishes that deliberate restart from power-on, external reset,
   brownout, watchdog and panic.

Both shall be confirmed on **production ESP32-C3 hardware against the pinned toolchain**, not against a
documentation claim for another release. Note the ESP32-C3 has **no RTC slow memory**: `RTC_DATA_ATTR`,
`RTC_SLOW_ATTR` and `RTC_FAST_ATTR` all resolve to RTC fast memory on this part, so **the region actually
used shall be named in the design and the test shall exercise that region.**

| Check | Expected |
| --- | --- |
| Write request, `esp_restart()` | request readable, reason `ESP_RST_SW` |
| Write request, force task watchdog | reason is a watchdog reason; boot selects `RUN` |
| Write request, force panic | reason `ESP_RST_PANIC`; boot selects `RUN` |
| Write request, remove power | reason `ESP_RST_POWERON`; boot selects `RUN` |
| Write request, assert external reset | reason `ESP_RST_EXT`; boot selects `RUN` |
| Consume a request, then `esp_restart()` again | request is invalid; boot selects `RUN` |

**The last row is the replay test, and it is the one that fails silently if consume-before-enter is
implemented as consume-*after*-enter.**

Where check 1 does not hold on the pinned build, `BOOT-05`'s fallback applies: an NVS-backed request that is
written, cleared and **verified cleared** before the requested mode is entered, with a durable-clear failure
falling back to `RUN`. That fallback costs a flash write per transition and a failure path to test, which is
why it is the fallback and not the design.

### `VAL-SPIKE-06` — Primary-stick maintenance gesture: gates nothing in V1

**Verifies:** `SCOPE-08`, `SETUP-01`

`DIAG` and `SETUP` are reachable without this, so the spike exists only to decide whether a stick-based
no-touch entry could ever be added safely. **It has an explicit kill branch, and the expected outcome is
that it is not worth it.**

Three questions, in order, and any one of them can end it:

1. **Do native stick commands leave a safe hold position?** Both platforms recognise stick commands from
   *sustained* positions, so only the gesture's held stage must be free of them; transitions may pass through
   a command position provided they are shorter than the platform's recognition hold. Enumerate the command
   tables at the pinned tags and determine whether a free hold position exists on both.
2. **Is an arming path reachable from that position?** This is the safety question, and the configuration
   space it must cover is cited in `PF-BF-20` and `PF-INAV-12`. **Two concrete hazards, not hypotheses:** on
   Betaflight with `enable_stick_arming` `ON` and no `ARM` mode range, throttle low + yaw high held for
   `ARM_DELAY_MS` **arms**; on an INAV fixed-wing with `fixed_wing_auto_arm` `ON`, throttle above `min_check`
   **arms, with no stick pattern involved at all.** The second is the harder constraint, because it makes
   **throttle position — not a pattern — the trigger**: the gesture's hold position and every transition
   through it shall keep throttle low. Both settings are non-default, which is exactly why the audit shall
   cover the configuration space rather than one stock build.
3. **Is primary-channel ordering resolvable?** `CTRL-12` keeps channels 1–4 out of scope precisely to avoid
   platform-specific ordering. `MSP_RC` is downstream of the FC's own channel remapping, so the transmitter's
   AETR/TAER choice does not reach the bridge — but the two platforms' internal channel constants shall be
   confirmed to agree at the pinned tags before any gesture indexes them.

| Outcome | Consequence |
| --- | --- |
| A hold position exists on both platforms, is unreachable from any arming configuration, and channel order agrees | the gesture may be specified as an **additional** entry authority, never as a replacement for `SETUP-01` authority 3 |
| Any question fails | the feature is dropped and this section records why, per `CAM-08`'s rule that a negative result is recorded rather than discarded |

Whatever the outcome, **the AUX gesture remains the specified no-touch route.** A stick gesture would remove
one setup field at the cost of the entire audit above, which is why it is not in V1 (`SCOPE-08`).

### `VAL-SPIKE-07` — Discovery dwell: closes the `PAIR-12` parameter

**Verifies:** `PAIR-12`, `PAIR-11`, `PAIR-03`, `REL-02`

**What is being measured is the camera, not the bridge:** the longest interval a supported camera in its
pairable state may go without emitting an advertisement the bridge can classify. The dwell is that interval
plus margin, because `PAIR-11` may conclude only once a further candidate can be ruled out.

1. With one supported camera in its pairable state, run a passive scan for **120 s** and record the arrival
   time of every classifiable advertisement. Repeat **≥ 5 times**, including at least one run started before
   the camera enters pairable state and one at the far end of the intended operating range (`RF-04`).
2. Take the **maximum observed inter-advertisement gap** across all runs. The dwell is that maximum plus a
   stated margin; the margin shall be stated, not folded silently into the number.
3. Repeat for every camera model the release claims (`SCOPE-03`…`SCOPE-05`). **The dwell is the maximum
   across claimed models**, since one dwell serves all of them (`PAIR-12`).
4. Confirm the result against the bound. A dwell exceeding the `PAIR-03` discovery-scan maximum means the
   scan cannot complete within its own bound. **This spike does not resolve that**, and in particular does
   not trim the dwell to fit — trimming would restore the defect the spike exists to close.

**What this spike may decide, and what it may not.** `REL-02` delegates one value to it: `PAIR-12`. It has
no authority over `PAIR-03`, over `PAIR-08`'s selection rules, or over any product behaviour, and a
measurement that implies a change to one of those **produces a change request, not a decision** (v1.4,
CR-28).

| Outcome | Consequence |
| --- | --- |
| A dwell is established at or below the `PAIR-03` bound | write it back into `PAIR-12`, mark `REL-02` closed |
| The measured dwell exceeds the `PAIR-03` bound | **`PAIR-12` stays `OPEN`** and a change request is raised against `PAIR-03`. The spike reports the measured dwell and the margin it needs; whether the discovery-scan maximum moves, and how far under the 120 s boot lifetime cap, is decided in the ledger and not here |
| A camera advertises unclassifiably in its pairable state | **record the measurement and stop.** The spike states the observation per `CAM-08` and raises a change request; it does not decide the model's fallback |

**Until this closes, `PAIR-12`'s interim value is the `PAIR-03` bound** — so a scan is correct but slow, never
early. **`PAIR-12` is `OPEN`, so this spike gates any release claiming discovery** (`REL-01`, `REL-02`).

---

## 4. Regression procedure

- **Configurator `diff` before and after every bench session.** Any change to USER / PINIO / mode /
  Programming Framework configuration is a `SAFE-02` violation, not a cosmetic one (`VAL-FUNC-71`).
- **Spike order:** `VAL-SPIKE-05` first (cheapest, gates the boot architecture), then `VAL-SPIKE-01`, then
  `VAL-SPIKE-03`, then `VAL-SPIKE-02`. `VAL-SPIKE-04` is independent and may run at any time.
  `VAL-SPIKE-07` needs only a camera and a scanner, so it may run alongside `VAL-SPIKE-03` on the same bench
  session — but it gates discovery, so it shall not be left until the driver work is finished.
- **`VAL-SPIKE-02` gates release, not features** — rig build-out can begin in parallel with driver work.
