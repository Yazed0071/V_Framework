local this = {}
local IsTypeString = Tpp.IsTypeString

this.registerIvars={
	"V_TppHelicopter_SetTaxiRidePose",
}

this.V_TppHelicopter_SetTaxiRidePose={
    save = IvarProc.CATEGORY_EXTERNAL,
    settings = { "Heli Edge", "Inside", "BackSeat" },
	OnChange=function(self,setting)
        this.SetTaxiRidePose(setting + 1)
  	end,
	  default = 0,
}

this.langStrings={
	eng={
		V_TppHelicopter_SetTaxiRidePose = "Set Taxi Ride Pose",
	},
	help={
		eng={
			V_TppHelicopter_SetTaxiRidePose = "Set Taxi Ride Pose",
		}
	}
}

function this.OnAllocate(missionTable)
    local taxiRidePose
    local taxiRidePoseNoRelay
    if Ivars.V_TppHelicopter_SetTaxiRidePose:Is("Heli Edge") then
        taxiRidePose = 1
    elseif Ivars.V_TppHelicopter_SetTaxiRidePose:Is("Inside") then
        taxiRidePose = 2
    elseif Ivars.V_TppHelicopter_SetTaxiRidePose:Is("BackSeat") then
        taxiRidePose = 3
    end
    if taxiRidePose then
        this.SetTaxiRidePose(taxiRidePose)
    end
end


local function isNameOrHash(v)
    local t = type(v)
    return t == "string" or t == "number"
end

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
    return V_Helicopter.SetEnableHeliVoice(isEnable, voiceEvent or "", radioEvent or "")
end

function this.PilotCallVoice(voice, slot, voiceType, param4)
    if not isNameOrHash(voice) then
        V_FrameWork.Log("V_TppHelicopter.PilotCallVoice: voice must be a name (string) or hash (number).")
        return false
    end
    if slot ~= nil and type(slot) ~= "number" then
        V_FrameWork.Log("V_TppHelicopter.PilotCallVoice: slot must be a number.")
        return false
    end
    if voiceType ~= nil and type(voiceType) ~= "number" then
        V_FrameWork.Log("V_TppHelicopter.PilotCallVoice: voiceType must be a number.")
        return false
    end
    if param4 ~= nil and not isNameOrHash(param4) then
        V_FrameWork.Log("V_TppHelicopter.PilotCallVoice: param4 must be a name (string) or hash (number).")
        return false
    end
    return V_Helicopter.PilotCallVoice(voice, slot or 0, voiceType or 0, param4 or 0)
end

function this.PilotCallRadio(line1, line2)
    if not isNameOrHash(line1) then
        V_FrameWork.Log("V_TppHelicopter.PilotCallRadio: first line must be a name (string) or hash (number).")
        return false
    end
    if line2 ~= nil and not isNameOrHash(line2) then
        V_FrameWork.Log("V_TppHelicopter.PilotCallRadio: second line must be a name (string) or hash (number).")
        return false
    end
    return V_Helicopter.PilotCallRadio(line1, line2)
end

function this.SetTaxiLandingZoneHidden(lz, hidden)
    if not isNameOrHash(lz) then
        V_FrameWork.Log("V_TppHelicopter.SetTaxiLandingZoneHidden: lz must be a locator name (string) or hash (number).")
        return false
    end
    if hidden == nil then
        hidden = true
    elseif type(hidden) ~= "boolean" then
        V_FrameWork.Log("V_TppHelicopter.SetTaxiLandingZoneHidden: hidden must be a boolean.")
        return false
    end
    return V_Helicopter.SetTaxiLandingZoneHidden(lz, hidden)
end

local TAXI_RIDE_POSE_STATES = { 0x03, 0x08, 0x0A }   -- 1 = StateAtk, 2 = StateInside, 3 = StateInsideBackSeatStart

function this.SetTaxiRidePose(option)
    local state = TAXI_RIDE_POSE_STATES[option]
    if not state then
        V_FrameWork.Log("V_TppHelicopter.SetTaxiRidePose: option must be 1 (edge), 2 (in front of door) or 3 (chair, third person).")
        return
    end
    return V_Helicopter.SetTaxiRideState(state)
end

function this.SetTaxiRideLog(enabled)
    if enabled == nil then enabled = true end
    return V_Helicopter.SetTaxiRideLog(enabled and true or false)
end

return this
