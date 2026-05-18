local this = {}

if rawget(_G, "V_FrameWork_Core") then
    return _G.V_FrameWork_Core
end

local ok, V_FrameWorkOrErr = pcall(require, "V_FrameWork")
if not ok then
    error("V_FrameWork_Core: failed to require V_FrameWork: " .. tostring(V_FrameWorkOrErr))
end

local VFW = V_FrameWorkOrErr

_G.VFW = VFW
_G.V_FrameWork = VFW
_G.V_FrameWork_Core = this

this.V_FrameWork = VFW


VFW.MessageListeners = VFW.MessageListeners or {}

function VFW.RegisterListener(module)
    if type(module) ~= "table" then
        VFW.Log("V_FrameWork.RegisterListener: argument is not a table.")
        return
    end
    if type(module.OnMessage) ~= "function" then
        VFW.Log("V_FrameWork.RegisterListener: module has no OnMessage function.")
        return
    end
    for _, m in ipairs(VFW.MessageListeners) do
        if m == module then return end
    end
    table.insert(VFW.MessageListeners, module)
end

function VFW.UnregisterListener(module)
    for i, m in ipairs(VFW.MessageListeners) do
        if m == module then
            table.remove(VFW.MessageListeners, i)
            return
        end
    end
end

function VFW.BroadcastMessage(category, msg, arg0, arg1, arg2, arg3)
    local senderHash = Fox.StrCode32(category)
    local msgHash    = Fox.StrCode32(msg)
    for _, m in ipairs(VFW.MessageListeners) do
        if m.OnMessage then
            m.OnMessage(senderHash, msgHash, arg0, arg1, arg2, arg3, "")
        end
    end
end

function this.OnTerminate()
    V_SoundCoreDaemon.ClearAllPerAkObjIdPitchBiases()
    V_TppCommandPost.UnsetCautionPhaseDuration()
    V_TppEnemy.ClearCallSignPatrolSoldiers()
    V_TppEnemy.ClearSoldierStealthCamoOverrides()
    V_TppEnemy.ClearAllEnemyInformationLangIdForSoldiers()
    V_TppEnemy.ClearAllEnemyUnitNameForSoldiers()
    V_TppHostage.ClearLostHostages()
    V_TppHostage.ClearAllCustomLostLabels()
    V_TppSahelan.ClearSahelanFova()
    V_TppSahelan.ClearEyeLampColor()
    V_TppSahelan.ClearHeartLightColor()
end

function this.AddMissionPacks(missionCode,packPaths)
	if InfMain.IsOnlineMission(missionCode) or missionCode < 5 then return end

	packPaths[#packPaths + 1] = "/Assets/tpp/pack/V_FrameWork/V_FrameWork_Common.fpk"
end


return this
