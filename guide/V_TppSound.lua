local this = {}
local GetGameObjectId = GameObject.GetGameObjectId
local IsTypeString = Tpp.IsTypeString

function this.SetSoldierVoicePitch(soldierNameOrId, cents)
    local gameObjectId = soldierNameOrId
    if IsTypeString(soldierNameOrId) then
        gameObjectId = GetGameObjectId(soldierNameOrId)
    end
    if gameObjectId == nil then return false end
    if type(cents) ~= "number" then
        V_FrameWork.Log("V_TppSound.SetSoldierVoicePitch: cents is not a number.")
        return false
    end
    return V_TppSoundDaemon.SetSoldierVoicePitch(gameObjectId, cents) -- WORK
end

function this.UnsetSoldierVoicePitch()
    V_TppSoundDaemon.UnsetSoldierVoicePitch() -- WORK
end

return this
