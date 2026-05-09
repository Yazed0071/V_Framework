local this = {}
local StrCode32 = Fox.StrCode32
local StrCode32Table = Tpp.StrCode32Table
local GetGameObjectId = GameObject.GetGameObjectId
local IsTypeString = Tpp.IsTypeString
local NULL_ID = GameObject.NULL_ID



-- Resolve string-name to gameObjectId, or pass-through numeric id.
local function ResolveSoldierId(soldierNameOrId, fnName)
    if soldierNameOrId == nil then
        V_FrameWork.Log("V_TppEnemy." .. fnName .. ": soldier is nil.")
        return nil
    end
    if IsTypeString(soldierNameOrId) then
        return GetGameObjectId(soldierNameOrId)
    end
    if type(soldierNameOrId) == "number" then
        return soldierNameOrId
    end
    V_FrameWork.Log("V_TppEnemy." .. fnName .. ": soldier must be string or number.")
    return nil
end


-- Resolve string label to StrCode32 hash, or pass-through numeric.
local function ResolveLabelHash(label)
    if label == nil then return 0 end
    if IsTypeString(label) then return StrCode32(label) end
    if type(label) == "number" then return label end
    return 0
end


function this.SetVIPImportant(soldierNameOrId, isOfficer, foundDeadBodyRadioLabel)
    local id = ResolveSoldierId(soldierNameOrId, "SetVIPImportant")
    if id == nil then return end
    if isOfficer ~= nil and type(isOfficer) ~= "boolean" then
        V_FrameWork.Log("V_TppEnemy.SetVIPImportant: isOfficer is not a boolean.")
        return
    end
    V_FrameWork.SetVIPImportant(id, isOfficer or false, ResolveLabelHash(foundDeadBodyRadioLabel))
end

function this.RemoveVIPImportant(soldierNameOrId)
    local id = ResolveSoldierId(soldierNameOrId, "RemoveVIPImportant")
    if id == nil then return end
    V_FrameWork.RemoveVIPImportant(id)
end

function this.ClearVIPImportant()
    V_FrameWork.ClearVIPImportant()
end

function this.SetUseConcernedHoldupRecovery(enable)
    if type(enable) ~= "boolean" then
        V_FrameWork.Log("V_TppEnemy.SetUseConcernedHoldupRecovery: enable is not a boolean.")
        return
    end
    V_FrameWork.SetUseConcernedHoldupRecovery(enable)
end

function this.HoldUpReactionCowardlyReaction(enable)
    if type(enable) ~= "boolean" then
        V_FrameWork.Log("V_TppEnemy.HoldUpReactionCowardlyReaction: enable is not a boolean.")
        return
    end
    V_FrameWork.HoldUpReactionCowardlyReaction(enable)
end

function this.AddCallSignPatrolSoldier(soldierNameOrId)
    local id = ResolveSoldierId(soldierNameOrId, "AddCallSignPatrolSoldier")
    if id == nil then return end
    V_FrameWork.AddCallSignPatrolSoldier(id)
end

function this.RemoveCallSignPatrolSoldier(soldierNameOrId)
    local id = ResolveSoldierId(soldierNameOrId, "RemoveCallSignPatrolSoldier")
    if id == nil then return end
    V_FrameWork.RemoveCallSignPatrolSoldier(id)
end

function this.ClearCallSignPatrolSoldiers()
    V_FrameWork.ClearCallSignPatrolSoldiers()
end

-- enable defaults to true.
function this.EnableSoldierStealthCamo(soldierNameOrId, enable)
    local id = ResolveSoldierId(soldierNameOrId, "EnableSoldierStealthCamo")
    if id == nil then return end
    if enable == nil then enable = true end
    if type(enable) ~= "boolean" then
        V_FrameWork.Log("V_TppEnemy.EnableSoldierStealthCamo: enable is not a boolean.")
        return
    end
    V_FrameWork.EnableSoldierStealthCamo(id, enable)
end

function this.ClearSoldierStealthCamoOverrides()
    V_FrameWork.ClearSoldierStealthCamoOverrides()
end

-- Per-soldier RTPC. rtpcNameOrId may be string or numeric id.
function this.SetRtpc(soldierNameOrId, rtpcNameOrId, value, timeMs)
    local id = ResolveSoldierId(soldierNameOrId, "SetRtpc")
    if id == nil then return end
    if rtpcNameOrId == nil then
        V_FrameWork.Log("V_TppEnemy.SetRtpc: rtpcNameOrId is nil.")
        return
    end
    if type(value) ~= "number" then
        V_FrameWork.Log("V_TppEnemy.SetRtpc: value is not a number.")
        return
    end
    if IsTypeString(rtpcNameOrId) then
        return V_FrameWork.SetSoldierRtpc(id, rtpcNameOrId, value, timeMs or 0)
    end
    if type(rtpcNameOrId) == "number" then
        return V_FrameWork.SetSoldierRtpcById(id, rtpcNameOrId, value, timeMs or 0)
    end
    V_FrameWork.Log("V_TppEnemy.SetRtpc: rtpcNameOrId must be string or number.")
end

-- Global RTPC.
function this.SetGlobalRtpc(rtpcNameOrId, value, timeMs)
    if rtpcNameOrId == nil then
        V_FrameWork.Log("V_TppEnemy.SetGlobalRtpc: rtpcNameOrId is nil.")
        return
    end
    if type(value) ~= "number" then
        V_FrameWork.Log("V_TppEnemy.SetGlobalRtpc: value is not a number.")
        return
    end
    if IsTypeString(rtpcNameOrId) then
        return V_FrameWork.SetGlobalRtpc(rtpcNameOrId, value, timeMs or 0)
    end
    if type(rtpcNameOrId) == "number" then
        return V_FrameWork.SetGlobalRtpcById(rtpcNameOrId, value, timeMs or 0)
    end
    V_FrameWork.Log("V_TppEnemy.SetGlobalRtpc: rtpcNameOrId must be string or number.")
end

this.VIP_MISSION_LIST = {
    [10036] = {
        {
            name ="sol_vip_0000",
            isOfficer = false
        }
    },
    [10041] = {
        {
            name ="sol_vip_field",
            isOfficer = false
        },
        {
            name ="sol_vip_village",
            isOfficer = false
        },
        {
            name ="sol_vip_enemyBase",
            isOfficer = false
        }
    },
    [10044] = {
        {
            name ="sol_enemyNorth_lvVIP",
            isOfficer = false
        },
    },
    [10086] = {
        {
            name ="sol_interpreter",
            isOfficer = false
        },
        {
            name ="sol_interrogator",
            isOfficer = true
        },
    },
    [10195] = {
        {
            name ="sol_vip",
            isOfficer = true
        },
        {
            name ="sol_dealer",
            isOfficer = true
        },
    },
    [10121] = {
        {
            name ="sol_pfCamp_vip_0001",
            isOfficer = true
        },
        {
            name ="sol_pfCamp_vip_guard",
            isOfficer = true
        },
    },
    [10115] = {
        {
            name ="ly003_cl02_10115_npc0000|cl02pl0_uq_0020_npc|sol_plnt0_0000",
            isOfficer = false
        },
    },
    [10211] = {
        {
            name ="sol_mis_0000",
            isOfficer = false
        },
    },
    [10171] = {
        {
            name ="sol_pfCamp_vip",
            isOfficer = false
        },
    },

}

function this.GetMissionCode()
    local missionCode = TppMission.GetMissionID()
    if TppMission.IsHardMission(missionCode) then
        missionCode = TppMission.GetNormalMissionCodeFromHardMission(missionCode)
    end
    return missionCode
end

function this.SetUpEnemy()
    local missionCode = this.GetMissionCode()
    if this.VIP_MISSION_LIST[missionCode] then
        for _, vipInfo in ipairs(this.VIP_MISSION_LIST[missionCode]) do
            if missionCode == 10121 then
                this.SetVIPImportant(vipInfo.name, vipInfo.isOfficer, "V_CPRGZ0040")
            else
                this.SetVIPImportant(vipInfo.name, vipInfo.isOfficer)
            end
        end
    else
        V_FrameWork.Log("No VIP setup for mission " .. tostring(missionCode))
    end
end

return this
