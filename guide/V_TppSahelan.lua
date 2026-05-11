
local this = {}
local IsTypeString=Tpp.IsTypeString

function this.SetSahelanFova(fv2Path)
    if not IsTypeString(fv2Path) then
        V_FrameWork.Log("V_Sahelan.SetSahelanFova: Invalid fv2Path provided. must be a string path to a .fv2 file.")
        return
    end

    if not fv2Path:match(".fv2$") then
        fv2Path = fv2Path .. ".fv2"
    end

    V_FrameWork.Log("V_Sahelan.SetSahelanFova: Setting Sahelan Fova to " .. fv2Path)
    V_FrameWork.SetSahelanFova(fv2Path)
end

-- Override the eye-lamp color for a specific AI mode (Sahelanthropus).
--   r, g, b      : color in 0..1 floats
--   pulseSpeed   : 1 = steady (no pulsing), 0 = normal speed pulse (1 Hz).
--                  Values in between lerp (e.g. 0.5 = half-speed pulse).
--                  Out-of-range values are clamped.
--   mode         : engine AI state (typically 0..5; visible in
--                  [EyeLamp] mode=... log lines when logging is on)
function this.SetEyeLampColor(r, g, b, pulseSpeed, mode)
    if type(r) ~= "number" or type(g) ~= "number" or type(b) ~= "number" then
        V_FrameWork.Log("V_Sahelan.SetEyeLampColor: r, g, b must be numbers.")
        return
    end
    if type(pulseSpeed) ~= "number" then
        V_FrameWork.Log("V_Sahelan.SetEyeLampColor: pulseSpeed must be a number.")
        return
    end
    if type(mode) ~= "number" then
        V_FrameWork.Log("V_Sahelan.SetEyeLampColor: mode must be a number.")
        return
    end
    V_FrameWork.SetEyeLampColor(r, g, b, pulseSpeed, mode)
end

function this.ClearEyeLampColor()
    V_FrameWork.ClearEyeLampColor()
end

-- Disco mode! Eye lamps cycle through the full rainbow continuously,
-- overriding any per-mode color you've set. Pure mood.
--   enabled : boolean — turn the party on / off
--   speed   : (optional) hue cycles per second. Default 2.0 — one full
--             rainbow lap every half-second. Try 0.5 for chill, 6 for
--             seizure mode.
function this.SetEyeLampDisco(enabled, speed)
    if type(enabled) ~= "boolean" then
        V_FrameWork.Log("V_Sahelan.SetEyeLampDisco: enabled must be a boolean.")
        return
    end
    if speed == nil then speed = 2.0 end
    if type(speed) ~= "number" then
        V_FrameWork.Log("V_Sahelan.SetEyeLampDisco: speed must be a number or nil.")
        return
    end
    V_FrameWork.SetEyeLampDisco(enabled, speed)
end

function this.SetEyeLampColorLogging(enabled)
    if type(enabled) ~= "boolean" then
        V_FrameWork.Log("V_Sahelan.SetEyeLampColorLogging: enabled must be a boolean.")
        return
    end
    V_FrameWork.SetEyeLampColorLogging(enabled)
end

V_FrameWork.ClearSahelanFova()

return this
