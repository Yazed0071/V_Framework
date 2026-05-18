
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
function this.ClearSahelanFova()
    V_FrameWork.Log("V_Sahelan.ClearSahelanFova: Clearing Sahelan Fova override.")
    V_FrameWork.ClearSahelanFova()
end

function this.SetEyeLampColor(r, g, b, pulseSpeed, mode)
    if type(r) ~= "number" or type(g) ~= "number" or type(b) ~= "number" then
        V_FrameWork.Log("V_Sahelan.SetEyeLampColor: r, g, b must be numbers.")
        return
    end
    if type(pulseSpeed) ~= "number" then
        V_FrameWork.Log("V_Sahelan.SetEyeLampColor: pulseSpeed must be a number.")
        return
    end
    if mode == nil then
        mode = -1
    elseif type(mode) ~= "number" then
        V_FrameWork.Log("V_Sahelan.SetEyeLampColor: mode must be a number or nil.")
        return
    end
    V_FrameWork.SetEyeLampColor(r, g, b, pulseSpeed, mode)
end

function this.ClearEyeLampColor()
    V_FrameWork.ClearEyeLampColor()
end

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

function this.SetHeartLightColor(r, g, b, pulseSpeed)
    if type(r) ~= "number" or type(g) ~= "number" or type(b) ~= "number" then
        V_FrameWork.Log("V_Sahelan.SetHeartLightColor: r, g, b must be numbers.")
        return
    end
    if pulseSpeed == nil then pulseSpeed = 1.0 end
    if type(pulseSpeed) ~= "number" then
        V_FrameWork.Log("V_Sahelan.SetHeartLightColor: pulseSpeed must be a number or nil.")
        return
    end
    V_FrameWork.SetHeartLightColor(r, g, b, pulseSpeed)
end

function this.ClearHeartLightColor()
    V_FrameWork.ClearHeartLightColor()
end

function this.SetEyeLampColorLogging(enabled)
    if type(enabled) ~= "boolean" then
        V_FrameWork.Log("V_Sahelan.SetEyeLampColorLogging: enabled must be a boolean.")
        return
    end
    V_FrameWork.SetEyeLampColorLogging(enabled)
end


function this.Messages()
    return Tpp.StrCode32Table {
        GameObject = {
            {
                msg = "ShalenSearchModeChange",
                func = function(mode)
                    V_FrameWork.Log("[V_TppSahelan] ShalenSearchModeChange mode=" .. mode)
                end
            },
        },
    }
end


function this.Init(missionTable)
    this.messageExecTable = Tpp.MakeMessageExecTable(this.Messages())
    V_FrameWork.RegisterListener(this)
end

function this.OnReload(missionTable)
    this.messageExecTable = Tpp.MakeMessageExecTable(this.Messages())
    V_FrameWork.RegisterListener(this)
end

function this.OnMessage(sender, messageId, arg0, arg1, arg2, arg3, strLogText)
    Tpp.DoMessage(this.messageExecTable, TppMission.CheckMessageOption, sender, messageId, arg0, arg1, arg2, arg3, strLogText)
end


return this
