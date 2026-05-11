local this = {}
local StrCode32 = Fox.StrCode32
local StrCode32Table = Tpp.StrCode32Table
local GetGameObjectId = GameObject.GetGameObjectId
local IsTypeString = Tpp.IsTypeString
local NULL_ID = GameObject.NULL_ID


function this.SetVIPImportant(soldierNameOrId, isOfficer, foundDeadBodyRadioLabel)
    local id = GetGameObjectId(soldierNameOrId)
    if id == nil then return end
    if isOfficer ~= nil and type(isOfficer) ~= "boolean" then
        V_FrameWork.Log("V_TppEnemy.SetVIPImportant: isOfficer is not a boolean.")
        return
    end
    V_FrameWork.SetVIPImportant(id, isOfficer or false, foundDeadBodyRadioLabel)
end

function this.RemoveVIPImportant(soldierNameOrId)
    local id = GetGameObjectId(soldierNameOrId)
    if id == nil then return end
    V_FrameWork.RemoveVIPImportant(id)
end

function this.ClearVIPImportant()
    V_FrameWork.ClearVIPImportant()
end

function this.SetUseConcernedHoldupRecovery(enable)
    if enable == nil then enable = true end
    if type(enable) ~= "boolean" then
        V_FrameWork.Log("V_TppEnemy.SetUseConcernedHoldupRecovery: enable is not a boolean.")
        return
    end
    V_FrameWork.SetUseConcernedHoldupRecovery(enable)
end

function this.HoldUpReactionCowardlyReaction(enable)
    if enable == nil then enable = true end
    if type(enable) ~= "boolean" then
        V_FrameWork.Log("V_TppEnemy.HoldUpReactionCowardlyReaction: enable is not a boolean.")
        return
    end
    V_FrameWork.HoldUpReactionCowardlyReaction(enable)
end

function this.AddCallSignPatrolSoldier(soldierNameOrId)
    local gameId = GetGameObjectId(soldierNameOrId)
    if gameId == nil then return end

    V_FrameWork.AddCallSignPatrolSoldier(gameId)
end

function this.RemoveCallSignPatrolSoldier(soldierNameOrId)
    local gameId = GetGameObjectId(soldierNameOrId)
    if gameId == nil then return end
    V_FrameWork.RemoveCallSignPatrolSoldier(gameId)
end

function this.ClearCallSignPatrolSoldiers()
    V_FrameWork.ClearCallSignPatrolSoldiers()
end

-- enable defaults to true.
function this.EnableSoldierStealthCamo(soldierNameOrId, enable)
    local gameId = GetGameObjectId(soldierNameOrId)
    if gameId == nil then return end
    if enable == nil then enable = true end
    if type(enable) ~= "boolean" then
        V_FrameWork.Log("V_TppEnemy.EnableSoldierStealthCamo: enable is not a boolean.")
        return
    end
    V_FrameWork.EnableSoldierStealthCamo(gameId, enable)
end

function this.ClearSoldierStealthCamoOverrides()
    V_FrameWork.ClearSoldierStealthCamoOverrides()
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
    local pitch = -200
    if this.VIP_MISSION_LIST[missionCode] then
        for _, vipInfo in ipairs(this.VIP_MISSION_LIST[missionCode]) do
            if missionCode == 10121 then
                this.SetVIPImportant(vipInfo.name, vipInfo.isOfficer, "V_CPRGZ0040")
            else
                this.SetVIPImportant(vipInfo.name, vipInfo.isOfficer)
            end
            V_SoundCoreDaemon.SetSoldierVoicePitch(vipInfo.name, pitch)
        end
    else
        V_FrameWork.Log("No VIP setup for mission " .. tostring(missionCode))
    end
end

return this
