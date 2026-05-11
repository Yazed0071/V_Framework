local this = {}
local StrCode32 = Fox.StrCode32
local StrCode32Table = Tpp.StrCode32Table
local GetGameObjectId = GameObject.GetGameObjectId
local IsTypeString = Tpp.IsTypeString
local NULL_ID = GameObject.NULL_ID


function this.SetVIPImportant(soldierNameOrId, isOfficer, foundDeadBodyRadioLabel)
    local id = GetGameObjectId(soldierNameOrId)
    if id == nil then return end
    if isOfficer ~= nil and type(isOfficer) ~= "boolean" then
        V_FrameWork.Log("V_TppEnemy.SetVIPImportant: isOfficer is not a boolean.")
        return
    end
    V_FrameWork.SetVIPImportant(id, isOfficer or false, foundDeadBodyRadioLabel)
end

function this.RemoveVIPImportant(soldierNameOrId)
    local id = GetGameObjectId(soldierNameOrId)
    if id == nil then return end
    V_FrameWork.RemoveVIPImportant(id)
end

function this.ClearVIPImportant()
    V_FrameWork.ClearVIPImportant()
end

function this.SetUseConcernedHoldupRecovery(enable)
    if enable == nil then enable = true end
    if type(enable) ~= "boolean" then
        V_FrameWork.Log("V_TppEnemy.SetUseConcernedHoldupRecovery: enable is not a boolean.")
        return
    end
    V_FrameWork.SetUseConcernedHoldupRecovery(enable)
end

function this.HoldUpReactionCowardlyReaction(enable)
    if enable == nil then enable = true end
    if type(enable) ~= "boolean" then
        V_FrameWork.Log("V_TppEnemy.HoldUpReactionCowardlyReaction: enable is not a boolean.")
        return
    end
    V_FrameWork.HoldUpReactionCowardlyReaction(enable)
end

function this.AddCallSignPatrolSoldier(soldierNameOrId)
    local gameId = GetGameObjectId(soldierNameOrId)
    if gameId == nil then return end

    V_FrameWork.AddCallSignPatrolSoldier(gameId)
end

function this.RemoveCallSignPatrolSoldier(soldierNameOrId)
    local gameId = GetGameObjectId(soldierNameOrId)
    if gameId == nil then return end
    V_FrameWork.RemoveCallSignPatrolSoldier(gameId)
end

function this.ClearCallSignPatrolSoldiers()
    V_FrameWork.ClearCallSignPatrolSoldiers()
end

-- enable defaults to true.
function this.EnableSoldierStealthCamo(soldierNameOrId, enable)
    local gameId = GetGameObjectId(soldierNameOrId)
    if gameId == nil then return end
    if enable == nil then enable = true end
    if type(enable) ~= "boolean" then
        V_FrameWork.Log("V_TppEnemy.EnableSoldierStealthCamo: enable is not a boolean.")
        return
    end
    V_FrameWork.EnableSoldierStealthCamo(gameId, enable)
end

function this.ClearSoldierStealthCamoOverrides()
    V_FrameWork.ClearSoldierStealthCamoOverrides()
end

-- Per-soldier RTPC. rtpcNameOrId may be string or numeric id.
function this.SetRtpc(soldierNameOrId, rtpcNameOrId, value, timeMs)
    local gameObjectId = soldierNameOrId
    if IsTypeString(soldierNameOrId) then
        gameObjectId = GetGameObjectId(soldierNameOrId)
    end
    if rtpcNameOrId == nil then
        V_FrameWork.Log("V_TppEnemy.SetRtpc: rtpcNameOrId is nil.")
        return
    end
    if type(value) ~= "number" then
        V_FrameWork.Log("V_TppEnemy.SetRtpc: value is not a number.")
        return
    end
    if IsTypeString(rtpcNameOrId) then
        return V_FrameWork.SetSoldierRtpc(gameObjectId, rtpcNameOrId, value, timeMs or 0)
    end
    if type(rtpcNameOrId) == "number" then
        return V_FrameWork.SetSoldierRtpcById(gameObjectId, rtpcNameOrId, value, timeMs or 0)
    end
    V_FrameWork.Log("V_TppEnemy.SetRtpc: rtpcNameOrId must be string or number.")
end

-- Per-soldier RTPC via the SoundController chain. The soldier must have been
-- voice-resolved at least once (i.e. spoken / been near the player) so the
-- voice-type hook has cached its SoundController. rtpcNameOrId may be a
-- string (DLL hashes via fox::sd::ConvertParameterID) or a numeric id.
function this.SetSoldierRtpc(soldierNameOrId, rtpcNameOrId, value, timeMs)
    local gameObjectId = soldierNameOrId
    if IsTypeString(soldierNameOrId) then
        gameObjectId = GetGameObjectId(soldierNameOrId)
    end
    if gameObjectId == nil then return false end
    if type(value) ~= "number" then
        V_FrameWork.Log("V_TppEnemy.SetSoldierRtpc: value is not a number.")
        return false
    end
    if IsTypeString(rtpcNameOrId) then
        return V_FrameWork.SetSoldierObjectRtpcByName(gameObjectId, rtpcNameOrId, value, timeMs or 0)
    end
    if type(rtpcNameOrId) == "number" then
        return V_FrameWork.SetSoldierObjectRtpc(gameObjectId, rtpcNameOrId, value, timeMs or 0)
    end
    V_FrameWork.Log("V_TppEnemy.SetSoldierRtpc: rtpcNameOrId must be string or number.")
    return false
end

function this.SetRtpcByAkObjId(akObjId, rtpcNameOrId, value, timeMs)
    if type(akObjId) ~= "number" then
        V_FrameWork.Log("V_TppEnemy.SetRtpcByAkObjId: akObjId must be a number.")
        return
    end
    if rtpcNameOrId == nil then
        V_FrameWork.Log("V_TppEnemy.SetRtpcByAkObjId: rtpcNameOrId is nil.")
        return
    end
    if type(value) ~= "number" then
        V_FrameWork.Log("V_TppEnemy.SetRtpcByAkObjId: value is not a number.")
        return
    end
    if IsTypeString(rtpcNameOrId) then
        return V_FrameWork.SetRtpcByAkObjId(akObjId, rtpcNameOrId, value, timeMs or 0)
    end
    if type(rtpcNameOrId) == "number" then
        return V_FrameWork.SetRtpcByAkObjIdById(akObjId, rtpcNameOrId, value, timeMs or 0)
    end
    V_FrameWork.Log("V_TppEnemy.SetRtpcByAkObjId: rtpcNameOrId must be string or number.")
end


-- Toggle verbose logging on AK::SoundEngine::SetRTPCValue. Returns previous state.
function this.SetRtpcLoggingEnabled(enabled)
    if type(enabled) ~= "boolean" then
        V_FrameWork.Log("V_TppEnemy.SetRtpcLoggingEnabled: enabled is not a boolean.")
        return
    end
    return V_FrameWork.SetRtpcLoggingEnabled(enabled)
end


function this.IsRtpcLoggingEnabled()
    return V_FrameWork.IsRtpcLoggingEnabled()
end

-- PHASE 0 PROOF OF CONCEPT — global pitch bias on EVERY active sound's
-- CAkResampler. Affects voice, footsteps, gunfire, music — everything.
-- Use this to confirm the engine accepts runtime pitch override at all.
-- Once confirmed, we narrow to per-soldier in Phase 1.
--   cents : pitch shift in cents (100 cents = 1 semitone). Negative = lower.
--           Engine clamps internally to ±2400. 0 = passthrough.
function this.SetGlobalVoicePitch(cents)
    if type(cents) ~= "number" then
        V_FrameWork.Log("V_TppEnemy.SetGlobalVoicePitch: cents is not a number.")
        return
    end
    V_FrameWork.SetGlobalVoicePitch(cents)
end

function this.GetGlobalVoicePitch()
    return V_FrameWork.GetGlobalVoicePitch()
end

-- HOT-PATH logging on every SetPitch call (throttled 1/200). Toggle off
-- once you've confirmed the hook fires.
function this.SetVoicePitchHookLogging(enabled)
    if type(enabled) ~= "boolean" then
        V_FrameWork.Log("V_TppEnemy.SetVoicePitchHookLogging: enabled is not a boolean.")
        return
    end
    V_FrameWork.SetVoicePitchHookLogging(enabled)
end


-- PHASE 1 — per-AkObjId pitch shift. Only the matched Wwise object's
-- playbacks get pitched. Pair with SetRtpcLoggingEnabled(true) to identify
-- a soldier's akObjId from the [AK] log lines.
--   akObjId : the Wwise game-object ID seen in [AK] SetRTPC akObj=0x...
--   cents   : pitch shift in cents (negative = lower). 0 removes override.
function this.SetPitchByAkObjId(akObjId, cents)
    if type(akObjId) ~= "number" then
        V_FrameWork.Log("V_TppEnemy.SetPitchByAkObjId: akObjId must be a number.")
        return
    end
    if type(cents) ~= "number" then
        V_FrameWork.Log("V_TppEnemy.SetPitchByAkObjId: cents must be a number.")
        return
    end
    V_FrameWork.SetPitchByAkObjId(akObjId, cents)
end

function this.ClearPitchByAkObjId(akObjId)
    if type(akObjId) ~= "number" then
        V_FrameWork.Log("V_TppEnemy.ClearPitchByAkObjId: akObjId must be a number.")
        return
    end
    V_FrameWork.ClearPitchByAkObjId(akObjId)
end

function this.ClearAllPerAkObjIdPitchBiases()
    V_FrameWork.ClearAllPerAkObjIdPitchBiases()
end

-- When ON, every SetPitch log line also includes the resolved akObjId so
-- you can correlate "this=0x..." with a soldier's akObjId. Hot path —
-- only enable while you're discovering IDs.
function this.SetVoicePitchChainLogging(enabled)
    if type(enabled) ~= "boolean" then
        V_FrameWork.Log("V_TppEnemy.SetVoicePitchChainLogging: enabled is not a boolean.")
        return
    end
    V_FrameWork.SetVoicePitchChainLogging(enabled)
end


-- Translate a soldier (name or gameObjectId) to their Wwise AkGameObjectID.
-- Returns 0 if the soldier hasn't been voice-resolved yet (must have been
-- processed by the engine's voice variant resolver — usually after a
-- soldier has spoken or been processed once during gameplay).
function this.GetSoldierAkObjId(soldierNameOrId)
    local gameObjectId = soldierNameOrId
    if IsTypeString(soldierNameOrId) then
        gameObjectId = GetGameObjectId(soldierNameOrId)
    end
    if gameObjectId == nil then return 0 end
    return V_FrameWork.GetSoldierAkObjId(gameObjectId)
end


-- Per-soldier voice pitch — fully automatic.
-- Walks: gameObjectId -> SoundController -> SourceMgr -> Source -> Object
--                     -> AudioGameObject::objectID (akObjId)
-- Then registers a per-akObjId pitch bias so only that soldier's audio is
-- shifted, leaving everyone else untouched.
--
-- Safe to call AT MISSION START — if the soldier hasn't been voice-resolved
-- yet, the request is queued and applied automatically the moment the
-- engine resolves their voice variant (typically when they speak, get
-- noticed, or are otherwise audio-active).
--
--   soldierNameOrId : soldier name string or numeric gameObjectId.
--   cents           : pitch shift (negative = lower). 0 cancels.
-- Returns true if applied immediately, false if queued (still success).
function this.SetSoldierVoicePitch(soldierNameOrId, cents)
    local gameObjectId = soldierNameOrId
    if IsTypeString(soldierNameOrId) then
        gameObjectId = GetGameObjectId(soldierNameOrId)
    end
    if gameObjectId == nil then return false end
    if type(cents) ~= "number" then
        V_FrameWork.Log("V_TppEnemy.SetSoldierVoicePitch: cents is not a number.")
        return false
    end
    return V_FrameWork.SetSoldierVoicePitch(gameObjectId, cents)
end

-- Global RTPC.
function this.SetGlobalRtpc(rtpcNameOrId, value, timeMs)
    if rtpcNameOrId == nil then
        V_FrameWork.Log("V_TppEnemy.SetGlobalRtpc: rtpcNameOrId is nil.")
        return
    end
    if type(value) ~= "number" then
        V_FrameWork.Log("V_TppEnemy.SetGlobalRtpc: value is not a number.")
        return
    end
    if IsTypeString(rtpcNameOrId) then
        return V_FrameWork.SetGlobalRtpc(rtpcNameOrId, value, timeMs or 0)
    end
    if type(rtpcNameOrId) == "number" then
        return V_FrameWork.SetGlobalRtpcById(rtpcNameOrId, value, timeMs or 0)
    end
    V_FrameWork.Log("V_TppEnemy.SetGlobalRtpc: rtpcNameOrId must be string or number.")
end

this.VIP_MISSION_LIST = {
    [10036] = {
        {
            name ="sol_vip_0000",
            isOfficer = false
        }
    },
    [10041] = {
        {
            name ="sol_vip_field",
            isOfficer = false
        },
        {
            name ="sol_vip_village",
            isOfficer = false
        },
        {
            name ="sol_vip_enemyBase",
            isOfficer = false
        }
    },
    [10044] = {
        {
            name ="sol_enemyNorth_lvVIP",
            isOfficer = false
        },
    },
    [10086] = {
        {
            name ="sol_interpreter",
            isOfficer = false
        },
        {
            name ="sol_interrogator",
            isOfficer = true
        },
    },
    [10195] = {
        {
            name ="sol_vip",
            isOfficer = true
        },
        {
            name ="sol_dealer",
            isOfficer = true
        },
    },
    [10121] = {
        {
            name ="sol_pfCamp_vip_0001",
            isOfficer = true
        },
        {
            name ="sol_pfCamp_vip_guard",
            isOfficer = true
        },
    },
    [10115] = {
        {
            name ="ly003_cl02_10115_npc0000|cl02pl0_uq_0020_npc|sol_plnt0_0000",
            isOfficer = false
        },
    },
    [10211] = {
        {
            name ="sol_mis_0000",
            isOfficer = false
        },
    },
    [10171] = {
        {
            name ="sol_pfCamp_vip",
            isOfficer = false
        },
    },

}

function this.GetMissionCode()
    local missionCode = TppMission.GetMissionID()
    if TppMission.IsHardMission(missionCode) then
        missionCode = TppMission.GetNormalMissionCodeFromHardMission(missionCode)
    end
    return missionCode
end

function this.SetUpEnemy()
    local missionCode = this.GetMissionCode()
    if this.VIP_MISSION_LIST[missionCode] then
        for _, vipInfo in ipairs(this.VIP_MISSION_LIST[missionCode]) do
            if missionCode == 10121 then
                this.SetVIPImportant(vipInfo.name, vipInfo.isOfficer, "V_CPRGZ0040")
            else
                this.SetVIPImportant(vipInfo.name, vipInfo.isOfficer)
            end
        end
    else
        V_FrameWork.Log("No VIP setup for mission " .. tostring(missionCode))
    end
end

return this
