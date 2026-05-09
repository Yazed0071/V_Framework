-- V_TppMusicManager — game-over BGM event overrides.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}
local IsTypeString = Tpp.IsTypeString

-- Game-over scenario constants.
this.GAME_OVER_GENERAL = 0
this.GAME_OVER_PARADOX = 1
this.GAME_OVER_STEALTH = 2
this.GAME_OVER_CYPRUS  = 3


-- Patches embedded Wwise event hashes at the chosen game-over BGM call
-- site. isEnable=false restores vanilla. Returns boolean (success).
function this.SetGameOverMusic(isEnable, gameOverType, playEvent, stopEvent)
    if type(isEnable) ~= "boolean" then
        V_FrameWork.Log("V_TppMusicManager.SetGameOverMusic: isEnable is not a boolean.")
        return false
    end
    if type(gameOverType) ~= "number" or gameOverType < 0 or gameOverType > 3 then
        V_FrameWork.Log("V_TppMusicManager.SetGameOverMusic: type must be 0..3 (use GAME_OVER_* constants).")
        return false
    end
    if isEnable then
        if not IsTypeString(playEvent) then
            V_FrameWork.Log("V_TppMusicManager.SetGameOverMusic: playEvent is not a string.")
            return false
        end
        if not IsTypeString(stopEvent) then
            V_FrameWork.Log("V_TppMusicManager.SetGameOverMusic: stopEvent is not a string.")
            return false
        end
    end
    return V_FrameWork.SetGameOverMusic(isEnable, gameOverType, playEvent or "", stopEvent or "")
end

return this
