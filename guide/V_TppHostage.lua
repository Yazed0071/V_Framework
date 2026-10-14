local this = {}

local StrCode32              = Fox.StrCode32
local IsTypeString           = Tpp.IsTypeString
local GetGameObjectId        = GameObject.GetGameObjectId
local GetGameObjectIdByIndex = GameObject.GetGameObjectIdByIndex
local SendCommand            = GameObject.SendCommand
local NULL_ID                = GameObject.NULL_ID

local HOSTAGE_OBJECT_TYPES = { "TppHostage2", "TppHostageUnique", "TppHostageUnique2" }

local function pushEntry(hostage)
    GameObject.SendCommand(hostage.gameObjectId, { id = "SetLostHostage", hostageType = hostage.gender })   -- no labels: keeps any label a mod already set
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

    GameObject.SendCommand(hostageNameOrId, { 
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
    GameObject.SendCommand(hostageNameOrId, { id = "RemoveLostHostage" })
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
                    this.BuildHostageList()
                    this.AutoSetLostHostage()
                end,
            },
        },
    }
end

function this.SetUpEnemy()
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