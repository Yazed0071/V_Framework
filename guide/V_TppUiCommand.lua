-- V_TppUiCommand — UI texture overrides + iDroid AnnouncePopup customization.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}
local StrCode32 = Fox.StrCode32
local IsTypeString=Tpp.IsTypeString

this.registerIvars={
	"V_FrameWork_FirstInstall",
}

this.V_FrameWork_FirstInstall={
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
    V_FrameWork.SetEquipIdIconFtexPath(equipId, path)
end

function this.SetDefaultEquipBgTexturePath(path)
    if not IsTypeString(path) then
        V_FrameWork.Log("V_TppUiCommand.SetDefaultEquipBgTexturePath: path is not a string.")
        return
    end
    V_FrameWork.SetDefaultEquipBgTexturePath(path)
end

function this.ClearDefaultEquipBgTexture()
    V_FrameWork.Log("V_TppUiCommand.ClearDefaultEquipBgTexture: Clearing default equip background texture.")
    V_FrameWork.ClearDefaultEquipBgTexture()
end

function this.SetEquipBgTexturePath(equipId, path)
    if equipId == nil then
        V_FrameWork.Log("V_TppUiCommand.SetEquipBgTexturePath: equipId is nil, cannot set texture path.")
        return
    end
    if not IsTypeString(path) then
        V_FrameWork.Log("V_TppUiCommand.SetEquipBgTexturePath: path is not a string.")
        return
    end
    V_FrameWork.SetEquipBgTexturePath(equipId, path)
end

function this.ClearEquipBgTexture(equipId)
    if equipId == nil then
        V_FrameWork.Log("V_TppUiCommand.ClearEquipBgTexture: equipId is nil, cannot clear texture.")
        return
    end
    V_FrameWork.ClearEquipBgTexture(equipId)
end

function this.SetEnemyWeaponBgTexturePath(path)
    if not IsTypeString(path) then
        V_FrameWork.Log("V_TppUiCommand.SetEnemyWeaponBgTexturePath: path is not a string.")
        return
    end
    V_FrameWork.SetEnemyWeaponBgTexturePath(path)
end

function this.ClearEnemyWeaponBgTexture()
    V_FrameWork.Log("V_TppUiCommand.ClearEnemyWeaponBgTexture: Clearing enemy weapon background texture.")
    V_FrameWork.ClearEnemyWeaponBgTexture()
end

function this.SetEnemyEquipBgTexturePath(equipId, path)
    if equipId == nil then
        V_FrameWork.Log("V_TppUiCommand.SetEnemyEquipBgTexturePath: equipId is nil, cannot set texture path.")
        return
    end
    if not IsTypeString(path) then
        V_FrameWork.Log("V_TppUiCommand.SetEnemyEquipBgTexturePath: path is not a string.")
        return
    end
    V_FrameWork.SetEnemyEquipBgTexturePath(equipId, path)
end

function this.ClearEnemyEquipBgTexture(equipId)
    if equipId == nil then
        V_FrameWork.Log("V_TppUiCommand.ClearEnemyEquipBgTexture: equipId is nil, cannot clear texture.")
        return
    end
    V_FrameWork.ClearEnemyEquipBgTexture(equipId)
end

function this.ClearAllEquipBgTextures()
    V_FrameWork.Log("V_TppUiCommand.ClearAllEquipBgTextures: Clearing all equip background textures.")
    V_FrameWork.ClearAllEquipBgTextures()
end

function this.SetLoadingSplashMainTexturePath(path)
    if not IsTypeString(path) then
        V_FrameWork.Log("V_TppUiCommand.SetLoadingSplashMainTexturePath: path is not a string." .. tostring(path))
       return
    end
    V_FrameWork.SetLoadingSplashMainTexturePath(path)
end

function this.SetLoadingSplashBlurTexturePath(path)
    if not IsTypeString(path) then
        V_FrameWork.Log("V_TppUiCommand.SetLoadingSplashBlurTexturePath: path is not a string." .. tostring(path))
        return
    end
    V_FrameWork.SetLoadingSplashBlurTexturePath(path)
end

function this.SetLoadingTexturePath(path)
    if not IsTypeString(path) then
        V_FrameWork.Log("V_TppUiCommand.SetLoadingTexturePath: path is not a string." .. tostring(path))
        return
    end
    V_FrameWork.SetLoadingSplashMainTexturePath(path)
    V_FrameWork.SetLoadingSplashBlurTexturePath(path)
end

function this.ClearLoadingSplashTextures()
    V_FrameWork.Log("V_TppUiCommand.ClearLoadingSplashTextures: Clearing loading splash textures.")
    V_FrameWork.ClearLoadingSplashTextures()
end


function this.SetGameOverSplashMainTexturePath(path)
    if not IsTypeString(path) then
        V_FrameWork.Log("V_TppUiCommand.SetGameOverSplashMainTexturePath: path is not a string." .. tostring(path))
        return
    end
    V_FrameWork.SetGameOverSplashMainTexturePath(path)
end

function this.SetGameOverSplashBlurTexturePath(path)
    if not IsTypeString(path) then
        V_FrameWork.Log("V_TppUiCommand.SetGameOverSplashBlurTexturePath: path is not a string." .. tostring(path))
        return
    end
    V_FrameWork.SetGameOverSplashBlurTexturePath(path)
end

function this.ClearGameOverSplashTextures()
    V_FrameWork.Log("V_TppUiCommand.ClearGameOverSplashTextures: Clearing game over splash textures.")
    V_FrameWork.ClearGameOverSplashTextures()
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
    return V_FrameWork.ShowMbDvcAnnouncePopupReport(title or "", body or "")
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
    return V_FrameWork.ShowMbDvcAnnouncePopupReportLangId(TitleLangId or "", BodyLangId or "")
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
    return V_FrameWork.ShowMbDvcAnnouncePopupReward(title or "", body or "")
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
    return V_FrameWork.ShowMbDvcAnnouncePopupRewardLangId(TitleLangId or "", BodyLangId or "")
end

-- HUD popup with title LABEL + LITERAL body text.
-- titleLabel : LangId label (DLL hashes via Fox StrCode64).
-- body       : literal body text (passed straight to SetPopupText).
-- popupType  : optional 1..6 (TppUiCommand.Popup.TYPE_*), default 1.
function this.ShowHudPopup(titleLabel, body, popupType)
    if not IsTypeString(titleLabel) then
        V_FrameWork.Log("V_TppUiCommand.ShowHudPopup: titleLabel is not a string.")
        return
    end
    if not IsTypeString(body) then
        V_FrameWork.Log("V_TppUiCommand.ShowHudPopup: body is not a string.")
        return
    end
    if popupType ~= nil and type(popupType) ~= "number" then
        V_FrameWork.Log("V_TppUiCommand.ShowHudPopup: popupType is not a number.")
        return
    end
    return V_FrameWork.ShowHudPopup(titleLabel or "", body or "", popupType or 1)
end

-- HUD popup with both title and body as LangId labels.
function this.ShowHudPopupLangId(titleLabel, bodyLabel, popupType)
    if not IsTypeString(titleLabel) then
        V_FrameWork.Log("V_TppUiCommand.ShowHudPopupLangId: titleLabel is not a string.")
        return
    end
    if not IsTypeString(bodyLabel) then
        V_FrameWork.Log("V_TppUiCommand.ShowHudPopupLangId: bodyLabel is not a string.")
        return
    end
    if popupType ~= nil and type(popupType) ~= "number" then
        V_FrameWork.Log("V_TppUiCommand.ShowHudPopupLangId: popupType is not a number.")
        return
    end
    return V_FrameWork.ShowHudPopupLangId(titleLabel or "", bodyLabel or "", popupType or 1)
end

-- HUD error popup with a NUMERIC error code.
function this.ShowHudErrorPopup(errorParam, popupType)
    if type(errorParam) ~= "number" then
        V_FrameWork.Log("V_TppUiCommand.ShowHudErrorPopup: errorParam is not a number.")
        return
    end
    if popupType ~= nil and type(popupType) ~= "number" then
        V_FrameWork.Log("V_TppUiCommand.ShowHudErrorPopup: popupType is not a number.")
        return
    end
    return V_FrameWork.ShowHudErrorPopup(errorParam, popupType or 1)
end

function this.Messages()
	return
	Tpp.StrCode32Table {
		Terminal = {
			{
				msg = "MbDvcActOpenTop",
				func = function()
					V_FrameWork.Log("#### Open Terminal ####")
                    if Ivars.V_FrameWork_FirstInstall:Get() == 0 then
                        V_FrameWork.Log("#### First Time Install ####")
                        Ivars.V_FrameWork_FirstInstall:Set(1)
                        V_TppUiCommand.ShowMbDvcAnnouncePopupLangId("MbDvcPopup_Title_FirstTimeInstall",  "MbDvcPopup_Text_FirstTimeInstall")
                    else
                        V_FrameWork.Log("#### Not First Time Install ####")
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
