-- V_TppEquip — weapons, equipment, develop tables, icon overrides.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}

function this.RegisterConstantEquipId(equipName)
    return V_FrameWork.RegisterConstantEquipId(equipName)
end

function this.DeclareWPs(weaponName)
    return V_FrameWork.DeclareWPs(weaponName)
end

function this.DeclareSWPs(supportWeaponName)
    return V_FrameWork.DeclareSWPs(supportWeaponName)
end

function this.DeclareRCs(receiverName)
    return V_FrameWork.DeclareRCs(receiverName)
end

function this.DeclareAMs(ammoName)
    return V_FrameWork.DeclareAMs(ammoName)
end

function this.SetSupportWeaponType(supportWeaponId, swpType)
    V_FrameWork.SetSupportWeaponType(supportWeaponId, swpType)
end

function this.AddToEquipIdTable(equipTable)
    return V_FrameWork.AddToEquipIdTable(equipTable)
end

function this.SetGunBasic(gunBasic)
    V_FrameWork.SetGunBasic(gunBasic)
end

function this.AddToEquipDevelopTable(equipName, developData)
    return V_FrameWork.AddToEquipDevelopTable(equipName, developData)
end

function this.SetEquipParameters(params)
    V_FrameWork.SetEquipParameters(params)
end

function this.SetEquipIdIconFtexPath(equipId, path)
    V_FrameWork.SetEquipIdIconFtexPath(equipId, path)
end

function this.ClearIconFtexPath(equipId)
    V_FrameWork.ClearIconFtexPath(equipId)
end

function this.ClearAllIconFtexPaths()
    V_FrameWork.ClearAllIconFtexPaths()
end

return this
