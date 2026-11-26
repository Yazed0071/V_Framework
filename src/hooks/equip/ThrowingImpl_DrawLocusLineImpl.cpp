#include <pch.h>
#include "ThrowingImpl_DrawLocusLineImpl.h"

#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SupportWeaponRegistry.h"
#include "ThrowingImpl_CalcInitialVelocityByAngle.h"
#include "ThrowingImpl_UpdateNextPosition.h"
#include "log.h"

namespace
{
    using DrawLocusLineImpl_t = void(__fastcall*)(void* impl, const std::uint8_t* desc, int drawMode);

    constexpr std::size_t   kDescSize               = 0x40;
    constexpr std::size_t   kDescPositionW          = 0x0C;
    constexpr std::size_t   kDescTarget             = 0x20;
    constexpr std::size_t   kDescEquipId            = 0x3C;   // low 11 bits, bits 11-12 the launch mode
    constexpr std::size_t   kDescOptions            = 0x3E;
    constexpr std::uint8_t  kGravityInPositionW     = 0x10;   // the game's own switch: the arc falls by position.w instead of 9.8
    constexpr std::uint16_t kEquipIdMask            = 0x7FF;
    constexpr std::uint16_t kLaunchModeBits         = 0x1800;
    constexpr float         kPreviewFallbackSeconds = 1.0f;   // the preview's answer when no arc at the launch angle reaches the target

    const std::uint8_t kEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x60,
    };

    DrawLocusLineImpl_t g_Orig   = nullptr;
    void*               g_Target = nullptr;

    void __fastcall hkDrawLocusLineImpl(void* impl, const std::uint8_t* desc, int drawMode)
    {
        if (!desc || !ThrowingImpl_ItemGravityActive() || MissionCodeGuard::ShouldBypassHooks()
            || (desc[kDescOptions] & kGravityInPositionW) != 0)
            return g_Orig(impl, desc, drawMode);
        const std::uint16_t equipIdAndMode = *reinterpret_cast<const std::uint16_t*>(desc + kDescEquipId);
        const float gravity = SupportWeapon::Gravity(equipIdAndMode & kEquipIdMask);
        if (gravity == SupportWeapon::kGameGravity)
            return g_Orig(impl, desc, drawMode);

        alignas(16) std::uint8_t ownGravity[kDescSize];
        std::memcpy(ownGravity, desc, kDescSize);
        alignas(16) float velocity[4];
        if (ThrowingImpl_AimedThrowVelocity(impl, desc, gravity, kPreviewFallbackSeconds, velocity))
        {
            const std::uint16_t givenVelocity = equipIdAndMode & static_cast<std::uint16_t>(~kLaunchModeBits);
            std::memcpy(ownGravity + kDescTarget, velocity, sizeof(velocity));
            std::memcpy(ownGravity + kDescEquipId, &givenVelocity, sizeof(givenVelocity));
        }
        std::memcpy(ownGravity + kDescPositionW, &gravity, sizeof(gravity));
        ownGravity[kDescOptions] |= kGravityInPositionW;
        g_Orig(impl, ownGravity, drawMode);
    }
}

bool Install_ThrowingImpl_DrawLocusLineImpl_Hook()
{
    g_Target = ResolveGameAddress(gAddr.ThrowingImpl_DrawLocusLineImpl);
    if (!g_Target)
    {
        LogError("SupportWeapon: DrawLocusLineImpl address missing for this build, the throw arc ignores gravity");
        return true;
    }
    const HookEntry entry = CheckHookEntry(g_Target, kEntry, sizeof(kEntry));
    if (entry == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(g_Target, desc, sizeof(desc));
        LogError("SupportWeapon: ThrowingImpl::DrawLocusLineImpl differs on this build (%s), the throw arc ignores gravity", desc);
        g_Target = nullptr;
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("SupportWeapon: ThrowingImpl::DrawLocusLineImpl", g_Target);
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkDrawLocusLineImpl), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("SupportWeapon: DrawLocusLineImpl hook failed, the throw arc ignores gravity");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_ThrowingImpl_DrawLocusLineImpl_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
