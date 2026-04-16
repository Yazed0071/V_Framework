-- V_TppUI.lua
-- V_FrameWork example: UI texture overrides
--
-- Shows how to override equip background textures, loading screen textures,
-- and game over screen textures. All paths are Fox Engine FTEX asset paths.
--
-- Usage: require this file in your mod's Lua entry point and call the functions
-- from your Init or module setup.

local this = {}

-- Set the default equip background texture for all weapons that have no
-- per-weapon override. Path is an FTEX asset path.
function this.SetDefaultEquipBgTexturePath(path)
    V_FrameWork.SetDefaultEquipBgTexturePath(path)
end

-- Clear the default equip background texture, restoring the vanilla default.
function this.ClearDefaultEquipBgTexture()
    V_FrameWork.ClearDefaultEquipBgTexture()
end

-- Set a per-weapon equip background texture for a specific equipId.
-- Overrides the default for that weapon only.
function this.SetEquipBgTexturePath(equipId, path)
    V_FrameWork.SetEquipBgTexturePath(equipId, path)
end

-- Clear the per-weapon equip background for a specific equipId.
function this.ClearEquipBgTexture(equipId)
    V_FrameWork.ClearEquipBgTexture(equipId)
end

-- Set the default equip background texture shown for enemy weapons
-- (the background displayed when aiming an enemy's weapon).
function this.SetEnemyWeaponBgTexturePath(path)
    V_FrameWork.SetEnemyWeaponBgTexturePath(path)
end

-- Clear the enemy weapon default equip background.
function this.ClearEnemyWeaponBgTexture()
    V_FrameWork.ClearEnemyWeaponBgTexture()
end

-- Set a per-equipId enemy equip background texture.
function this.SetEnemyEquipBgTexturePath(equipId, path)
    V_FrameWork.SetEnemyEquipBgTexturePath(equipId, path)
end

-- Clear a per-equipId enemy equip background texture.
function this.ClearEnemyEquipBgTexture(equipId)
    V_FrameWork.ClearEnemyEquipBgTexture(equipId)
end

-- Clear all per-weapon and default equip background overrides at once.
function this.ClearAllEquipBgTextures()
    V_FrameWork.ClearAllEquipBgTextures()
end

-- Set the main (foreground) texture on the mission loading splash screen.
function this.SetLoadingSplashMainTexturePath(path)
    V_FrameWork.SetLoadingSplashMainTexturePath(path)
end

-- Set the blurred background texture on the mission loading splash screen.
function this.SetLoadingSplashBlurTexturePath(path)
    V_FrameWork.SetLoadingSplashBlurTexturePath(path)
end

-- Clear both loading splash textures, restoring vanilla.
function this.ClearLoadingSplashTextures()
    V_FrameWork.ClearLoadingSplashTextures()
end

-- Set the main (foreground) texture on the game over splash screen.
function this.SetGameOverSplashMainTexturePath(path)
    V_FrameWork.SetGameOverSplashMainTexturePath(path)
end

-- Set the blurred background texture on the game over splash screen.
function this.SetGameOverSplashBlurTexturePath(path)
    V_FrameWork.SetGameOverSplashBlurTexturePath(path)
end

-- Clear both game over splash textures, restoring vanilla.
function this.ClearGameOverSplashTextures()
    V_FrameWork.ClearGameOverSplashTextures()
end

return this

--[[
Example usage:

local UI = require("V_TppUI")

-- Override the default background shown behind all weapons in the equip menu
UI.SetDefaultEquipBgTexturePath("/Assets/tpp/ui/texture/equip_bg/my_custom_bg.ftex")

-- Override only weapon equip ID 200
UI.SetEquipBgTexturePath(200, "/Assets/tpp/ui/texture/equip_bg/assault_bg.ftex")

-- Custom loading screen
UI.SetLoadingSplashMainTexturePath("/Assets/tpp/ui/texture/splash/loading_main.ftex")
UI.SetLoadingSplashBlurTexturePath("/Assets/tpp/ui/texture/splash/loading_blur.ftex")

-- Custom game over screen
UI.SetGameOverSplashMainTexturePath("/Assets/tpp/ui/texture/splash/gameover_main.ftex")
UI.SetGameOverSplashBlurTexturePath("/Assets/tpp/ui/texture/splash/gameover_blur.ftex")
]]
