-- V_TppHostage — lost-hostage tracking.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local StrCode32 = Fox.StrCode32
local IsTypeString = Tpp.IsTypeString

local this = {}

-- hostageLostLabel (optional): "prisoner gone" radio voice line. Accepts
-- EITHER a label-name string (e.g. "CPR0250_KEEP", hashed here via
-- StrCode32) OR a pre-hashed numeric StrCode32. Omit / nil for the built-in
-- male/female/child × taken matrix.
function this.SetLostHostage(hostageNameOrId, gender, hostageLostLabel)
    local labelHash = 0
    if hostageLostLabel ~= nil then
        if IsTypeString(hostageLostLabel) then
            labelHash = StrCode32(hostageLostLabel)
        else
            labelHash = hostageLostLabel
        end
    end
    V_FrameWork.SetLostHostage(hostageNameOrId, gender, labelHash)
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
