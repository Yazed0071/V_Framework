-- V_TppCassette.lua
-- V_FrameWork example: Cassette tape system
--
-- Shows how to play, pause, stop, and query cassette tapes, control the
-- speaker toggle, and register custom cassette albums and tracks.
--
-- Notes:
--   PlayCassetteTape accepts either a numeric trackId or a string track name.
--   When a name is given, GetTapeTrackDirectPlayId is called automatically to
--   resolve it. Returns false if the track could not be found or played.
--
--   PauseCassette/ResumeCassette/StopCassette each accept an optional fade
--   duration in milliseconds (integer). Omit or pass nil for an instant
--   pause/resume/stop.

local this = {}

-- Play a cassette tape.
-- trackOrName: numeric trackId OR a string track name (e.g. "track_heaven")
-- isLoop: (optional) true to loop the track. Default false.
-- playAll: (optional) true to play all tracks in the album. Default false.
-- Returns true on success, false on failure.
function this.PlayCassetteTape(trackOrName, isLoop, playAll)
    local albumIndex = 0  -- album 0 is the standard tape deck album
    local trackId

    if type(trackOrName) == "string" then
        -- Resolve the name to a numeric direct-play ID
        trackId = V_FrameWork.GetTapeTrackDirectPlayId(trackOrName)
        if not trackId or trackId < 0 then
            return false
        end
    else
        trackId = trackOrName
    end

    return V_FrameWork.PlayCassetteTapeByTrackId(albumIndex, trackId, isLoop or false, playAll or false)
end

-- Returns the current cassette playing time as a number (milliseconds).
function this.GetCassettePlayingTime()
    return V_FrameWork.GetCassettePlayingTime()
end

-- Returns the track ID of the currently playing cassette track.
function this.GetCassettePlayingTrackId()
    return V_FrameWork.GetCassettePlayingTrackId()
end

-- Pause the cassette.
-- fadeSec: (optional) fade-out duration in milliseconds. Default 0 (instant).
-- Returns an error code (0 = success).
function this.PauseCassette(fadeSec)
    return V_FrameWork.PauseCassette(fadeSec or 0)
end

-- Resume a paused cassette.
-- fadeSec: (optional) fade-in duration in milliseconds. Default 0 (instant).
-- Returns an error code (0 = success).
function this.ResumeCassette(fadeSec)
    return V_FrameWork.ResumeCassette(fadeSec or 0)
end

-- Stop the cassette.
-- fadeSec: (optional) fade-out duration in milliseconds. Default 0.
-- stopByUser: (optional) true if the stop is user-initiated. Default false.
-- Returns an error code (0 = success).
function this.StopCassette(fadeSec, stopByUser)
    return V_FrameWork.StopCassette(fadeSec or 0, stopByUser or false)
end

-- Enable or disable the cassette speaker mode (plays through the iDroid speaker).
-- Returns true on success.
function this.SetCassetteSpeakerEnabled(enable)
    return V_FrameWork.SetCassetteSpeakerEnabled(enable)
end

-- Returns true if the cassette speaker is currently enabled.
function this.IsCassetteSpeakerEnabled()
    return V_FrameWork.IsCassetteSpeakerEnabled()
end

-- Register a custom cassette album and its tracks.
--
-- albumInfo: table describing the album.
--   Required fields:
--     albumId  (string) -- unique identifier for the album, e.g. "ALBUM_MYMOD"
--     langId   (string) -- language entry ID for the album title
--     type     (string) -- album type string (e.g. "ORIGINAL")
--
-- trackList: array of track definition tables.
--   Required per entry:
--     fileName    (string) -- audio file name without extension
--     langId      (string) -- language entry ID for the track title
--   Optional per entry:
--     dataTimeJp  (number) -- Japanese release timestamp
--     dataTimeEn  (number) -- English release timestamp
--     saveIndex   (number) -- save-data slot index (-1 = auto)
--     important   (number) -- importance flag
--     special     (number) -- special flag
--     unlocked    (number) -- 1 if unlocked by default, 0 otherwise
--
-- Returns true on success.
function this.RegisterCustomCassetteAlbum(albumInfo, trackList)
    local tapes = {
        albums = {
            {
                albumId = albumInfo.albumId,
                langId  = albumInfo.langId,
                type    = albumInfo.type,
            }
        },
        tracks = trackList,
    }

    return V_FrameWork.RegisterCustomTapes(tapes)
end

return this

--[[
Example usage:

local Cassette = require("V_TppCassette")

-- Play a track by name
Cassette.PlayCassetteTape("track_heaven", false, false)

-- Play by track ID with looping
Cassette.PlayCassetteTape(5, true)

-- Pause with a 500ms fade
Cassette.PauseCassette(500)

-- Resume instantly
Cassette.ResumeCassette()

-- Stop with a 1000ms fade, mark as user-initiated
Cassette.StopCassette(1000, true)

-- Enable speaker mode
Cassette.SetCassetteSpeakerEnabled(true)

-- Register a custom album
Cassette.RegisterCustomCassetteAlbum(
    {
        albumId = "ALBUM_MYMOD",
        langId  = "MYMOD_ALBUM_TITLE",
        type    = "ORIGINAL",
    },
    {
        {
            fileName   = "mymod_track_01",
            langId     = "MYMOD_TRACK_01_TITLE",
            dataTimeJp = 19840101,
            dataTimeEn = 19840101,
            unlocked   = 1,
        },
        {
            fileName   = "mymod_track_02",
            langId     = "MYMOD_TRACK_02_TITLE",
            unlocked   = 1,
        },
    }
)
]]
