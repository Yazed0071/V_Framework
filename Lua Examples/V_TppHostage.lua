-- V_TppHostage.lua
-- V_FrameWork example: Hostage tracking
--
-- Shows how to register, update, and clear lost hostage tracking.
-- "Lost hostages" are NPCs the game tracks as hostages that must be rescued
-- or protected. V_FrameWork extends the vanilla system to let mods add custom
-- hostages from Lua.
--
-- hostageNameOrId: the hostage's gameObjectId (uint32) obtained at runtime,
--   typically from an OnMessage handler for the hostage NPC's init message.
--
-- Gender values:
--   0 = male
--   1 = female
--   2 = child

local this = {}

-- Register a lost hostage for tracking.
-- The game will treat this NPC as a mission hostage (discovery, trap logic, etc).
-- hostageNameOrId: the hostage's gameObjectId (uint32).
-- gender: 0 = male, 1 = female, 2 = child.
function this.SetLostHostage(hostageNameOrId, gender)
    V_FrameWork.SetLostHostage(hostageNameOrId, gender)
end

-- Stop tracking a previously registered hostage.
-- hostageNameOrId: the hostage's gameObjectId (uint32).
function this.RemoveLostHostage(hostageNameOrId)
    V_FrameWork.RemoveLostHostage(hostageNameOrId)
end

-- Clear all registered lost hostages.
function this.ClearLostHostages()
    V_FrameWork.ClearLostHostages()
end

-- Mark whether the player was responsible for taking this hostage.
-- Used to correctly attribute hostage outcomes in mission scoring.
-- hostageNameOrId: the hostage's gameObjectId (uint32).
-- enable: true if the player took the hostage, false otherwise.
function this.SetLostHostageFromPlayer(hostageNameOrId, enable)
    V_FrameWork.SetLostHostageFromPlayer(hostageNameOrId, enable)
end

return this

--[[
Example usage (called from an OnMessage handler after the NPC is initialized):

local Hostage = require("V_TppHostage")

function this.OnMessage(messageId, args)
    if messageId == Fox.StrCode32("TppHostage2.HostageInitialize") then
        local id = args.gameObjectId

        -- Register this NPC as a male hostage
        Hostage.SetLostHostage(id, 0)

        -- Mark that the player is responsible for this hostage
        Hostage.SetLostHostageFromPlayer(id, true)
    end
end

-- At mission end or mod teardown, clear all registrations
Hostage.ClearLostHostages()
]]
