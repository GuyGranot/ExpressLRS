-- Measures elrs.lua's Lua heap cost on a real handset. Development tool; users
-- do not need this file.
--
-- WHY THIS EXISTS
-- elrs.lua is the one component of the TX spectrum feature that cannot be put
-- behind a build flag -- it is an SD-card file, so it ships to every handset
-- regardless of firmware and outlives a firmware rollback. The spectrum view
-- grew it ~31%, and the script's own comments already warn it "runs near the
-- Lua heap limit". This measures whether that is still true.
--
-- WHY THE SIMULATOR CANNOT ANSWER THIS
-- radio/src/CMakeLists.txt:573-575 returns for NATIVE_BUILD *before* :618 adds
-- -DUSE_CUSTOM_ALLOCATOR, so the simulator never compiles custom_allocator.cpp
-- and Lua falls through to luaL_newstate() -- plain desktop malloc, gigabytes.
-- LUA_MEM_MAX is 0 ("unlimited") on B&W targets in both builds, so there is no
-- soft cap standing in either. The simulator is strictly more permissive on
-- memory in every dimension. This must run on the radio.
--
-- WHY NOT THE BUILT-IN READOUT
-- EdgeTX's "GV Use: <n>b" display (lua/interface.cpp:1205-1212) is wrapped in
-- #if defined(KEYS_GPIO_REG_MENU), and the Boxer has no MENU key -- it is
-- compiled out. collectgarbage("count") is the only readout available here.
--
-- USE
--   1. Copy this file to SCRIPTS/TOOLS/heapchk.lua (TOOLS root -- EdgeTX only
--      enumerates the top level, so it will not appear from a subdirectory).
--   2. Put the modified elrs.lua at SCRIPTS/TOOLS/elrs.lua and a pristine copy
--      at SCRIPTS/TOOLS/elrs_stock.lua.
--   3. Run this from the Tools menu. It reports both and the delta.
-- Stock firmware is fine and preferred -- this measures the script, not the TX.

local ELRS_MOD = "/SCRIPTS/TOOLS/elrs.lua"
local ELRS_STOCK = "/SCRIPTS/TOOLS/elrs_stock.lua"

local results = nil

-- Returns compileKB, loadedKB -- or nil if the script would not load at all,
-- which is itself the answer we are looking for.
local function measure(path)
  collectgarbage("collect")
  local before = collectgarbage("count")

  -- "T" forces the SOURCE to be parsed. Without it the radio's default mode is
  -- "bt" -- binary or text, newer wins, binary preferred on a tie -- so a stale
  -- elrs.luac sitting next to elrs.lua would be measured instead, and we would
  -- report the load cost of the wrong script. It also keeps the comparison
  -- apples-to-apples: a freshly copied elrs_stock.lua has no .luac to prefer.
  local chunk = loadScript(path, "T")
  if chunk == nil then return nil end
  collectgarbage("collect")
  local compiled = collectgarbage("count")

  -- Running the chunk defines every local and closure. 'loaded' is the resident
  -- cost and the one that matters -- measured on a Boxer it comes out ABOVE
  -- 'compiled' (50.0 vs 45.2), so bytecode alone understates the script.
  --
  -- Neither figure captures the parser's transient peak, which is freed before
  -- 'compiled' is read. That peak is what a pre-generated .luac would avoid --
  -- but it is also self-evidently survivable, since loadScript() above returned.
  -- If you are here because a script would not load, that transient is the
  -- suspect and .luac is the fix.
  local t = chunk()
  collectgarbage("collect")
  local loaded = collectgarbage("count")

  -- Drop both so the next measurement starts from the same baseline.
  chunk, t = nil, nil
  collectgarbage("collect")

  return compiled - before, loaded - before
end

local function gather()
  local r = { }
  r.modCompile, r.modLoaded = measure(ELRS_MOD)
  r.stockCompile, r.stockLoaded = measure(ELRS_STOCK)
  collectgarbage("collect")
  r.baseline = collectgarbage("count")
  return r
end

local function kb(v)
  if v == nil then return "--" end
  return string.format("%.1fKB", v)
end

local function run(event)
  if event == nil then return 2 end
  if results == nil then results = gather() end
  local r = results

  lcd.clear()
  lcd.drawText(0, 0, "elrs.lua Lua heap", INVERS)

  local y = 10
  if r.modLoaded == nil then
    lcd.drawText(0, y, "MODIFIED: LOAD FAILED")
    y = y + 9
    lcd.drawText(0, y, "(too big, or a syntax err)")
  else
    lcd.drawText(0, y, "mod   " .. kb(r.modCompile) .. " / " .. kb(r.modLoaded))
    y = y + 8
    lcd.drawText(0, y, "stock " .. kb(r.stockCompile) .. " / " .. kb(r.stockLoaded))
    y = y + 8
    if r.stockLoaded ~= nil then
      lcd.drawText(0, y, "delta " .. kb(r.modLoaded - r.stockLoaded), INVERS)
    else
      lcd.drawText(0, y, "(no elrs_stock.lua)")
    end
    y = y + 9
    lcd.drawText(0, y, "compile / loaded")
  end

  y = y + 9
  lcd.drawText(0, y, "vm now " .. kb(r.baseline))

  return event == EVT_VIRTUAL_EXIT and 1 or 0
end

return { run = run }
