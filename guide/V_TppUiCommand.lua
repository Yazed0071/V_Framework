-- V_TppUiCommand — UI texture overrides + iDroid AnnouncePopup customization.
-- Renamed from V_TppUI.lua to align with the underlying tpp::ui::UiCommand
-- layer that backs the popup helpers.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}
local IsTypeString=Tpp.IsTypeString


function this.SetEquipIdIconFtexPath(equipId, path)
    V_FrameWork.SetEquipIdIconFtexPath(equipId, path)
end

function this.SetDefaultEquipBgTexturePath(path)
    V_FrameWork.SetDefaultEquipBgTexturePath(path)
end

function this.ClearDefaultEquipBgTexture()
    V_FrameWork.ClearDefaultEquipBgTexture()
end

function this.SetEquipBgTexturePath(equipId, path)
    V_FrameWork.SetEquipBgTexturePath(equipId, path)
end

function this.ClearEquipBgTexture(equipId)
    V_FrameWork.ClearEquipBgTexture(equipId)
end

function this.SetEnemyWeaponBgTexturePath(path)
    V_FrameWork.SetEnemyWeaponBgTexturePath(path)
end

function this.ClearEnemyWeaponBgTexture()
    V_FrameWork.ClearEnemyWeaponBgTexture()
end

function this.SetEnemyEquipBgTexturePath(equipId, path)
    V_FrameWork.SetEnemyEquipBgTexturePath(equipId, path)
end

function this.ClearEnemyEquipBgTexture(equipId)
    V_FrameWork.ClearEnemyEquipBgTexture(equipId)
end

function this.ClearAllEquipBgTextures()
    V_FrameWork.ClearAllEquipBgTextures()
end

function this.SetLoadingSplashMainTexturePath(path)
    V_FrameWork.SetLoadingSplashMainTexturePath(path)
end

function this.SetLoadingSplashBlurTexturePath(path)
    V_FrameWork.SetLoadingSplashBlurTexturePath(path)
end

function this.ClearLoadingSplashTextures()
    V_FrameWork.ClearLoadingSplashTextures()
end

function this.SetGameOverSplashMainTexturePath(path)
    V_FrameWork.SetGameOverSplashMainTexturePath(path)
end

function this.SetGameOverSplashBlurTexturePath(path)
    V_FrameWork.SetGameOverSplashBlurTexturePath(path)
end

function this.ClearGameOverSplashTextures()
    V_FrameWork.ClearGameOverSplashTextures()
end

-- Overrides the loading splash main + blur textures used during mission
-- load transitions (LoadingScreenOrGameOverSplash2 and LoadingTipsEv). The
-- DD-emblem baked into the mission-start title card (telop) itself cannot
-- be swapped at runtime — that texture is bound inside the compiled .uilb
-- layout asset and only asset-level FPK replacement can change it.
function this.SetMissionTelopBgTexturePath(path)
    V_FrameWork.SetLoadingSplashMainTexturePath(path)
    V_FrameWork.SetLoadingSplashBlurTexturePath(path)
end

function this.ClearMissionTelopBgTexture()
    V_FrameWork.ClearLoadingSplashTextures()
end


-- ----------------------------------------------------------------------------
-- iDroid AnnouncePopup — custom title/body for the corner notification popup.
-- ----------------------------------------------------------------------------
--
-- Queues a popup with literal title and body strings. Multiple back-to-back
-- calls fan out across the controller's 16-slot ring and play sequentially as
-- the iDroid menu state machine cycles (one fades out, the next fades in).
--
-- It is safe to call this BEFORE opening the iDroid menu for the first time
-- during the current session — entries queue unreserved and the framework
-- drains them on the first popup pipeline tick. As a result, queued popups
-- can appear on the very first iDroid open.
--
-- Game popups always take priority: if the slot ring is full and MGSV itself
-- queues an announcement, the framework evicts our oldest pending custom
-- popup to make room and our queued text is dropped.
--
-- PARAMETERS:
--   title  string  Title bar text. Truncated to ~127 chars.
--   body   string  Body text. Truncated to ~1023 chars.
--
-- RETURNS: bool — true if queued + (eventually) reserved successfully.
function this.ShowMbDvcAnnouncePopup(title, body)
    return V_FrameWork.ShowMbDvcAnnouncePopup(title or "", body or "")
end


-- Same as ShowMbDvcAnnouncePopup but the arguments are LangId LABEL NAMES
-- whose displayable text comes from the game's localized string table. The
-- DLL hashes each label via Fox StrCode64 and resolves through the game's
-- lang manager at popup display time, so the shown text follows the current
-- locale.
--
-- Use this for built-in localized strings whose label names you know (e.g.
-- "msg_xxx" entries from the lang archive).
--
-- PARAMETERS:
--   titleLabel  string  Label name; hashed with StrCode64 inside the DLL.
--   bodyLabel   string  Label name; hashed with StrCode64 inside the DLL.
--
-- RETURNS: bool — true if queued + (eventually) reserved successfully.
function this.ShowMbDvcAnnouncePopupLangId(titleLabel, bodyLabel)
    return V_FrameWork.ShowMbDvcAnnouncePopupLangId(titleLabel or "", bodyLabel or "")
end


return this
