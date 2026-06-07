local this = {}
local IsTypeString = Tpp.IsTypeString


function this.SetPlayerVoiceFpkPathForType(playerType, path)
    if type(playerType) ~= "number" then
        V_FrameWork.Log("V_TppPlayer.SetPlayerVoiceFpkPathForType: playerType is not a number.")
        return
    end
    if not IsTypeString(path) then
        V_FrameWork.Log("V_TppPlayer.SetPlayerVoiceFpkPathForType: path is not a string.")
        return
    end
    V_Player.SetPlayerVoiceFpkPathForType(playerType, path)
end

function this.ClearPlayerVoiceFpkPathForType(playerType)
    if type(playerType) ~= "number" then
        V_FrameWork.Log("V_TppPlayer.ClearPlayerVoiceFpkPathForType: playerType is not a number.")
        return
    end
    V_Player.ClearPlayerVoiceFpkPathForType(playerType)
end

function this.ClearAllPlayerVoiceFpkOverrides()
    V_FrameWork.Log("V_TppPlayer.ClearAllPlayerVoiceFpkOverrides: clearing all overrides.")
    V_Player.ClearAllPlayerVoiceFpkOverrides()
end

return this
