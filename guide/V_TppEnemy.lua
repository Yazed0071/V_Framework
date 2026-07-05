local this = {}
local StrCode32 = Fox.StrCode32
local StrCode32Table = Tpp.StrCode32Table
local IsString=Tpp.IsTypeString
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
    GameObject.SendCommand(id, { id = "SetVIPImportant", isOfficer = isOfficer or false, deadBodyLabel = foundDeadBodyRadioLabel })
end

function this.RemoveVIPImportant(soldierNameOrId)
    local id = GetGameObjectId(soldierNameOrId)
    if id == nil then return end
    GameObject.SendCommand(id, { id = "RemoveVIPImportant" })
end

function this.ClearVIPImportant()
    GameObject.SendCommand({ type = "TppSoldier2" }, { id = "ClearVIPImportant" })
end

function this.SetUseConcernedHoldupRecovery(enable)
    if enable == nil then enable = true end
    if type(enable) ~= "boolean" then
        V_FrameWork.Log("V_TppEnemy.SetUseConcernedHoldupRecovery: enable is not a boolean.")
        return
    end
    GameObject.SendCommand({ type = "TppSoldier2" }, { id = "SetUseConcernedHoldupRecovery", enable = enable })
end

function this.AddCallSignPatrolSoldier(gameId)
    if IsString(gameId) then
        gameId = GetGameObjectId(gameId)
    end
    if gameId == nil then return end

    GameObject.SendCommand(gameId, { id = "AddCallSignPatrolSoldier" })
end

function this.RemoveCallSignPatrolSoldier(gameId)
    if IsString(gameId) then
        gameId = GetGameObjectId(gameId)
    end
    if gameId == nil then return end
    GameObject.SendCommand(gameId, { id = "RemoveCallSignPatrolSoldier" })
end

function this.ClearCallSignPatrolSoldiers()
    GameObject.SendCommand({ type = "TppSoldier2" }, { id = "ClearCallSignPatrolSoldiers" })
end

function this.EnableSoldierStealthCamo(soldierNameOrId, enable)
    local gameId = GetGameObjectId(soldierNameOrId)
    if gameId == nil then return end
    if enable == nil then enable = true end
    if type(enable) ~= "boolean" then
        V_FrameWork.Log("V_TppEnemy.EnableSoldierStealthCamo: enable is not a boolean.")
        return
    end
    GameObject.SendCommand(gameId, { id = "EnableSoldierStealthCamo", enable = enable })
end

function this.ClearSoldierStealthCamoOverrides()
    GameObject.SendCommand({ type = "TppSoldier2" }, { id = "ClearSoldierStealthCamoOverrides" })
end

function this.SetEnemyInformationLangId(langId)
    if not IsTypeString(langId) then
        V_FrameWork.Log("V_TppEnemy.SetEnemyInformationLangId: langId is not a string.")
        return
    end
    V_TppUiCommand.SetEnemyInformationLangId(langId)
end

function this.ClearEnemyInformationLangId()
    V_TppUiCommand.ClearEnemyInformationLangId()
end

function this.SetEnemyUnitName(langId)
    if not IsTypeString(langId) then
        V_FrameWork.Log("V_TppEnemy.SetEnemyUnitName: langId is not a string.")
        return
    end
    V_TppUiCommand.SetEnemyUnitName(langId)
end

function this.ClearEnemyUnitName()
    V_TppUiCommand.ClearEnemyUnitName()
end

function this.SetEnemyInformationLangIdForSoldier(soldierNameOrId, langId)
    local id = GetGameObjectId(soldierNameOrId)
    if id == nil then return end
    if not IsTypeString(langId) then
        V_FrameWork.Log("V_TppEnemy.SetEnemyInformationLangIdForSoldier: langId is not a string.")
        return
    end
    V_TppUiCommand.SetEnemyInformationLangIdForSoldier(id, langId)
end

function this.ClearEnemyInformationLangIdForSoldier(soldierNameOrId)
    local id = GetGameObjectId(soldierNameOrId)
    if id == nil then return end
    V_TppUiCommand.ClearEnemyInformationLangIdForSoldier(id)
end

function this.ClearAllEnemyInformationLangIdForSoldiers()
    V_TppUiCommand.ClearAllEnemyInformationLangIdForSoldiers()
end

function this.SetEnemyUnitNameForSoldier(soldierNameOrId, langId)
    local id = GetGameObjectId(soldierNameOrId)
    if id == nil then return end
    if not IsTypeString(langId) then
        V_FrameWork.Log("V_TppEnemy.SetEnemyUnitNameForSoldier: langId is not a string.")
        return
    end
    V_TppUiCommand.SetEnemyUnitNameForSoldier(id, langId)
end

function this.ClearEnemyUnitNameForSoldier(soldierNameOrId)
    local id = GetGameObjectId(soldierNameOrId)
    if id == nil then return end
    V_TppUiCommand.ClearEnemyUnitNameForSoldier(id)
end

function this.ClearAllEnemyUnitNameForSoldiers()
    V_TppUiCommand.ClearAllEnemyUnitNameForSoldiers()
end

function this.SetOccasionalChatList(labels)
    GameObject.SendCommand({ type = "TppSoldier2"},
    { id = "SetOccasionalChatList", labels = labels or {} })
end

function this.InsertToOccasionalChatList(labels)
    if type(labels) ~= "table" then labels = { labels } end
    GameObject.SendCommand({type = "TppSoldier2"},
        { id = "InsertToOccasionalChatList", labels = labels })
end

function this.RemoveFromOccasionalChatList(labels)
    if type(labels) ~= "table" then labels = { labels } end
    GameObject.SendCommand({ type = "TppSoldier2"},
        { id = "RemoveFromOccasionalChatList", labels = labels })
end

function this.ResetOccasionalChatList()
    GameObject.SendCommand({ type = "TppSoldier2"}, { id = "ResetOccasionalChatList" })
end

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
            this.AddCallSignPatrolSoldier(soldierId)
        end
    end
end

function this.Messages()
    return Tpp.StrCode32Table {
        GameObject = {
            {
                msg = "ChangePhase",
                func = function(gameObjectId, phaseName)
	                local closestCp= InfMain.GetClosestCp{vars.playerPosX,vars.playerPosY,vars.playerPosZ}
	                local cp = GameObject.GetGameObjectId( closestCp )

                    if gameObjectId == cp then
                        if phaseName >= TppGameObject.PHASE_CAUTION then
                            this.SetUseConcernedHoldupRecovery(true)
                        else
                            this.SetUseConcernedHoldupRecovery(false)
                        end
                    end
                end,
            },
        },
    }
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
            elseif missionCode == 10041 then
                this.SetVIPImportant(vipInfo.name, vipInfo.isOfficer, "V_CPR0042_KEEP")
            else
                this.SetVIPImportant(vipInfo.name, vipInfo.isOfficer)
            end
            V_TppSound.SetSoldierVoicePitch(vipInfo.name, pitch)
        end
    else
        V_FrameWork.Log("No VIP setup for mission " .. tostring(missionCode))
    end
end

function this.Init(missionTable)
    this.messageExecTable = Tpp.MakeMessageExecTable(this.Messages())
end

function this.OnMessage(sender, messageId, arg0, arg1, arg2, arg3, strLogText)
    Tpp.DoMessage(this.messageExecTable, TppMission.CheckMessageOption, sender, messageId, arg0, arg1, arg2, arg3, strLogText)
end

return this
