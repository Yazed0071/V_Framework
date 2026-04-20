-- V_TppEnemy — soldier behavior overrides (VIP, holdup, patrol, stealth camo, RTPC).
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.
--
-- Every function that takes a soldier accepts EITHER a string name
-- (e.g. "sol_vip_0000") OR a numeric gameObjectId. Strings are resolved to
-- gameObjectIds on the Lua side via GameObject.GetGameObjectId before the
-- C++ bridge is called — the bridge only handles integers.

local StrCode32 = Fox.StrCode32
local StrCode32Table = Tpp.StrCode32Table
local GetGameObjectId = GameObject.GetGameObjectId
local IsTypeString = Tpp.IsTypeString
local NULL_ID = GameObject.NULL_ID

local this = {}

function this.SetVIPImportant(soldierNameOrId, isOfficer)
    local gameObjectId = soldierNameOrId
    if IsTypeString(soldierNameOrId) then
        gameObjectId = GetGameObjectId(soldierNameOrId)
    end
    V_FrameWork.SetVIPImportant(gameObjectId, isOfficer)
end

function this.RemoveVIPImportant(soldierNameOrId)
    local gameObjectId = soldierNameOrId
    if IsTypeString(soldierNameOrId) then
        gameObjectId = GetGameObjectId(soldierNameOrId)
    end
    V_FrameWork.RemoveVIPImportant(gameObjectId)
end

function this.ClearVIPImportant()
    V_FrameWork.ClearVIPImportant()
end

function this.SetUseConcernedHoldupRecovery(enable)
    V_FrameWork.SetUseConcernedHoldupRecovery(enable)
end

function this.HoldUpReactionCowardlyReaction(enable)
    V_FrameWork.HoldUpReactionCowardlyReaction(enable)
end

function this.AddCallSignPatrolSoldier(soldierNameOrId)
    local gameObjectId = soldierNameOrId
    if IsTypeString(soldierNameOrId) then
        gameObjectId = GetGameObjectId(soldierNameOrId)
    end
    V_FrameWork.AddCallSignPatrolSoldier(gameObjectId)
end

function this.RemoveCallSignPatrolSoldier(soldierNameOrId)
    local gameObjectId = soldierNameOrId
    if IsTypeString(soldierNameOrId) then
        gameObjectId = GetGameObjectId(soldierNameOrId)
    end
    V_FrameWork.RemoveCallSignPatrolSoldier(gameObjectId)
end

function this.ClearCallSignPatrolSoldiers()
    V_FrameWork.ClearCallSignPatrolSoldiers()
end

-- enable defaults to true when omitted.
function this.EnableSoldierStealthCamo(soldierNameOrId, enable)
    if enable == nil then enable = true end
    local gameObjectId = soldierNameOrId
    if IsTypeString(soldierNameOrId) then
        gameObjectId = GetGameObjectId(soldierNameOrId)
    end
    V_FrameWork.EnableSoldierStealthCamo(gameObjectId, enable)
end

function this.ClearSoldierStealthCamoOverrides()
    V_FrameWork.ClearSoldierStealthCamoOverrides()
end

-- Per-soldier Wwise RTPC. Use for voice pitch, combat state, wetness, etc.
-- Discover RTPC names from the [ParamID] log lines written by the
-- ConvertParameterIdLogger diagnostic hook during gameplay.
-- Params:
--   soldierNameOrId        — string soldier name OR numeric gameObjectId
--   rtpcNameOrId           — string Wwise RTPC name OR numeric (pre-hashed) id
--   value (number)
--   timeMs (int, optional)
-- Returns: AKRESULT (1 = AK_Success)
function this.SetRtpc(soldierNameOrId, rtpcNameOrId, value, timeMs)
    local gameObjectId = soldierNameOrId
    if IsTypeString(soldierNameOrId) then
        gameObjectId = GetGameObjectId(soldierNameOrId)
    end
    if IsTypeString(rtpcNameOrId) then
        return V_FrameWork.SetSoldierRtpc(gameObjectId, rtpcNameOrId, value, timeMs or 0)
    end
    return V_FrameWork.SetSoldierRtpcById(gameObjectId, rtpcNameOrId, value, timeMs or 0)
end

-- Global RTPC (AK_INVALID_GAME_OBJECT scope — affects all listeners).
-- Params:
--   rtpcNameOrId — string Wwise RTPC name OR numeric (pre-hashed) id
--   value (number)
--   timeMs (int, optional)
-- Returns: AKRESULT
function this.SetGlobalRtpc(rtpcNameOrId, value, timeMs)
    if IsTypeString(rtpcNameOrId) then
        return V_FrameWork.SetGlobalRtpc(rtpcNameOrId, value, timeMs or 0)
    end
    return V_FrameWork.SetGlobalRtpcById(rtpcNameOrId, value, timeMs or 0)
end

return this
