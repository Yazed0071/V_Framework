local this = {}
local GetGameObjectId = GameObject.GetGameObjectId
local IsTypeString = Tpp.IsTypeString


-- Per-soldier RTPC via AK::SoundEngine::SetRTPCValue with the engine
-- gameObjectId passed as akObj. rtpcNameOrId may be string or numeric id.
function this.SetRtpc(soldierNameOrId, rtpcNameOrId, value, timeMs)
    local gameObjectId = soldierNameOrId
    if IsTypeString(soldierNameOrId) then
        gameObjectId = GetGameObjectId(soldierNameOrId)
    end
    if rtpcNameOrId == nil then
        V_FrameWork.Log("V_SoundCoreDaemon.SetRtpc: rtpcNameOrId is nil.")
        return
    end
    if type(value) ~= "number" then
        V_FrameWork.Log("V_SoundCoreDaemon.SetRtpc: value is not a number.")
        return
    end
    if IsTypeString(rtpcNameOrId) then
        return V_TppSoundDaemon.SetSoldierRtpc(gameObjectId, rtpcNameOrId, value, timeMs or 0)
    end
    if type(rtpcNameOrId) == "number" then
        return V_TppSoundDaemon.SetSoldierRtpcById(gameObjectId, rtpcNameOrId, value, timeMs or 0)
    end
    V_FrameWork.Log("V_SoundCoreDaemon.SetRtpc: rtpcNameOrId must be string or number.")
end

-- Per-soldier RTPC via the SoundController chain. The soldier must have
-- been audio-Activated (mission load) so the chain is resolvable.
-- rtpcNameOrId may be a string (hashed via fox::sd::ConvertParameterID) or
-- a numeric id.
function this.SetSoldierRtpc(soldierNameOrId, rtpcNameOrId, value, timeMs)
    local gameObjectId = soldierNameOrId
    if IsTypeString(soldierNameOrId) then
        gameObjectId = GetGameObjectId(soldierNameOrId)
    end
    if gameObjectId == nil then return false end
    if type(value) ~= "number" then
        V_FrameWork.Log("V_SoundCoreDaemon.SetSoldierRtpc: value is not a number.")
        return false
    end
    if IsTypeString(rtpcNameOrId) then
        return V_TppSoundDaemon.SetSoldierObjectRtpcByName(gameObjectId, rtpcNameOrId, value, timeMs or 0)
    end
    if type(rtpcNameOrId) == "number" then
        return V_TppSoundDaemon.SetSoldierObjectRtpc(gameObjectId, rtpcNameOrId, value, timeMs or 0)
    end
    V_FrameWork.Log("V_SoundCoreDaemon.SetSoldierRtpc: rtpcNameOrId must be string or number.")
    return false
end

-- Raw RTPC on a specific Wwise AkGameObjectID.
function this.SetRtpcByAkObjId(akObjId, rtpcNameOrId, value, timeMs)
    if type(akObjId) ~= "number" then
        V_FrameWork.Log("V_SoundCoreDaemon.SetRtpcByAkObjId: akObjId must be a number.")
        return
    end
    if rtpcNameOrId == nil then
        V_FrameWork.Log("V_SoundCoreDaemon.SetRtpcByAkObjId: rtpcNameOrId is nil.")
        return
    end
    if type(value) ~= "number" then
        V_FrameWork.Log("V_SoundCoreDaemon.SetRtpcByAkObjId: value is not a number.")
        return
    end
    if IsTypeString(rtpcNameOrId) then
        return V_TppSoundDaemon.SetRtpcByAkObjId(akObjId, rtpcNameOrId, value, timeMs or 0)
    end
    if type(rtpcNameOrId) == "number" then
        return V_TppSoundDaemon.SetRtpcByAkObjIdById(akObjId, rtpcNameOrId, value, timeMs or 0)
    end
    V_FrameWork.Log("V_SoundCoreDaemon.SetRtpcByAkObjId: rtpcNameOrId must be string or number.")
end

-- Global (no akObj) RTPC.
function this.SetGlobalRtpc(rtpcNameOrId, value, timeMs)
    if rtpcNameOrId == nil then
        V_FrameWork.Log("V_SoundCoreDaemon.SetGlobalRtpc: rtpcNameOrId is nil.")
        return
    end
    if type(value) ~= "number" then
        V_FrameWork.Log("V_SoundCoreDaemon.SetGlobalRtpc: value is not a number.")
        return
    end
    if IsTypeString(rtpcNameOrId) then
        return V_TppSoundDaemon.SetGlobalRtpc(rtpcNameOrId, value, timeMs or 0)
    end
    if type(rtpcNameOrId) == "number" then
        return V_TppSoundDaemon.SetGlobalRtpcById(rtpcNameOrId, value, timeMs or 0)
    end
    V_FrameWork.Log("V_SoundCoreDaemon.SetGlobalRtpc: rtpcNameOrId must be string or number.")
end

-- Toggle verbose logging on AK::SoundEngine::SetRTPCValue. Returns previous state.
function this.SetRtpcLoggingEnabled(enabled)
    if type(enabled) ~= "boolean" then
        V_FrameWork.Log("V_SoundCoreDaemon.SetRtpcLoggingEnabled: enabled is not a boolean.")
        return
    end
    return V_TppSoundDaemon.SetRtpcLoggingEnabled(enabled)
end

function this.IsRtpcLoggingEnabled()
    return V_TppSoundDaemon.IsRtpcLoggingEnabled()
end


-- Global pitch bias added to every active sound's CAkResampler. Affects
-- voice, footsteps, gunfire, music — everything. Cents (100 = 1 semitone,
-- negative = lower). Engine clamps internally to ±2400. 0 = passthrough.
function this.SetGlobalVoicePitch(cents)
    if type(cents) ~= "number" then
        V_FrameWork.Log("V_SoundCoreDaemon.SetGlobalVoicePitch: cents is not a number.")
        return
    end
    V_TppSoundDaemon.SetGlobalVoicePitch(cents)
end

function this.GetGlobalVoicePitch()
    return V_TppSoundDaemon.GetGlobalVoicePitch()
end

-- Per-AkObjId pitch bias. Only the matched Wwise object's playbacks get
-- pitched. cents=0 removes the override for that akObjId.
function this.SetPitchByAkObjId(akObjId, cents)
    if type(akObjId) ~= "number" then
        V_FrameWork.Log("V_SoundCoreDaemon.SetPitchByAkObjId: akObjId must be a number.")
        return
    end
    if type(cents) ~= "number" then
        V_FrameWork.Log("V_SoundCoreDaemon.SetPitchByAkObjId: cents must be a number.")
        return
    end
    V_TppSoundDaemon.SetPitchByAkObjId(akObjId, cents)
end

function this.ClearPitchByAkObjId(akObjId)
    if type(akObjId) ~= "number" then
        V_FrameWork.Log("V_SoundCoreDaemon.ClearPitchByAkObjId: akObjId must be a number.")
        return
    end
    V_TppSoundDaemon.ClearPitchByAkObjId(akObjId)
end

function this.ClearAllPerAkObjIdPitchBiases()
    V_TppSoundDaemon.ClearAllPerAkObjIdPitchBiases()
end

-- Look up a soldier's most recently captured Wwise AkGameObjectID.
-- Returns 0 if the soldier hasn't been observed in a voice event yet.
function this.GetSoldierAkObjId(soldierNameOrId)
    local gameObjectId = soldierNameOrId
    if IsTypeString(soldierNameOrId) then
        gameObjectId = GetGameObjectId(soldierNameOrId)
    end
    if gameObjectId == nil then return 0 end
    return V_TppSoundDaemon.GetSoldierAkObjId(gameObjectId)
end

-- Per-soldier voice pitch — automatically re-applies to every fresh akObjId
-- the soldier registers (akObjIds churn through Wwise's 256-entry ring).
-- cents=0 cancels the override for that soldier.
function this.SetSoldierVoicePitch(soldierNameOrId, cents)
    local gameObjectId = soldierNameOrId
    if IsTypeString(soldierNameOrId) then
        gameObjectId = GetGameObjectId(soldierNameOrId)
    end
    if gameObjectId == nil then return false end
    if type(cents) ~= "number" then
        V_FrameWork.Log("V_SoundCoreDaemon.SetSoldierVoicePitch: cents is not a number.")
        return false
    end
    return V_TppSoundDaemon.SetSoldierVoicePitch(gameObjectId, cents)
end


return this
