local this = {}
local IsTypeString = Tpp.IsTypeString


function this.PlayCassetteTape(trackOrName, isLoop, playAll)
    if trackOrName == nil then
        V_FrameWork.Log("V_TppCassette.PlayCassetteTape: trackOrName is nil.")
        return false
    end

    local trackId
    if IsTypeString(trackOrName) then
        trackId = V_CassetteCommand.GetTapeTrackId(trackOrName)
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

    return V_CassetteCommand.PlayCassetteTapeByTrackId(trackId, isLoop or false, playAll or false)
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


function this.RegisterCustomCassetteAlbum(albumInfo, trackList)
    if type(albumInfo) ~= "table" or not albumInfo.albumId then
        V_FrameWork.Log("V_TppCassette.RegisterCustomCassetteAlbum: albumInfo.albumId required.")
        return false
    end

    local tracks = {}
    for i, track in ipairs(trackList or {}) do
        local t = {}
        for k, v in pairs(track) do t[k] = v end
        if not t.albumId then t.albumId = albumInfo.albumId end
        t.langId = t.langId or "sub_mission_untitle"
        tracks[i] = t
    end

    return V_CassetteCommand.RegisterCustomTapes({
        albums = {
            {
                albumId = albumInfo.albumId,
                langId  = albumInfo.langId or "sub_mission_untitle",
                type    = albumInfo.type,
            }
        },
        tracks = tracks,
    })
end


function this.RegisterRadioCassette(gimmickName, fox2Path, wwiseEvent, fileName)
    if not IsTypeString(gimmickName) then
        V_FrameWork.Log("V_TppCassette.RegisterRadioCassette: gimmickName must be a string.")
        return false
    end
    if fox2Path ~= nil and not IsTypeString(fox2Path) then
        V_FrameWork.Log("V_TppCassette.RegisterRadioCassette: fox2Path must be a string when provided.")
        return false
    end
    if wwiseEvent == nil then
        V_FrameWork.Log("V_TppCassette.RegisterRadioCassette: wwiseEvent (event name or id) is required.")
        return false
    end
    if not IsTypeString(wwiseEvent) and type(wwiseEvent) ~= "number" then
        V_FrameWork.Log("V_TppCassette.RegisterRadioCassette: wwiseEvent must be a string or number.")
        return false
    end
    if fileName ~= nil and not IsTypeString(fileName) then
        V_FrameWork.Log("V_TppCassette.RegisterRadioCassette: fileName must be a string when provided.")
        return false
    end
    return V_CassetteCommand.RegisterRadioCassette(gimmickName, fox2Path, wwiseEvent, fileName)
end

return this
