local this = {}
local IsTypeString = Tpp.IsTypeString

--V_TppGameObject.GAME_OVER_GENERAL
--V_TppGameObject.GAME_OVER_PARADOX
--V_TppGameObject.GAME_OVER_STEALTH
--V_TppGameObject.GAME_OVER_CYPRUS 

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
    return V_TppSoundDaemon.SetGameOverMusic(isEnable, gameOverType, playEvent or "", stopEvent or "")
end

return this
