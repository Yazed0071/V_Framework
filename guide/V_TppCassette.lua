local this = {}
local IsTypeString = Tpp.IsTypeString


function this.PlayCassetteTape(trackOrName, isLoop, playAll)
    if trackOrName == nil then
        V_FrameWork.Log("V_TppCassette.PlayCassetteTape: trackOrName is nil.")
        return false
    end

    local trackId
    if IsTypeString(trackOrName) then
        trackId = V_CassetteCommand.GetTapeTrackDirectPlayId(trackOrName)
        if not trackId or trackId < 0 then
            V_FrameWork.Log("V_TppCassette.PlayCassetteTape: track not found: " .. tostring(trackOrName))
            return false
        end
    elseif type(trackOrName) == "number" then
        trackId = trackOrName
    else
        V_FrameWork.Log("V_TppCassette.PlayCassetteTape: trackOrName must be string or number.")
        return false
    end

    return V_CassetteCommand.PlayCassetteTapeByTrackId(0, trackId, isLoop or false, playAll or false)
end

function this.GetCassettePlayingTime()
    return V_CassetteCommand.GetCassettePlayingTime()
end

function this.GetCassettePlayingTrackId()
    return V_CassetteCommand.GetCassettePlayingTrackId()
end

function this.PauseCassette(fadeSec)
    if fadeSec ~= nil and type(fadeSec) ~= "number" then
        V_FrameWork.Log("V_TppCassette.PauseCassette: fadeSec is not a number.")
        return
    end
    return V_CassetteCommand.PauseCassette(fadeSec or 0)
end

function this.ResumeCassette(fadeSec)
    if fadeSec ~= nil and type(fadeSec) ~= "number" then
        V_FrameWork.Log("V_TppCassette.ResumeCassette: fadeSec is not a number.")
        return
    end
    return V_CassetteCommand.ResumeCassette(fadeSec or 0)
end

function this.StopCassette(fadeSec, stopByUser)
    if fadeSec ~= nil and type(fadeSec) ~= "number" then
        V_FrameWork.Log("V_TppCassette.StopCassette: fadeSec is not a number.")
        return
    end
    return V_CassetteCommand.StopCassette(fadeSec or 0, stopByUser or false)
end

function this.SetCassetteSpeakerEnabled(enable)
    if type(enable) ~= "boolean" and enable~= nil then
        V_FrameWork.Log("V_TppCassette.SetCassetteSpeakerEnabled: enable is not a boolean.")
        return
    elseif enable == nil then
        enable = true
    end
    return V_CassetteCommand.SetCassetteSpeakerEnabled(enable)
end

function this.IsCassetteSpeakerEnabled()
    return V_CassetteCommand.IsCassetteSpeakerEnabled()
end

return this
