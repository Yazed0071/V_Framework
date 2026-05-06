-- V_TppHelicopter — heli pilot voice / radio event overrides.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}

-- Patches the embedded Wwise event hashes used by the heli pilot voice and
-- radio call sites (DD_vox_SH_voice + the three DD_vox_SH_radio sites) so
-- custom events fire instead of the vanilla ones.
--   isEnable    boolean  true to apply the patch, false to restore vanilla
--   voiceEvent  string   custom voice event name (FNV-1 hashed at runtime)
--   radioEvent  string   custom radio event name (applied to all 3 radio sites)
-- Returns boolean (success).
function this.SetEnableHeliVoice(isEnable, voiceEvent, radioEvent)
    return V_FrameWork.SetEnableHeliVoice(isEnable, voiceEvent, radioEvent)
end

return this
