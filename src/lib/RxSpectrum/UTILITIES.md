# RX Spectrum — utilities and workflow

Host-side and build tooling for the RX spectrum analyzer (see `DESIGN.md`) and for
building / flashing / recovering the BetaFPV SuperX Nano (ESP32-C3, dual LR1121).
Paths below are relative to `src/`. Commands are shown for PowerShell on Windows;
adjust `COM6` to your flight-controller port and `python` to your interpreter.

| Tool | Location | Purpose |
|---|---|---|
| `rxspectrum.py` | `python/` | Trigger a scan, decode, plot, log, replay |
| `bf_passthrough_min.py` | `python/` | Put the FC into Betaflight serial passthrough |
| `elrs_cfg_tool.py` | `python/` | Inspect / graft the ELRS config block in a firmware image |
| `build_superx_nano.sh` | `src/` | Build + bake the board layout into a ready image |

---

## 1. `rxspectrum.py` — the host analyzer

Triggers a scan on the RX and renders the streamed trace. Requires **pyserial**
(and **matplotlib** for the plot; without it, falls back to a text peak readout).

```
python python\rxspectrum.py --port COM6 --band both
python python\rxspectrum.py --port COM6 --band 2g4 --log capture.jsonl
python python\rxspectrum.py --replay capture.jsonl
python python\rxspectrum.py --selftest        # codec round-trip, no hardware
```

| Flag | Meaning |
|---|---|
| `--port COMx` | Serial port (the FC's port, bridged via passthrough). Required unless `--selftest`/`--replay`. |
| `--baud N` | Default `420000` (the CRSF UART rate). |
| `--band 900\|2g4\|both` | Antenna port / band. Default `both`. |
| `--no-plot` | Text peak readout only (`<band>: peak <dBm> @ <MHz>`), no matplotlib. |
| `--log FILE` | Append each decoded frame to `FILE` as JSON-lines (timestamped) for replay / offline analysis. |
| `--replay FILE` | Re-play a logged capture into the same plot/readout — no hardware, no trigger. |
| `--selftest` | Encode→decode round-trip of the wire codec and exit. |

**Two preconditions for a live scan** (both from `DESIGN.md` §2):
1. The RX must be **disconnected** — turn the paired TX off / out of range. A scan is
   refused while a link is up.
2. A **passthrough bridge** to the RX must be open (§2 below).

The plot shows live (solid) + max-hold (dashed) per band with a peak-dBm box; the
console echoes the peak line. **Exit by resetting the RX** — there is no stop command
(entry stopped the timer and detached the RX-done ISR by design). To scan again after
a reset, re-open passthrough (the reboot drops the bridge) and re-run.

Log format: one JSON object per decoded frame — `{"t": <s since start>, ...decoded
fields...}` (trace type, bin offset/count/total, `startFreqKhz`, `stepKhz`, `bins`).
Each frame carries its own frequency axis, so a log is band-agnostic and fully
self-describing for offline processing.

---

## 2. `bf_passthrough_min.py` — enable Betaflight passthrough

```
python python\bf_passthrough_min.py COM6 420000
```

Opens the FC CLI at 115200, verifies `serialrx_provider = CRSF`, auto-detects the
RX UART (the `serial` map entry whose function mask has bit 64 set), and issues
`serialpassthrough <uart> <baud>`. On success it prints `PASSTHROUGH ENABLED` and
frees the port; the FC stays bridged until it reboots.

**Why this exists.** esptool's `--passthrough` flag only selects a transport mode —
it does **not** tell the FC to enter passthrough; the FC needs the CLI
`serialpassthrough` command first. ELRS's own `BFinitPassthrough.py` sends it but
cannot be run standalone (its imports pull in `external/__init__.py` →
`binary_configurator`, a circular import). This is a self-contained re-implementation
of just the passthrough-init step, with no ELRS package imports.

Run it once before `rxspectrum.py`, and again after any RX reset (the reset reboots
the shared FC stack and drops the bridge).

---

## 3. `elrs_cfg_tool.py` — inspect / graft the config block

ELRS "Unified" firmware appends a config block **after** the ESP image (past its
hash): product name (128 B) + lua name (16 B) + options JSON (512 B) + hardware
layout JSON (2048 B). `binary_configurator` writes it; this tool reads and transplants
it.

```
python python\elrs_cfg_tool.py inspect firmware-superx-nano-rxspectrum-bound.bin
python python\elrs_cfg_tool.py graft <src_with_config.bin> <dst_raw.bin> <out.bin>
```

- **`inspect`** dumps the embedded options (`uid`, `wifi-on-interval`, `domain`,
  `lock-on-first-connection`) and the key hardware-layout pins — use it to verify a
  built image before flashing.
- **`graft`** copies the entire config block from `src` onto `dst`'s image and writes
  `out`. This is how you reproduce a known-good bind + WiFi + domain + layout without
  the plaintext binding phrase: **a phrase is stored one-way as its derived `uid`**,
  so you can't recover the phrase — but you can graft the exact config block from a
  working Configurator-built image onto a freshly built scanner app. The result is
  byte-identical in the config region (verify with `inspect` + a hash of the tail).

> Note: `find_firmware_end` and the 128/16/512/2048 layout mirror
> `python/UnifiedConfiguration.py` (kept standalone to dodge the same circular import
> as §2). If the upstream image/config format ever changes, keep this in sync.

---

## 4. `build_superx_nano.sh` — build + bake

```
./build_superx_nano.sh                 # scanner image (RX_SPECTRUM_SCAN), no baked phrase
FEATURE= ./build_superx_nano.sh        # STOCK image (no scanner) -- for recovery
PHRASE="my binding phrase" ./build_superx_nano.sh
```

Builds the correct C3/LR1121 RX env and bakes the SuperX Nano layout into the image,
so the result is complete (LED, RF-switch, dual-radio pins, power tables) — not a
"bare" image. It writes an isolated `super_defines.txt` for the build and **always
restores** your original on exit (trap), so it can't leak TX flags in or clobber your
config.

Overridable env vars: `FEATURE` (default `RX_SPECTRUM_SCAN`; empty → stock),
`DOMAIN` (`FCC_915`), `HW_TARGET` (`betafpv.rx_dual.superx-nano`), `ENV_NAME`
(`Unified_ESP32C3_LR1121_RX_via_UART`), `PHRASE` (default none), `OUT`.

Output: `firmware-superx-nano-rxspectrum.bin` (feature) or `firmware-superx-nano.bin`
(stock). Run it from Git Bash: `& "C:\Program Files\Git\bin\bash.exe" -c "FEATURE= ./build_superx_nano.sh"`
(note: PowerShell's `$env:FEATURE = ""` *deletes* the var — pass `FEATURE=` inside
bash's own syntax to get the stock build).

---

## 5. Flashing

### WiFi (simplest, once the RX runs a working image)
WiFi OTA replaces only the app partition — fine once a correct bootloader/partition
table is in place. Requirements: the RX must be **bound** (a UID) and the **TX off**,
or it stays in binding mode and never auto-starts the AP. Join SSID `ExpressLRS RX`
(pw `expresslrs`), browse to `http://10.0.0.1`, and upload the baked `.bin`.

### Full flash + erase over passthrough (dead-app recovery)
When the app won't boot, the Configurator's Betaflight-passthrough path can't help —
it's app-only (writes just `0x10000`) and needs a live app to self-reboot into the
bootloader. Recover with esptool directly, holding the RX **BOOT** button:

1. Build the image (§4).
2. Power up with **BOOT held** → the C3 waits in ROM download mode. Release BOOT.
3. Enable passthrough (§2): `python python\bf_passthrough_min.py COM6 420000`.
4. Probe (read-only, confirms the bootloader is reachable):
   ```
   python python\external\esptool\esptool.py --passthrough --chip esp32c3 --port COM6 \
     --baud 420000 --before no_reset --after no_reset flash_id
   ```
   Expect `Chip is ESP32-C3` + `Detected flash size`.
5. Full flash + erase (all four regions):
   ```
   $B = ".pio\build\Unified_ESP32C3_LR1121_RX_via_UART"
   python python\external\esptool\esptool.py --passthrough --chip esp32c3 --port COM6 \
     --baud 420000 --before no_reset --after hard_reset write_flash --erase-all -z \
     --flash_mode dio --flash_freq 40m --flash_size detect \
     0x0 "$B\bootloader.bin" 0x8000 "$B\partitions.bin" 0xe000 "$B\boot_app0.bin" \
     0x10000 firmware-superx-nano.bin
   ```
6. Power-cycle (without BOOT). A stock image boots to the disconnected blink; an
   unbound image shows the orange 2× "binding mode" blink until bound.

> The three `firmware-superx-nano*.bin` artifacts are **not committed** — they are
> build output. Rebuild them with §4.
