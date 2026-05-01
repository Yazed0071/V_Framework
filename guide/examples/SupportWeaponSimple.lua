local sw_registered = false

function this.OnAllocate()
    if sw_registered then return end

    local mySwpType = V_TppEquip.RegisterSupportWeaponCategory{
        name     = "MyChaff",
        behavior = "smoke",
        blast = {
            flag     = 2,
            maxRange = 12,
            optRange = 8,
        },
        swpRow = {
            ammo  = 4,
            p1    = 3,
            p2    = 0,
            grade = 1,
        },
    }

    local equipId = V_TppEquip.RegisterConstantEquipId("EQP_SWP_MyChaffGrenade")
    local swpId   = V_TppEquip.DeclareSWPs("SWP_MyChaffGrenade")

    V_TppEquip.SetSupportWeaponType(equipId, mySwpType)

    V_TppEquip.AddToEquipIdTable({
        { equipId,
          TppEquip.EQP_TYPE_Throwing,
          swpId,
          TppEquip.EQP_BLOCK_COMMON,
          "/Assets/tpp/parts/weapon/thr/tr02_main0_def_v00.parts",
          "" },
    })

    V_TppEquip.AddToEquipDevelopTable("EQP_DEV_MyChaffGrenade", {
        p00 = 0,
        p01 = equipId,
        p02 = TppMbDev.EQP_DEV_TYPE_Throwing,
        p03 = 0,
        p04 = 0,
        p05 = 65535,
        p06 = "name_wp_10060",
        p07 = "info_wp_10060",
        p08 = "/Assets/tpp/ui/texture/EquipIcon/supportweapon/ui_swp_stungrenade_00_10_alp",
        p09 = TppMbDev.EQP_DEV_GROUP_SUPPORT_090,
        p30 = "name_wp_10060",
        p31 = 0, p32 = 0, p33 = 0, p34 = 1, p35 = 1, p36 = 0,
    })

    sw_registered = true

    V_FrameWork.Log(string.format(
        "[MyChaff] Registered: equipId=0x%X swpId=0x%X SWP_TYPE_MyChaff=0x%X",
        equipId, swpId, mySwpType))
end
