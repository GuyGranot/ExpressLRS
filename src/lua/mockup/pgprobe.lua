-- pgprobe.lua  --  page-event probe for the RadioMaster Boxer (EdgeTX B&W)
-- Install: copy to  SD:/SCRIPTS/TOOLS/pgprobe.lua  then open  SYS -> Tools -> "Page Probe"
-- Purpose: find out which physical buttons emit EVT_VIRTUAL_NEXT_PAGE / PREV_PAGE
--          (the events the spectrum band-switch relies on). Press every button.
-- Exit: long-press RTN/EXIT.

-- Candidate events, resolved from the running firmware (nil-safe: a symbol
-- absent on this EdgeTX build simply won't appear).
local function E(name) return _G[name] end
local WATCH = {
  { "NEXT_PAGE",  E("EVT_VIRTUAL_NEXT_PAGE") },
  { "PREV_PAGE",  E("EVT_VIRTUAL_PREV_PAGE") },
  { "ENTER",      E("EVT_VIRTUAL_ENTER") },
  { "ENTER_LONG", E("EVT_VIRTUAL_ENTER_LONG") },
  { "MENU",       E("EVT_VIRTUAL_MENU") },
  { "MENU_LONG",  E("EVT_VIRTUAL_MENU_LONG") },
  { "NEXT",       E("EVT_VIRTUAL_NEXT") },
  { "PREV",       E("EVT_VIRTUAL_PREV") },
  { "EXIT",       E("EVT_VIRTUAL_EXIT") },
}

local seen = {}          -- name -> true, once its event has fired
local lastEvt = 0        -- last raw event code
local lastName = "-"     -- last matched name (or "?" if unmapped)
local count = 0          -- total key events observed

local function nameFor(evt)
  for _, w in ipairs(WATCH) do
    if w[2] ~= nil and evt == w[2] then return w[1] end
  end
  return "?"
end

local function run(event)
  if event ~= nil and event ~= 0 then
    count = count + 1
    lastEvt = event
    lastName = nameFor(event)
    if lastName ~= "?" then seen[lastName] = true end
    -- long-press EXIT quits
    if EVT_VIRTUAL_EXIT_LONG ~= nil and event == EVT_VIRTUAL_EXIT_LONG then
      return 2
    end
  end

  lcd.clear()
  lcd.drawText(0, 0, "PAGE PROBE  press all buttons", SMLSIZE)
  lcd.drawText(0, 9, "last: 0x" .. string.format("%X", lastEvt)
                     .. " " .. lastName .. " (#" .. count .. ")", SMLSIZE)

  -- checklist: which watched events have fired at least once
  local x, y = 0, 20
  for _, w in ipairs(WATCH) do
    local mark
    if w[2] == nil then mark = "n/a"
    elseif seen[w[1]] then mark = "YES"
    else mark = " . " end
    lcd.drawText(x, y, "[" .. mark .. "] " .. w[1], SMLSIZE)
    y = y + 9
    if y > 55 then x = 74; y = 20 end   -- second column
  end

  lcd.drawText(0, 57, "RTN-long = exit", SMLSIZE)
  return 0
end

return { run = run }
