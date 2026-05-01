-- ChaffGrenade — custom support weapon that fires the engine's real chaff
-- disruption effect when it lands (instead of a grenade explosion).
--
-- The 'chaffEffect' field in RegisterSupportWeaponCategory hooks
-- ThrowingImpl::UpdateActionGrenade / UpdateActionSmoke and calls
-- RangeAttackSystemImpl::RequestToChaff at the projectile's landing
-- position. Downstream consumers (security cameras, command-post radio,
-- soldier radio actions, visual chaff cloud) all kick in for free — same
-- behaviour as the helicopter Mother Base CHAFF support call.
--
-- Pick `behavior = "smoke"` for a quiet drop-and-deploy feel (smoke cloud
-- is the visual carrier), or `behavior = "grenade"` if you also want a
-- bang. The chaff effect itself is independent of behavior.

local sw_registered = false

function this.OnAllocate()
    if sw_registered then return end

    local mySwpType = V_TppEquip.RegisterSupportWeaponCategory{
        name     = "ChaffGrenade",
        behavior = "smoke",

        -- Minimal blast — chaff isn't supposed to hurt anyone.
        blast = {
            flag     = 1,
            maxRange = 6,
            optRange = 4,
        },

        -- Token damage row — chaff is non-lethal and primarily disruptive.
        damage = {
            lethalDamage  = 0,
            staminaDamage = 200,
            impactForce   = 800,
            isStun        = 1,
            damageSource  = TppDamage.DAM_SOURCE_Throwing,
        },

        swpRow = {
            ammo  = 5,
            p1    = 8,
            p2    = 0,
            grade = 4,
        },

        -- The new field — when present, the framework auto-fires
        -- RequestToChaff at the landing position on detonation.
        chaffEffect = {
            radius   = 18,    -- meters; vanilla helicopter CHAFF is ~15m
            duration = 25,    -- seconds; vanilla is ~20s
        },
    }

    local equipId = V_TppEquip.RegisterConstantEquipId("EQP_SWP_ChaffGrenade")
    local swpId   = V_TppEquip.DeclareSWPs("SWP_ChaffGrenade")

    V_TppEquip.SetSupportWeaponType(equipId, mySwpType)

    V_TppEquip.AddToEquipIdTable({
        { equipId,
          TppEquip.EQP_TYPE_Throwing,
          swpId,
          TppEquip.EQP_BLOCK_COMMON,
          "/Assets/tpp/parts/weapon/thr/tr01_main0_def_v00.parts",
          "" },
    })

    V_TppEquip.AddToEquipDevelopTable("EQP_DEV_ChaffGrenade", {
        p00 = 0,
        p01 = equipId,
        p02 = TppMbDev.EQP_DEV_TYPE_Throwing,
        p03 = 0,
        p04 = 0,
        p05 = 65535,
        p06 = "name_wp_chaffgrenade",
        p07 = "info_wp_chaffgrenade",
        p08 = "/Assets/tpp/ui/texture/EquipIcon/supportweapon/ui_swp_grenade_00_10_alp",
        p09 = TppMbDev.EQP_DEV_GROUP_SUPPORT_010,
        p30 = "name_wp_chaffgrenade",
        p31 = 0, p32 = 0, p33 = 0, p34 = 1, p35 = 1, p36 = 0,
    })

    sw_registered = true

    V_FrameWork.Log(string.format(
        "[ChaffGrenade] Registered: equipId=0x%X swpId=0x%X SWP_TYPE_ChaffGrenade=0x%X",
        equipId, swpId, mySwpType))
end


-- ---------------------------------------------------------------------------
-- Lua primitive: V_FrameWork.RequestChaffAt(x, y, z [, radius=15] [, duration=20])
-- ---------------------------------------------------------------------------
-- Direct call. Useful for triggering chaff from non-throwable events (a
-- mission script, a damage callback, a debug bind). Returns 1 on success,
-- 0 if the engine's pending-chaff queue is full (max 4 simultaneous).
--
-- Example: drop chaff at the player's current spot.
--
--   local px, py, pz = TppPlayer.GetPlayerPosition()
--   V_FrameWork.RequestChaffAt(px, py, pz, 20, 30)
