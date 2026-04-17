-- V_TppCommandPost — caution (alert) phase duration override.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}

function this.SetCautionPhaseDuration(seconds)
    V_FrameWork.SetCautionStepNormalDurationSeconds(seconds)
end

function this.GetCautionPhaseDuration()
    return V_FrameWork.GetCautionStepNormalDurationSeconds()
end

function this.UnsetCautionPhaseDuration()
    V_FrameWork.UnsetCautionStepNormalDurationSeconds()
end

function this.GetRemainingCautionPhaseTime()
    return V_FrameWork.GetCautionStepNormalRemainingSeconds()
end

return this
