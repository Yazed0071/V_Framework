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


function this.OnAllocate(missionTable)
    V_SoundCoreDaemon.ClearAllPerAkObjIdPitchBiases()
    V_TppCommandPost.UnsetCautionPhaseDuration()
    V_TppEnemy.ClearCallSignPatrolSoldiers()
    V_TppEnemy.ClearSoldierStealthCamoOverrides()
    V_TppEnemy.ClearEnemyInformationLangId()
    V_TppEnemy.ClearEnemyUnitName()
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
    packPaths[#packPaths + 1] = "/Assets/tpp/pack/mission2/online/o50050/o50050_additional.fpk"
end


return this
