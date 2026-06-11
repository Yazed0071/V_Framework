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
        if not (IsTypeString(playEvent) or type(playEvent) == "number") then
            V_FrameWork.Log("V_TppMusicManager.SetGameOverMusic: playEvent must be a string (event name) or number (event hash).")
            return false
        end
        if not (IsTypeString(stopEvent) or type(stopEvent) == "number") then
            V_FrameWork.Log("V_TppMusicManager.SetGameOverMusic: stopEvent must be a string (event name) or number (event hash).")
            return false
        end
    end
    return V_TppSoundDaemon.SetGameOverMusic(isEnable, gameOverType, playEvent, stopEvent)
end

return this
