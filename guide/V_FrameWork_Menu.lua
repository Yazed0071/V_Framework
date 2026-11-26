local this = {}

this.registerMenus = {
    "V_FrameWork",
    "V_FrameWork_PlayerMenu",
    "V_FrameWork_SoldiersMenu",
    "V_FrameWork_SupportHeliMenu",
    "V_FrameWork_MotherBaseMenu",
}

this.registerIvars = {
    "V_FrameWork_V_Quiet_QuietHoldCQC",
    "V_FrameWork_V_Quiet_QuietInterr",
    "V_FrameWork_V_ACC_iDroidClose",
    "V_FrameWork_V_Idroid_RestoredLines",
    "V_FrameWork_V_Player_CallEm",
    "V_FrameWork_V_Player_LieToThem",
    "V_FrameWork_V_Player_PainAndDeathLines",
    "V_FrameWork_V_Player_BuddyLines",
    "V_FrameWork_V_Quiet_Lines",
    "V_FrameWork_V_Player_PinUpPosters",
    "V_FrameWork_V_Ocelot_BinocularComments",
    "V_FrameWork_V_Liquid_Driving",
    "V_FrameWork_V_Holdup_SelfRelease",
    "V_FrameWork_V_Enemy_SurveyTeams",
    "V_FrameWork_V_Enemy_BackupRequests",
    "V_FrameWork_V_Enemy_UavDownChecks",
    "V_FrameWork_V_Enemy_RadioAnswers",
    "V_FrameWork_V_Enemy_Conversations",
    "V_FrameWork_V_Enemy_LongBarrelRadio",
    "V_FrameWork_V_Marker_DyingColour",
    "V_FrameWork_V_Vip_DeepVoice",
    "V_FrameWork_V_Heli_DangerLz",
    "V_FrameWork_V_Heli_ModerateLz",
    "V_FrameWork_V_Pequod_ExtraLines",
    "V_FrameWork_V_Pequod_LzMove",
    "V_FrameWork_V_Heli_MillerReports",
    "V_FrameWork_V_Staff_SaluteNews",
    "V_FrameWork_V_Staff_Talk",
}

this.V_FrameWork = {
    parentRefs = {"InfMenuDefs.safeSpaceMenu", "InfMenuDefs.inMissionMenu"},
    options = {
        "V_FrameWork_Menu.V_FrameWork_PlayerMenu",
        "V_FrameWork_Menu.V_FrameWork_SoldiersMenu",
        "V_FrameWork_Menu.V_FrameWork_SupportHeliMenu",
        "V_FrameWork_Menu.V_FrameWork_MotherBaseMenu",
    }
}

this.V_FrameWork_PlayerMenu = {
    options = {
        "Ivars.V_FrameWork_V_Quiet_QuietHoldCQC",
        "Ivars.V_FrameWork_V_Quiet_QuietInterr",
        "Ivars.V_FrameWork_V_ACC_iDroidClose",
        "Ivars.V_FrameWork_V_Idroid_RestoredLines",
        "Ivars.V_FrameWork_V_Player_CallEm",
        "Ivars.V_FrameWork_V_Player_LieToThem",
        "Ivars.V_FrameWork_V_Player_PainAndDeathLines",
        "Ivars.V_FrameWork_V_Player_BuddyLines",
        "Ivars.V_FrameWork_V_Quiet_Lines",
        "Ivars.V_FrameWork_V_Player_PinUpPosters",
        "Ivars.V_FrameWork_V_Ocelot_BinocularComments",
        "Ivars.V_FrameWork_V_Liquid_Driving",
    }
}

this.V_FrameWork_SoldiersMenu = {
    options = {
        "Ivars.V_FrameWork_V_Holdup_SelfRelease",
        "Ivars.V_FrameWork_V_Enemy_SurveyTeams",
        "Ivars.V_FrameWork_V_Enemy_BackupRequests",
        "Ivars.V_FrameWork_V_Enemy_UavDownChecks",
        "Ivars.V_FrameWork_V_Enemy_RadioAnswers",
        "Ivars.V_FrameWork_V_Enemy_Conversations",
        "Ivars.V_FrameWork_V_Enemy_LongBarrelRadio",
        "Ivars.V_FrameWork_V_Marker_DyingColour",
        "Ivars.V_FrameWork_V_Vip_DeepVoice",
    }
}

this.V_FrameWork_SupportHeliMenu = {
    options = {
        "Ivars.V_FrameWork_V_Heli_DangerLz",
        "Ivars.V_FrameWork_V_Heli_ModerateLz",
        "Ivars.V_FrameWork_V_Pequod_ExtraLines",
        "Ivars.V_FrameWork_V_Pequod_LzMove",
        "Ivars.V_FrameWork_V_Heli_MillerReports",
    }
}

this.V_FrameWork_MotherBaseMenu = {
    options = {
        "Ivars.V_FrameWork_V_Staff_SaluteNews",
        "Ivars.V_FrameWork_V_Staff_Talk",
    }
}

this.langStrings = {
    eng = {
        V_FrameWork = "V_FrameWork",
        V_FrameWork_PlayerMenu = "Player",
        V_FrameWork_SoldiersMenu = "Soldiers",
        V_FrameWork_SupportHeliMenu = "Support heli",
        V_FrameWork_MotherBaseMenu = "Mother Base",
        V_FrameWork_V_Quiet_QuietHoldCQC = "Quiet CQC hold",
        V_FrameWork_V_Quiet_QuietInterr = "Quiet interrogation",
        V_FrameWork_V_ACC_iDroidClose = "ACC iDroid put-away",
        V_FrameWork_V_Idroid_RestoredLines = "Restored iDroid lines",
        V_FrameWork_V_Player_CallEm = "Call Em",
        V_FrameWork_V_Player_LieToThem = "Lie to Them",
        V_FrameWork_V_Player_PainAndDeathLines = "Pain and death lines",
        V_FrameWork_V_Player_BuddyLines = "Buddy lines",
        V_FrameWork_V_Quiet_Lines = "Quiet's lines",
        V_FrameWork_V_Player_PinUpPosters = "Pin-up poster reactions",
        V_FrameWork_V_Ocelot_BinocularComments = "Ocelot binocular comments",
        V_FrameWork_V_Liquid_Driving = "Liquid drives vehicles",
        V_FrameWork_V_Holdup_SelfRelease = "Hold-up self-release",
        V_FrameWork_V_Enemy_SurveyTeams = "Survey teams",
        V_FrameWork_V_Enemy_BackupRequests = "Backup requests",
        V_FrameWork_V_Enemy_UavDownChecks = "Downed UAV checks",
        V_FrameWork_V_Enemy_RadioAnswers = "Soldier radio answers",
        V_FrameWork_V_Enemy_Conversations = "Soldier conversations",
        V_FrameWork_V_Enemy_LongBarrelRadio = "Long-barrel crouch radio",
        V_FrameWork_V_Marker_DyingColour = "Dying soldier marker",
        V_FrameWork_V_Vip_DeepVoice = "VIP deep voice",
        V_FrameWork_V_Heli_DangerLz = "Danger High LZs",
        V_FrameWork_V_Heli_ModerateLz = "Danger Moderate LZs",
        V_FrameWork_V_Pequod_ExtraLines = "Pequod extra lines",
        V_FrameWork_V_Pequod_LzMove = "Pequod LZ move",
        V_FrameWork_V_Heli_MillerReports = "Miller's chopper reports",
        V_FrameWork_V_Staff_SaluteNews = "Staff salute news",
        V_FrameWork_V_Staff_Talk = "Staff talk",
    },
    help = {
        eng = {
            V_FrameWork = "Switch V_FrameWork features on or off.",
            V_FrameWork_PlayerMenu = "What the player can do and say.",
            V_FrameWork_SoldiersMenu = "Enemy soldiers, their radio and their talk.",
            V_FrameWork_SupportHeliMenu = "Pequod, the LZs and Miller's chopper radio.",
            V_FrameWork_MotherBaseMenu = "What Mother Base staff say.",
            V_FrameWork_V_Quiet_QuietHoldCQC = "Playing as Quiet, you can grab enemies in a CQC hold.",
            V_FrameWork_V_Quiet_QuietInterr = "Playing as Quiet, you can interrogate an enemy you hold.",
            V_FrameWork_V_ACC_iDroidClose = "In the ACC, closing the iDroid while seated plays the put-away animation.",
            V_FrameWork_V_Idroid_RestoredLines = "The iDroid speaks its unused lines: chopper calls and LZ picks, strike requests, staff lists, developments, fultons, unit level changes, side ops, emergency missions, dispatches and a platform's first deck. Some lines get their alternate take.",
            V_FrameWork_V_Player_CallEm = "While you hold an enemy, the knock row of the Call Menu becomes Call Em, and he calls a nearby comrade over.",
            V_FrameWork_V_Player_LieToThem = "Hold up or grab an enemy his CP is waiting on, then pick the sonar row of the Call Menu: he reports all clear.",
            V_FrameWork_V_Player_PainAndDeathLines = "The player has his own lines for catching fire, bites, electrocution and shield hits, and for dying from a fall, in fire or in water.",
            V_FrameWork_V_Player_BuddyLines = "Buddy orders mix in unused takes, D-Dog gets told to be quiet or scolded, the player shouts a buddy's name in Alert, and he talks to D-Horse while riding.",
            V_FrameWork_V_Quiet_Lines = "Quiet uses every take she recorded after her shots and when you keep hitting her with non-lethal weapons, laughs after saving you, more the closer you two are, and snaps at you when you hit her with a lethal weapon.",
            V_FrameWork_V_Player_PinUpPosters = "Looking at a gravure or moe poster, the player reacts: puzzled the first time, then appreciative.",
            V_FrameWork_V_Ocelot_BinocularComments = "Through binoculars, Ocelot also comments on the moon, the sun, the Big Dipper, the Southern Cross (Africa), a placed decoy, a rough diamond, an enemy mine, comms equipment and medicinal plants.",
            V_FrameWork_V_Liquid_Driving = "Playing as Liquid, you can take the driver's seat of vehicles. Off, he only rides as a passenger.",
            V_FrameWork_V_Holdup_SelfRelease = "A held-up soldier left alone may free himself: Mid skill 10% after 180 s, High 20% after 120 s, SP 50% after 60 s, Low never. Staying within 10 m or making noise restarts the wait. Once free he radios it in and his base goes to Caution.",
            V_FrameWork_V_Enemy_SurveyTeams = "From STEALTH revenge level 3, a guard who finds something to check may get a two-soldier team sent to search the spot, more often at higher levels. A vehicle blowing up sends two soldiers to its blast site.",
            V_FrameWork_V_Enemy_BackupRequests = "A guard who notices something suspicious again, with nobody close by, radios for backup, and the nearest free soldier joins him to check it.",
            V_FrameWork_V_Enemy_UavDownChecks = "When a UAV goes down, its CP radios that the feed is lost and sends the nearest soldier to check. When he reports back, the base goes to Caution.",
            V_FrameWork_V_Enemy_RadioAnswers = "The soldier nearest you answers the CP's camera or UAV call, a guard sent to check twice answers grudgingly, and destroyed comms gear or a generator gets its own report.",
            V_FrameWork_V_Enemy_Conversations = "PF and Soviet soldiers open conversations with greetings or weather talk, and unused conversations play. Off, silent or cut-short conversations still play in full.",
            V_FrameWork_V_Enemy_LongBarrelRadio = "Soldiers holding a long-barrel gun crouch to radio in, as in Ground Zeroes.",
            V_FrameWork_V_Marker_DyingColour = "A soldier bleeding out gets his own marker colour.",
            V_FrameWork_V_Vip_DeepVoice = "VIPs, such as in Red Brass or War Economy, speak with a deep voice, as in Ground Zeroes.",
            V_FrameWork_V_Heli_DangerLz = "LZs inside enemy bases show Danger High until the base is cleared, and calling the chopper there gets a hesitant reply.",
            V_FrameWork_V_Heli_ModerateLz = "With Danger High LZs on: once every anti-aircraft gun of a base is destroyed, its LZ shows Danger Moderate until the base is cleared, and calling the chopper there may get a cautious reply.",
            V_FrameWork_V_Pequod_ExtraLines = "Pequod reacts to hits, reports damage, calls out where gunfire comes from, announces the doors, and has extra lines at LZs and when lifting off or leaving.",
            V_FrameWork_V_Pequod_LzMove = "If something other than Snake keeps blocking Pequod's LZ, he moves to the nearest free LZ.",
            V_FrameWork_V_Heli_MillerReports = "Calling the chopper gets two unused replies from Miller, and losing choppers to enemies gets reports that grow harsher with each loss.",
            V_FrameWork_V_Staff_SaluteNews = "Saluting staff mention what happened since your last visit: your mission rank, new staff, a new platform or weapon, a lost FOB defense, a disposed nuke or a section level-up.",
            V_FrameWork_V_Staff_Talk = "Staff talk about resource operations and your FOB, and the conversation about the boss's old outfit and Cipher has its cut lines back.",
        },
    },
}

function this.IsCustomMission()
    return InfMission ~= nil and InfMission.missionInfo ~= nil and InfMission.missionInfo[vars.missionCode] ~= nil
end

this.V_FrameWork_V_Quiet_QuietHoldCQC = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 0,
    OnChange = function(self, setting)
        V_Player.SetQuietHoldCqc(setting == 1)
    end
}

this.V_FrameWork_V_Quiet_QuietInterr = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 0,
    OnChange = function(self, setting)
        V_Player.SetQuietInterrogate(setting == 1)
    end
}

this.V_FrameWork_V_ACC_iDroidClose = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_title_sequence.SetPlayIDroidEndMotion(setting == 1)
    end
}

this.V_FrameWork_V_Idroid_RestoredLines = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("RestoredIdroidLines", setting == 1 or this.IsCustomMission())
    end
}

this.V_FrameWork_V_Player_CallEm = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        if setting == 1 then
            V_Player.ResetCallMenuItem(V_CallMenuItem.CALL_COMRADE)
        else
            V_Player.HideCallMenuItem(V_CallMenuItem.CALL_COMRADE)
        end
    end
}

this.V_FrameWork_V_Player_LieToThem = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        if setting == 1 then
            V_Player.ResetCallMenuItem(V_CallMenuItem.FALSE_RADIO_RESPONSE)
        else
            V_Player.HideCallMenuItem(V_CallMenuItem.FALSE_RADIO_RESPONSE)
        end
    end
}

this.V_FrameWork_V_Player_PainAndDeathLines = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("PainAndDeathLines", setting == 1 or this.IsCustomMission())
    end
}

this.V_FrameWork_V_Player_BuddyLines = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("BuddyLines", setting == 1 or this.IsCustomMission())
    end
}

this.V_FrameWork_V_Quiet_Lines = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("QuietLines", setting == 1 or this.IsCustomMission())
    end
}

this.V_FrameWork_V_Player_PinUpPosters = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("PinUpPosterReactions", setting == 1 or this.IsCustomMission())
    end
}

this.V_FrameWork_V_Ocelot_BinocularComments = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("OcelotBinocularComments", setting == 1 or this.IsCustomMission())
    end
}

this.V_FrameWork_V_Liquid_Driving = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("LiquidDriving", setting == 1 or this.IsCustomMission())
    end
}

this.V_FrameWork_V_Holdup_SelfRelease = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        GameObject.SendCommand({ type = "TppSoldier2" }, { id = "SetHoldupSelfRelease", enabled = setting == 1 })
    end
}

this.V_FrameWork_V_Enemy_SurveyTeams = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("SurveyTeams", setting == 1 or this.IsCustomMission())
    end
}

this.V_FrameWork_V_Enemy_BackupRequests = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("BackupRequests", setting == 1 or this.IsCustomMission())
    end
}

this.V_FrameWork_V_Enemy_UavDownChecks = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("UavDownChecks", setting == 1 or this.IsCustomMission())
    end
}

this.V_FrameWork_V_Enemy_RadioAnswers = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("SoldierRadioAnswers", setting == 1 or this.IsCustomMission())
    end
}

this.V_FrameWork_V_Enemy_Conversations = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("SoldierConversations", setting == 1 or this.IsCustomMission())
    end
}

this.V_FrameWork_V_Enemy_LongBarrelRadio = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("LongBarrelRadio", setting == 1 or this.IsCustomMission())
    end
}

this.V_FrameWork_V_Marker_DyingColour = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("DyingMarkerColour", setting == 1 or this.IsCustomMission())
    end
}

this.V_FrameWork_V_Vip_DeepVoice = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_TppEnemy.SetVipDeepVoice(setting == 1)
    end
}

this.V_FrameWork_V_Heli_DangerLz = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("DangerLandingZones", setting == 1 or this.IsCustomMission())
    end
}

this.V_FrameWork_V_Heli_ModerateLz = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("ModerateLandingZones", setting == 1 or this.IsCustomMission())
    end
}

this.V_FrameWork_V_Pequod_ExtraLines = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("PequodExtraLines", setting == 1 or this.IsCustomMission())
    end
}

this.V_FrameWork_V_Pequod_LzMove = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("PequodLzMove", setting == 1 or this.IsCustomMission())
    end
}

this.V_FrameWork_V_Heli_MillerReports = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("MillerChopperReports", setting == 1 or this.IsCustomMission())
    end
}

this.V_FrameWork_V_Staff_SaluteNews = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("StaffSaluteNews", setting == 1 or this.IsCustomMission())
    end
}

this.V_FrameWork_V_Staff_Talk = {
    save = IvarProc.CATEGORY_EXTERNAL,
    range = Ivars.switchRange,
    settingNames = "set_switch",
    default = 1,
    OnChange = function(self, setting)
        V_FrameWork.SetFeatureEnabled("StaffTalk", setting == 1 or this.IsCustomMission())
    end
}

function this.OnAllocate(missionTable)
    V_Player.SetQuietHoldCqc(Ivars.V_FrameWork_V_Quiet_QuietHoldCQC:Get() == 1)
    V_Player.SetQuietInterrogate(Ivars.V_FrameWork_V_Quiet_QuietInterr:Get() == 1)
    V_title_sequence.SetPlayIDroidEndMotion(Ivars.V_FrameWork_V_ACC_iDroidClose:Get() == 1)
    GameObject.SendCommand({ type = "TppSoldier2" }, { id = "SetHoldupSelfRelease", enabled = Ivars.V_FrameWork_V_Holdup_SelfRelease:Get() == 1 })

    local customMission = this.IsCustomMission()
    V_FrameWork.SetFeatureEnabled("RestoredIdroidLines", Ivars.V_FrameWork_V_Idroid_RestoredLines:Get() == 1 or customMission)
    V_FrameWork.SetFeatureEnabled("PainAndDeathLines", Ivars.V_FrameWork_V_Player_PainAndDeathLines:Get() == 1 or customMission)
    V_FrameWork.SetFeatureEnabled("BuddyLines", Ivars.V_FrameWork_V_Player_BuddyLines:Get() == 1 or customMission)
    V_FrameWork.SetFeatureEnabled("QuietLines", Ivars.V_FrameWork_V_Quiet_Lines:Get() == 1 or customMission)
    V_FrameWork.SetFeatureEnabled("PinUpPosterReactions", Ivars.V_FrameWork_V_Player_PinUpPosters:Get() == 1 or customMission)
    V_FrameWork.SetFeatureEnabled("OcelotBinocularComments", Ivars.V_FrameWork_V_Ocelot_BinocularComments:Get() == 1 or customMission)
    V_FrameWork.SetFeatureEnabled("LiquidDriving", Ivars.V_FrameWork_V_Liquid_Driving:Get() == 1 or customMission)
    V_FrameWork.SetFeatureEnabled("SurveyTeams", Ivars.V_FrameWork_V_Enemy_SurveyTeams:Get() == 1 or customMission)
    V_FrameWork.SetFeatureEnabled("BackupRequests", Ivars.V_FrameWork_V_Enemy_BackupRequests:Get() == 1 or customMission)
    V_FrameWork.SetFeatureEnabled("UavDownChecks", Ivars.V_FrameWork_V_Enemy_UavDownChecks:Get() == 1 or customMission)
    V_FrameWork.SetFeatureEnabled("SoldierRadioAnswers", Ivars.V_FrameWork_V_Enemy_RadioAnswers:Get() == 1 or customMission)
    V_FrameWork.SetFeatureEnabled("SoldierConversations", Ivars.V_FrameWork_V_Enemy_Conversations:Get() == 1 or customMission)
    V_FrameWork.SetFeatureEnabled("LongBarrelRadio", Ivars.V_FrameWork_V_Enemy_LongBarrelRadio:Get() == 1 or customMission)
    V_FrameWork.SetFeatureEnabled("DyingMarkerColour", Ivars.V_FrameWork_V_Marker_DyingColour:Get() == 1 or customMission)
    V_FrameWork.SetFeatureEnabled("DangerLandingZones", Ivars.V_FrameWork_V_Heli_DangerLz:Get() == 1 or customMission)
    V_FrameWork.SetFeatureEnabled("ModerateLandingZones", Ivars.V_FrameWork_V_Heli_ModerateLz:Get() == 1 or customMission)
    V_FrameWork.SetFeatureEnabled("PequodExtraLines", Ivars.V_FrameWork_V_Pequod_ExtraLines:Get() == 1 or customMission)
    V_FrameWork.SetFeatureEnabled("PequodLzMove", Ivars.V_FrameWork_V_Pequod_LzMove:Get() == 1 or customMission)
    V_FrameWork.SetFeatureEnabled("MillerChopperReports", Ivars.V_FrameWork_V_Heli_MillerReports:Get() == 1 or customMission)
    V_FrameWork.SetFeatureEnabled("StaffSaluteNews", Ivars.V_FrameWork_V_Staff_SaluteNews:Get() == 1 or customMission)
    V_FrameWork.SetFeatureEnabled("StaffTalk", Ivars.V_FrameWork_V_Staff_Talk:Get() == 1 or customMission)
end

function this.Init(missionTable)
    -- the Call Menu puts its built-in rows back every mission
    if Ivars.V_FrameWork_V_Player_CallEm:Get() == 0 then
        V_Player.HideCallMenuItem(V_CallMenuItem.CALL_COMRADE)
    end
    if Ivars.V_FrameWork_V_Player_LieToThem:Get() == 0 then
        V_Player.HideCallMenuItem(V_CallMenuItem.FALSE_RADIO_RESPONSE)
    end
end

return this
