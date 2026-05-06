-- V_TppPlayer — voice FPK overrides.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}

-- ============================================================
-- Player voice FPK overrides
-- ============================================================

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
