local this = {}
local IsString=Tpp.IsTypeString
local GetGameObjectId = GameObject.GetGameObjectId
local SendCommand = GameObject.SendCommand
local NULL_ID = GameObject.NULL_ID

function this.LoadLibraries()
    local ApplyPowerSetting = TppEnemy.ApplyPowerSetting
    TppEnemy.ApplyPowerSetting = function(soldierId, powerSettings)
        ApplyPowerSetting(soldierId, powerSettings)

        if soldierId == NULL_ID then
            return
        end

        local powerLoadout = mvars.ene_soldierPowerSettings[soldierId]
        local hasRadio = (powerLoadout and powerLoadout.RADIO)
                      or (mvars.ene_soldierLrrp and mvars.ene_soldierLrrp[soldierId])
        if hasRadio then
            if IsString(soldierId) then
                soldierId = GetGameObjectId(soldierId)
            end
            if soldierId ~= NULL_ID then
                SendCommand(soldierId, { id = "SetRadioCallSign", callSign = V_TppCallSign.PATROL })
            end
        end
    end
end


this.VIP_MISSION_LIST = {
    [10036] = {
        {
            name ="sol_vip_0000",
        }
    },
    [10041] = {
        {
            name ="sol_vip_field",
        },
        {
            name ="sol_vip_village",
        },
        {
            name ="sol_vip_enemyBase",
        }
    },
    [10044] = {
        {
            name ="sol_enemyNorth_lvVIP",
        },
    },
    [10086] = {
        {
            name ="sol_interpreter",
        },
        {
            name ="sol_interrogator",
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
        },
    },
    [10211] = {
        {
            name ="sol_mis_0000",
        },
    },
    [10171] = {
        {
            name ="sol_pfCamp_vip",
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
            local vipName = GetGameObjectId(vipInfo.name)
            if missionCode == 10121 then
                SendCommand(vipName, { id = "SetDeadBodyLabel", deadBodyLabel = "V_CPRGZ0040" })
            elseif missionCode == 10041 then
                SendCommand(vipName, { id = "SetDeadBodyLabel", deadBodyLabel = "V_CPR0042_KEEP" })
            elseif vipInfo.isOfficer then
                SendCommand(vipName, { id = "SetDeadBodyLabel", deadBodyLabel = "V_CPR0042" })
            end
        end
        this.SetVipDeepVoice(Ivars.V_FrameWork_V_Vip_DeepVoice:Get() == 1)
    else
        Fox.Log("No VIP setup for mission " .. tostring(missionCode))
    end
end

function this.SetVipDeepVoice(enable)
    local pitch = 0
    if enable then
        pitch = -200
    end
    for _, vipInfo in ipairs(this.VIP_MISSION_LIST[this.GetMissionCode()] or {}) do
        local vipId = GetGameObjectId(vipInfo.name)
        if vipId ~= NULL_ID then
            SendCommand(vipId, { id = "SetVoicePitch", pitch = pitch })
        end
    end
end

return this
