local this = {}
local TppCommandPost2 = {type="TppCommandPost2"}
local IsString=Tpp.IsTypeString

function this.SetGlobalCautionPhaseDuration(seconds)
    if type(seconds) ~= "number" then
        V_FrameWork.Log("V_TppCommandPost.SetGlobalCautionPhaseDuration: Value must be a number.")
        return
    end
    GameObject.SendCommand(TppCommandPost2, {id="SetCautionPhaseDuration", duration=seconds})
end

function this.SetCautionPhaseDuration(cpId, seconds)
    if IsString(cpId) then
        cpId = GameObject.GetGameObjectId(cpId)
    end
    if type(seconds) ~= "number" then
        V_FrameWork.Log("V_TppCommandPost.SetCautionPhaseDuration: Value must be a number.")
        return
    end
    GameObject.SendCommand(cpId, {id="SetCautionPhaseDuration", duration=seconds})
end

function this.GetCautionPhaseDuration(cpId)
    if cpId == nil then
        V_FrameWork.Log("V_TppCommandPost.GetCautionPhaseDuration: cpId is nil.")
        return
    end
    if IsString(cpId) then
        cpId = GameObject.GetGameObjectId(cpId)
    end
    return GameObject.SendCommand(cpId, {id="GetCautionPhaseDuration"})
end

function this.UnsetCautionPhaseDuration(cpId)
    if cpId == nil then
        V_FrameWork.Log("V_TppCommandPost.UnsetCautionPhaseDuration: cpId is nil.")
        return
    end
    if IsString(cpId) then
        cpId = GameObject.GetGameObjectId(cpId)
    end
    GameObject.SendCommand(cpId, {id="UnsetCautionPhaseDuration"})
end

function this.GetRemainingCautionPhaseTime(cpId)
     if cpId == nil then
        V_FrameWork.Log("V_TppCommandPost.GetRemainingCautionPhaseTime: cpId is nil.")
        return
    end
    if IsString(cpId) then
        cpId = GameObject.GetGameObjectId(cpId)
    end
    return GameObject.SendCommand(TppCommandPost2, {id="GetCautionPhaseRemaining"})
end

return this
