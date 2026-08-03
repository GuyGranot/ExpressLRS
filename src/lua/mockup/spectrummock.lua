-- Fake TX for the spectrum view, simulator only (loaded by elrs.lua's setMock).
-- Replays firmware-encoder frames (spectrumgolden.lua) through elrs.lua's own
-- decoder and the real click -> popup -> plot -> exit path.

local golden = loadScript("mockup/spectrumgolden.lua")
if golden == nil then return end
golden = golden()

-- Must match the Start Scan command field in elrsmock.lua; Next Source is the
-- field after it, as on the module.
local SCAN_FIELD_ID = 28

local FRAME_TICKS = 5 -- getTime() is 10ms ticks; SPECTRUM_SWEEP_EMIT_INTERVAL_MS 50

-- The TX keeps sweeping until its reboot lands ~400ms after lcsCancel, so ~8
-- frames arrive after RTN -- the case elrs.lua's fieldPopup entry guard exists for.
local REBOOT_TICKS = 40

-- Two bands streamed one at a time, the cross-band model. Falls back to the
-- 2.4 set if an older spectrumgolden.lua without frames900 is on the card.
local bands = { golden.frames, golden.frames900 or golden.frames }
local curBand = 1
local frameIdx = 0

-- Dev hook: swap in the two traces of one band, what a same-band dual-radio TX
-- streams when comparing antennas.
local function mockCompareMode()
  bands = { golden.framesCmpA, golden.framesCmpB }
  curBand = 1
  frameIdx = 0
end

local streaming = false
local nextFrameAt = 0
local stopAt = nil

-- Intercept both directions; elrs.lua calls these as globals.
crossfireTelemetryPush = function(command, data)
  if command ~= 0x2D then return end
  if data[3] == SCAN_FIELD_ID and data[4] == 1 then
    streaming = true -- lcsClick: start, or reset max-hold mid-scan
    stopAt = nil
    nextFrameAt = 0
  elseif data[3] == SCAN_FIELD_ID + 1 and data[4] == 1 then
    curBand = (curBand == 1) and 2 or 1 -- Next Source; a band flip restarts cold
    frameIdx = 0
    nextFrameAt = 0
  elseif data[3] == SCAN_FIELD_ID and data[4] == 5 then
    stopAt = getTime() + REBOOT_TICKS -- lcsCancel: reboot lands in ~400ms
  end
end

crossfireTelemetryPop = function()
  if not streaming then return end
  if stopAt ~= nil and getTime() > stopAt then
    streaming = false
    stopAt = nil
    return
  end
  -- nil when no frame is due is required: refreshNext drains until nil
  local now = getTime()
  if now < nextFrameAt then return end
  nextFrameAt = now + FRAME_TICKS
  local frames = bands[curBand]
  frameIdx = (frameIdx % #frames) + 1
  return 0x83, frames[frameIdx] -- CRSF_FRAMETYPE_ELRS_VENDOR, PROVISIONAL value
end
