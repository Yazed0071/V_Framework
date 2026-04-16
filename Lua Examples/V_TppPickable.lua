-- V_TppPickable.lua
-- V_FrameWork example: Pickable item count overrides
--
-- Shows how to override the countRaw (stack size) of pickable items at
-- specific locator slots. This changes how many items are available to pick
-- up at a given locator index in the mission.
--
-- locatorName: in this context, this is the locator's index (integer) within
--   the TppPickable runtime table — not a string name. The index corresponds
--   to the order in which pickable locators are registered at mission load.
--
-- countRaw: the raw item count (integer). The game interprets this as the
--   number of pickable units available at that locator.

local this = {}

-- Override the pickable item count at a specific locator index.
-- locatorName: locator index (integer, 0-based).
-- countRaw: item count to set (integer).
-- Returns true on success, false if the locator index is out of range.
function this.SetCountRaw(locatorName, countRaw)
    return V_FrameWork.SetPickableCountRawByIndex(locatorName, countRaw)
end

-- Get the current pickable item count at a specific locator index.
-- locatorName: locator index (integer, 0-based).
-- Returns the count as a number, or false if the locator index is out of range.
function this.GetCountRaw(locatorName)
    return V_FrameWork.GetPickableCountRawByIndex(locatorName)
end

return this

--[[
Example usage:

local Pickable = require("V_TppPickable")

-- Set the pickable count at locator index 0 to 5
local ok = Pickable.SetCountRaw(0, 5)
if ok then
    InfCore.Log("Set pickable count at index 0 to 5")
end

-- Read back the count at locator index 0
local count = Pickable.GetCountRaw(0)
if count then
    InfCore.Log("Count at index 0: " .. tostring(count))
end
]]
