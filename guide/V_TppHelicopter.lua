-- V_TppHelicopter — heli pilot voice / radio event overrides.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}
local IsTypeString = Tpp.IsTypeString


-- Patches DD_vox_SH_voice + the three DD_vox_SH_radio sites with custom
-- Wwise event names (FNV-1 hashed at runtime). isEnable=false restores
-- vanilla. Returns boolean (success).
function this.SetEnableHeliVoice(isEnable, voiceEvent, radioEvent)
    if type(isEnable) ~= "boolean" then
        V_FrameWork.Log("V_TppHelicopter.SetEnableHeliVoice: isEnable is not a boolean.")
        return false
    end
    if isEnable then
        if not IsTypeString(voiceEvent) then
            V_FrameWork.Log("V_TppHelicopter.SetEnableHeliVoice: voiceEvent is not a string.")
            return false
        end
        if not IsTypeString(radioEvent) then
            V_FrameWork.Log("V_TppHelicopter.SetEnableHeliVoice: radioEvent is not a string.")
            return false
        end
    end
    return V_FrameWork.SetEnableHeliVoice(isEnable, voiceEvent or "", radioEvent or "")
end

return this
