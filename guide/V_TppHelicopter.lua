local this = {}
local IsTypeString = Tpp.IsTypeString

this.registerIvars={
	"V_TppHelicopter_SetTaxiRidePose",
}

this.registerMenus={
	"V_FrameWork_V_TppHelicopter",
}

this.V_FrameWork_V_TppHelicopter={
	parentRefs={"V_FrameWork_Core.V_FrameWork"},
	options={
		"Ivars.V_TppHelicopter_SetTaxiRidePose",
	}
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
        V_FrameWork_V_TppHelicopter = "V_TppHelicopter",
		V_TppHelicopter_SetTaxiRidePose = "Set Taxi Ride Pose",
	},
	help={
		eng={
			V_TppHelicopter_SetTaxiRidePose = "Set Taxi Ride Pose",
		}
	}
}

local TAXI_RIDE_POSE_STATES = { 0x03, 0x08, 0x0A }   -- 1 = StateAtk, 2 = StateInside, 3 = StateInsideBackSeatStart

function this.SetTaxiRidePose(option)
    local state = TAXI_RIDE_POSE_STATES[option]
    if not state then
        V_FrameWork.Log("V_TppHelicopter.SetTaxiRidePose: option must be 1 (edge), 2 (in front of door) or 3 (chair, third person).")
        return
    end
    return V_Helicopter.SetTaxiRideState(state)
end

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


return this
