-- V_TppEnemy — soldier behavior overrides (VIP, holdup, patrol, stealth camo).
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}

function this.SetVIPImportant(soldierNameOrId, isOfficer)
    V_FrameWork.SetVIPImportant(soldierNameOrId, isOfficer)
end

function this.RemoveVIPImportant(soldierNameOrId)
    V_FrameWork.RemoveVIPImportant(soldierNameOrId)
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
    V_FrameWork.AddCallSignPatrolSoldier(soldierNameOrId)
end

function this.RemoveCallSignPatrolSoldier(soldierNameOrId)
    V_FrameWork.RemoveCallSignPatrolSoldier(soldierNameOrId)
end

function this.ClearCallSignPatrolSoldiers()
    V_FrameWork.ClearCallSignPatrolSoldiers()
end

-- enable defaults to true when omitted.
function this.EnableSoldierStealthCamo(soldierNameOrId, enable)
    if enable == nil then enable = true end
    V_FrameWork.EnableSoldierStealthCamo(soldierNameOrId, enable)
end

function this.ClearSoldierStealthCamoOverrides()
    V_FrameWork.ClearSoldierStealthCamoOverrides()
end

return this
