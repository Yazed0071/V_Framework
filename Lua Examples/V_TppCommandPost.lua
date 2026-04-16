-- V_TppCommandPost.lua
-- V_FrameWork example: Caution phase duration override
--
-- Shows how to get, set, and clear the caution (alert) phase timer.
-- The caution phase is the countdown soldiers run after losing sight of the
-- player before returning to normal patrol.
--
-- SetCautionPhaseDuration persists until you call UnsetCautionPhaseDuration
-- or the mod is unloaded. It does not need to be set every frame.

local this = {}

-- Set a custom caution phase duration in seconds.
-- Replaces the vanilla hardcoded timer for all caution phases.
-- seconds: positive float (e.g. 30.0 for 30 seconds)
function this.SetCautionPhaseDuration(seconds)
    V_FrameWork.SetCautionStepNormalDurationSeconds(seconds)
end

-- Get the currently active caution phase duration in seconds.
-- Returns the overridden value, or the vanilla default if no override is set.
function this.GetCautionPhaseDuration()
    return V_FrameWork.GetCautionStepNormalDurationSeconds()
end

-- Clear the custom caution phase duration, restoring vanilla behavior.
function this.UnsetCautionPhaseDuration()
    V_FrameWork.UnsetCautionStepNormalDurationSeconds()
end

-- Get the remaining time in the current caution phase in seconds.
-- Returns 0 if not currently in a caution phase.
function this.GetRemainingCautionPhaseTime()
    return V_FrameWork.GetCautionStepNormalRemainingSeconds()
end

return this

--[[
Example usage:

local CommandPost = require("V_TppCommandPost")

-- Give players 60 seconds before soldiers relax
CommandPost.SetCautionPhaseDuration(60.0)

-- Check how much time is left mid-mission
local remaining = CommandPost.GetRemainingCautionPhaseTime()
InfCore.Log("Caution time remaining: " .. tostring(remaining))

-- Restore vanilla duration
CommandPost.UnsetCautionPhaseDuration()
]]
