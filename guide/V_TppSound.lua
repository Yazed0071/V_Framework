local this = {}
local GetGameObjectId = GameObject.GetGameObjectId
local IsTypeString = Tpp.IsTypeString

function this.SetRtpc(soldierNameOrId, rtpcNameOrId, value, timeMs)
    local gameObjectId = soldierNameOrId
    if IsTypeString(soldierNameOrId) then
        gameObjectId = GetGameObjectId(soldierNameOrId)
    end
    if rtpcNameOrId == nil then
        V_FrameWork.Log("V_TppSound.SetRtpc: rtpcNameOrId is nil.")
        return
    end
    if type(value) ~= "number" then
        V_FrameWork.Log("V_TppSound.SetRtpc: value is not a number.")
        return
    end
    if IsTypeString(rtpcNameOrId) then
        return V_TppSoundDaemon.SetSoldierRtpc(gameObjectId, rtpcNameOrId, value, timeMs or 0)
    end
    if type(rtpcNameOrId) == "number" then
        return V_TppSoundDaemon.SetSoldierRtpcById(gameObjectId, rtpcNameOrId, value, timeMs or 0)
    end
    V_FrameWork.Log("V_TppSound.SetRtpc: rtpcNameOrId must be string or number.")
end

function this.SetSoldierRtpc(soldierNameOrId, rtpcNameOrId, value, timeMs)
    local gameObjectId = soldierNameOrId
    if IsTypeString(soldierNameOrId) then
        gameObjectId = GetGameObjectId(soldierNameOrId)
    end
    if gameObjectId == nil then return false end
    if type(value) ~= "number" then
        V_FrameWork.Log("V_TppSound.SetSoldierRtpc: value is not a number.")
        return false
    end
    if IsTypeString(rtpcNameOrId) then
        return V_TppSoundDaemon.SetSoldierObjectRtpcByName(gameObjectId, rtpcNameOrId, value, timeMs or 0)
    end
    if type(rtpcNameOrId) == "number" then
        return V_TppSoundDaemon.SetSoldierObjectRtpc(gameObjectId, rtpcNameOrId, value, timeMs or 0)
    end
    V_FrameWork.Log("V_TppSound.SetSoldierRtpc: rtpcNameOrId must be string or number.")
    return false
end

function this.SetRtpcByAkObjId(akObjId, rtpcNameOrId, value, timeMs)
    if type(akObjId) ~= "number" then
        V_FrameWork.Log("V_TppSound.SetRtpcByAkObjId: akObjId must be a number.")
        return
    end
    if rtpcNameOrId == nil then
        V_FrameWork.Log("V_TppSound.SetRtpcByAkObjId: rtpcNameOrId is nil.")
        return
    end
    if type(value) ~= "number" then
        V_FrameWork.Log("V_TppSound.SetRtpcByAkObjId: value is not a number.")
        return
    end
    if IsTypeString(rtpcNameOrId) then
        return V_TppSoundDaemon.SetRtpcByAkObjId(akObjId, rtpcNameOrId, value, timeMs or 0)
    end
    if type(rtpcNameOrId) == "number" then
        return V_TppSoundDaemon.SetRtpcByAkObjIdById(akObjId, rtpcNameOrId, value, timeMs or 0)
    end
    V_FrameWork.Log("V_TppSound.SetRtpcByAkObjId: rtpcNameOrId must be string or number.")
end

function this.SetGlobalRtpc(rtpcNameOrId, value, timeMs)
    if rtpcNameOrId == nil then
        V_FrameWork.Log("V_TppSound.SetGlobalRtpc: rtpcNameOrId is nil.")
        return
    end
    if type(value) ~= "number" then
        V_FrameWork.Log("V_TppSound.SetGlobalRtpc: value is not a number.")
        return
    end
    if IsTypeString(rtpcNameOrId) then
        return V_TppSoundDaemon.SetGlobalRtpc(rtpcNameOrId, value, timeMs or 0)
    end
    if type(rtpcNameOrId) == "number" then
        return V_TppSoundDaemon.SetGlobalRtpcById(rtpcNameOrId, value, timeMs or 0)
    end
    V_FrameWork.Log("V_TppSound.SetGlobalRtpc: rtpcNameOrId must be string or number.")
end

function this.SetRtpcLoggingEnabled(enabled)
    if type(enabled) ~= "boolean" then
        V_FrameWork.Log("V_TppSound.SetRtpcLoggingEnabled: enabled is not a boolean.")
        return
    end
    return V_TppSoundDaemon.SetRtpcLoggingEnabled(enabled)
end

function this.IsRtpcLoggingEnabled()
    return V_TppSoundDaemon.IsRtpcLoggingEnabled()
end

function this.SetGlobalVoicePitch(cents)
    if type(cents) ~= "number" then
        V_FrameWork.Log("V_TppSound.SetGlobalVoicePitch: cents is not a number.")
        return
    end
    V_TppSoundDaemon.SetGlobalVoicePitch(cents)
end

function this.GetGlobalVoicePitch()
    return V_TppSoundDaemon.GetGlobalVoicePitch()
end

function this.SetPitchByAkObjId(akObjId, cents)
    if type(akObjId) ~= "number" then
        V_FrameWork.Log("V_TppSound.SetPitchByAkObjId: akObjId must be a number.")
        return
    end
    if type(cents) ~= "number" then
        V_FrameWork.Log("V_TppSound.SetPitchByAkObjId: cents must be a number.")
        return
    end
    V_TppSoundDaemon.SetPitchByAkObjId(akObjId, cents)
end

function this.ClearPitchByAkObjId(akObjId)
    if type(akObjId) ~= "number" then
        V_FrameWork.Log("V_TppSound.ClearPitchByAkObjId: akObjId must be a number.")
        return
    end
    V_TppSoundDaemon.ClearPitchByAkObjId(akObjId)
end

function this.ClearAllPerAkObjIdPitchBiases()
    V_TppSoundDaemon.ClearAllPerAkObjIdPitchBiases()
end

function this.GetSoldierAkObjId(soldierNameOrId)
    local gameObjectId = soldierNameOrId
    if IsTypeString(soldierNameOrId) then
        gameObjectId = GetGameObjectId(soldierNameOrId)
    end
    if gameObjectId == nil then return 0 end
    return V_TppSoundDaemon.GetSoldierAkObjId(gameObjectId)
end

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
    return V_TppSoundDaemon.SetSoldierVoicePitch(gameObjectId, cents)
end


return this
