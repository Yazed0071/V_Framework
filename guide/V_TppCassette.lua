-- V_TppCassette — cassette tape playback + custom album/track registration.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}

-- Play a tape by numeric trackId or by track name (name is resolved via
-- GetTapeTrackDirectPlayId). Album index 0 is the standard tape deck.
function this.PlayCassetteTape(trackOrName, isLoop, playAll)
    local trackId
    if type(trackOrName) == "string" then
        trackId = V_FrameWork.GetTapeTrackDirectPlayId(trackOrName)
        if not trackId or trackId < 0 then return false end
    else
        trackId = trackOrName
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
    return V_FrameWork.PauseCassette(fadeSec or 0)
end

function this.ResumeCassette(fadeSec)
    return V_FrameWork.ResumeCassette(fadeSec or 0)
end

function this.StopCassette(fadeSec, stopByUser)
    return V_FrameWork.StopCassette(fadeSec or 0, stopByUser or false)
end

function this.SetCassetteSpeakerEnabled(enable)
    return V_FrameWork.SetCassetteSpeakerEnabled(enable)
end

function this.IsCassetteSpeakerEnabled()
    return V_FrameWork.IsCassetteSpeakerEnabled()
end

return this
