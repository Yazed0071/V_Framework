local this = {}
local StrCode32 = Fox.StrCode32
local SendCommand = GameObject.SendCommand

local TAXI_MISSION = 30010

local LANDING_ZONE_GROUP_TABLE = {
    [StrCode32("RV_StartCliff")] = "groupStartCliff",
    [StrCode32("RV_WareHouse")] = "groupWareHouse",
    [StrCode32("lz_sovietBase_N0000|lz_sovietBase_N_0000")] = "sovietBase_N",
    [StrCode32("lz_citadelSouth_S0000|lz_citadelSouth_S_0000")] = "citadelSouth_S",
}

function this.GetLandingZoneGroup(landingZoneName)
    return LANDING_ZONE_GROUP_TABLE[landingZoneName]
end

local HELI_TAXI_SETTING_TABLE = {
    ["groupStartCliff"] = {
        ["groupStartCliff"] = { currentClusterRoute = "rt_hltx_takeoff_StartCliff", },
        ["groupSeaSide"]    = { currentClusterRoute = "rt_hltx_takeoff_StartCliff", relayRoute = "rt_hltx_StartCliff_toSeaSide", nextClusterRoute = "rt_hltx_arrive_StartCliff", },
    },
    ["sovietBase_N"] = {
        ["sovietBase_N"]    = { currentClusterRoute = "rt_hltx_takeoff_test_00"},
        ["citadelSouth_S"]  = { currentClusterRoute = "rt_hltx_takeoff_test_00", nextClusterRoute = "rt_hltx_takeoff_test_00",},
    },
    ["citadelSouth_S"] = {
        ["sovietBase_N"]    = { currentClusterRoute = "rt_hltx_takeoff_test_01", nextClusterRoute = "rt_hltx_takeoff_test_01",},
        ["citadelSouth_S"]  = { currentClusterRoute = "rt_hltx_takeoff_test_01"},
    },
}

function this.RequestHeliTaxi(gameObjectId, currentLandingZoneName, nextLandingZoneName)
    local currentGroup = this.GetLandingZoneGroup(currentLandingZoneName)
    local nextGroup    = this.GetLandingZoneGroup(nextLandingZoneName)

    local heliTaxiSettings = HELI_TAXI_SETTING_TABLE[currentGroup]
                             and HELI_TAXI_SETTING_TABLE[currentGroup][nextGroup]
    if not heliTaxiSettings then
        V_FrameWork.Log("V_FieldHeliTaxi: no routes for " .. tostring(currentGroup) .. " -> " .. tostring(nextGroup))
        return
    end

    SendCommand(
        { type = "TppHeli2", index = 0 },
        {
            id = "SetTaxiRoute",
            currentClusterRoute = heliTaxiSettings.currentClusterRoute,
            relayRoute = heliTaxiSettings.relayRoute,
            nextClusterRoute = heliTaxiSettings.nextClusterRoute,
        }
    )
end

function this.EnableTaxi()
    if V_FrameWork and V_FrameWork.SetFieldTaxiMissionEnabled then
        V_FrameWork.SetFieldTaxiMissionEnabled(TAXI_MISSION, true)
    end
end

function this.Messages()
    return Tpp.StrCode32Table {
        GameObject = {
            {
                msg = "RequestedHeliTaxi",
                func = function(gameObjectId, currentLandingZoneName, nextLandingZoneName)
                    this.RequestHeliTaxi(gameObjectId, currentLandingZoneName, nextLandingZoneName)
                end,
            },
        },
    }
end

function this.Init(missionTable)
    this.messageExecTable = Tpp.MakeMessageExecTable(this.Messages())
    this.EnableTaxi()
end

function this.OnReload(missionTable)
    this.messageExecTable = Tpp.MakeMessageExecTable(this.Messages())
    this.EnableTaxi()
end

function this.OnMessage(sender, messageId, arg0, arg1, arg2, arg3, strLogText)
    Tpp.DoMessage(this.messageExecTable, TppMission.CheckMessageOption, sender, messageId, arg0, arg1, arg2, arg3, strLogText)
end

return this
