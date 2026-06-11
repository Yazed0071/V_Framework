
local this = {}
local IsTypeString=Tpp.IsTypeString

local SAHELAN = { type = "TppSahelan2", group = 0, index = 0 }

function this.SetSahelanFova(fv2Path)
    if not IsTypeString(fv2Path) then
        V_FrameWork.Log("V_TppSahelan.SetSahelanFova: Invalid fv2Path provided. must be a string path to a .fv2 file.")
        return
    end

    if not fv2Path:match(".fv2$") then
        fv2Path = fv2Path .. ".fv2"
    end

    GameObject.SendCommand(SAHELAN, { id = "SetSahelanFova", fv2 = fv2Path })
end

function this.ClearSahelanFova()
    GameObject.SendCommand(SAHELAN, { id = "ClearSahelanFova" }) -- REMOVE
end

function this.SetEyeLampColor(r, g, b, a, mode)
    if type(r) ~= "number" or type(g) ~= "number" or type(b) ~= "number" then
        V_FrameWork.Log("V_TppSahelan.SetEyeLampColor: r, g, b must be numbers.")
        return
    end
    if a ~= nil and type(a) ~= "number" then
        V_FrameWork.Log("V_TppSahelan.SetEyeLampColor: a must be a number or nil.")
        return
    end
    if mode == nil then
        mode = -1
    elseif type(mode) ~= "number" then
        V_FrameWork.Log("V_TppSahelan.SetEyeLampColor: mode must be a number or nil.")
        return
    end
    GameObject.SendCommand(SAHELAN, { id = "SetEyeLampColor", color = { r = r, g = g, b = b, a = a }, Phase = mode })
end

function this.ClearEyeLampColor()
    GameObject.SendCommand(SAHELAN, { id = "ClearEyeLampColor" })
end

function this.SetEyeLampDisco(enabled, speed, a)
    if type(enabled) ~= "boolean" then
        V_FrameWork.Log("V_TppSahelan.SetEyeLampDisco: enabled must be a boolean.")
        return
    end
    if speed == nil then speed = 2.0 end
    if type(speed) ~= "number" then
        V_FrameWork.Log("V_TppSahelan.SetEyeLampDisco: speed must be a number or nil.")
        return
    end
    if a ~= nil and type(a) ~= "number" then
        V_FrameWork.Log("V_TppSahelan.SetEyeLampDisco: a must be a number or nil.")
        return
    end
    GameObject.SendCommand(SAHELAN, { id = "SetEyeLampDisco", enabled = enabled, speed = speed, a = a })
end

function this.SetHeartLightColor(r, g, b, a, phase)
    if type(r) ~= "number" or type(g) ~= "number" or type(b) ~= "number" then
        V_FrameWork.Log("V_TppSahelan.SetHeartLightColor: r, g, b must be numbers.")
        return
    end
    if a ~= nil and type(a) ~= "number" then
        V_FrameWork.Log("V_TppSahelan.SetHeartLightColor: a must be a number or nil.")
        return
    end
    if phase == nil then
        phase = -1
    elseif type(phase) ~= "number" then
        V_FrameWork.Log("V_TppSahelan.SetHeartLightColor: phase must be a number or nil.")
        return
    end
    GameObject.SendCommand(SAHELAN, { id = "SetHeartLightColor", color = { r = r, g = g, b = b, a = a }, phase = phase })
end

function this.ClearHeartLightColor()
    GameObject.SendCommand(SAHELAN, { id = "ClearHeartLightColor" })
end

function this.SetHeartLightDisco(enabled, speed, a)
    if type(enabled) ~= "boolean" then
        V_FrameWork.Log("V_TppSahelan.SetHeartLightDisco: enabled must be a boolean.")
        return
    end
    if speed == nil then speed = 2.0 end
    if type(speed) ~= "number" then
        V_FrameWork.Log("V_TppSahelan.SetHeartLightDisco: speed must be a number or nil.")
        return
    end
    if a ~= nil and type(a) ~= "number" then
        V_FrameWork.Log("V_TppSahelan.SetHeartLightDisco: a must be a number or nil.")
        return
    end
    GameObject.SendCommand(SAHELAN, { id = "SetHeartLightDisco", enabled = enabled, speed = speed, a = a })
end

function this.SetEyeLampColorLogging(enabled)
    if type(enabled) ~= "boolean" then
        V_FrameWork.Log("V_TppSahelan.SetEyeLampColorLogging: enabled must be a boolean.")
        return
    end
    V_Sahelan.SetEyeLampColorLogging(enabled)
end


function this.SetPhase(phase)
    if type(phase) ~= "number" then
        V_FrameWork.Log("V_TppSahelan.SetPhase: phase must be a number (TppSahelan2.SAHELAN2_PHASE_*).")
        return
    end
    GameObject.SendCommand(SAHELAN, { id = "SetSahelanPhase", phase = phase })
end

function this.GetPhase()
    return GameObject.SendCommand(SAHELAN, { id = "GetSahelanPhase" })
end

return this
