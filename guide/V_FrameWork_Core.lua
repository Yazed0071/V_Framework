local this = {}

if rawget(_G, "V_FrameWork_Core") then
    return _G.V_FrameWork_Core
end

local VFW = rawget(_G, "V_FrameWork")
if not VFW then
    error("V_FrameWork_Core: V_FrameWork.dll isn't loaded. It goes in MGS_TPP\\plugins\\ and needs IHHook r25 or newer with enable_dll_loader=true in ihhook_config.lua")
end
local V_FrameWork = VFW

_G.VFW = VFW
_G.V_FrameWork = VFW
_G.V_FrameWork_Core = this


this.VFW = VFW

this.registerMenus = {
    "V_FrameWork",
}

this.registerIvars = {
    "V_FrameWork_V_TppHelicopter_SetTaxiRidePose",
    "V_FrameWork_V_Quiet_QuietHoldCQC",
    "V_FrameWork_V_Quiet_QuietInterr",
    "V_FrameWork_V_ACC_iDroidClose",
}

-- Infinite Heaven menu definition
this.V_FrameWork = {
    parentRefs = {"InfMenuDefs.safeSpaceMenu", "InfMenuDefs.inMissionMenu"},
    options = {
        "Ivars.V_FrameWork_V_TppHelicopter_SetTaxiRidePose",
        "Ivars.V_FrameWork_V_Quiet_QuietHoldCQC",
        "Ivars.V_FrameWork_V_Quiet_QuietInterr",
        "Ivars.V_FrameWork_V_ACC_iDroidClose",
    }
}

this.langStrings = {
    eng = {
        V_FrameWork = "V_FrameWork",
        V_FrameWork_V_TppHelicopter_SetTaxiRidePose = "Set Taxi Ride Pose",
        V_FrameWork_V_Quiet_QuietHoldCQC = "Enables Quiet to hold with CQC",
        V_FrameWork_V_Quiet_QuietInterr = "Enables Quiet to Interrogate",
        V_FrameWork_V_ACC_iDroidClose = "Play close iDroid animation while in the ACC.",
    },
    help = {
        eng = {
            V_FrameWork = "Toggle individual options for V_FrameWork mod.",
            V_FrameWork_V_TppHelicopter_SetTaxiRidePose = "Set Taxi Ride Pose",
            V_FrameWork_V_Quiet_QuietHoldCQC = "Enables Quiet to hold with CQC.",
            V_FrameWork_V_Quiet_QuietInterr = "Enables Quiet to Interrogate enemies",
        },
    },
}

this.V_FrameWork_V_TppHelicopter_SetTaxiRidePose = {
    save = IvarProc.CATEGORY_EXTERNAL,
    settings = { "Heli Edge", "Inside", "BackSeat" },
    OnChange = function(self, setting)
        this.SetTaxiRidePose(setting + 1)
    end,
    default = 0,
}

this.V_FrameWork_V_Quiet_QuietHoldCQC = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 0,
    OnChange = function(self, setting)
        this._SetQuietHoldCqc(setting)
    end
}

this.V_FrameWork_V_Quiet_QuietInterr = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 0,
    OnChange = function(self, setting)
        this._SetQuietInterrogate(setting)
    end
}
this.V_FrameWork_V_ACC_iDroidClose = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        this._SetPlayIDroidEndMotion(setting)
    end
}

local TAXI_RIDE_POSE_STATES = { 0x03, 0x08, 0x0A }   -- 1 = StateAtk, 2 = StateInside, 3 = StateInsideBackSeatStart

function this.SetTaxiRidePose(option)
    local state = TAXI_RIDE_POSE_STATES[option]
    
    if not state then
        V_FrameWork.Log("V_TppHelicopter.SetTaxiRidePose: option must be 1 (edge), 2 (in front of door) or 3 (chair, third person).")
        return
    end
    
    if V_Helicopter then
        return V_Helicopter.SetTaxiRideState(state)
    end
end

function this._SetQuietHoldCqc(enable)
	if  enable == 1 then
		V_Player.SetQuietHoldCqc(true)
	else
		V_Player.SetQuietHoldCqc(false)
	end
end
function this._SetQuietInterrogate(enable)
	if  enable == 1 then
		V_Player.SetQuietInterrogate(true)
	else
		V_Player.SetQuietInterrogate(false)
	end
end
function this._SetPlayIDroidEndMotion(enable)
	if  enable == 1 then
		V_title_sequence.SetPlayIDroidEndMotion(true)
	else
		V_title_sequence.SetPlayIDroidEndMotion(false)
	end
end

-- IH Appearance > Headgear: add custom heads once, after every module's LoadLibraries registered them
function this.LoadSave()
    local list = InfFova.playerFaceEquipIdInfo
    for _, head in ipairs(V_Player.GetHeadOptions() or {}) do
        while #list < head.slot do
            table.insert(list, { name = "", playerFaceEquipId = #list, playerTypes = {} })
        end
        table.insert(list, {
            name = head.key,
            description = head.name,
            playerFaceEquipId = head.slot,
            playerTypes = head.playerTypes,
        })
    end
end

function this.OnAllocate(missionTable)
    -- HELI TAXI POSE
    local taxiSetting = Ivars.V_FrameWork_V_TppHelicopter_SetTaxiRidePose:Get()
    local taxiRidePose = (tonumber(taxiSetting) or 0) + 1
    
    this.SetTaxiRidePose(taxiRidePose)

    -- QUIET
    this._SetQuietHoldCqc(Ivars.V_FrameWork_V_Quiet_QuietHoldCQC:Get())
    this._SetQuietInterrogate(Ivars.V_FrameWork_V_Quiet_QuietInterr:Get())

    -- IDROID END
    this._SetPlayIDroidEndMotion(Ivars.V_FrameWork_V_ACC_iDroidClose:Get())
end

function this.AddMissionPacks(missionCode, packPaths)
    if (TppMission.IsFOBMission(missionCode)) and missionCode < 5 then
        return
    end

    packPaths[#packPaths + 1] = "/Assets/tpp/pack/V_FrameWork/V_FrameWork_Common.fpk"
    packPaths[#packPaths + 1] = "/Assets/tpp/pack/mission2/online/o50050/o50050_additional.fpk"
end

return this