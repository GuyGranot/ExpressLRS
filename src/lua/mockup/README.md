This file is for development purposes only and provides field definitions for filling the screen in the OpenTX Companion Simulator. Users do not need this file on their handset's SD card!

### Using
Copy the entire mockup directory into the same directory as `elrsV2.lua` on your hard drive where you've set the OpenTX Companion "SD Structure Path". The SD structure path should look like this:
```
SCRIPTS/TOOLS/elrsV2.lua
SCRIPTS/TOOLS/mockup/elrsmock.lua
SCRIPTS/TOOLS/mockup/README.md <- this file
```
When you execute elrsV2.lua, the screen will be populated with fake ELRS Lua config fields.

---

## TX spectrum analyzer

`TxSpectrumDecodeFrame()` is unit tested, but **the handset never runs it** —
`parseSpectrumMessage()` in `elrs.lua` is a hand-rolled *second* decoder. The files below
exist to compare the two, since nothing else does.

| File | Runs on | Proves |
|---|---|---|
| `gen_spectrumgolden.cpp` | desktop (g++) | generates the golden frames from the **real** firmware encoder |
| `spectrumgolden.lua` | — | generated output; checked in so the sim works out of the box |
| `spectrummock.lua` | simulator | fake TX: replays the golden through the real click → plot → exit path |
| `simcheck.py` | desktop (python) | drives the whole Lua path headless, with assertions |
| `heapchk.lua` | **real radio** | Lua heap cost — the one thing the simulator cannot measure |

### spectrumgolden.lua — regenerating

The frames come out of `TxSpectrumEncodeFrame()` itself. That provenance is the point:
hand-written frames would encode someone's *reading* of the wire format, which is the thing
under test. Regenerate after **any** wire-format change:
```
cd src/lua/mockup
g++ -I../../lib/TxSpectrum -o gen_spectrumgolden gen_spectrumgolden.cpp
./gen_spectrumgolden > spectrumgolden.lua
```
`test_golden_vector_frame_is_pinned` (in `test/test_txspectrum/`) fails if you forget — the
golden is a checked-in file, so nothing else would notice it going stale.

### simcheck.py — headless pre-flight

Drives `elrs.lua` in a real Lua VM with an EdgeTX stand-in. Not a replacement for the
Companion simulator — it stubs the API rather than implementing it — but it runs in a second
and catches the whole silent-mismatch class (endianness, bin offsets, index base, the EXIT
lockout) before you open a GUI.
```
pip install lupa
cd src/lua && python mockup/simcheck.py
```

### Boxer / Companion simulator

Colour handsets **cannot** show this view by construction: `setLCDvar` leaves `SPEC_Y1` nil on
colour, and `parseSpectrumMessage` refuses to enter. Use a B&W profile (Boxer). Navigate to
`Spectrum` → `Start Scan`. What to look for:

- **Bin 1 reads 2400MHz.** The decisive check — an endianness or index-base error reads
  ~2426414080 instead. There is no subtle failure mode here.
- A clean diagonal ramp with a **3-bin gap at bin 40** (2439MHz). The gap straddles the
  40-bin frame boundary on purpose, so it also proves multi-frame reassembly.
- Max-hold **dots** ride 5dB above the bars — that is `lcd.drawPoint`, one of only two
  EdgeTX primitives in this view with no precedent elsewhere in `elrs.lua` (the other is the
  `DOTTED` cursor line).
- **RTN closes the plot and it stays closed**, even though the mock keeps streaming for
  ~400ms afterwards — exactly as the TX does until its reboot lands.

Known artifact, not a bug: RTN calls `reloadAllField()`, which queues re-reads that no
firmware will answer, so the title keeps a loading gauge.

### heapchk.lua — the one that needs real hardware

Measures `elrs.lua`'s Lua heap cost on a **real handset**. The simulator cannot answer this:
it never compiles `custom_allocator.cpp` (`CMakeLists.txt:573-575` returns before
`-DUSE_CUSTOM_ALLOCATOR`), so Lua falls back to plain desktop `malloc` — no CCM, no ceiling.
`LUA_MEM_MAX` is 0 ("unlimited") on B&W targets in both builds, so there is no soft cap
standing in either. See `lib/TxSpectrum/DESIGN.md` 4.

Copy it to the TOOLS **root** (EdgeTX only enumerates the top level, so it will not appear
from a subdirectory), alongside a pristine `elrs_stock.lua` to compare against:
```
SCRIPTS/TOOLS/heapchk.lua
SCRIPTS/TOOLS/elrs.lua        <- the version under test
SCRIPTS/TOOLS/elrs_stock.lua  <- a pristine copy to diff against
```
Stock firmware is fine and preferred — this measures the script, not the TX.