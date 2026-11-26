#include <pch.h>
#include "EffectControllerImpl_CreateEffect.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using CreateEffect_t = std::uint32_t(__fastcall*)(void* controller, const void* request);

    constexpr std::size_t   kControllerVfxCount = 0x08; // .vfx slots in the FilePtr table at +0x18
    constexpr std::size_t   kRequestVfxSlot     = 0x40;
    constexpr std::uint32_t kNoEffect           = 0xFFFFFFFF;

    const std::uint8_t kCreateEffectPrologue[] = {
        0x40, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81, 0xEC,
        0x00, 0x01, 0x00, 0x00, 0x48, 0xC7, 0x45, 0x87, 0xFE, 0xFF, 0xFF, 0xFF
    };

    struct Capture
    {
        float* matrix;
        bool   taken;
    };

    CreateEffect_t       g_OrigCreateEffect = nullptr;
    void*                g_HookTarget       = nullptr;
    std::atomic<bool>    g_WarnedSlot{ false };
    thread_local Capture t_Capture{};

    // A vehicle gun asks its vehicle's effect table for the muzzle flash by a slot fixed per gun kind (WeaponBulletImpl
    // 0xC / 0xE); on a vehicle built without that gun the slot can lie past the table, which vanilla reads unchecked
    bool SlotOutsideTableSEH(const void* controller, const void* request, std::uint32_t& slot, std::uint32_t& count)
    {
        __try
        {
            count = *reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(controller) + kControllerVfxCount);
            slot = *reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(request) + kRequestVfxSlot);
            return slot >= count;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t __fastcall hk_CreateEffect(void* controller, const void* request)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigCreateEffect, controller, request);
        std::uint32_t slot = 0;
        std::uint32_t count = 0;
        if (!controller || !request || !SlotOutsideTableSEH(controller, request, slot, count))
            return g_OrigCreateEffect(controller, request);
        if (t_Capture.matrix)
        {
            std::memcpy(t_Capture.matrix, request, sizeof(float) * 16); // the request starts with the effect's world matrix
            t_Capture.taken = true;
            return kNoEffect;
        }
        if (!g_WarnedSlot.exchange(true, std::memory_order_relaxed))
            LogWarn("EffectGuard: an effect asked for slot %u of a vehicle effect table with %u slots and was skipped (a vehicle gun on a vehicle that has no effect for it)", slot, count);
        return kNoEffect;
    }
}

void EffectGuard_BeginCapture(float* matrix)
{
    t_Capture = { matrix, false };
}

bool EffectGuard_EndCapture()
{
    const bool taken = t_Capture.taken;
    t_Capture = {};
    return taken;
}

bool Install_EffectControllerImpl_CreateEffect_Hook()
{
    if (g_HookTarget)
        return true;
    auto* target = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.EffectControllerImpl_CreateEffect));
    if (!target)
        return true;
    const HookEntry entry = CheckHookEntry(target, kCreateEffectPrologue, sizeof(kCreateEffectPrologue));
    if (entry == HookEntry::Unknown)
    {
        LogError("EffectGuard: vehicle effect code does not match %s, an armed jeep's gun can crash the game", GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("EffectGuard: EffectControllerImpl::CreateEffect", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_CreateEffect), reinterpret_cast<void**>(&g_OrigCreateEffect)))
    {
        LogError("EffectGuard: EffectControllerImpl::CreateEffect hook failed, an armed jeep's gun can crash the game");
        return false;
    }
    g_HookTarget = target;
    return true;
}

bool Uninstall_EffectControllerImpl_CreateEffect_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_OrigCreateEffect = nullptr;
    }
    return true;
}
