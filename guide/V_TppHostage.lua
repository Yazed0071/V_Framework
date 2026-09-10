local this = {}

local StrCode32              = Fox.StrCode32
local IsTypeString           = Tpp.IsTypeString
local GetGameObjectId        = GameObject.GetGameObjectId
local GetGameObjectIdByIndex = GameObject.GetGameObjectIdByIndex
local SendCommand            = GameObject.SendCommand
local NULL_ID                = GameObject.NULL_ID

local HOSTAGE_OBJECT_TYPES = { "TppHostage2", "TppHostageUnique", "TppHostageUnique2" }

this.labels = {}

local function lookupCustomLabel(gameObjectId, gender, scenario)
    local label = this.labels[gameObjectId]

    if label == nil then
        for k, v in pairs(this.labels) do
            if type(k) == "string" and k ~= "male" and k ~= "female" and k ~= "child" then
                if GetGameObjectId(k) == gameObjectId then
                    label = v
                    break
                end
            end
        end
    end

    if label == nil then
        if gender == 0 then label = this.labels.male
        elseif gender == 1 then label = this.labels.female
        elseif gender == 2 then label = this.labels.child end
    end

    if type(label) == "table" then return label[scenario] end
    return label
end

local function pushEntry(hostage)
    local labelGone = lookupCustomLabel(hostage.gameObjectId, hostage.gender, "gone")
    local labelTaken = lookupCustomLabel(hostage.gameObjectId, hostage.gender, "taken")
    
    GameObject.SendCommand(hostage.gameObjectId, { 
        id = "SetLostHostage", 
        hostageType = hostage.gender, 
        customLostLabel = labelGone or 0,
        customLostLabelTaken = labelTaken or 0
    })
end

function this.SetLostHostage(hostageNameOrId, gender, hostageLostLabel, hostageLostLabelTaken)
    if hostageNameOrId == nil then
        V_FrameWork.Log("V_TppHostage.SetLostHostage: hostageNameOrId is nil.")
        return
    end
    if IsTypeString(hostageNameOrId) then
        hostageNameOrId = GetGameObjectId(hostageNameOrId)
    end
    if hostageNameOrId == NULL_ID then
        V_FrameWork.Log("V_TppHostage.SetLostHostage: hostageId is NULL_ID.")
        return
    end
    if type(gender) ~= "number" then
        V_FrameWork.Log("V_TppHostage.SetLostHostage: gender is not a number (0 = male, 1 = female, 2 = child).")
        gender = 0
    end
    
    local customLabel = (type(hostageLostLabel) == "string" or type(hostageLostLabel) == "number") and hostageLostLabel or 0
    local customLabelTaken = (type(hostageLostLabelTaken) == "string" or type(hostageLostLabelTaken) == "number") and hostageLostLabelTaken or customLabel

    SendCommand(hostageNameOrId, { 
        id = "SetLostHostage", 
        hostageType = gender, 
        customLostLabel = customLabel,
        customLostLabelTaken = customLabelTaken
    })
end

function this.RemoveLostHostage(hostageNameOrId)
    if hostageNameOrId == nil then
        V_FrameWork.Log("V_TppHostage.RemoveLostHostage: hostageNameOrId is nil.")
        return
    end
    if IsTypeString(hostageNameOrId) then
        hostageNameOrId = GetGameObjectId(hostageNameOrId)
    end
    if hostageNameOrId == NULL_ID then
        V_FrameWork.Log("V_TppHostage.RemoveLostHostage: hostageId is NULL_ID.")
        return
    end
    SendCommand(hostageNameOrId, { id = "RemoveLostHostage" })
end

function this.ClearLostHostages()
    SendCommand({ type = "TppHostage2" }, { id = "ClearLostHostages" })
end

function this.IsHostageFemale(hostageNameOrId)
    if hostageNameOrId == nil then
        V_FrameWork.Log("V_TppHostage.IsHostageFemale: hostageNameOrId is nil.")
        return
    end
    if IsTypeString(hostageNameOrId) then
        hostageNameOrId = GetGameObjectId(hostageNameOrId)
    end
    if hostageNameOrId == NULL_ID then
        V_FrameWork.Log("V_TppHostage.IsHostageFemale: hostageId is NULL_ID.")
        return
    end

    local isFemale = SendCommand(hostageNameOrId, { id = "IsFemale" })
    return isFemale
end

function this.IsHostageChild(hostageNameOrId)
    if hostageNameOrId == nil then
        V_FrameWork.Log("V_TppHostage.IsHostageChild: hostageNameOrId is nil.")
        return
    end
    if IsTypeString(hostageNameOrId) then
        hostageNameOrId = GetGameObjectId(hostageNameOrId)
    end
    if hostageNameOrId == NULL_ID then
        V_FrameWork.Log("V_TppHostage.IsHostageChild: hostageId is NULL_ID.")
        return
    end

    local isChild = SendCommand(hostageNameOrId, { id = "IsChild" })
    return isChild
end

function this.SetCustomLostLabel(key, value)
    this.labels[key] = value
    this.RefreshCustomLabels()
end

function this.ClearCustomLostLabel(key)
    this.labels[key] = nil
    this.RefreshCustomLabels()
end

function this.ClearAllCustomLostLabels()
    this.labels = {}
    this.RefreshCustomLabels()
end

function this.RegisterCustomLostLabels(t)
    if type(t) ~= "table" then
        V_FrameWork.Log("V_TppHostage.RegisterCustomLostLabels: argument is not a table.")
        return
    end
    for k, v in pairs(t) do
        this.labels[k] = v
    end
    this.RefreshCustomLabels()
end

function this.RefreshCustomLabels()
    if mvars.V_HostageList == nil then return end
    for _, hostage in ipairs(mvars.V_HostageList) do
        pushEntry(hostage)
    end
end

function this.BuildHostageList()
    mvars.V_HostageList = {}

    for _, hostageObjectType in ipairs(HOSTAGE_OBJECT_TYPES) do
        local hostageCount = SendCommand({ type = hostageObjectType }, { id = "GetMaxInstanceCount" })
        if hostageCount ~= nil then
            for i = 0, hostageCount - 1 do
                local hostageGameObjectId = GetGameObjectIdByIndex(hostageObjectType, i)
                if hostageGameObjectId ~= NULL_ID then
                    local gender = 0  -- male
                    if this.IsHostageChild(hostageGameObjectId) then
                        gender = 2 -- child
                    elseif this.IsHostageFemale(hostageGameObjectId) then
                        gender = 1 -- female
                    end
                    
                    table.insert(mvars.V_HostageList, {
                        gameObjectId = hostageGameObjectId,
                        gender       = gender
                    })
                end
            end
        end
    end

    V_FrameWork.Log("[V_TppHostage]: Built V_HostageList with " .. tostring(#mvars.V_HostageList) .. " entries")
end

function this.AutoSetLostHostage()
    if mvars.V_HostageList == nil then
        this.BuildHostageList()
    end
    for _, hostage in ipairs(mvars.V_HostageList) do
        pushEntry(hostage)
    end
end

function this.Messages()
    return Tpp.StrCode32Table {
        UI = {
            {
                msg = "QuestAreaAnnounceText",
                func = function()
                    this.ClearLostHostages()
                    this.BuildHostageList()
                    this.AutoSetLostHostage()
                end,
            },
        },
        Mission = {
            {
                msg = "MissionStateReset",
                func = function(code)
                    this.ClearAllCustomLostLabels()
                end,
            },
        },
    }
end

function this.SetUpEnemy()
    this.ClearLostHostages()
    this.BuildHostageList()
    this.AutoSetLostHostage()
end

function this.Init(missionTable)
    if TppMission.IsFOBMission(vars.missionCode) then return end
    this.messageExecTable = Tpp.MakeMessageExecTable(this.Messages())
end

function this.OnMessage(sender, messageId, arg0, arg1, arg2, arg3, strLogText)
    if TppMission.IsFOBMission(vars.missionCode) then return end
    Tpp.DoMessage(this.messageExecTable, TppMission.CheckMessageOption, sender, messageId, arg0, arg1, arg2, arg3, strLogText)
end

return this