# SpeedyBee BLE — protocol spec, link model, and bench logging

Host-side support for the `tx-ble-msp` feature: a SpeedyBee-compatible BLE
peripheral on the RadioMaster Nomad TX that tunnels MSP over the ELRS link to
a flight controller with no Bluetooth of its own.

The **firmware architecture** — layers, task/core model, data paths, link
shaping, and the audit surface outside the feature directory — is
[`src/lib/BLEMSP/DESIGN.md`](../../src/lib/BLEMSP/DESIGN.md). This document is
the protocol spec, the link model, and the bench procedure.

## Why this runs against hardware, not a PC emulator

Windows cannot host this peripheral, for reasons no amount of protocol fidelity
fixes: its own LE Audio services cannot be removed and are published alongside
yours (a genuine FC exposes only 0x1800, 0x1801 and 0xABF0),
`BluetoothLEAdvertisementPublisher` reserves `LocalName` so the device always
advertises as the machine name, and manufacturer data is only permitted from a
separate advertising set with its own address, which appears to a phone as a
second, serviceless device.

The SpeedyBee app connects to such an emulator and reports "device
initialization failed" having performed **zero** GATT operations on our service,
while nRF Connect from the same phone drives the handshake and a full MSP round
trip against it. That pair of results is what isolates the fault to discovery
rather than to the protocol, and it is why what is left here talks to real
hardware.

## What's here

- `sb_protocol.py` — the SpeedyBee device-side handshake responder and MSP
  stream framer. **This is the executable spec** for the C++
  `src/lib/BLEMSPCore/SpeedyBeeHandshake.cpp`; its vectors are the golden bytes
  in `src/test/test_speedybee_handshake/`.
- `link_sim.py` — token-bucket throughput + latency + silent-frame-drop model
  of the MSP-over-CRSF tunnel (no NAK, matching the firmware).
- `selftest.py` — no-hardware regression over both: handshake vectors, MSP v1
  and v2 framing, schedule math, and an end-to-end 625 B/s / 2%-drop session.
- `capture_fc.py` — connects to a real board as a BLE central, the role the
  phone plays, and hex-dumps its handshake. This is what produced
  `fc_handshake.txt` and settled two bugs no amount of reasoning had.
- `../external/speedybee_ble_bridge/` — third-party *client*-side reference
  (dunaevai135/speedybee_ble_bridge, HCI-snoop-derived). Do not edit; it is
  an independent cross-check of `sb_protocol.py`, not a dependency.

```
python selftest.py                     # all checks, no hardware
python capture_fc.py --scan            # list nearby BLE devices
python capture_fc.py --addr AA:BB:...  # dump a board's handshake
```

Only `bleak` is required, on any supported Python 3.

## Arming while the bridge is open

**Starting BLE MSP prevents the model arming, and that is deliberate.**

The interlock is the flight controller's, not the TX's. Betaflight refuses to
arm while an MSP client is connected, and it is the right place for the rule:
it owns arming and knows which channel the model arms on, which the TX cannot
know — a TX-side rule would have to guess the arm channel and would silently
fail on any model that arms elsewhere.

The FC only counts a client as connected once MSP traffic has actually reached
it, so opening the bridge is not by itself enough. The TX therefore sends one
`MSP_API_VERSION` request as soon as the link is up. Its answer is discarded;
it exists to make the connection real from the FC's side.

That request is the one thing sent **regardless of armed state**. Everything
else is withheld while armed, but `isArmed` is reported by the handset, so it
can be true with the flight controller still powered off:

> start the bridge → pull the arm switch → power the model

Without the exception the probe would never go out, and the model would come
up with a config bridge open and no interlock. Betaflight's arm-switch-at-boot
rule happens to cover that sequence, but it is the FC's behaviour to change,
not something to depend on. Sending while armed is safe: an `ARMING_DISABLED`
flag blocks arming, it does not disarm a model already flying.

**Consequence:** once you start BLE MSP you cannot arm until the session ends.
Exiting the Lua command reboots the module, which clears it.

The TX adds one guard of its own — it refuses to *start* the bridge while
armed, since the FC's protection cannot exist before MSP is open, and bringing
up NimBLE stalls the loop core for tens of milliseconds.

## Bench protocol

Testing happens against the real module, and the instrument is the **ELRS Lua
screen** — there is no usable serial channel on an assembled module. `DBGLN`
goes to the backpack UART (Nomad: GPIO5, wired to the backpack MCU); the
module's USB port is a CP210x on a different line carrying CRSF, not the log.
Reading it needs an adapter on GPIO5, so `DEBUG_LOG` is best left off unless
you have wired one.

Start the bridge from the handset (ELRS Lua → **BLE MSP**); it does not run
until asked. Its status line reports:

| Reading | Meaning |
|---|---|
| `Adv 150:1. w0` | advertising, nothing received |
| `Con 150:1. c2 0E>248 d0` | connected; 2 handshake writes, last cmd `0x0E`, answered with 248 bytes |
| `Adv 150:1. c2 !0E d19` | cmd `0x0E` had no builder so we sent nothing, and the peer hung up |
| `Con 150:1- w24 u5 r0 x0` | 24 BLE writes, 5 frames forwarded, no replies, none dropped |
| `Con+ 333F:2+ w17 u17 r17 x0` | working end to end, shaping engaged |

`w`/`u`/`r`/`x` separate the pipeline: BLE writes in, frames forwarded to the
FC, replies reassembled, and frames dropped because the RF link was down. A
stall names its own stage.

`333F:2+` is the **live** link: packet rate, `F` if full-res, telemetry
denominator, then how link shaping stands. None of those can be read anywhere
else — the Lua's Packet Rate and Telem Ratio items show the *configured*
values, because shaping deliberately never writes config, and the EdgeTX
telemetry screen cannot be reached without leaving the popup, which ends the
session.

| Marker | Shaping |
|---|---|
| `.` | not enabled |
| `+` | engaged |
| `A` | enabled, held off because the handset reports the model armed |
| `-` | enabled but not engaged |

So a session that should have been shaped and reads `150:1-` never engaged,
while `333F:2+` with `r0` means it engaged and the fault is past the TX.

**Flight-controller setup.** Enable **telemetry** on the receiver: MSP
replies ride the telemetry downlink, so with it off the FC receives every
request and has no channel to answer on — `u` climbs and `r` stays at 0. Do
**not** enable the *MSP* function on that port; it is unset on a working
setup. Betaflight's CRSF driver unwraps MSP frames itself, and the port's MSP
function is for a serial MSP link — which is how a genuine SpeedyBee BLE
module attaches, and why the app's "enable MSP at 115200 on UART4" error
points the wrong way here.

The receiver's serial protocol must be CRSF, and the Betaflight Receiver tab
should show moving channels before you start.

`d` is the BLE disconnect reason, and it is the useful half: `d19` (0x13)
means the peer *chose* to drop us, having read an answer and judged it wrong;
`d8` is a supervision timeout, meaning we never answered at all. Those point
at different bugs and are otherwise indistinguishable.

1. **Recognition** — does the app list and connect, and does `w` move? A
   connection with `w0` means it rejected us before writing anything.
2. **Timeout/pipelining** — load every tab, watch how many requests the app
   keeps outstanding.
3. **Loss tolerance** — full config read, PID change, save, reboot, re-read
   and diff.
4. **Slow rates** — 100Hz and below; decides whether a "use 250Hz+" warning
   is needed.
5. **CLI tab** — expected to fail (`msp2crsf` drops non-`$M`/`$X` bytes);
   confirm how the app presents that.

**Link shaping** (Lua → BLE MSP → Link Shaping, default **off**) pins
telemetry to 1:2 and hops to the fastest full-res LoRa rate the model's band
allows. It runs for the whole session, from **Start** — not from the phone
connecting. Keying it on the phone put the hop, and the few hundred ms of
reacquisition behind it, exactly where the app's opening MSP burst lands, and
the app timed out on a link that was busy changing rate. Neither change writes
config — which is exactly why the status line has to report them — and both
revert on arming. TX power is left alone.
