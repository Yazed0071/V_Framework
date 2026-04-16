-- V_TppEnemy.lua
-- V_FrameWork example: Enemy/soldier behavior overrides
--
-- Shows how to mark VIP soldiers, modify holdup reactions, manage call-sign
-- patrol soldiers, and toggle stealth camo on individual soldiers.
--
-- VIP soldier IDs are gameObjectIds (uint32). These are obtained at runtime
-- from the game's soldier tracking system, typically in an OnMessage handler
-- for TppSoldier2.SoldierInitialize or similar messages.
--
-- Example VIP setup with message handler:
--
--   function this.OnMessage(messageId, args)
--       if messageId == Fox.StrCode32("TppSoldier2.SoldierInitialize") then
--           local name = args.name
--           local id   = args.gameObjectId
--           if name == "soldier_vip_01" then
--               V_TppEnemy.SetVIPImportant(id, true)
--           end
--       end
--   end

local this = {}

-- Mark a soldier as a VIP target. VIPs will not be put to sleep/fainted,
-- held up, or called on the radio by normal AI routines.
-- soldierNameOrId: the soldier's gameObjectId (uint32) at runtime.
-- isOfficer: true if the soldier should be treated as an officer VIP.
function this.SetVIPImportant(soldierNameOrId, isOfficer)
    V_FrameWork.SetVIPImportant(soldierNameOrId, isOfficer)
end

-- Remove a soldier from the VIP list.
-- soldierNameOrId: the soldier's gameObjectId (uint32).
function this.RemoveVIPImportant(soldierNameOrId)
    V_FrameWork.RemoveVIPImportant(soldierNameOrId)
end

-- Clear all VIP soldier registrations.
function this.ClearVIPImportant()
    V_FrameWork.ClearVIPImportant()
end

-- Enable or disable the "concerned holdup recovery" behavior.
-- When enabled, soldiers that were held up but not the VIP will use a
-- concerned recovery animation rather than the standard one.
-- enable: true to enable, false to disable.
function this.SetUseConcernedHoldupRecovery(enable)
    V_FrameWork.SetUseConcernedHoldupRecovery(enable)
end

-- Enable or disable cowardly holdup reactions for soldiers.
-- When enabled, soldiers will show a cowardly reaction when held up.
-- enable: true to enable, false to disable.
function this.HoldUpReactionCowardlyReaction(enable)
    V_FrameWork.HoldUpReactionCowardlyReaction(enable)
end

-- Add a soldier to the call-sign patrol list.
-- Soldiers on this list will be included in radio call-sign patrols.
-- soldierNameOrId: the soldier's gameObjectId (uint32).
function this.AddCallSignPatrolSoldier(soldierNameOrId)
    V_FrameWork.AddCallSignPatrolSoldier(soldierNameOrId)
end

-- Remove a soldier from the call-sign patrol list.
-- soldierNameOrId: the soldier's gameObjectId (uint32).
function this.RemoveCallSignPatrolSoldier(soldierNameOrId)
    V_FrameWork.RemoveCallSignPatrolSoldier(soldierNameOrId)
end

-- Clear all soldiers from the call-sign patrol list.
function this.ClearCallSignPatrolSoldiers()
    V_FrameWork.ClearCallSignPatrolSoldiers()
end

-- Enable or disable stealth camo on a single soldier.
-- soldierNameOrId: the soldier's mappedIndex (uint32) — this is a slot index,
--   not a gameObjectId. Obtain it from the soldier's runtime data.
-- enable: (optional) true to enable camo, false to disable. Default true.
function this.EnableSoldierStealthCamo(soldierNameOrId, enable)
    if enable == nil then enable = true end
    V_FrameWork.EnableSoldierStealthCamo(soldierNameOrId, enable)
end

-- Clear all per-soldier stealth camo overrides.
function this.ClearSoldierStealthCamoOverrides()
    V_FrameWork.ClearSoldierStealthCamoOverrides()
end

return this

--[[
Example usage:

local Enemy = require("V_TppEnemy")

-- Mark a soldier as a VIP officer (call from a message handler after init)
Enemy.SetVIPImportant(soldierGameObjectId, true)

-- After the mission, clean up
Enemy.ClearVIPImportant()

-- Make non-VIP soldiers use the concerned holdup recovery animation
Enemy.SetUseConcernedHoldupRecovery(true)

-- Enable cowardly holdup reactions
Enemy.HoldUpReactionCowardlyReaction(true)

-- Add a patrol soldier to the call-sign pool
Enemy.AddCallSignPatrolSoldier(soldierGameObjectId)

-- Give a specific soldier stealth camo (mappedIndex, not gameObjectId)
Enemy.EnableSoldierStealthCamo(soldierMappedIndex, true)
]]
