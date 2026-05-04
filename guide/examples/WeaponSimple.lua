--==============================================================================
-- V_FrameWork weapon example — MINIMUM VIABLE
--==============================================================================
-- Reuses every vanilla component (receiver, barrel, ammo, etc).
-- Only the weapon ID and equip slot are custom.
--
-- Use this as the starting point. Add Declare* / SetEquipParameters /
-- AddToEquipMotionDataTable / icon overrides as you need them. See
-- WeaponAPI.md for the full registration flow.
--==============================================================================

local equipId = V_TppEquip.RegisterConstantEquipId("EQP_WP_MyMod_BasicAR")
local wpId    = V_TppEquip.DeclareWPs("WP_MyMod_BasicAR_00")

-- Register the .parts / .fpk paths against the equipId.
V_TppEquip.AddToEquipIdTable({
    { equipId, TppEquip.EQP_TYPE_Assault, 290,
      TppEquip.EQP_BLOCK_MISSION,
      "/Assets/tpp/parts/wp/wp_mymod_basic_ar.parts",
      "/Assets/tpp/pack/wp/wp_mymod_basic_ar.fpk" },
})

-- Wire to vanilla internals. Replace these with the vanilla RC_/BA_/AM_
-- constants for whatever weapon family you're cloning — print
-- `for k,v in pairs(TppEquip) do print(k,v) end` from a Lua hook to
-- enumerate all available IDs in a running game.
V_TppEquip.SetGunBasic({
    weaponId   = wpId,
    receiverId = TppEquip.RC_30001,   -- example: replace with real vanilla RC_*
    barrelId   = TppEquip.BA_30115,   -- example: replace with real vanilla BA_*
    -- ammoId omitted → SetGunBasic falls back to the receiver's default ammo
    grade      = 4,
})

-- R&D entry so the player can develop and equip it.
V_TppEquip.AddToEquipDevelopTable("MyMod:BasicAR", {
    const = {
        equipID             = equipId,
        equipDevelopTypeID  = TppMbDev.EQP_DEV_TYPE_Assault,
        langEquipName       = "wp_mymod_basic_ar_name",
        langEquipInfo       = "wp_mymod_basic_ar_desc",
        equipDevelopGroupID = TppMbDev.EQP_DEV_GROUP_WEAPON_120,
        unk36 = 1,
    },
    flow = {
        grade              = 4,
        developGmpCost     = 25000,
        resourceType1      = "CommonMetal",
        resourceType1Count = 120,
    },
})
