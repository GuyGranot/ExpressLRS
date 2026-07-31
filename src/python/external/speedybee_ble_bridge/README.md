# speedybee_ble_bridge

Unmodified copy of `speedybee_ble_bridge.py` from
[dunaevai135/speedybee_ble_bridge](https://github.com/dunaevai135/speedybee_ble_bridge),
MIT licensed — see `LICENSE`.

It is a *client*-side bridge: it drives a SpeedyBee flight controller's BLE module from a
PC. `lib/BLEMSP` implements the other end of the same protocol, so this file is kept here
as the reference for what the app expects a device to answer, and as something to diff
against if the handshake ever stops being accepted.

Nothing in the firmware build depends on it. Kept byte-identical to upstream on purpose,
so re-fetching the original is a clean comparison.
