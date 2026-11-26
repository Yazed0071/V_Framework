#include <pch.h>
#include "EquipIdTableImpl_GetEquipIdFromStrCode32Impl.h"

#include <cstdint>

#include "AddressSet.h"
#include "EquipAliasLease.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "TppEquipConstRegistry.h"
#include "log.h"

namespace
{
    using GetEquipIdFromStrCode32_t = std::uint32_t(__fastcall*)(std::uint32_t nameHash);

    constexpr std::uint8_t kEntry[] = { 0x8B, 0xD1, 0x81, 0xF9, 0xAE, 0x2E, 0x55, 0x48 };   // mov edx,ecx; cmp ecx,<Claymore hash>

    GetEquipIdFromStrCode32_t g_Orig   = nullptr;
    void*                     g_Target = nullptr;

    std::uint32_t __fastcall hkGetEquipIdFromStrCode32(std::uint32_t nameHash)   // fox2 placed/pickable locators name their equipId this way
    {
        const std::uint32_t equipId = g_Orig(nameHash);
        if (equipId != 0 || MissionCodeGuard::ShouldBypassHooks())
            return equipId;
        std::int32_t custom = 0;
        if (!TppEquipConst_FindEquipIdByNameHash(nameHash, custom))
            return 0;
        const std::int32_t engineId = equip::ToEngine(custom);
        return engineId > 0 ? static_cast<std::uint32_t>(engineId) : 0;
    }
}

bool Install_EquipIdTableImpl_GetEquipIdFromStrCode32Impl_Hook()
{
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.EquipIdTableImpl_GetEquipIdFromStrCode32Impl);
    if (!target)
    {
        LogError("Fox2EquipNames: equip name lookup address missing on this build, fox2 files cannot name custom equipIds");
        return false;
    }
    const HookEntry state = CheckHookEntry(target, kEntry, sizeof(kEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("Fox2EquipNames: equip name lookup differs on this build (%s), fox2 files cannot name custom equipIds", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("Fox2EquipNames: GetEquipIdFromStrCode32Impl", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkGetEquipIdFromStrCode32), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("Fox2EquipNames: equip name lookup hook failed, fox2 files cannot name custom equipIds");
        return false;
    }
    g_Target = target;
    return true;
}

bool Uninstall_EquipIdTableImpl_GetEquipIdFromStrCode32Impl_Hook()
{
    if (!g_Target)
        return true;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
    return true;
}
