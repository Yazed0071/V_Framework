-- V_TppHostage — lost-hostage tracking.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local StrCode32    = Fox.StrCode32
local IsTypeString = Tpp.IsTypeString

local this = {}


-- gender: 0 = male, 1 = female, 2 = child.
function this.SetLostHostage(hostageNameOrId, gender, hostageLostLabel)
    if hostageNameOrId == nil then
        V_FrameWork.Log("V_TppHostage.SetLostHostage: hostageNameOrId is nil.")
        return
    end
    if type(gender) ~= "number" then
        V_FrameWork.Log("V_TppHostage.SetLostHostage: gender is not a number.")
        return
    end

    local labelHash = 0
    if hostageLostLabel ~= nil then
        if IsTypeString(hostageLostLabel) then
            labelHash = StrCode32(hostageLostLabel)
        elseif type(hostageLostLabel) == "number" then
            labelHash = hostageLostLabel
        else
            V_FrameWork.Log("V_TppHostage.SetLostHostage: hostageLostLabel must be string or number.")
            return
        end
    end

    V_FrameWork.SetLostHostage(hostageNameOrId, gender, labelHash)
end

function this.RemoveLostHostage(hostageNameOrId)
    if hostageNameOrId == nil then
        V_FrameWork.Log("V_TppHostage.RemoveLostHostage: hostageNameOrId is nil.")
        return
    end
    V_FrameWork.RemoveLostHostage(hostageNameOrId)
end

function this.ClearLostHostages()
    V_FrameWork.ClearLostHostages()
end

function this.SetLostHostageFromPlayer(hostageNameOrId, enable)
    if hostageNameOrId == nil then
        V_FrameWork.Log("V_TppHostage.SetLostHostageFromPlayer: hostageNameOrId is nil.")
        return
    end
    if type(enable) ~= "boolean" then
        V_FrameWork.Log("V_TppHostage.SetLostHostageFromPlayer: enable is not a boolean.")
        return
    end
    V_FrameWork.SetLostHostageFromPlayer(hostageNameOrId, enable)
end

return this
