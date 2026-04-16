-- V_TppEquip.lua
-- V_FrameWork example: Equipment system
--
-- Shows how to register custom weapons, weapon parts, ammo, receivers,
-- support weapons, equip ID tables, develop tables, gun basics, equip
-- parameters, and icon texture overrides.
--
-- These functions are typically called during module initialization before
-- the game loads equip data, so they should be called from Init or the
-- module-level setup (not inside a message handler).

local this = {}

-- Register a persistent equip ID by name.
-- equipName: string key used to look up the ID later (e.g. "WP_MY_RIFLE").
-- Returns the assigned equip ID number.
function this.RegisterConstantEquipId(equipName)
    return V_FrameWork.RegisterConstantEquipId(equipName)
end

-- Declare a weapon part (WP) entry.
-- weaponName: the weapon part name string as used in equip tables.
-- Returns a table with the declared WP's equip IDs and metadata.
function this.DeclareWPs(weaponName)
    return V_FrameWork.DeclareWPs(weaponName)
end

-- Declare a support weapon part (SWP) entry.
-- supportWeaponName: the support weapon name string.
-- Returns a table with the declared SWP's equip IDs and metadata.
function this.DeclareSWPs(supportWeaponName)
    return V_FrameWork.DeclareSWPs(supportWeaponName)
end

-- Declare a receiver (RC) entry.
-- receiverName: the receiver name string.
-- Returns a table with the declared RC's equip IDs and metadata.
function this.DeclareRCs(receiverName)
    return V_FrameWork.DeclareRCs(receiverName)
end

-- Declare an ammo type (AM) entry.
-- ammoName: the ammo type name string.
-- Returns a table with the declared AM's equip IDs and metadata.
function this.DeclareAMs(ammoName)
    return V_FrameWork.DeclareAMs(ammoName)
end

-- Set the type ID for a support weapon equip ID.
-- supportWeaponId: the equip ID of the support weapon.
-- swpType: the support weapon type integer.
function this.SetSupportWeaponType(supportWeaponId, swpType)
    V_FrameWork.SetSupportWeaponType(supportWeaponId, swpType)
end

-- Add entries to the equip ID table.
-- equipTable: a table of equip entries in the game's equip ID table format.
-- Returns the number of entries added, or false on failure.
function this.AddToEquipIdTable(equipTable)
    return V_FrameWork.AddToEquipIdTable(equipTable)
end

-- Set gun basic parameters for a weapon.
-- gunBasic: table of gun basic fields. weaponId is required.
--   Optional fields (all integers, -1 = use vanilla default):
--     receiverId, barrelId, ammoId, stockId, muzzleId, muzzleOptionId,
--     scope1Id, scope2Id, underBarrelId, laserFlash1Id, laserFlash2Id,
--     weaponGrade
function this.SetGunBasic(gunBasic)
    V_FrameWork.SetGunBasic(gunBasic)
end

-- Add an entry to the equip develop table.
-- equipName: unique string key identifying this develop entry.
-- developData: table with required sub-tables:
--   developData.const = { ... }  -- constant develop data fields
--   developData.flow  = { ... }  -- flow develop data fields
-- Returns the develop ID on success, or false on failure.
function this.AddToEquipDevelopTable(equipName, developData)
    return V_FrameWork.AddToEquipDevelopTable(equipName, developData)
end

-- Set equip parameters (receiver stats, wobbling, system, and sound data).
-- params: table containing receiver parameter tables in the game's format.
function this.SetEquipParameters(params)
    V_FrameWork.SetEquipParameters(params)
end

-- Set a custom icon FTEX texture path for a specific equip ID.
-- equipId: the equip ID to assign the icon to.
-- path: FTEX asset path for the icon texture.
function this.SetEquipIdIconFtexPath(equipId, path)
    V_FrameWork.SetEquipIdIconFtexPath(equipId, path)
end

-- Clear the custom icon FTEX path for a specific equip ID, restoring vanilla.
function this.ClearIconFtexPath(equipId)
    V_FrameWork.ClearIconFtexPath(equipId)
end

-- Clear all custom icon FTEX path overrides, restoring vanilla for all equips.
function this.ClearAllIconFtexPaths()
    V_FrameWork.ClearAllIconFtexPaths()
end

return this

--[[
Example usage:

local Equip = require("V_TppEquip")

-- Register a persistent equip ID for a custom rifle
local rifleEquipId = Equip.RegisterConstantEquipId("WP_MY_CUSTOM_RIFLE")

-- Declare weapon parts
local wps = Equip.DeclareWPs("WP_MY_CUSTOM_RIFLE")

-- Declare matching ammo
local ams = Equip.DeclareAMs("AM_556")

-- Set the gun basic configuration (which parts this weapon uses by default)
Equip.SetGunBasic({
    weaponId  = rifleEquipId,
    receiverId = wps.receiverId,
    ammoId    = ams.ammoId,
    weaponGrade = 1,
})

-- Add a custom icon for the weapon
Equip.SetEquipIdIconFtexPath(rifleEquipId, "/Assets/tpp/ui/texture/equip_icon/my_rifle_icon.ftex")

-- Add a develop entry so the weapon appears in development
Equip.AddToEquipDevelopTable("WP_MY_CUSTOM_RIFLE", {
    const = { ... },
    flow  = { ... },
})
]]
