-- V_TppPlayer — player voice overrides.

local this = {}

-- Voice FPK overrides per player type (0=Snake, 1=DD_Male, 2=DD_Female, 3=Avatar).
function this.SetPlayerVoiceFpkPathForType(playerType, path)
    V_FrameWork.SetPlayerVoiceFpkPathForType(playerType, path)
end

function this.ClearPlayerVoiceFpkPathForType(playerType)
    V_FrameWork.ClearPlayerVoiceFpkPathForType(playerType)
end

function this.ClearAllPlayerVoiceFpkOverrides()
    V_FrameWork.ClearAllPlayerVoiceFpkOverrides()
end

return this
