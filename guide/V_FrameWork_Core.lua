local this = {}

if rawget(_G, "V_FrameWork_Core") then
    return _G.V_FrameWork_Core
end

local VFW = rawget(_G, "V_FrameWork")
if not VFW then
    local loadlib = type(package) == "table" and package.loadlib
    local open, why = nil, "this Lua can't load DLLs"
    if loadlib then
        open, why = loadlib("plugins\\V_FrameWork.dll", "luaopen_V_FrameWork")
    end
    if open then
        open()
        VFW = rawget(_G, "V_FrameWork")
        why = "it's off, see V_FrameWork's log"
    end
    if not VFW then
        error("V_FrameWork_Core: V_FrameWork.dll isn't loaded (" .. tostring(why) .. "). It goes in MGS_TPP\\plugins\\")
    end
end
local V_FrameWork = VFW

_G.VFW = VFW
_G.V_FrameWork = VFW
_G.V_FrameWork_Core = this


this.VFW = VFW

if Vehicle and Vehicle.subType then
    rawset(Vehicle.subType, "EASTERN_LIGHT_VEHICLE_MACHINE_GUN", 1)
    rawset(Vehicle.subType, "WESTERN_LIGHT_VEHICLE_MACHINE_GUN", 1)
end

this.registerIvars = {
    "V_FrameWork_V_TppHelicopter_SetTaxiRidePose",
}

-- not in the IH menu: the Taxi pose call menu options set it, and the save keeps the last pick
this.V_FrameWork_V_TppHelicopter_SetTaxiRidePose = {
    save = IvarProc.CATEGORY_EXTERNAL,
    settings = { "Heli Edge", "Inside", "BackSeat", "Inside (Door Open)" },
    OnChange = function(self, setting)
        this.SetTaxiRidePose(setting + 1)
    end,
    default = 0,
}

local TAXI_RIDE_POSE_STATES = { 0x03, 0x08, 0x0A, 0x06 }   -- 1 = StateAtk, 2 = StateInside, 3 = StateInsideBackSeatStart, 4 = StateInsideStart (inside, door left open)
local TAXI_POSE_CLIP = nil   -- any clip from player2_heli.mtar or player2_resident.mtar, replaces the menu pose's clip on taxi rides; SetPlayerDropSeatPose still wins on the back seat

function this.SetTaxiRidePose(option)
    local state = TAXI_RIDE_POSE_STATES[option]

    if not state then
        V_FrameWork.Log("V_TppHelicopter.SetTaxiRidePose: option must be 1 (edge), 2 (in front of door), 3 (chair, third person) or 4 (in front of door, door open).")
        return
    end

    this.taxiPoseState = state
    this.taxiPoseClip = TAXI_POSE_CLIP
    if this.taxiPoseSwapped then
        this._RestoreTaxiPoseClip()
        this._SwapTaxiPoseClip()
    end
    if this.taxiPoseMenuReady then
        this._GreyCurrentTaxiPose()
    end

    if V_Helicopter then
        return V_Helicopter.SetTaxiRideState(state)
    end
end

function this._SwapTaxiPoseClip()
    if not this.taxiPoseClip then
        return
    end

    if this.taxiPoseState == 0x03 then
        V_Fox.SwapGani("/Assets/tpp/motion/SI_game/fani/bodies/snap/snaputh/snaputh_q_pos_idl", this.taxiPoseClip)
    elseif this.taxiPoseState == 0x08 or this.taxiPoseState == 0x06 then
        V_Fox.SwapGani("/Assets/tpp/motion/SI_game/fani/bodies/snap/snaputh/snaputh_q_pos_to_in_r_idl", this.taxiPoseClip)
        V_Fox.SwapGani("/Assets/tpp/motion/SI_game/fani/bodies/snap/snaputh/snaputh_q_pos_to_in_idl", this.taxiPoseClip)
    else
        V_Fox.SwapGani("/Assets/tpp/motion/SI_game/fani/bodies/snap/snaputh/snaputh_q_seat_idl_b_lp", this.taxiPoseClip)
    end
    this.taxiPoseSwapped = this.taxiPoseClip
end

function this._RestoreTaxiPoseClip()
    if not this.taxiPoseSwapped then
        return
    end

    V_Fox.RestoreGani("/Assets/tpp/motion/SI_game/fani/bodies/snap/snaputh/snaputh_q_pos_idl", this.taxiPoseSwapped)
    V_Fox.RestoreGani("/Assets/tpp/motion/SI_game/fani/bodies/snap/snaputh/snaputh_q_pos_to_in_r_idl", this.taxiPoseSwapped)
    V_Fox.RestoreGani("/Assets/tpp/motion/SI_game/fani/bodies/snap/snaputh/snaputh_q_pos_to_in_idl", this.taxiPoseSwapped)
    V_Fox.RestoreGani("/Assets/tpp/motion/SI_game/fani/bodies/snap/snaputh/snaputh_q_seat_idl_b_lp", this.taxiPoseSwapped)
    this.taxiPoseSwapped = nil
end

-- IH Appearance: custom heads in Headgear, custom outfits in Suit with their variants in Camo; rebuilt each time a menu opens
local function RefreshHeadgear()
    local list = InfFova.playerFaceEquipIdInfo
    while #list > 0 and list[#list].vfwHead do
        table.remove(list)
    end
    for _, head in ipairs(V_Player.GetHeadOptions() or {}) do
        while #list < head.slot do
            table.insert(list, { name = "", playerFaceEquipId = #list, playerTypes = {}, vfwHead = true })
        end
        if #list == head.slot then
            table.insert(list, {
                name = head.key,
                description = head.name,
                playerFaceEquipId = head.slot,
                playerTypes = head.playerTypes,
                vfwHead = true,
            })
        end
    end
end

local customOutfits = {}
local customOutfitOrder = {}

local function WornOutfit()
    for _, outfit in pairs(customOutfits) do
        if outfit.partsType ~= 0 and outfit.partsType == vars.playerPartsType then
            return outfit
        end
    end
end

local function RefreshOutfits()
    customOutfits = {}
    customOutfitOrder = {}
    if not V_Player.GetOutfits then
        return
    end
    local checkDeveloped = Ivars.skipDevelopChecks:Is(0)
    for _, outfit in ipairs(V_Player.GetOutfits(vars.playerType) or {}) do
        customOutfits[outfit.key] = outfit
        if not checkDeveloped or outfit.developId == 0 or outfit.partsType == vars.playerPartsType
            or TppMotherBaseManagement.IsEquipDevelopedFromDevelopID{ equipDevelopID = outfit.developId } then
            customOutfitOrder[#customOutfitOrder + 1] = outfit.key
        end
    end
end

local function WornVariant(outfit)
    for i, variant in ipairs(outfit.variants) do
        if variant.camo ~= 0 and variant.camo == vars.playerCamoType then
            return i
        end
    end
end

local function CamoSetting(key, i)
    return key .. "#" .. i
end

local function ParseCamoSetting(setting)
    local key, i = string.match(tostring(setting), "^(.*)#(%d+)$")
    if key and customOutfits[key] then
        return customOutfits[key], tonumber(i)
    end
end

local function WearOutfit(outfit, variant)
    local partsType, camoType = V_Player.GetOutfitInfo(outfit.key, outfit.variants[variant].variant or variant - 1)
    if partsType then
        vars.playerPartsType = partsType
        vars.playerCamoType = camoType
    end
end

local function SetSilently(ivar, fn, ...)
    local own = rawget(ivar, "Set")
    rawset(ivar, "Set", function() end)
    local ok, err = pcall(fn, ivar, ...)
    rawset(ivar, "Set", own)
    if not ok then
        error(err)
    end
end

local function WrapAppearanceMenus()
    local headgear, suit, camo = Ivars.playerFaceEquipId, Ivars.playerPartsType, Ivars.playerCamoType
    if not headgear or not suit or not camo or rawget(suit, "vfwWrapped") then
        return
    end
    rawset(suit, "vfwWrapped", true)

    local headgearSelect = headgear.OnSelect
    headgear.OnSelect = function(self, ...)
        RefreshHeadgear()
        return headgearSelect(self, ...)
    end

    local suitSelect, suitText, suitChange = suit.OnSelect, suit.GetSettingText, suit.OnChange
    suit.OnSelect = function(self)
        RefreshOutfits()
        SetSilently(self, suitSelect)
        local settings = {}
        for _, name in ipairs(self.settings or {}) do
            settings[#settings + 1] = name
        end
        for _, key in ipairs(customOutfitOrder) do
            settings[#settings + 1] = key
        end
        if #settings == 0 then
            return
        end
        SetSilently(self, IvarProc.SetSettings, settings)
        local worn = WornOutfit()
        local current = worn and worn.key or InfFova.playerPartsTypes[vars.playerPartsType + 1]
        if current and self.enum[current] then
            self:Set(self.enum[current])
        else
            ivars[self.name] = 0
        end
    end
    suit.GetSettingText = function(self, setting)
        local outfit = customOutfits[self.settings[setting + 1]]
        if outfit then
            return outfit.name
        end
        return suitText(self, setting)
    end
    suit.OnChange = function(self, setting, ...)
        local outfit = customOutfits[self.settings[setting + 1]]
        if not outfit then
            return suitChange(self, setting, ...)
        end
        if WornOutfit() ~= outfit or not WornVariant(outfit) then
            WearOutfit(outfit, 1)
        end
        Ivars.playerCamoType:OnSelect()
    end

    local camoSelect, camoText, camoChange = camo.OnSelect, camo.GetSettingText, camo.OnChange
    camo.OnSelect = function(self)
        RefreshOutfits()
        local outfit = WornOutfit()
        if not outfit and InfFova.playerPartsTypes[vars.playerPartsType + 1] then
            return camoSelect(self)
        end
        if not outfit then
            IvarProc.SetSettings(self, {})
            ivars[self.name] = 0
            return
        end
        local settings = {}
        for i in ipairs(outfit.variants) do
            settings[i] = CamoSetting(outfit.key, i)
        end
        SetSilently(self, IvarProc.SetSettings, settings)
        local current = WornVariant(outfit)
        if current then
            self:Set(current - 1)
        else
            ivars[self.name] = 0
        end
    end
    camo.GetSettingText = function(self, setting)
        local outfit, i = ParseCamoSetting(self.settings[setting + 1])
        if outfit then
            local variant = outfit.variants[i]
            return variant and variant.name or (#outfit.variants > 1 and outfit.name .. " " .. i or outfit.name)
        end
        return camoText(self, setting)
    end
    camo.OnChange = function(self, setting, ...)
        local outfit, i = ParseCamoSetting(self.settings[setting + 1])
        if not outfit then
            return camoChange(self, setting, ...)
        end
        if WornOutfit() ~= outfit or WornVariant(outfit) ~= i then
            WearOutfit(outfit, i)
        end
    end
end

function this.LoadSave()
    if not V_Player or not InfFova or not Ivars or not IvarProc then
        return
    end
    RefreshHeadgear()
    WrapAppearanceMenus()
end

function this.OnAllocate(missionTable)
    -- MISSIONS LIST LOCATION TABS
    this._RefreshMissionListLocationTabs()

    -- HELI TAXI POSE
    local taxiSetting = Ivars.V_FrameWork_V_TppHelicopter_SetTaxiRidePose:Get()
    local taxiRidePose = (tonumber(taxiSetting) or 0) + 1
    
    this.SetTaxiRidePose(taxiRidePose)
end

function this._RefreshMissionListLocationTabs()
    if not V_TppUiCommand or not V_TppUiCommand.SetMissionListLocationTabVisible then
        return
    end
    local hasOpenMission = {}
    for locationName, locationId in pairs(TppDefine.LOCATION_ID) do
        hasOpenMission[locationId] = hasOpenMission[locationId] or false
        for _, missionCode in ipairs(TppDefine.LOCATION_HAVE_MISSION_LIST[locationName] or {}) do
            if missionCode < 30000 and TppStory.IsMissionOpen(missionCode) then
                hasOpenMission[locationId] = true
            end
        end
    end
    for locationId, visible in pairs(hasOpenMission) do
        V_TppUiCommand.SetMissionListLocationTabVisible(locationId, visible)
    end
end

function this.Messages()
    return Tpp.StrCode32Table {
        GameObject = {
            {
                msg = "RequestedHeliTaxi",
                func = function()
                    this._RestoreTaxiPoseClip()
                    this._SwapTaxiPoseClip()
                    this._ShowTaxiPoseCallMenu()
                end,
            },
        },
        Player = {
            {
                msg = "PlayerHeliGetOff",
                func = function()
                    this._RestoreTaxiPoseClip()
                    this._HideTaxiPoseCallMenu()
                end,
            },
            {
                msg = "ItemSelected",
                sender = "V_TaxiPoseEdge",
                func = function()
                    Ivars.V_FrameWork_V_TppHelicopter_SetTaxiRidePose:Set(0)
                end,
            },
            {
                msg = "ItemSelected",
                sender = "V_TaxiPoseInside",
                func = function()
                    Ivars.V_FrameWork_V_TppHelicopter_SetTaxiRidePose:Set(1)
                end,
            },
            {
                msg = "ItemSelected",
                sender = "V_TaxiPoseBackSeat",
                func = function()
                    Ivars.V_FrameWork_V_TppHelicopter_SetTaxiRidePose:Set(2)
                end,
            },
            {
                msg = "ItemSelected",
                sender = "V_TaxiPoseInsideOpen",
                func = function()
                    Ivars.V_FrameWork_V_TppHelicopter_SetTaxiRidePose:Set(3)
                end,
            },
        },
    }
end

function this.Update()
    if Ivars.V_FrameWork_V_ACC_iDroidClose:Get() == 1 and TppMission.IsHelicopterSpace(vars.missionCode) then
        TppDataUtility.InvisibleMeshFromIdentifier("HeliModelIdentifier","uth0_intr0_def_0000","MESH_IDOROID")
    end
end

function this._ShowTaxiPoseCallMenu()
    V_Player.SetCallMenuItemInPlaceOf("V_FrameWork_TaxiPoseEdge", V_CallMenuItem.REQUEST_INFO)
    V_Player.SetCallMenuItemInPlaceOf("V_FrameWork_TaxiPoseInside", V_CallMenuItem.SELL_OUT_COMRADE)
    V_Player.SetCallMenuItemInPlaceOf("V_FrameWork_TaxiPoseBackSeat", V_CallMenuItem.REQUEST_CRAWL)
    V_Player.SetCallMenuItemInPlaceOf("V_FrameWork_TaxiPoseInsideOpen", V_CallMenuItem.KNOCK)
    V_Player.ShowCallMenuItem("V_FrameWork_TaxiPoseEdge")
    V_Player.ShowCallMenuItem("V_FrameWork_TaxiPoseInside")
    V_Player.ShowCallMenuItem("V_FrameWork_TaxiPoseBackSeat")
    V_Player.ShowCallMenuItem("V_FrameWork_TaxiPoseInsideOpen")
    this._GreyCurrentTaxiPose()
end

function this._GreyCurrentTaxiPose()
    V_Player.EnableCallMenuItem("V_FrameWork_TaxiPoseEdge")
    V_Player.EnableCallMenuItem("V_FrameWork_TaxiPoseInside")
    V_Player.EnableCallMenuItem("V_FrameWork_TaxiPoseBackSeat")
    V_Player.EnableCallMenuItem("V_FrameWork_TaxiPoseInsideOpen")
    if this.taxiPoseState == 0x03 then
        V_Player.DisableCallMenuItem("V_FrameWork_TaxiPoseEdge")
    elseif this.taxiPoseState == 0x08 then
        V_Player.DisableCallMenuItem("V_FrameWork_TaxiPoseInside")
    elseif this.taxiPoseState == 0x0A then
        V_Player.DisableCallMenuItem("V_FrameWork_TaxiPoseBackSeat")
    elseif this.taxiPoseState == 0x06 then
        V_Player.DisableCallMenuItem("V_FrameWork_TaxiPoseInsideOpen")
    end
end

function this._HideTaxiPoseCallMenu()
    V_Player.SetCallMenuItemInPlaceOf("V_FrameWork_TaxiPoseEdge", nil)
    V_Player.SetCallMenuItemInPlaceOf("V_FrameWork_TaxiPoseInside", nil)
    V_Player.SetCallMenuItemInPlaceOf("V_FrameWork_TaxiPoseBackSeat", nil)
    V_Player.SetCallMenuItemInPlaceOf("V_FrameWork_TaxiPoseInsideOpen", nil)
    V_Player.HideCallMenuItem("V_FrameWork_TaxiPoseEdge")
    V_Player.HideCallMenuItem("V_FrameWork_TaxiPoseInside")
    V_Player.HideCallMenuItem("V_FrameWork_TaxiPoseBackSeat")
    V_Player.HideCallMenuItem("V_FrameWork_TaxiPoseInsideOpen")
end

function this.Init(missionTable)
    this.taxiPoseSwapped = nil
    if TppMission.IsFOBMission(vars.missionCode) then return end
    this.messageExecTable = Tpp.MakeMessageExecTable(this.Messages())

    V_Player.AddCallMenuItem{
        key         = "V_FrameWork_TaxiPoseEdge",
        label       = "V_taxi_pose_edge",
        description = "V_taxi_pose_edge_help",
        message     = "V_TaxiPoseEdge",
        column      = V_CallMenuColumn.INTERROGATION,
        exclusive   = "V_FrameWork_TaxiPose",
    }
    V_Player.AddCallMenuItem{
        key         = "V_FrameWork_TaxiPoseInside",
        label       = "V_taxi_pose_inside",
        description = "V_taxi_pose_inside_help",
        message     = "V_TaxiPoseInside",
        column      = V_CallMenuColumn.INTERROGATION,
        exclusive   = "V_FrameWork_TaxiPose",
    }
    V_Player.AddCallMenuItem{
        key         = "V_FrameWork_TaxiPoseBackSeat",
        label       = "V_taxi_pose_backseat",
        description = "V_taxi_pose_backseat_help",
        message     = "V_TaxiPoseBackSeat",
        column      = V_CallMenuColumn.INTERROGATION,
        exclusive   = "V_FrameWork_TaxiPose",
    }
    V_Player.AddCallMenuItem{
        key         = "V_FrameWork_TaxiPoseInsideOpen",
        label       = "V_taxi_pose_inside_open",
        description = "V_taxi_pose_inside_open_help",
        message     = "V_TaxiPoseInsideOpen",
        column      = V_CallMenuColumn.KNOCK,
        exclusive   = "V_FrameWork_TaxiPose",
    }
    this.taxiPoseMenuReady = true
    this._HideTaxiPoseCallMenu()
    this._GreyCurrentTaxiPose()
end

function this.OnMessage(sender, messageId, arg0, arg1, arg2, arg3, strLogText)
    if TppMission.IsFOBMission(vars.missionCode) then return end
    Tpp.DoMessage(this.messageExecTable, TppMission.CheckMessageOption, sender, messageId, arg0, arg1, arg2, arg3, strLogText)
end

function this.AddMissionPacks(missionCode, packPaths)
    if TppMission.IsFOBMission(missionCode) then
        return
    end

    packPaths[#packPaths + 1] = "/Assets/tpp/pack/V_FrameWork/V_FrameWork_Common.fpk"
    packPaths[#packPaths + 1] = "/Assets/tpp/pack/mission2/online/o50050/o50050_additional.fpk"
    if TppLocation.IsAfghan() then
        packPaths[#packPaths + 1] = "/Assets/tpp/pack/V_FrameWork/V_ArmedJeep_afgh.fpk"
    elseif TppLocation.IsMiddleAfrica() then
        packPaths[#packPaths + 1] = "/Assets/tpp/pack/V_FrameWork/V_ArmedJeep_mafr.fpk"
    end
end

return this