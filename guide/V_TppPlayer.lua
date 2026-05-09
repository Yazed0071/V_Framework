-- V_TppPlayer — voice FPK overrides.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

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
    V_FrameWork.SetPlayerVoiceFpkPathForType(playerType, path)
end

function this.ClearPlayerVoiceFpkPathForType(playerType)
    if type(playerType) ~= "number" then
        V_FrameWork.Log("V_TppPlayer.ClearPlayerVoiceFpkPathForType: playerType is not a number.")
        return
    end
    V_FrameWork.ClearPlayerVoiceFpkPathForType(playerType)
end

function this.ClearAllPlayerVoiceFpkOverrides()
    V_FrameWork.Log("V_TppPlayer.ClearAllPlayerVoiceFpkOverrides: clearing all overrides.")
    V_FrameWork.ClearAllPlayerVoiceFpkOverrides()
end

return this
