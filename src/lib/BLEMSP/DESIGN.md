# SpeedyBee BLE → MSP-over-ELRS Bridge

Presents a **SpeedyBee-compatible BLE peripheral on the TX module**, so the stock SpeedyBee
phone app can configure a flight controller that has no Bluetooth of its own. MSP is
tunnelled over the live ELRS link to the FC's CRSF UART.

Build flag **`-DTX_BLE_MSP`**, off by default. Started from the handset (`elrs.lua` → **BLE
MSP** → Start); never at boot.

| | |
|---|---|
| Validated on | RadioMaster Nomad TX (main ESP32, dual LR1121, ESP32-C3 backpack) |
| Builds unchanged on | RadioMaster Boxer internal (ESP32, 2.4-only — BLE/RF coexistence untested there) |
| Requires | `TARGET_TX && PLATFORM_ESP32 && TX_BLE_MSP` |
| App tested | SpeedyBee **iOS**; Android's handshake differs and is untested |

**The RF link stays up.** This is the defining constraint and the thing that separates this
feature from BLE Joystick (`Radio.End()`) and from the spectrum analyzer (a mode state with
the link intentionally dead). Everything in §1 follows from it: code runs alongside a live
packet loop, so it has to be safe there rather than merely safe on the bench.

**Non-goals:** the CLI tab, DFU or flashing over the bridge, Android, no ACK/NAK layer over
the tunnel, and no persistence of anything.

The CLI is out for a reason worth stating once, because it looks like it ought to work.
`msp2crsf::getVersion()` recognises only `$M`/`$X`; anything else yields
`MSP_FRAME_UNKNOWN`, a zero-length frame, and the error bit — CLI text is simply gone. But
the deeper block is Betaflight's: **there is no MSP command that carries a CLI command.**
`msp_protocol_v2_common.h` defines none, and the CLI-over-MSP feature added in 4.6
([betaflight#13940](https://github.com/betaflight/betaflight/pull/13940)) works by in-band
control characters — STX `0x02` to enter, ETX `0x03` to leave — handled in
`mspSerialProcess()`. Like the older `#` hook, that is a property of a serial byte stream,
not of MSP, so there is nothing to put inside a CRSF frame. ELRS's own WiFi bridge has the
same gap for the same reason, stated in
[ExpressLRS#1274](https://github.com/ExpressLRS/ExpressLRS/pull/1274): "CLI doesn't work
(there is currently no way encapsulate MSP frames to CLI)", and "Presets don't work
(requires CLI)". Not fixable from the TX. If Betaflight ever exposes CLI as a real MSP
command this bridge inherits it for free, since `msp2crsf` does not care what a command
number means.

Protocol spec (the BLE wire format, the device-info blob layout, the handshake byte
vectors), the bench procedure, and the FC-side setup live in
[`python/ble-msp-emulator/README.md`](../../python/ble-msp-emulator/README.md). This
document is the firmware architecture and does not repeat them.

---

## 1. Safety model

### S1 — Arming interlock belongs to the flight controller

**Betaflight refuses to arm while an MSP client is connected, and that is the interlock.**
It is the right place for the rule: the FC owns arming and knows which channel the model
arms on, which the TX cannot know — a TX-side rule would have to guess the arm channel and
would silently fail on any model that arms elsewhere.

The FC only counts a client as connected once MSP traffic has actually reached it, so
opening the bridge is not by itself enough. `BleMspConnector::pump()` therefore sends one
`MSP_API_VERSION` request as soon as the link is up. Its answer is discarded — it exists to
make the connection real from the FC's side.

That probe is **the one thing sent regardless of armed state**. `isArmed` is reported by the
handset, so it can be true with the FC still powered off:

> start the bridge → pull the arm switch → power the model

Without the exception the probe never goes out and the model comes up with a config bridge
open and no interlock. Betaflight's arm-switch-at-boot rule happens to cover that sequence,
but that is the FC's behaviour to change, not something to depend on. Sending while armed is
safe: an `ARMING_DISABLED` flag blocks arming, it does not disarm a model already flying.

**Consequence, intended:** once you start BLE MSP you cannot arm until the session ends.
Exiting the Lua command reboots the module, which clears it.

**Cross-project dependency, stated explicitly.** S1 is not a property this firmware can
enforce; it is a property of the flight controller, and it is the only thing standing
between an open config bridge and an armed model. It holds on:

| FC firmware | Behaviour | Status |
|---|---|---|
| Betaflight ≥ 4.0 | `ARMING_DISABLED_MSP` raised while an MSP client is connected | relied on |
| INAV | equivalent MSP arming block | believed equivalent, **not verified** |
| Other (Ardupilot, custom) | unknown | **unverified — do not assume S1** |

If a target FC does not implement this, the feature has no arming interlock and S2.2/S2.4
are all that remain — neither of which can stop a model arming mid-session. Any port to a
new FC firmware must re-verify S1 on the bench — open a session, flip the arm switch, and
confirm the FC refuses to arm — before the feature is offered for it. This is recorded as a dependency rather than a code comment because the
behaviour lives in another project's source and can change there without warning.

### S2 — What the TX guards itself

| Rule | Mechanism |
|---|---|
| S2.1 | Entire feature behind `-DTX_BLE_MSP`, off by default. Flag-off costs +204 bytes of flash and no RAM — near-stock, not byte-identical (§4) |
| S2.2 | **Refuse to start while armed** (`BleMspStart`) — the FC's protection cannot exist before MSP is open, and bringing up NimBLE allocates tens of KB and stalls the loop core for tens of ms |
| S2.3 | **Refuse to start while `connectionState > MODE_STATES`** — BLE Joystick owns NimBLE exclusively; WiFi/serial update own the flash and want the heap. Never fight either for the stack, and never jeopardise a firmware update in progress |
| S2.4 | Uplink withheld while armed or unlinked (`BleMspMayForward`), defence in depth behind S1 |
| S2.5 | Shaping reverts on arming (§3.6), without a reboot |
| S2.6 | **Exit by reboot**, but not immediately: cancelling hands the radio back first (§3.6), then reboots. NimBLE still needs no teardown |

S2.2 and S2.3 are refusals with a *reason*, surfaced on the Lua line ("Disarm first",
"Busy: WiFi/BLE") rather than an unchanged line that reads as a dead button.

### S3 — Rollback must be a non-event

**No config fields, no `TX_CONFIG_VERSION` bump.** `lib/CONFIG/config.cpp` wipes the entire
TX config — model matches, rates, power, VTX, bind phrase — when the stored version exceeds
the firmware's, so a bump means a later reflash of stock firmware silently destroys the
pilot's settings.

This is why **link shaping never writes config** (§3), and why the Link Shaping selector
itself is RAM-only: a session does not survive a reboot, so there is nothing to remember.
It is also why the handset needs the live-rate readout in §5 — with config untouched, the
Lua's own Packet Rate and Telem Ratio items keep showing the configured values all session.

### S4 — Zero receiver changes

Verified, and worth keeping: the receiver learns the `0x12` origin address dynamically
(`CRSFRouter.cpp`) and unknown-destination flooding covers it regardless. A stock RX of a
compatible OTA version bridges MSP without knowing this feature exists.

### Invariant quick reference

| # | Invariant | Enforced at |
|---|---|---|
| I1 | Nothing runs until the Lua command asks | `devBleMsp.cpp` `start()` → `DURATION_NEVER` |
| I2 | `0x12` is claimed only while the bridge is up | `BleMspConnector::begin()` from `timeout()`, not from boot |
| I3 | Router/OTA calls only from the loop core | device affinity 1; `pushFromBle` is enqueue-only |
| I4 | One MSP frame uplink at a time | `otaConnector.uplinkBusy()` gate |
| I5 | Never truncate an MSP frame — drop it whole | `INBOUND_MAX` overflow path, `outbound.free()` check |
| I6 | No config writes, ever | shaping goes through `commitRadioRate`, not `ConfigChangeCommit` |
| I7 | Sync packets advertise where the radio is *going* | `intendedRate()` in `GenerateSyncPacketData` |

---

## 2. Architecture

Four layers, split so the transport can move without touching the logic. The split is not
decoration: the documented fallback if BLE/RF coexistence had failed was to move the GATT
layers verbatim onto the backpack ESP32-C3 and swap `BleMspConnector`'s byte transport to
the TX↔backpack UART. One thing that fallback now also has to carry: the C3 has no
`lib/OPTIONS`, so `device_name` and `product_name` (§2.0) would have to cross that UART too.
Budget for it — the layers move unchanged, the identity does not come along for free.

| Layer | Files | Depends on | Runs on |
|---|---|---|---|
| Handshake | `lib/BLEMSPCore/SpeedyBeeHandshake.{h,cpp}` | nothing (no Arduino, no NimBLE) | caller's task |
| GATT | `lib/BLEMSP/SpeedyBeeGatt.{h,cpp}` | NimBLE | NimBLE host task |
| CRSF bridge | `lib/BLEMSP/BleMspConnector.{h,cpp}` | router, `msp2crsf`, `crsf2msp` | loop core |
| Device/policy | `lib/BLEMSP/devBleMsp.{h,cpp}` | all of the above | loop core |

### 2.0 Identity comes from the image, not a literal

The advertised local name and the app-displayed name in the device-info blob are
`device_name` and `product_name` from `lib/OPTIONS` — the per-target strings
`binary_configurator` bakes in. They were literals (`"ELRS Nomad TX"`) until a Boxer build
proved the obvious consequence: it advertised itself as a Nomad. The feature builds for any
ESP32 TX, so nothing here may name one module.

`device_name` is the short handset name (≤16 chars: `RM Boxer`, `RM Nomad X-Band`) and is what
goes in the advertisement, where the 31-byte budget is shared with the service UUID.
`product_name` is the full name and can be 128 bytes — safe because `setBlobField` bounds
every field to its captured width, truncating at 31 rather than running into `wifiName` at
offset 56.

`productCode` stays a literal, `ELRSTXMOD` (it was `ELRSNOMAD`). It is not displayed and the
app accepts an arbitrary code, so there is nothing per-target to put there and a product name
would not fit 9 characters anyway.

Nine is the field's usable width, not a constraint the code depends on, and the difference
matters because the comments used to claim otherwise: "the fields after it sit at fixed
offsets, so a different length would shift them" was **wrong** in the handshake header for as
long as the field table has existed. `buildDeviceInfoResponse` writes every field *at* its
offset and `setBlobField` clamps to the width, so an over-long value loses its tail and can
never disturb its neighbour. Nothing needs a `static_assert`; the blob writer already
guarantees it. `wifiName` and `serial` are likewise constants and not per-unit values.

`BLEMSPCore` is a **separate library directory on purpose**. Left in `BLEMSP`, the LDF drags
NimBLE and Arduino headers into the native test environment and the host tests stop
building; `platformio.ini`'s native env `lib_ignore`s `BLEMSP` and keeps `BLEMSPCore`. The
handshake's golden vectors are `src/test/test_speedybee_handshake/`, generated from
`python/ble-msp-emulator/sb_protocol.py` — that Python file is the executable spec.

### 2.1 Task and core model

Two tasks touch the bridge, and the boundary is one FIFO wide.

```
NimBLE host task                    loop core (affinity 1)
────────────────                    ──────────────────────
ABF1 write
  └─ onAppSerialBytes()
       └─ pushFromBle()  ──►  FIFO<512> fromBle  ──►  pump()
            atomicPushBytes                             ├─ msp2crsf → crsfRouter
            (portMUX)                                   └─ outbound → notifySerial
```

`fromBle` is the only member both sides touch, and `atomicPushBytes` takes the portMUX, so
nothing else needs locking. `outbound` is loop-core at both ends and deliberately unlocked.

**Affinity 1 is load-bearing.** `pump()` re-enters `crsfRouter.processMessage()` and
`TXOTAConnector::forwardMessage()`, which the rest of the codebase only ever calls from the
loop core.

The device ticks at **5 ms connected, 100 ms advertising** — never `DURATION_IMMEDIATELY`.
MSP responsiveness only matters once a phone is talking; until then this is a heartbeat.
NimBLE init is deferred out of `event()` into `timeout()` because it allocates tens of KB
and takes tens of ms.

### 2.2 Why this needed no router changes

`BleMspConnector`'s constructor calls `addDevice(CRSF_ADDRESS_BLUETOOTH_WIFI)` — `0x12`.
That single line is what makes the feature drop in:

- `TXOTAConnector` already claims the FC address, so MSP addressed to the FC routes over the
  air with no new rules.
- The RX learns `0x12` as an origin the first time a request arrives, and floods
  unknown destinations as a backstop, so replies find their way home (S4).
- `lib/WIFI/TcpMspConnector.cpp` — the receiver's WiFi → Betaflight-configurator TCP bridge
  — is the same shape, and was the template. The transport differs; the CRSF side is
  identical.

### 2.3 Data paths, end to end

**App → FC**

```
ABF1 write (NimBLE task)
  → FIFO<512> fromBle
  → pump() (loop core), gated on !otaConnector.uplinkBusy()
  → MSP2CROSSFIRE::parse(..., 0x12, CRSF_ADDRESS_FLIGHT_CONTROLLER)   57-byte MSP_REQ chunks
  → crsfRouter → TXOTAConnector::forwardMessage   (gated connectionState == connected)
  → DataUlSender   5 B/packet standard, 10 B full-res, ack-gated per telemetry packet
  → RX DataUlReceiveComplete → router (learns origin 0x12 here)
  → SerialCRSF → FC UART
```

**FC → App**

```
FC MSP_RESP dest 0x12 → SerialCRSF::processBytes → router
  → RXOTAConnector (learned 0x12; flood as backstop)
  → TelemetryFifo (512 B)
  → DataDlSender   5/10 B per telemetry packet
  → TX tx_main downlink handler → router
  → BleMspConnector::forwardMessage   filters MSP_RESP/MSP_REQ
  → CROSSFIRE2MSP reassembly → FIFO<1024> outbound
  → pump() → SpeedyBeeGatt::notifySerial → ABF2 notify (fragmented to MTU)
```

### 2.4 Failure handling — drop whole, never half

There is no NAK anywhere in this path, so a partial frame is worse than a missing one: the
FC or the app would parse a length header that lies. Every buffer limit therefore drops a
whole frame and lets the peer's own timeout retry.

| Site | Limit | On overflow |
|---|---|---|
| `inbound` | `INBOUND_MAX` 512 B | flush `fromBle`, drop the frame, `DBGLN` |
| `outbound` | 1024 B | drop the reply rather than evict its front |
| `TXOTAConnector::outputQueue` | 256 B | never reached — the `uplinkBusy()` gate is what prevents it |
| RX `TelemetryFifo` | 512 B | early chunks of a large `MSP_RESP` can evict under load → seq gap → silent frame loss. **Most likely field failure**; a 1024 B bump is held in reserve and would break "zero RX changes" |

Phone disconnect calls `reset()`: partial frames and `crsf2msp` state are dropped so a
reconnect cannot resume mid-frame against a fresh app session. Counters deliberately
**survive** a disconnect — the app dropping the link is exactly when they want reading —
and `startSession()` clears them on the next connect.

---

## 3. Link shaping

Opt-in from the Lua folder, **default off**, because it changes the air rate and telemetry
ratio out from under the pilot. While a session is shaped:

- telemetry ratio pinned to **1:2** (`UpdateTlmRatioEffective`)
- air rate hopped to the **fastest full-res LoRa rate the model's band allows**

Neither writes config (S3). TX power is left alone — an earlier design clamped it to
`MinPower`; that was dropped.

The problem it solves: a long-range setup flies slow with telemetry off, where a config
session is unusably slow. 150 Hz standard is ~375 B/s against 1665 B/s at 333 Hz Full, and
full-res doubles the uplink too.

### 3.1 It follows the session, not the phone

`linkShapeWanted = linkShapingEnabled && !isArmed`, evaluated once the GATT server is up —
i.e. from **Start**, not from the phone connecting.

Keying it on the phone put the rate hop, and the reacquisition behind it, exactly where the
app's opening MSP burst lands, and **the app timed out** on a link that was busy changing
rate underneath it. Starting the bridge is already a deliberate act with the model on the
bench, so the hop happens there and the link is settled long before anything is asked of it.

### 3.2 `intendedRate()` is load-bearing — do not fold it back into `config.GetRate()`

`GenerateSyncPacketData()` advertises `intendedRate()` while `syncSpamCounter` is set, not
`config.GetRate()`.

Everywhere else in ELRS, changing rate *is* a config change, so advertising the configured
rate is correct. A shaped session is the only case where intended ≠ configured. And sending
MSP sets `syncSpamCounter` — tx_main requests a sync packet after every data uplink to boost
the reply — so before this existed, **every MSP frame told the receiver to leave for the
configured rate**: it went, heard nothing, hunted, reacquired, and was sent away again. The
session's own traffic broke the link. The same bug made the pre-hop warning advertise the
rate we were already on, costing a 35 s hunt.

`sessionRate` is `TX_SESSION_RATE_NONE` (`0xFF`) when nothing is steering the radio.
Note it holds where the radio is *heading*, which during a revert is already the configured
rate — that is why it is not the same variable as the session's chosen rate.

### 3.3 The hop follows the protocol a Lua rate change does

`UpdateSessionRate()` runs from `loop()`, right after `CheckConfigChangePending()`, as
a two-phase state machine. Calling `SetRFLinkRate` bare is how a rate change turns into a
silent dropout and a reacquisition hunt.

1. **Warn.** `syncSpamCounter = syncSpamAmount` (3) on the *old* rate, so the receiver
   learns the new one before we move. Only says anything because `sessionRate` is
   published first (§3.2).
2. **Hop.** Wait for `syncSpamCounter` to drain, then `beginRadioTransition()` →
   `commitRadioRate(target)` → clear `commitInProgress`, then
   `syncSpamCounterAfterRateChange = syncSpamAmountAfterRateChange` (10).

`beginRadioTransition()` and `commitRadioRate()` were **extracted from
`CheckConfigChangePending`/`ChangeRadioParams`**, not retyped: both callers need the radio
idle and off the SPI bus, and the ordering inside `commitRadioRate` (`ResetPower` before
`SetRFLinkRate`, then `LbtEnableIfRequired`) is load-bearing. `config` is untouched, so
`ConfigChangeCommit`'s path cannot be reused directly.

The wanted rate is compared against the **live** rate rather than a record of what we last
set, so a rate change from elsewhere is observed instead of silently desyncing. Selection
runs once per session; only the comparison runs per loop.

The mechanism is deliberately feature-neutral. `tx_main` exposes
`TxRequestSessionRate(rateIndex)` / `TxSessionRateIsHome()` (`rxtx_intf.h`) and owns the
warn → wait → hop → warn sequence, because `SetRFLinkRate`, `commitInProgress` and the
sync-spam counters are all file-local there. BLE MSP owns only the policy: whether a
session wants a different rate, and which one. A future feature needing a temporary rate
uses the same two calls instead of growing a second copy of the sequence.

### 3.4 Rate selection policy

`selectSessionRate()` lives with the feature, not in tx_main: it is pure rate-table
policy, and tx_main owns only the mechanism (`SetRFLinkRate` is file-local there by design,
the same way binding drives it with `RATE_BINDING`). It returns the configured rate when
nothing beats it, so the caller treats "no change" and "change" identically.

Candidates are ranked by telemetry bytes/sec at 1:2, and filtered on:

- **Band reachable.** A dual-band model has both radios so it can run dual, 2.4 or sub-GHz
  rates; single-band hardware cannot cross either way. This is what lets an X150 model reach
  2.4's 333 Hz Full (1665 B/s) instead of being stuck with dual 100 Hz Full (500 B/s).
- **Full-res** (`PayloadLength == OTA8_PACKET_SIZE`) — 10 telemetry bytes against 5 is the
  whole point.
- **LoRa, `numOfSends == 1`.** FSK (K rates) and FLRC (F/D) are what a receiver is most
  likely not to implement, and a rate it cannot follow costs the link. Derived rather than
  whitelisted so a future rate is not silently skipped — note `RATE_FSK_900_1000HZ_8CH` is
  full-res with `numOfSends` 1, so the modulation test does the real work.

Deliberately **not** replicated from `SetPacketRateIdx()`: the switch-mode adjustment
(`adjustSwitchModeForAirRate`) and the forced-Gemini antenna mode for dual-band. Both write
config; the hop does not (S3).

### 3.5 Exit must hand the radio back before rebooting

A receiver follows a rate change when a sync packet tells it to, but with
`LOCK_ON_FIRST_CONNECTION` it can follow and never search: `rx_main.cpp:864` sets
`LockRFmode` on connect, `:1628` gates `cycleRfMode()` on it, and only `ExitBindingMode()`
clears it. So a receiver that followed the TX onto the session rate is stranded there by a
bare reboot — deaf to the TX returning on the configured rate, unable to scan for it, and
recoverable only by power-cycling the model. On the bench this looked like a link that would
not come back, where **resetting the TX did nothing** and resetting the FC did.

The entry path always warned the receiver — that is what `intendedRate()` (§3.2) exists for —
and arming reverts gracefully. Only the exit threw that away, scheduling a reboot 400 ms out
while still shaped. `BleMspStop()` now drops shaping and holds the reboot until
`TxSessionRateIsHome()`, which is *not* a timer: it is the configured rate **plus both
sync-spam counters drained, i.e. the warning has actually gone out**. Timing it instead is
wrong at slow rates, where ten post-hop sync packets take 400 ms at 25 Hz — longer than any
settle constant short enough to feel responsive.

The latch is one-way. Once teardown starts, `timeout()` returns `DURATION_NEVER` rather than
clearing the flag, because a device that keeps ticking would set `linkShapeWanted` true again
and re-arm a hop in the moments before the reboot lands — reproducing the very bug, in the
gap left by its own fix.

**Still unprotected**, and worth knowing before shaping is trusted in the field: a TX power
loss, crash, or firmware flash mid-session strands a locked receiver exactly the same way,
and nothing on the TX can prevent it. Entering WiFi update while shaped is the same gap by
another route — S2.3 stops BLE MSP starting during WiFi, not WiFi starting during BLE MSP.
The recovery is always an RX power cycle, which on a bench is cheap. A deeper fix would stash
the session rate in RTC memory and revert on boot; that is not built.

### 3.6 Self-restore

The flag drops on arming or on the session ending, and everything comes back without a
reboot: `UpdateTlmRatioEffective`'s existing branches return the configured ratio, and the
device requests `TX_SESSION_RATE_NONE`, so `UpdateSessionRate` targets `config.GetRate()`
and hops back through the same two phases.

---

## 4. Intervention points — the audit surface

Everything the feature touches outside its own directories. All of it is `#if
defined(TX_BLE_MSP)`-guarded except the two refactors marked ⚠, which change stock code
paths on **every TX build**.

| File | Change |
|---|---|
| `lib/DEVICE/device.h` | `EVENT_BLE_MSP = 1 << 7` (bit was free) |
| `lib/tx-crsf/TXOTAConnector.h` | `uplinkBusy()` accessor over existing members; loop-core-only read |
| `lib/tx-crsf/TXModuleParameters.cpp` | BLE MSP folder, Start command, Link Shaping selector; `handleWifiBle` gains an `isRunning` function pointer and a `liveStatus` flag (§5) |
| `src/tx_main.cpp` | include, `{&BleMsp_device, 1}` in `ui_devices[]`, `UpdateTlmRatioEffective` 1:2 branch |
| ⚠ `src/tx_main.cpp` | `intendedRate()` replaces `config.GetRate()` in `GenerateSyncPacketData` — **unguarded**, returns `config.GetRate()` whenever no session rate is requested |
| ⚠ `src/tx_main.cpp` | `sessionRate` / `TxRequestSessionRate()` / `TxSessionRateIsHome()` / `UpdateSessionRate()` — **unguarded** and feature-neutral; no-ops until something requests a rate |
| ⚠ `src/tx_main.cpp` | `beginRadioTransition()` / `commitRadioRate()` extracted from `CheckConfigChangePending` / `ChangeRadioParams` — behaviour-preserving, but stock code now goes through them |
| `platformio.ini` | native env `lib_ignore`s `BLEMSP` (keeps `BLEMSPCore` host-testable) |
| `user_defines.txt` | commented `#-DTX_BLE_MSP` block |

The ⚠ rows are why the guard-off image is only *near*-stock. Measured on
`Unified_ESP32_LR1121_TX_via_UART`, FCC_915 + ISM_2400:

| | Flash | Static RAM |
|---|---|---|
| stock master | 1,688,041 | 71,432 |
| this branch, flags off | 1,688,245 (**+204**) | 71,432 (**+0**) |
| this branch, `-DTX_BLE_MSP` | 1,697,653 (+9,612) | 74,328 (+2,896) |

Measured at `26357a94`; the upstream commits since touch only `elrs.lua`.

So **do not read "additive-only" as "byte-identical"**: the extracted transition helpers do
not inline back to exactly what they replaced. 204 bytes is the price of not retyping the
commit ordering per caller, which is a good trade — but it is a real deviation, and it is why
the guard regression set (2400 TX, C3 TX, ESP32 RX, native) is worth keeping green.

Measure it, do not remember it. An earlier note in this file quoted a stock baseline from
memory that was 220 bytes off, and reasoning from it produced two confident and opposite
wrong answers (−16 and +16) before anyone built the merge-base.

`handleWifiBle` previously assumed every command it serves owns a `connectionState`. BLE MSP
keeps the RF link up and so never enters a mode state — `lcsCancel`'s reboot check would
never fire. Rather than special-case it, the test became overridable: `targetState` stays the
default (`connectionState == targetState`), and a command whose activity is not a mode state
supplies a `bool (*isRunning)()` instead — `BleMspIsRunning` here. The running text became
live too: each poll re-renders it rather than echoing what was stored at startup.

The override is a **nullable** pointer, not one that always points somewhere. The tidier-looking
alternative — default it to a function that compares against the target — cannot work without
lifting `targetState` to file scope, because a plain function pointer cannot reach a local. That
trades a value used within one call for permanent mutable state which then routinely holds some
other command's stale target, and it costs flash in every TX build including flag-off ones. The
nullable pointer keeps the value a local and the cost at zero.

---

## 5. Handset UX and the status line

The Lua screen is **the only instrument on an assembled module.** `DBGLN` goes to the
backpack UART (Nomad: GPIO5, wired to the backpack MCU); the module's USB port is a CP210x
on a different line carrying CRSF, not the log. Reading it needs an adapter on GPIO5, which
is why `DEBUG_LOG` is not worth enabling on an assembled module.

That constraint shaped the status line, which localised every bug found so far.

```
Con+ 333F:2+ w17 u17 r17 x0 s412
 │    │  │ │  │   │   │   │   └─ ms the link took to come back across the last rate hop
 │    │  │ │  └─ pipeline stages: BLE writes in / forwarded to FC / replies / dropped, no link
 │    │  │ └─ shaping marker
 │    │  └─ live telemetry denominator
 │    └─ live packet rate, F if full-res
 └─ Adv | Con | Con+ (handshake complete)
```

`w`/`u`/`r`/`x` separate the pipeline, so **a stall names its own stage**. A handshake line
substitutes `c<writes> <cmd>><resplen> d<reason>` for the counters, where `d19` (0x13) means
the peer *chose* to drop us having judged an answer wrong, and `d8` is a supervision timeout
meaning we never answered at all — different bugs, otherwise indistinguishable.

| Marker | Shaping |
|---|---|
| `.` | not enabled |
| `+` | engaged |
| `A` | enabled, held off because the handset reports the model armed |
| `-` | enabled but not engaged |

Two of those details are scar tissue, and both are load-bearing:

- **The marker is never blank.** An absent marker cannot be told apart from a misread one. A
  blank one cost a full round of misdiagnosis: `150:1` was read as proof shaping was off
  when the real reading was `333F:2+`.
- **The link token is on every line**, not just the ones with MSP counters. Shaping is armed
  by the Lua selector long before a phone connects, so hiding it until the first app write
  made the one state worth checking first — did the selector reach the firmware — unreadable
  without a phone in hand.

The live rate and ratio have to be shown *here* because shaping never writes config, so the
Lua's own Packet Rate and Telem Ratio items show the configured values all session, and the
EdgeTX telemetry screen cannot be reached without leaving the popup, which ends the session.

### The Link Shaping selector needs an explicit echo

`registerParameter`'s callback for Link Shaping calls `setTextSelectionValue()` on itself.
This looks redundant and is not: `parameterUpdateReq` calls the callback but never stores
the written value. Every *other* selection here writes config, which raises a device event,
and `updateParameters()` (subscribed to `EVENT_ALL`) republishes all values. Shaping raises
no event, so the handset's read-after-write returned the stale `0` and **the selector
snapped straight back to Off while the firmware was in fact on**.

Fixing `parameterUpdateReq` generally would be the deeper fix, but its blast radius is every
selection parameter on both endpoints, and some callbacks deliberately clamp or reject a
written value — storing unconditionally would display values that were refused.

---

## 6. Building

The flag is off by default and no target env sets it, so an ordinary build is unaffected.

```sh
PLATFORMIO_BUILD_FLAGS="-DTX_BLE_MSP" pio run -e Unified_ESP32_LR1121_TX_via_UART
pio test -e native -f test_speedybee_handshake
python python/ble-msp-emulator/selftest.py    # protocol + link model, no hardware
```

`DEBUG_LOG` is worth leaving off. `DBGLN` goes to the backpack UART, not to the module's USB
port, so on an assembled module it costs flash for output nobody can read (see §5).

Flash: Nomad ~86.3%, Boxer internal 82.5%. NimBLE is already a `lib_dep` of every ESP32 TX
build via BLE-Gamepad, so that cost was already paid.
