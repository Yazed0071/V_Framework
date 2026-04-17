-- V_TppHostage — lost-hostage tracking.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}

function this.SetLostHostage(hostageNameOrId, gender)
    V_FrameWork.SetLostHostage(hostageNameOrId, gender)
end

function this.RemoveLostHostage(hostageNameOrId)
    V_FrameWork.RemoveLostHostage(hostageNameOrId)
end

function this.ClearLostHostages()
    V_FrameWork.ClearLostHostages()
end

function this.SetLostHostageFromPlayer(hostageNameOrId, enable)
    V_FrameWork.SetLostHostageFromPlayer(hostageNameOrId, enable)
end

return this
