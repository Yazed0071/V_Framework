#include <pch.h>
#include "AttackActionImpl_CalcThrowingVelocity.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SupportWeaponRegistry.h"
#include "log.h"

namespace
{
    using CalcThrowingVelocity_t = void(__fastcall*)(void* attackAction, const void* work, std::uint32_t playerIndex,
                                                     float* velocity);

    constexpr std::size_t   kWorkThrownEquipId = 0x24C;   // the support weapon in the hand, low 11 bits
    constexpr std::uint16_t kEquipIdMask       = 0x7FF;

    const std::uint8_t kEntry[] = {
        0x48, 0x8B, 0xC4, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48, 0x8D, 0x68, 0x88, 0x48, 0x81, 0xEC,
        0x50, 0x01, 0x00, 0x00, 0x0F, 0x29, 0x70, 0xC8, 0x0F, 0x29, 0x78, 0xB8, 0x44, 0x0F, 0x29, 0x40,
    };

    CalcThrowingVelocity_t g_Orig   = nullptr;
    void*                  g_Target = nullptr;

    std::uint16_t ReadThrownEquipIdSEH(const void* work)
    {
        __try
        {
            return *reinterpret_cast<const std::uint16_t*>(static_cast<const std::uint8_t*>(work) + kWorkThrownEquipId) & kEquipIdMask;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void __fastcall hkCalcThrowingVelocity(void* attackAction, const void* work, std::uint32_t playerIndex, float* velocity)
    {
        g_Orig(attackAction, work, playerIndex, velocity);
        if (!work || !velocity || MissionCodeGuard::ShouldBypassHooks())
            return;
        SupportWeapon::Physics physics;
        if (!SupportWeapon::GetPhysics(ReadThrownEquipIdSEH(work), physics) || physics.throwSpeed == 1.0f)
            return;
        velocity[0] *= physics.throwSpeed;
        velocity[1] *= physics.throwSpeed;
        velocity[2] *= physics.throwSpeed;
    }
}

bool Install_AttackActionImpl_CalcThrowingVelocity_Hook()
{
    g_Target = ResolveGameAddress(gAddr.AttackActionImpl_CalcThrowingVelocity);
    if (!g_Target)
    {
        LogError("SupportWeapon: CalcThrowingVelocity address missing for this build, throwSpeed is off");
        return true;
    }
    const HookEntry entry = CheckHookEntry(g_Target, kEntry, sizeof(kEntry));
    if (entry == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(g_Target, desc, sizeof(desc));
        LogError("SupportWeapon: AttackActionImpl::CalcThrowingVelocity differs on this build (%s), throwSpeed is off", desc);
        g_Target = nullptr;
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("SupportWeapon: AttackActionImpl::CalcThrowingVelocity", g_Target);
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkCalcThrowingVelocity), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("SupportWeapon: CalcThrowingVelocity hook failed, throwSpeed is off");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_AttackActionImpl_CalcThrowingVelocity_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
