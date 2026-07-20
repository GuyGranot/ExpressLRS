# Building & Flashing the TX Spectrum Analyzer

How to build and flash the `-DTX_SPECTRUM_SCAN` feature for each supported TX, on the
**ELRS 4.1.0** base. For what the feature is and why it's safe, see [`DESIGN.md`](./DESIGN.md);
for validation history see [`STATUS.md`](./STATUS.md). This document is the practical runbook.

For the measurement engine itself — the per-bin cycle, every driver intervention point, and
what to change when porting to another radio family — see [`DESIGN.md`](./DESIGN.md) §3.3,
§3.9 and §11.

The feature is **one shared codebase, no per-target fork** — the differences between targets are
entirely in *which PlatformIO env you build*, *which regulatory-domain flag you set*, and *which
hardware layout you pick at the build menu*. Everything below follows from that.

---

## 1. Supported targets

| Radio | Bands | Radio chip | PlatformIO env | Sub-GHz? | Build menu pick |
|-------|-------|-----------|----------------|----------|-----------------|
| **BETAFPV SuperG** | 2.4 | dual SX1280 (Gemini) | `Unified_ESP32_2400_TX` | no | `BETAFPV SuperG 2.4GHz Gemini TX` |
| **RadioMaster Boxer internal** | 2.4 | single SX1280 | `Unified_ESP32_2400_TX` | no | `RadioMaster Boxer Internal 2.4GHz TX` |
| **RadioMaster Nomad** | 900 + 2.4 | dual LR1121 | `Unified_ESP32_LR1121_TX` | **yes** | `RadioMaster Nomad 2.4/900 TX` |

All three are hardware-validated end to end. The SuperG exercises the Gemini *split* sweep, the
Boxer the *single-radio* sweep, the Nomad the *cross-band* sweep — between them they cover every
sweep topology the code supports.

---

## 2. Prerequisites

- **PlatformIO Core** (`pio`) in your PATH. This repo's PIO project root is `src/`.
- **Python 3** with `pyserial` (bundled with PlatformIO's penv; the byte-proof script also uses it).
- For flashing: the **ExpressLRS Configurator** (for the Boxer-internal bootstrap) and a device on
  WiFi (for OTA). Details in §5.
- Everything runs from `src/`:
  ```
  cd C:\Users\guygr\Documents\Code\elrs\src
  ```

---

## 3. `super_defines.txt` — the local build config

`src/super_defines.txt` is **gitignored** and is read *after* `user_defines.txt` by
`python/build_flags.py`, so it **overrides** user_defines and is honored by every build (including
the ELRS Configurator's local/source build). It survives `git pull` and is never touched by the
Configurator UI — so it's the robust place for the feature flag and your build-time choices.

### The flags

| Flag | Meaning |
|------|---------|
| `-DTX_SPECTRUM_SCAN` | **The feature.** Enables the TX-side analyzer. Omit it → the firmware is byte-identical to stock (see §6, N0). |
| `-DRegulatory_Domain_*` | **Required.** Selects the band/region — see below. Wrong or missing → build error or wrong band. |
| `-DMY_BINDING_PHRASE="…"` | Optional. Bakes a binding phrase at build time instead of setting it at flash time. |

### Regulatory domain per target

- **2.4-only** (SuperG, Boxer): `-DRegulatory_Domain_ISM_2400` (worldwide) or
  `-DRegulatory_Domain_EU_CE_2400` (EU LBT).
- **Dual-band** (Nomad): set a **sub-GHz** domain — the 2.4 half is implicit. Options include
  `-DRegulatory_Domain_FCC_915` (US), `-DRegulatory_Domain_AU_915`, `-DRegulatory_Domain_EU_868`,
  `-DRegulatory_Domain_IN_866`. **Do not** use `ISM_2400` for the Nomad — the LR1121 sub-GHz build
  needs a sub-GHz domain.

### Ready-to-use `super_defines.txt` per target

**SuperG / Boxer (2.4):**
```
-DTX_SPECTRUM_SCAN
-DRegulatory_Domain_ISM_2400
-DMY_BINDING_PHRASE="your-phrase"
```

**Nomad (cross-band, US example):**
```
-DTX_SPECTRUM_SCAN
-DRegulatory_Domain_FCC_915
-DMY_BINDING_PHRASE="your-phrase"
```

> **Binding phrase:** for a bench test any phrase works (bind your spare RX to the same one). For a
> radio you'll fly, set your real phrase here — it's a local, gitignored file, so it never leaves
> your machine. Switching targets = swap the `Regulatory_Domain_*` line and rebuild.

---

## 4. Building

Same command shape for every target — only the **env** changes. The interactive product menu at the
end is where you pick the **hardware layout** (that's the "which radio" step):

```
pio run -e <ENV>
```

| Target | Command | At the menu, pick |
|--------|---------|-------------------|
| SuperG | `pio run -e Unified_ESP32_2400_TX_via_ETX`  | `BETAFPV SuperG 2.4GHz Gemini TX` |
| Boxer  | `pio run -e Unified_ESP32_2400_TX_via_ETX`  | `RadioMaster Boxer Internal 2.4GHz TX` |
| Nomad  | `pio run -e Unified_ESP32_LR1121_TX_via_ETX`| `RadioMaster Nomad 2.4/900 TX` |

Output: `.pio/build/<ENV>/firmware.bin` — layout-patched and ready to flash.

**The `_via_ETX` / `_via_UART` / `_via_WIFI` suffixes all produce the same `firmware.bin`** — they
differ only in what `pio ... -t upload` does. For WiFi/web-UI flashing (§5) you upload the bin by
hand, so the suffix doesn't matter; `_via_ETX` is fine.

### Two things you'll see — both benign

- **`FAILED` at `UnifiedConfiguration.appendConfiguration` at the very end.** This post-step needs
  an interactive product choice (which you gave) / the `hardware/` layout repo. It runs *after*
  `firmware.bin` is written and does not touch it. If you picked a layout, the bin is complete and
  valid. (Headless builds always hit this — that's expected.)
- Compiler warnings from unrelated upstream files. Not ours.

---

## 5. Flashing

### 5a. SuperG and Nomad — WiFi (easy path)

Both are **external** modules with their own WiFi AP. This is the clean, proven path:

1. Select the module (SuperG/Nomad) as the **external** RF; ELRS Lua → **Enable WiFi Connectivity**.
2. Join the module's AP **`ExpressLRS TX`** (password `expresslrs`).
3. Browse to **`http://10.0.0.1`** (or `http://elrs_tx.local`), upload `firmware.bin`, **Update**,
   let it reboot.
4. Reopen the ELRS Lua → the **Spectrum** folder should appear. (Nomad: with `SPEC 900` / `SPEC 2.4`
   and the page-button band flip.)

### 5b. Boxer internal — the two-step path (READ THIS)

The Boxer's ELRS module is **internal**, and it has a **gotcha that cost hours** the first time:

> **`pio run -e Unified_ESP32_2400_TX_via_ETX -t upload` (esptool full-image passthrough) VERIFIES
> every hash but leaves the module UNBOOTABLE.** It writes its own bootloader + partition table with
> PlatformIO's default flash settings, and that combination doesn't boot on this module. Symptom:
> no CRSF (Lua hangs on "…Internal TX…") and no WiFi. Note the internal bay is CRSF-fed by the
> handset, so it **never auto-WiFis** — "no AP" is a red herring, not proof of a dead app.

**The working path** keeps a known-good bootloader and only swaps the app:

1. **Flash stock 4.1.0 first, via the ExpressLRS Configurator → EdgeTX passthrough** (RadioMaster
   Boxer Internal → 4.1.0). This lands a bootable, known-good image. Confirm the ELRS Lua loads.
2. **Then push the feature build over WiFi OTA** (app-only): ELRS Lua → **Enable WiFi** → join
   `ExpressLRS TX` → `http://10.0.0.1` → upload your `.pio/build/Unified_ESP32_2400_TX_via_ETX/firmware.bin`
   → Update.
3. Reopen the Lua → **Spectrum** folder appears.

You need the SD-card `elrs.lua` to match 4.1.0 (see §7).

#### Windows driver fix (if the passthrough COM port won't open)

The Boxer's USB serial is an STM32 VCP (`VID:PID 0483:5740`). If flashing fails with
`could not open port … FileNotFoundError` or the port shows **Code 10 / Code 28** in Device Manager:

1. Unplug the radio. In an **elevated** PowerShell, purge the failed + phantom nodes:
   ```powershell
   Get-PnpDevice | Where-Object {
     $_.InstanceId -match 'VID_0483&PID_5740' -or $_.FriendlyName -match 'CP210x'
   } | ForEach-Object { pnputil /remove-device "$($_.InstanceId)" }
   ```
   (`Remove-PnpDevice` may not exist on your PowerShell — use `pnputil` as above.)
2. Replug (radio **on**, main screen, pick **USB Serial (VCP)**), and if a device still shows
   **Code 10/28**, Device Manager → that device → **Update driver → Browse → Let me pick →
   USB Serial Device**. A reboot + a rear/motherboard USB port clears stubborn cases.
3. Also set the radio's **SYS → Hardware → USB-VCP → CLI** so the passthrough can reach the EdgeTX
   serial console.

---

## 6. Verifying a build

### Native + simulator (no hardware)
```
pio test -e native -f test_txspectrum -f test_spectrum   # 24/24
cd lua && python mockup/simcheck.py                        # 18/18 (run from src/lua)
```

### N0 — flag-off byte-identical proof (the zero-footprint guarantee)

Proves that **with `-DTX_SPECTRUM_SCAN` off, the flashed image is byte-identical to stock 4.1.0** —
i.e. the feature adds nothing to the shipping firmware unless enabled. Re-run it per target env; the
method is in the `byteproof_*.sh` scripts (session scratchpad):

1. Build the target env **twice at HEAD**: once feature-present-but-flag-off (`super_defines.txt`
   reduced to just the `-DRegulatory_Domain_*` line), once with the ~9 TX-compiled shared files
   reverted to stock `a9d4a9cb`.
2. Pin the random `flash-discriminator` (`build_flags.py`) so both builds share a git-version string.
3. `cmp` the two `firmware.bin`s. The only differing bytes are the two SHA-256 blocks
   (`app_elf_sha256` @176–207 + esptool's trailing whole-image hash) — mask those and the images are
   **byte-identical**. A raw `cmp` always shows ~65 bytes even on a pass (ELF symbol/debug entries
   for the feature files, stripped from the `.bin`); the masked compare is the real test.

Results to date: PASS on all three envs (2400 SX1280, LR1121, and 4.0.1 base). See `STATUS.md`.

---

## 7. Handset side (`elrs.lua`)

The ELRS Lua on the radio's SD card (`\SCRIPTS\TOOLS\`) must **match the firmware version**. Copy
this repo's `src/lua/elrs.lua` onto the card — it's the 4.1.0-merged script (handsetId `0xEA`) **with
the Spectrum UI**, so it both fixes any version mismatch and gives you the Spectrum folder. A stale
script (e.g. 3.x, handsetId `0xEF`) hangs on "Loading…"/"…Internal TX…".

To use the feature: ELRS Lua → **Spectrum** → **Start Scan** (must be **disarmed**). On the Nomad,
the page button flips 900↔2.4. Exit is by reboot.

---

## 8. Safety & rollback

- **Never scan while armed.** The firmware refuses Start Scan while armed and aborts (reboots to a
  live link) if you arm mid-scan. This is a hard interlock — don't rely on remembering.
- **Receive-only.** The scan never keys the PA; a paired RX will failsafe for the duration of a scan
  and re-link on exit. That failsafe *is* the proof it's transmitting nothing.
- **Rollback is clean.** `TX_CONFIG_VERSION` is unchanged from stock, so flashing stock 4.1.0 back on
  (or a flag-off build) boots clean with your config intact and the Spectrum folder gone. Flash stock
  via the same WiFi/Configurator path you used for the feature build.

---

## Quick reference

```
# SuperG (2.4, Gemini)     env=Unified_ESP32_2400_TX_via_ETX    domain=ISM_2400    menu: BETAFPV SuperG
# Boxer  (2.4, 1x SX1280)  env=Unified_ESP32_2400_TX_via_ETX    domain=ISM_2400    menu: RadioMaster Boxer Internal
# Nomad  (900+2.4, LR1121) env=Unified_ESP32_LR1121_TX_via_ETX  domain=FCC_915*    menu: RadioMaster Nomad 2.4/900
#   *sub-GHz domain for your region

cd src
# 1. set super_defines.txt (feature flag + domain + phrase)
# 2. pio run -e <ENV>            → pick the layout at the menu
# 3. flash firmware.bin:
#      SuperG/Nomad → WiFi web-UI (own AP)
#      Boxer        → Configurator/EdgeTX passthrough for STOCK first, then WiFi OTA for the feature build
# 4. copy src/lua/elrs.lua to the SD card \SCRIPTS\TOOLS\
```
