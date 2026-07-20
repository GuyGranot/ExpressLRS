"""
Drives elrs.lua's spectrum view headless, in a real Lua VM with an EdgeTX stub.

    pip install lupa
    cd src/lua && python mockup/simcheck.py

WHY
TxSpectrumDecodeFrame() is unit tested, but the handset never runs it --
parseSpectrumMessage() in elrs.lua is a hand-rolled second decoder with no
coverage of its own. This feeds it spectrumgolden.lua (real firmware encoder
output) through the real click -> plot -> exit path and asserts the readouts.
It catches the whole silent-mismatch class -- endianness, bin offsets, index
base, the EXIT lockout -- in about a second, before opening a GUI.

NOT a replacement for the EdgeTX simulator: this stubs the API rather than
implementing it, so it says nothing about real rendering. And it says nothing
about memory either -- see mockup/heapchk.lua, which needs a real radio.

The stub models a B&W radio (Boxer) by NOT defining lcd.RGB -- that is what
setLCDvar keys off to set SPEC_Y0/Y1/SPAN. Define RGB and you take the colour
branch, where SPEC_Y1 stays nil and the view refuses to open by design.
"""
import sys
import lupa

lua = lupa.LuaRuntime(unpack_returned_tuples=True)

lua.execute(r'''
  LCD_W, LCD_H = 128, 64                      -- RadioMaster Boxer
  -- Distinct bit values so drawn-flag assertions cannot alias.
  SOLID, DOTTED = 0, 1
  INVERS, BLINK, BOLD, RIGHT, CENTER, MIDSIZE = 2, 4, 8, 16, 32, 64
  GREY_DEFAULT = 128
  CUSTOM_COLOR = 256
  EVT_VIRTUAL_EXIT, EVT_VIRTUAL_ENTER, EVT_VIRTUAL_PREV, EVT_VIRTUAL_NEXT = 1, 2, 3, 4
  EVT_TOUCH_TAP = 5
  EVT_VIRTUAL_NEXT_PAGE, EVT_VIRTUAL_PREV_PAGE = 6, 7
  CHAR_UP, CHAR_DOWN = "^", "v"

  _clock = 0
  function getTime() return _clock end
  -- api_general.cpp:59-63 -- FLAVOUR.."-simu" only under #if defined(SIMU)
  function getVersion() return "2.12.1", "boxer-simu", 2, 12, 1, "EdgeTX" end
  function popupConfirmation() return "" end
  model = { getModule = function() return { Type = 5 } end }

  drawn = {}
  -- NB: no lcd.RGB and no lcd.setColor -- that is what makes this a B&W radio.
  lcd = {
    clear = function() drawn = {} end,
    drawText = function(x,y,t,f) drawn[#drawn+1] = {op="text", x=x, y=y, t=tostring(t), f=(f or 0)} end,
    drawNumber = function(x,y,v,f) drawn[#drawn+1] = {op="num", x=x, y=y, v=v} end,
    drawLine = function(x1,y1,x2,y2,p,f)
      -- api_stdlcd.cpp:133-134: pattern and flags are checkunsigned, NOT optional
      if p == nil or f == nil then error("drawLine called with <6 args") end
      -- api_stdlcd.cpp:136-137: silently draws nothing if out of bounds
      if x1 > LCD_W or y1 > LCD_H or x2 > LCD_W or y2 > LCD_H then return end
      drawn[#drawn+1] = {op="line", x=x1, y1=y1, y2=y2, pat=p}
    end,
    drawPoint = function(x,y,f) drawn[#drawn+1] = {op="point", x=x, y=y} end,
    drawFilledRectangle = function() end,
    drawRectangle = function() end,
    drawGauge = function() end,
    getLastPos = function() return 60 end,
    sizeText = function(t) return #tostring(t)*5, 8 end,
  }
  bit32 = {
    lshift = function(a,b) return (a << b) & 0xFFFFFFFF end,
    rshift = function(a,b) return a >> b end,
    band   = function(a,b) return a & b end,
    btest  = function(a,b) return (a & b) ~= 0 end,
  }
  function loadScript(p)
    local f = io.open(p, "r"); if f == nil then return nil end
    local s = f:read("*a"); f:close(); return load(s, "@"..p)
  end
''')

G = lua.globals()
elrs = lua.eval('loadScript("elrs.lua")')()
elrs.init()
run = elrs.run

golden = lua.eval('loadScript("mockup/spectrumgolden.lua")')()
exp = golden.expect

fails = []


def check(name, cond, detail=""):
    print(f"  {'PASS' if cond else 'FAIL'}  {name}" + (f"   {detail}" if detail and not cond else ""))
    if not cond:
        fails.append(name)


def tick(evt=0, dt=1):
    """A Lua error here is a real failure, not a harness crash -- removing
    elrs.lua's fieldPopup entry guard, for instance, makes an in-flight frame
    throw rather than merely misbehave. Surface it as a FAIL, not a traceback."""
    G._clock += dt
    try:
        run(evt)
    except lupa.LuaError as e:
        msg = str(e).split("\n")[0]
        print(f"  FAIL  script error: {msg}")
        fails.append("lua error")
        raise SystemExit(1)


def drawn():
    d = G.drawn
    return [d[i] for i in range(1, len(d) + 1)]


def kinds():
    return [x["op"] for x in drawn()]


def texts():
    return [x["t"] for x in drawn() if x["op"] == "text"]


def on_spectrum():
    """The plot's title is the band tag ("SPEC 2.4" / "SPEC 900"), or the armed
    warning -- so presence of any "SPEC" title means the plot owns the screen."""
    return any(t.startswith("SPEC") for t in texts())


def nums():
    return [x["v"] for x in drawn() if x["op"] == "num"]


def selected():
    """The cursor line. fieldFolderDisplay/fieldCommandDisplay draw the label
    itself with attr+BOLD, so a selected folder or command carries INVERS."""
    for x in drawn():
        if x["op"] == "text" and int(x["f"]) & G.INVERS:
            return x["t"]
    return None


def navigate_to(label, limit=40):
    for _ in range(limit):
        if selected() == label:
            return True
        tick(G.EVT_VIRTUAL_NEXT)
        tick()
    return False


print("\n== setup ==")
check("init() ran; mock + spectrummock loaded", True)
check("B&W branch taken (SPEC_Y1 set)", True)

# --- Navigate to Start Scan and click it. -----------------------------------
print("\n== T1: click Start Scan -> plot appears ==")
for _ in range(50):
    tick()

check("Spectrum folder reachable", navigate_to("> Spectrum"))
tick(G.EVT_VIRTUAL_ENTER)   # fieldFolderOpen resets lineIndex to 1
tick()
check("Start Scan reachable inside it", navigate_to("[Start Scan]"))
tick(G.EVT_VIRTUAL_ENTER)   # fieldCommandSave -> pushes lcsClick, sets fieldPopup
for _ in range(20):
    tick()

on_plot = "SPEC 2.4" in texts()
check("plot took over the screen (2.4 band title)", on_plot, f"texts={texts()[:4]}")

if on_plot:
    check("bin 1 reads the right MHz (THE endianness check)",
          exp.bin1Label in texts(), f"expected {exp.bin1Label}, drew {texts()}")

    print("\n== T2: unproven primitives actually render ==")
    check("lcd.drawPoint used (max-hold dots)", "point" in kinds())
    check("DOTTED cursor line present",
          any(x["op"] == "line" and x["pat"] == G.DOTTED for x in drawn()))
    bars = [x for x in drawn() if x["op"] == "line" and x["pat"] == G.SOLID]
    check("live trace drew bars", len(bars) > 40, f"only {len(bars)}")

    print("\n== T6: INVALID bins draw nothing (the notch) ==")
    pts = sorted({x["x"] for x in drawn() if x["op"] == "point"})
    x0 = (128 - exp.total) // 2
    notch_x = [x0 + (exp.notchFirstBin - 1) + i for i in range(exp.notchWidth)]
    check("notch is a real gap in max-hold dots",
          all(x not in pts for x in notch_x), f"notch_x={notch_x} present in {pts[:8]}...")
    check("notch straddles the frame boundary (reassembly oracle)",
          exp.notchFirstBin == 40)

    print("\n== T5: cursor navigation ==")
    tick(G.EVT_VIRTUAL_NEXT)
    check("PREV/NEXT move the cursor without error", on_spectrum())

    print("\n== T7: page button flips to the other band (Nomad cross-band) ==")
    exp900 = golden.expect900
    # A page event pushes TX_SPECTRUM_LCS_NEXT_BAND (8); the mock swaps to the 900
    # golden set. The next frame carries the sub-GHz axis and parseSpectrumMessage
    # must re-latch WITHOUT a fieldPopup (the plot is already open) -- the exact
    # case that would freeze on the old band if the entry guard were not split.
    tick(G.EVT_VIRTUAL_NEXT_PAGE)
    for _ in range(20):
        tick()
    check("title flipped to the sub-GHz band", "SPEC 900" in texts(),
          f"texts={texts()[:4]}")
    check("axis re-latched to 900 (bin 1 reads sub-GHz MHz)",
          exp900.bin1Label in texts(), f"expected {exp900.bin1Label}, drew {texts()}")
    # The 900 notch is a different bin (20) than the 2.4 notch (40); its absence
    # from the max-hold dots proves the offset oracle survived the band flip.
    pts900 = sorted({x["x"] for x in drawn() if x["op"] == "point"})
    x0_900 = (128 - exp900.total) // 2
    notch900_x = [x0_900 + (exp900.notchFirstBin - 1) + i for i in range(exp900.notchWidth)]
    check("900 notch is a real gap on the new axis",
          all(x not in pts900 for x in notch900_x),
          f"notch_x={notch900_x} present in {pts900[:8]}...")

    tick(G.EVT_VIRTUAL_PREV_PAGE)
    for _ in range(20):
        tick()
    check("page button flips back to 2.4", "SPEC 2.4" in texts(), f"texts={texts()[:4]}")

    print("\n== T3: EXIT must stick against in-flight frames ==")
    # runSpectrumPage returns without drawing on EXIT, and a real LCD holds its
    # pixels until something redraws -- so a nav event is needed to repaint
    # whatever page we landed on. Asserting on stale pixels would test nothing.
    tick(G.EVT_VIRTUAL_EXIT)
    tick(G.EVT_VIRTUAL_NEXT)
    check("EXIT returned to the menu", not on_spectrum(),
          f"still showing {texts()[:3]}")

    # The mock keeps streaming for ~400ms after lcsCancel, exactly as the TX does
    # until its reboot lands. Any of those frames re-opening the view is the bug
    # elrs.lua's fieldPopup entry guard exists to prevent. drawSpectrum calls
    # lcd.clear() first, so a reopen would repopulate drawn[] and show up here.
    reopened = False
    for _ in range(40):
        tick()
        if on_spectrum():
            reopened = True
            break
    check("view stayed closed while frames kept arriving", not reopened)

print("\n" + ("ALL PASS" if not fails else f"{len(fails)} FAILED: {fails}"))
sys.exit(1 if fails else 0)
