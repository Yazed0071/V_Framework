#include <pch.h>
#include "ThrowingImpl_ActivateThrowingAtEmptyWork.h"

#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "SupportWeaponRegistry.h"
#include "ThrowingImpl_CalcInitialVelocityByAngle.h"
#include "ThrowingImpl_UpdateAction.h"
#include "ThrowingImpl_UpdateNextPosition.h"
#include "ThrowingImpl_UpdateStatus.h"
#include "ThrowingSystemLayout.h"
#include "log.h"

namespace
{
    using ActivateThrowingAtEmptyWork_t = std::uint32_t(__fastcall*)(void* impl, const void* desc);
    using GetSupportWeaponTypeId_t      = int(__fastcall*)(void* equipIdTable, std::uint32_t equipId);

    constexpr std::uint32_t kNoItem         = 0x3F;
    constexpr std::size_t   kDescOwner      = 0x32;
    constexpr std::size_t   kDescEquipId    = 0x3C;   // low 11 bits
    constexpr std::uint16_t kEquipIdMask    = 0x7FF;

    constexpr std::size_t   kGetSupportWeaponTypeIdSlot = 0x48 / sizeof(void*);
    constexpr int           kTypeMagazine     = 0;
    constexpr int           kTypeFirstDecoy   = 7;
    constexpr int           kTypeLastDecoy    = 9;
    constexpr int           kTypeKibidango    = 0x11;
    constexpr std::uint16_t kAttackGrenadeHit   = 0xAE;
    constexpr std::uint16_t kAttackDecoyHit     = 0xAF;
    constexpr std::uint16_t kAttackKibidangoHit = 0xB1;

    constexpr float kFallbackFlightSeconds = 5.0f;   // the game's answer when no arc at the launch angle reaches the target

    ActivateThrowingAtEmptyWork_t g_Orig   = nullptr;
    void*                         g_Target = nullptr;

    void WritePhysicsSEH(std::uint8_t* impl, std::uint32_t localIndex, const SupportWeapon::Physics& physics)
    {
        __try
        {
            using namespace ThrowingSystemLayout;
            std::uint8_t* row = Read<std::uint8_t*>(impl, kImplPhysics) + localIndex * kPhysicsSize;
            if (physics.radius > 0.0f)
                *reinterpret_cast<float*>(row + kPhysicsRadius) = physics.radius;
            if (physics.bounce >= 0)
                row[kPhysicsBounce] = static_cast<std::uint8_t>(physics.bounce);
            if (physics.friction >= 0)
                row[kPhysicsFriction] = static_cast<std::uint8_t>(physics.friction);
            if (physics.rollingFriction >= 0)
                *reinterpret_cast<std::uint16_t*>(row + kPhysicsRollingFriction) = static_cast<std::uint16_t>(physics.rollingFriction);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void WriteNoticeHitSEH(std::uint8_t* impl, std::uint32_t localIndex, std::uint32_t noticeAs)
    {
        __try
        {
            using namespace ThrowingSystemLayout;
            void* equipIdTable = Read<void*>(Read<std::uint8_t*>(impl, kImplEquipIdTable), 8);
            const auto getType = (*static_cast<GetSupportWeaponTypeId_t* const*>(equipIdTable))[kGetSupportWeaponTypeIdSlot];
            const int type = getType(equipIdTable, noticeAs);
            std::uint8_t* offense = Read<std::uint8_t*>(impl, kImplOffenseData) + localIndex * kOffenseDataSize;
            std::uint16_t attackId = kAttackGrenadeHit;
            if (type >= kTypeFirstDecoy && type <= kTypeLastDecoy)
                attackId = kAttackDecoyHit;
            else if (type == kTypeKibidango)
                attackId = kAttackKibidangoHit;
            *reinterpret_cast<std::uint16_t*>(offense + kOffenseAttackId) = attackId;
            if (type == kTypeMagazine)
                offense[kOffenseFlags] |= kOffenseMagazineHit;
            else
                offense[kOffenseFlags] &= static_cast<std::uint8_t>(~kOffenseMagazineHit);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void WriteVelocitySEH(std::uint8_t* impl, std::uint32_t localIndex, const float* velocity)
    {
        __try
        {
            using namespace ThrowingSystemLayout;
            std::memcpy(Read<std::uint8_t*>(impl, kImplVelocities) + localIndex * 4 * sizeof(float), velocity, 4 * sizeof(float));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void ApplyItemGravity(std::uint8_t* impl, std::uint32_t localIndex, const std::uint8_t* desc, std::uint16_t engineEquipId)
    {
        const float gravity = SupportWeapon::Gravity(engineEquipId);
        alignas(16) float velocity[4];
        if (gravity != SupportWeapon::kGameGravity && ThrowingImpl_ItemGravityActive()
            && ThrowingImpl_AimedThrowVelocity(impl, desc, gravity, kFallbackFlightSeconds, velocity))
            WriteVelocitySEH(impl, localIndex, velocity);
    }

    void ApplyCustomItem(std::uint8_t* impl, std::uint32_t localIndex, std::uint16_t engineEquipId)
    {
        SupportWeapon::Physics physics;
        if (SupportWeapon::GetPhysics(engineEquipId, physics))
            WritePhysicsSEH(impl, localIndex, physics);
        const std::uint32_t noticeAs = SupportWeapon::NoticeAs(engineEquipId);
        if (noticeAs != 0 && noticeAs != SupportWeapon::kNoticeNothing)
            WriteNoticeHitSEH(impl, localIndex, noticeAs);
    }

    std::uint32_t __fastcall hkActivateThrowingAtEmptyWork(void* impl, const void* desc)
    {
        const std::uint32_t localIndex = g_Orig(impl, desc);
        if (localIndex == kNoItem)
            return localIndex;

        using namespace ThrowingSystemLayout;
        auto* bytes = static_cast<std::uint8_t*>(impl);
        const std::uint32_t handle = HandleOf(bytes, localIndex);
        ThrowingImpl_ForgetActivation(handle);
        ThrowingImpl_ForgetStop(handle);
        if (MissionCodeGuard::ShouldBypassHooks())
            return localIndex;

        const auto* descBytes = static_cast<const std::uint8_t*>(desc);
        const std::uint16_t engineEquipId = Read<std::uint16_t>(descBytes, kDescEquipId) & kEquipIdMask;
        ApplyCustomItem(bytes, localIndex, engineEquipId);
        ApplyItemGravity(bytes, localIndex, descBytes, engineEquipId);

        const std::uint16_t equipId = static_cast<std::uint16_t>(equip::FromEngine(engineEquipId));
        const std::uint16_t owner   = Read<std::uint16_t>(descBytes, kDescOwner);
        V_FrameWork::QueueMessage("Throwing", "OnThrow", handle, equipId, owner);
        return localIndex;
    }
}

bool Install_ThrowingImpl_ActivateThrowingAtEmptyWork_Hook()
{
    g_Target = ResolveGameAddress(gAddr.ThrowingImpl_ActivateThrowingAtEmptyWork);
    if (!g_Target)
    {
        LogError("ThrowingSystem: ActivateThrowingAtEmptyWork address missing for this build, OnThrow is off");
        return true;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkActivateThrowingAtEmptyWork),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ThrowingSystem: ActivateThrowingAtEmptyWork hook failed, OnThrow is off");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_ThrowingImpl_ActivateThrowingAtEmptyWork_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
