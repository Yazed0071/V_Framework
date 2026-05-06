-- V_TppMusicManager — game-over BGM event overrides.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}

-- Game-over scenario constants (mirror of GAME_OVER_TYPE in C++).
this.GAME_OVER_GENERAL = 0
this.GAME_OVER_PARADOX = 1
this.GAME_OVER_STEALTH = 2
this.GAME_OVER_CYPRUS  = 3

-- Patches the embedded Wwise event hashes at the chosen game-over BGM call
-- sites so a custom event plays instead of the vanilla one.
--   isEnable    boolean  true to apply the patch, false to restore vanilla
--   type        number   0..3 (use this.GAME_OVER_* constants)
--   playEvent   string   custom Play event name (FNV-1 hashed at runtime)
--   stopEvent   string   custom Stop event name
-- Returns boolean (success).
function this.SetGameOverMusic(isEnable, type, playEvent, stopEvent)
    return V_FrameWork.SetGameOverMusic(isEnable, type, playEvent, stopEvent)
end

return this
