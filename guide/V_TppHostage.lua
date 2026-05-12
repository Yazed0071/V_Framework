-- V_TppHostage — lost-hostage tracking.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}
local StrCode32       = Fox.StrCode32
local IsTypeString    = Tpp.IsTypeString
local GetGameObjectId = GameObject.GetGameObjectId


-- gender: 0 = male, 1 = female, 2 = child.
-- hostageLostLabel is OPTIONAL. Accepts a LangId string (hashed to
-- StrCode32 on the C++ side), a pre-hashed 32-bit number, or nil/omitted
-- to fall back to the engine's male/female/child x taken/not-taken matrix.
function this.SetLostHostage(hostageNameOrId, gender, hostageLostLabel)
    if hostageNameOrId == nil then
        V_FrameWork.Log("V_TppHostage.SetLostHostage: hostageNameOrId is nil.")
        return
    end
    if IsTypeString(hostageNameOrId) then
        local id = GetGameObjectId(hostageNameOrId)
        if id == nil then
            V_FrameWork.Log("V_TppHostage.SetLostHostage: No game object found for name: " .. hostageNameOrId)
            return
        end
        hostageNameOrId = id
    end
    if type(gender) ~= "number" then
        V_FrameWork.Log("V_TppHostage.SetLostHostage: gender is not a number.")
        return
    end
    if hostageLostLabel ~= nil
        and not IsTypeString(hostageLostLabel)
        and type(hostageLostLabel) ~= "number" then
        V_FrameWork.Log("V_TppHostage.SetLostHostage: hostageLostLabel must be a string, number, or nil.")
        return
    end

    V_FrameWork.SetLostHostage(hostageNameOrId, gender, hostageLostLabel or 0)
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


function this.SetUpEnemy()
    this.ClearLostHostages()
    this.SetLostHostage("hos_target_0000", 0)
    this.SetLostHostage("hos_target_0001", 1)
end

return this
