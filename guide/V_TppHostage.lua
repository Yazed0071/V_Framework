-- V_TppHostage — lost-hostage tracking.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}

local StrCode32              = Fox.StrCode32
local IsTypeString           = Tpp.IsTypeString
local GetGameObjectId        = GameObject.GetGameObjectId
local GetGameObjectIdByIndex = GameObject.GetGameObjectIdByIndex
local SendCommand            = GameObject.SendCommand
local NULL_ID                = GameObject.NULL_ID

local HOSTAGE_OBJECT_TYPES = { "TppHostage2", "TppHostageUnique", "TppHostageUnique2" }

local function ResolveHostageId(hostageNameOrId, callerLogTag)
    if hostageNameOrId == nil then
        V_FrameWork.Log(callerLogTag .. ": hostageNameOrId is nil.")
        return nil
    end
    if IsTypeString(hostageNameOrId) then
        local id = GetGameObjectId(hostageNameOrId)
        if id == nil then
            V_FrameWork.Log(callerLogTag .. ": No game object found for name: " .. hostageNameOrId)
            return nil
        end
        return id
    end
    return hostageNameOrId
end

-- gender: 0 = male, 1 = female, 2 = child.
function this.SetLostHostage(hostageNameOrId, gender, hostageLostLabel)
    local id = ResolveHostageId(hostageNameOrId, "V_TppHostage.SetLostHostage")
    if id == nil then return end

    if type(gender) ~= "number" then
        V_FrameWork.Log("V_TppHostage.SetLostHostage: gender is not a number.")
        return
    end
    if hostageLostLabel ~= nil
        and not IsTypeString(hostageLostLabel)
        and type(hostageLostLabel) ~= "number" then
        V_FrameWork.Log("V_TppHostage.SetLostHostage: hostageLostLabel must be a string, number, or nil.")
        return
    end

    V_FrameWork.SetLostHostage(id, gender, hostageLostLabel or 0)
end

function this.RemoveLostHostage(hostageNameOrId)
    local id = ResolveHostageId(hostageNameOrId, "V_TppHostage.RemoveLostHostage")
    if id == nil then return end
    V_FrameWork.RemoveLostHostage(id)
end

function this.ClearLostHostages()
    V_FrameWork.ClearLostHostages()
end

function this.SetLostHostageFromPlayer(hostageNameOrId, enable)
    local id = ResolveHostageId(hostageNameOrId, "V_TppHostage.SetLostHostageFromPlayer")
    if id == nil then return end
    if type(enable) ~= "boolean" then
        V_FrameWork.Log("V_TppHostage.SetLostHostageFromPlayer: enable is not a boolean.")
        return
    end
    V_FrameWork.SetLostHostageFromPlayer(id, enable)
end

function this.IsHostageFemale(hostageNameOrId)
    local id = ResolveHostageId(hostageNameOrId, "V_TppHostage.IsHostageFemale")
    if id == nil then return false end
    return SendCommand(id, { id = "IsFemale" }) or false
end

function this.IsHostageChild(hostageNameOrId)
    local id = ResolveHostageId(hostageNameOrId, "V_TppHostage.IsHostageChild")
    if id == nil then return false end
    return SendCommand(id, { id = "IsChild" }) or false
end

function this.BuildHostageList()
    mvars.V_HostageList = {}

    for _, hostageObjectType in ipairs(HOSTAGE_OBJECT_TYPES) do
        local hostageCount = SendCommand({ type = hostageObjectType }, { id = "GetMaxInstanceCount" })
        if hostageCount == nil then
            V_FrameWork.Log("V_TppHostage.BuildHostageList: Failed to get hostage count for type " .. hostageObjectType)
        else
            for i = 0, hostageCount - 1 do
                local hostageGameObjectId = GetGameObjectIdByIndex(hostageObjectType, i)
                if hostageGameObjectId ~= NULL_ID then
                    local gender = 0  -- male
                    if this.IsHostageChild(hostageGameObjectId) then
                        gender = 2
                    elseif this.IsHostageFemale(hostageGameObjectId) then
                        gender = 1
                    end
                    table.insert(mvars.V_HostageList, {
                        gameObjectId = hostageGameObjectId,
                        gender       = gender,
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
    for _, entry in ipairs(mvars.V_HostageList) do
        this.SetLostHostage(entry.gameObjectId, entry.gender)
    end
end


function this.AutoSetLostHostageFromPlayer(enable)
    if mvars.V_HostageList == nil then
        this.BuildHostageList()
    end
    for _, entry in ipairs(mvars.V_HostageList) do
        this.SetLostHostageFromPlayer(entry.gameObjectId, enable)
    end
end

function this.Messages()
    return Tpp.StrCode32Table {
        GameObject = {
            {
                msg = "ChangePhase",
                func = function(gameObjectId, phaseName)
                    if phaseName >= TppGameObject.PHASE_CAUTION then
                        this.AutoSetLostHostageFromPlayer(true)
                    else
                        this.AutoSetLostHostageFromPlayer(false)
					end
                end,
            },
        },
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
    }
end


function this.SetUpEnemy()
    this.ClearLostHostages()
    this.BuildHostageList()
    this.AutoSetLostHostage()
    this.AutoSetLostHostageFromPlayer(false)
end


function this.Init(missionTable)
    this.messageExecTable = Tpp.MakeMessageExecTable(this.Messages())
end

function this.OnReload(missionTable)
    this.messageExecTable = Tpp.MakeMessageExecTable(this.Messages())
end

function this.OnMessage(sender, messageId, arg0, arg1, arg2, arg3, strLogText)
    Tpp.DoMessage(this.messageExecTable, TppMission.CheckMessageOption, sender, messageId, arg0, arg1, arg2, arg3, strLogText)
end


return this
