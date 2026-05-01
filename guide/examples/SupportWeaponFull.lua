local sw_registered = false

function this.OnAllocate()
    if sw_registered then return end

    local mySwpType = V_TppEquip.RegisterSupportWeaponCategory{
        name     = "PlasmaCharge",
        behavior = "grenade",

        blast = {
            flag     = 0,
            maxRange = 18,
            optRange = 12,
        },

        damage = {
            lethalDamageUI = 4500,
            unk3           = 3500,
            unk4           = 4000,
            unk5           = 0,
            unk6           = 0,
            unk7           = 4000,
            unk8           = 4000,
            injureType     = TppDamage.INJ_TYPE_DISLOCATED,
            injurePart     = TppDamage.INJ_PART_ALL,
            unk11          = 4,
            unk12          = 20,
            hitNPC         = 1,
            unk14          = 0,
            unk15          = 0,
            isTranq        = 0,
            isStun         = 0,
            unk18          = 0,
            unk19          = 0,
            unk20          = 0,
            isFire         = 0,
            unk22          = 0,
            isGas          = 0,
            unk24          = 0,
            unk25          = 0,
            isElectric     = 0,
            unk27          = 0,
            unk28          = 0,
            damageSource   = TppDamage.DAM_SOURCE_Throwing,
            lethalDamage   = 4500,
            staminaDamage  = 0,
            impactForce    = 2000,
        },

        swpRow = {
            ammo  = 6,
            p1    = 4,
            p2    = 0,
            grade = 5,
        },
    }

    local equipId = V_TppEquip.RegisterConstantEquipId("EQP_SWP_PlasmaCharge")
    local swpId   = V_TppEquip.DeclareSWPs("SWP_PlasmaCharge")

    V_TppEquip.SetSupportWeaponType(equipId, mySwpType)

    V_TppEquip.AddToEquipIdTable({
        { equipId,
          TppEquip.EQP_TYPE_Throwing,
          swpId,
          TppEquip.EQP_BLOCK_COMMON,
          "/Assets/tpp/parts/weapon/thr/tr01_main0_def_v00.parts",
          "" },
    })

    V_TppEquip.AddToEquipDevelopTable("EQP_DEV_PlasmaCharge", {
        p00 = 0,
        p01 = equipId,
        p02 = TppMbDev.EQP_DEV_TYPE_Throwing,
        p03 = 0,
        p04 = 0,
        p05 = 65535,
        p06 = "name_wp_plasmacharge",
        p07 = "info_wp_plasmacharge",
        p08 = "/Assets/tpp/ui/texture/EquipIcon/supportweapon/ui_swp_grenade_00_10_alp",
        p09 = TppMbDev.EQP_DEV_GROUP_SUPPORT_010,
        p30 = "name_wp_plasmacharge",
        p31 = 0, p32 = 0, p33 = 0, p34 = 1, p35 = 1, p36 = 0,
    })

    sw_registered = true

    V_FrameWork.Log(string.format(
        "[PlasmaCharge] Registered: equipId=0x%X swpId=0x%X SWP_TYPE_PlasmaCharge=0x%X",
        equipId, swpId, mySwpType))
end
