local this = {}
local StrCode32 = Fox.StrCode32
local IsTypeNumber = Tpp.IsTypeNumber
local IsTypeString = Tpp.IsTypeString

this.registerIvars={
	"V_FrameWork_Welcome_Message",
}

this.V_FrameWork_Welcome_Message={
	save=IvarProc.CATEGORY_EXTERNAL,
	default=0,
	range={min=0,max=1,},
}

function this.SetEquipIdIconFtexPath(equipId, path)
    if equipId == nil then
        V_FrameWork.Log("V_TppUiCommand.SetEquipIdIconFtexPath: equipId is nil, cannot set texture path.")
        return
    end
    if not IsTypeString(path) then
        V_FrameWork.Log("V_TppUiCommand.SetEquipIdIconFtexPath: path is not a string.")
        return
    end
    V_TppUiCommand.SetEquipIdIconFtexPath(equipId, path) -- WORK
end

function this.ClearIconFtexPath(equipId)
    V_TppUiCommand.ClearIconFtexPath(equipId) -- WORK
end

function this.ClearAllIconFtexPaths()
    V_TppUiCommand.ClearAllIconFtexPaths() -- WORK
end

function this.SetDefaultEquipBgTexturePath(path, colored, opacity)
    if not IsTypeString(path) then
        V_FrameWork.Log("V_TppUiCommand.SetDefaultEquipBgTexturePath: path is not a string.")
        return
    end
    V_TppUiCommand.SetDefaultEquipBgTexturePath(path, colored, opacity)
end

function this.ClearDefaultEquipBgTexture()
    V_TppUiCommand.ClearDefaultEquipBgTexture()
end

function this.SetEquipBgTexturePath(equipId, path, colored, opacity)
    if equipId == nil then
        V_FrameWork.Log("V_TppUiCommand.SetEquipBgTexturePath: equipId is nil.")
        return
    end
    if not IsTypeString(path) then
        V_FrameWork.Log("V_TppUiCommand.SetEquipBgTexturePath: path is not a string.")
        return
    end
    V_TppUiCommand.SetEquipBgTexturePath(equipId, path, colored, opacity)
end

function this.ClearEquipBgTexture(equipId)
    if equipId == nil then
        V_FrameWork.Log("V_TppUiCommand.ClearEquipBgTexture: equipId is nil.")
        return
    end
    V_TppUiCommand.ClearEquipBgTexture(equipId)
end

function this.SetEnemyWeaponBgTexturePath(path, colored, opacity)
    if not IsTypeString(path) then
        V_FrameWork.Log("V_TppUiCommand.SetEnemyWeaponBgTexturePath: path is not a string.")
        return
    end
    V_TppUiCommand.SetEnemyWeaponBgTexturePath(path, colored, opacity)
end

function this.ClearEnemyWeaponBgTexture()
    V_TppUiCommand.ClearEnemyWeaponBgTexture()
end

function this.SetEnemyEquipBgTexturePath(equipId, path, colored, opacity)
    if equipId == nil then
        V_FrameWork.Log("V_TppUiCommand.SetEnemyEquipBgTexturePath: equipId is nil.")
        return
    end
    if not IsTypeString(path) then
        V_FrameWork.Log("V_TppUiCommand.SetEnemyEquipBgTexturePath: path is not a string.")
        return
    end
    V_TppUiCommand.SetEnemyEquipBgTexturePath(equipId, path, colored, opacity)
end

function this.ClearEnemyEquipBgTexture(equipId)
    if equipId == nil then
        V_FrameWork.Log("V_TppUiCommand.ClearEnemyEquipBgTexture: equipId is nil.")
        return
    end
    V_TppUiCommand.ClearEnemyEquipBgTexture(equipId)
end

function this.ClearAllEquipBgTextures()
    V_TppUiCommand.ClearAllEquipBgTextures()
end

function this.SetLoadingSplashMainTexturePath(path)
    if not IsTypeString(path) then
        V_FrameWork.Log("V_TppUiCommand.SetLoadingSplashMainTexturePath: path is not a string." .. tostring(path))
       return
    end
    V_TppUiCommand.SetLoadingSplashMainTexturePath(path)
end

function this.SetLoadingSplashBlurTexturePath(path)
    if not IsTypeString(path) then
        V_FrameWork.Log("V_TppUiCommand.SetLoadingSplashBlurTexturePath: path is not a string." .. tostring(path))
        return
    end
    V_TppUiCommand.SetLoadingSplashBlurTexturePath(path)
end

function this.SetLoadingTexturePath(path)
    if not IsTypeString(path) then
        V_FrameWork.Log("V_TppUiCommand.SetLoadingTexturePath: path is not a string." .. tostring(path))
        return
    end
    V_TppUiCommand.SetLoadingSplashMainTexturePath(path)
    V_TppUiCommand.SetLoadingSplashBlurTexturePath(path)
end

function this.ClearLoadingSplashTextures()
    V_FrameWork.Log("V_TppUiCommand.ClearLoadingSplashTextures: Clearing loading splash textures.")
    V_TppUiCommand.ClearLoadingSplashTextures()
end


function this.SetGameOverSplashMainTexturePath(path)
    if not IsTypeString(path) then
        V_FrameWork.Log("V_TppUiCommand.SetGameOverSplashMainTexturePath: path is not a string." .. tostring(path))
        return
    end
    V_TppUiCommand.SetGameOverSplashMainTexturePath(path)
end

function this.SetGameOverSplashBlurTexturePath(path)
    if not IsTypeString(path) then
        V_FrameWork.Log("V_TppUiCommand.SetGameOverSplashBlurTexturePath: path is not a string." .. tostring(path))
        return
    end
    V_TppUiCommand.SetGameOverSplashBlurTexturePath(path)
end

function this.ClearGameOverSplashTextures()
    V_FrameWork.Log("V_TppUiCommand.ClearGameOverSplashTextures: Clearing game over splash textures.")
    V_TppUiCommand.ClearGameOverSplashTextures()
end

function this.ShowMbDvcAnnouncePopupReport(title, body)
    if not IsTypeString(title) then
        V_FrameWork.Log("V_TppUiCommand.ShowMbDvcAnnouncePopupReport: title is not a string.")
        return
    end
    if not IsTypeString(body) then
        V_FrameWork.Log("V_TppUiCommand.ShowMbDvcAnnouncePopupReport: body is not a string.")
        return
    end
    return V_TppUiCommand.ShowMbDvcAnnouncePopupReport(title or "", body or "")
end

function this.ShowMbDvcAnnouncePopupReportLangId(TitleLangId, BodyLangId)
    if not IsTypeString(TitleLangId) then
        V_FrameWork.Log("V_TppUiCommand.ShowMbDvcAnnouncePopupLangId: TitleLangId is not a string.")
        return
    end
    if not IsTypeString(BodyLangId) then
        V_FrameWork.Log("V_TppUiCommand.ShowMbDvcAnnouncePopupLangId: BodyLangId is not a string.")
        return
    end
    return V_TppUiCommand.ShowMbDvcAnnouncePopupReportLangId(TitleLangId or "", BodyLangId or "")
end

function this.ShowMbDvcAnnouncePopupReward(title, body)
    if not IsTypeString(title) then
        V_FrameWork.Log("V_TppUiCommand.ShowMbDvcAnnouncePopupReward: title is not a string.")
        return
    end
    if not IsTypeString(body) then
        V_FrameWork.Log("V_TppUiCommand.ShowMbDvcAnnouncePopupReward: body is not a string.")
        return
    end
    return V_TppUiCommand.ShowMbDvcAnnouncePopupReward(title or "", body or "")
end

function this.ShowMbDvcAnnouncePopupRewardLangId(TitleLangId, BodyLangId)
    if not IsTypeString(TitleLangId) then
        V_FrameWork.Log("V_TppUiCommand.ShowMbDvcAnnouncePopupRewardLangId: TitleLangId is not a string.")
        return
    end
    if not IsTypeString(BodyLangId) then
        V_FrameWork.Log("V_TppUiCommand.ShowMbDvcAnnouncePopupRewardLangId: BodyLangId is not a string.")
        return
    end
    return V_TppUiCommand.ShowMbDvcAnnouncePopupRewardLangId(TitleLangId or "", BodyLangId or "")
end

function this.SetMissionEmergency(missionCode, enabled)
    if not IsTypeNumber(missionCode) then
        V_FrameWork.Log("V_TppUiCommand.SetMissionEmergency: missionCode must be a number.")
        return
    end
    V_TppUiCommand.SetMissionEmergency(missionCode, enabled)
end

function this.IsMissionEmergency(missionCode)
    if not IsTypeNumber(missionCode) then return false end
    return V_TppUiCommand.IsMissionEmergency(missionCode)
end

function this.ClearAllMissionEmergencies()
    V_TppUiCommand.ClearAllMissionEmergencies()
end

function this.SetEmergencyMissionPopup(title, body)
    if title ~= nil and not IsTypeString(title) then
        V_FrameWork.Log("V_TppUiCommand.SetEmergencyMissionPopup: title is not a string or nil.")
        return
    end
    if body ~= nil and not IsTypeString(body) then
        V_FrameWork.Log("V_TppUiCommand.SetEmergencyMissionPopup: body is not a string or nil.")
        return
    end
    return V_TppUiCommand.SetEmergencyMissionPopup(title, body)
end

function this.SetEmergencyMissionPopupLangId(titleLabel, bodyLabel)
    this.ClearEmergencyMissionPopupOverride()
    if titleLabel ~= nil and not IsTypeString(titleLabel) then
        V_FrameWork.Log("V_TppUiCommand.SetEmergencyMissionPopupLangId: titleLabel is not a string or nil.")
        return
    end
    if bodyLabel ~= nil and not IsTypeString(bodyLabel) then
        V_FrameWork.Log("V_TppUiCommand.SetEmergencyMissionPopupLangId: bodyLabel is not a string or nil.")
        return
    end
    return V_TppUiCommand.SetEmergencyMissionPopupLangId(titleLabel, bodyLabel)
end

function this.ClearEmergencyMissionPopupOverride()
    V_TppUiCommand.ClearEmergencyMissionPopupOverride()
end

function this.ShowMissionIcon(title, body, time)
    if title ~= nil and type(title) ~= "string" and type(title) ~= "number" then
        V_FrameWork.Log("V_TppUiCommand.ShowMissionIcon: title must be string, number or nil.")
        return
    end
    if body ~= nil and not IsTypeString(body) then
        V_FrameWork.Log("V_TppUiCommand.ShowMissionIcon: body must be a string or nil.")
        return
    end
    if time ~= nil and type(time) ~= "number" then
        V_FrameWork.Log("V_TppUiCommand.ShowMissionIcon: time must be a number or nil.")
        return
    end
    V_TppUiCommand.ShowMissionIcon(title, body, time or 6)
end

function this.HideMissionIcon()
    TppUiCommand.HideMissionIcon()
end

function this.ShowTimeCigaretteUi()
    return V_TppUiCommand.ShowTimeCigaretteUi()
end

function this.HideTimeCigaretteUi()
    return V_TppUiCommand.HideTimeCigaretteUi()
end

function this.SetAnnounceLogSE(label, value, chara, dialogueEvent)
    if not IsTypeString(label) then
        V_FrameWork.Log("V_TppUiCommand.SetAnnounceLogSE: label must be a string (the announce lang label).")
        return
    end
    if value == nil then
        V_FrameWork.Log("V_TppUiCommand.SetAnnounceLogSE: value is nil -- pass a condition/sound-id number or an event-name string.")
        return
    end
    if chara ~= nil and not IsTypeNumber(chara) then
        V_FrameWork.Log("V_TppUiCommand.SetAnnounceLogSE: chara must be a number or nil.")
        return
    end
    if dialogueEvent ~= nil and not IsTypeNumber(dialogueEvent) then
        V_FrameWork.Log("V_TppUiCommand.SetAnnounceLogSE: dialogueEvent must be a number or nil.")
        return
    end
    return V_TppUiCommand.SetAnnounceLogSE(label, value, chara, dialogueEvent)
end

function this.Messages()
	return
	Tpp.StrCode32Table {
		Terminal = {
			{
				msg = "MbDvcActOpenTop",
				func = function()
                    if Ivars.V_FrameWork_Welcome_Message:Get() == 0 then
                        Ivars.V_FrameWork_Welcome_Message:Set(1)
                        this.ShowMbDvcAnnouncePopupRewardLangId("MbDvcPopup_Title_FirstTimeInstall",  "MbDvcPopup_Text_FirstTimeInstall")
                    end
				end
			},
		},
	}
end

function this.Init(missionTable)
  this.messageExecTable=Tpp.MakeMessageExecTable(this.Messages())
end

function this.OnReload(missionTable)
  this.messageExecTable=Tpp.MakeMessageExecTable(this.Messages())
end

function this.OnMessage(sender,messageId,arg0,arg1,arg2,arg3,strLogText)
  Tpp.DoMessage(this.messageExecTable,TppMission.CheckMessageOption,sender,messageId,arg0,arg1,arg2,arg3,strLogText)
end

return this
