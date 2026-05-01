-- V_TppUI — UI texture overrides (equip BGs, loading/game-over splashes, telop).
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}

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


function this.ShowIDroidPopup(text, popupType)
    return V_FrameWork.ShowIDroidPopup(text, popupType or 1)
end


return this
