local this = {}
local TppCommandPost2 = {type="TppCommandPost2"}

function this.SetCautionPhaseDuration(seconds)
    if type(seconds) ~= "number" then
        V_FrameWork.Log("V_TppCommandPost.SetCautionPhaseDuration: Value must be a number.")
        return
    end
    GameObject.SendCommand(TppCommandPost2, {id="SetCautionPhaseDuration", duration=seconds})
end

function this.GetCautionPhaseDuration()
    return GameObject.SendCommand(TppCommandPost2, {id="GetCautionPhaseDuration"})
end

function this.UnsetCautionPhaseDuration()
    GameObject.SendCommand(TppCommandPost2, {id="UnsetCautionPhaseDuration"})
end

function this.GetRemainingCautionPhaseTime()
    return GameObject.SendCommand(TppCommandPost2, {id="GetCautionPhaseRemaining"})
end

return this
