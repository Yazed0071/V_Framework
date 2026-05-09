-- V_TppCassette — cassette tape playback.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}
local IsTypeString = Tpp.IsTypeString


-- trackOrName: string track name OR numeric trackId.
function this.PlayCassetteTape(trackOrName, isLoop, playAll)
    if trackOrName == nil then
        V_FrameWork.Log("V_TppCassette.PlayCassetteTape: trackOrName is nil.")
        return false
    end

    local trackId
    if IsTypeString(trackOrName) then
        trackId = V_FrameWork.GetTapeTrackDirectPlayId(trackOrName)
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

    return V_FrameWork.PlayCassetteTapeByTrackId(0, trackId, isLoop or false, playAll or false)
end

function this.GetCassettePlayingTime()
    return V_FrameWork.GetCassettePlayingTime()
end

function this.GetCassettePlayingTrackId()
    return V_FrameWork.GetCassettePlayingTrackId()
end

function this.PauseCassette(fadeSec)
    if fadeSec ~= nil and type(fadeSec) ~= "number" then
        V_FrameWork.Log("V_TppCassette.PauseCassette: fadeSec is not a number.")
        return
    end
    return V_FrameWork.PauseCassette(fadeSec or 0)
end

function this.ResumeCassette(fadeSec)
    if fadeSec ~= nil and type(fadeSec) ~= "number" then
        V_FrameWork.Log("V_TppCassette.ResumeCassette: fadeSec is not a number.")
        return
    end
    return V_FrameWork.ResumeCassette(fadeSec or 0)
end

function this.StopCassette(fadeSec, stopByUser)
    if fadeSec ~= nil and type(fadeSec) ~= "number" then
        V_FrameWork.Log("V_TppCassette.StopCassette: fadeSec is not a number.")
        return
    end
    return V_FrameWork.StopCassette(fadeSec or 0, stopByUser or false)
end

function this.SetCassetteSpeakerEnabled(enable)
    if type(enable) ~= "boolean" then
        V_FrameWork.Log("V_TppCassette.SetCassetteSpeakerEnabled: enable is not a boolean.")
        return
    end
    return V_FrameWork.SetCassetteSpeakerEnabled(enable)
end

function this.IsCassetteSpeakerEnabled()
    return V_FrameWork.IsCassetteSpeakerEnabled()
end

return this
