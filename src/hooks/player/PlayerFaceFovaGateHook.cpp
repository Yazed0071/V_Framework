#include "pch.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "CustomSuitRegistry.h"
#include "PlayerFaceFovaGateHook.h"

namespace
{
    using DoesNeedFaceFova_t = bool(__fastcall*)(std::uint32_t playerPartsType);
    using DoesNeedFaceFovaForAvatar_t = bool(__fastcall*)(std::uint32_t playerPartsType);

    static DoesNeedFaceFova_t g_OrigDoesNeedFaceFova = nullptr;
    static DoesNeedFaceFovaForAvatar_t g_OrigDoesNeedFaceFovaForAvatar = nullptr;

    static bool g_InstalledFace = false;
    static bool g_InstalledAvatar = false;

    static const CustomSuitEntry* GetCustomEntry(std::uint32_t playerPartsType)
    {
        const CustomSuitEntry* entry = nullptr;
        if (!TryGetCustomSuitByPartsType(static_cast<std::uint8_t>(playerPartsType), &entry) || !entry)
            return nullptr;

        return entry;
    }
}

static bool __fastcall hkDoesNeedFaceFova(std::uint32_t playerPartsType)
{
    const CustomSuitEntry* entry = GetCustomEntry(playerPartsType);
    if (entry && entry->IsFaceEnabled())
    {
        return true;
    }

    return g_OrigDoesNeedFaceFova(playerPartsType);
}

static bool __fastcall hkDoesNeedFaceFovaForAvatar(std::uint32_t playerPartsType)
{
    const CustomSuitEntry* entry = GetCustomEntry(playerPartsType);

    // IMPORTANT:
    // Only force the avatar-specific gate for actual avatar custom suits.
    // Never force it for DD male/female or Snake custom suits.
    if (entry && entry->IsFaceEnabled() && entry->playerType == 3)
    {
        return true;
    }

    return g_OrigDoesNeedFaceFovaForAvatar(playerPartsType);
}

bool Install_PlayerFaceFovaGate_Hook()
{
    bool okFace = g_InstalledFace;
    bool okAvatar = g_InstalledAvatar;

    if (!g_InstalledFace)
    {
        void* targetFace = ResolveGameAddress(gAddr.ResourceTable_DoesNeedFaceFova);
        if (!targetFace)
        {
            Log("[Hook] PlayerFaceFovaGate: failed to resolve DoesNeedFaceFova\n");
            return false;
        }

        okFace = CreateAndEnableHook(
            targetFace,
            reinterpret_cast<void*>(&hkDoesNeedFaceFova),
            reinterpret_cast<void**>(&g_OrigDoesNeedFaceFova)
        );

        Log("[Hook] PlayerFaceFovaGate DoesNeedFaceFova: %s\n", okFace ? "OK" : "FAIL");
        g_InstalledFace = okFace;
    }

    if (!g_InstalledAvatar)
    {
        void* targetAvatar = ResolveGameAddress(gAddr.ResourceTable_DoesNeedFaceFovaForAvatar);
        if (!targetAvatar)
        {
            Log("[Hook] PlayerFaceFovaGate: failed to resolve DoesNeedFaceFovaForAvatar\n");
            return false;
        }

        okAvatar = CreateAndEnableHook(
            targetAvatar,
            reinterpret_cast<void*>(&hkDoesNeedFaceFovaForAvatar),
            reinterpret_cast<void**>(&g_OrigDoesNeedFaceFovaForAvatar)
        );

        Log("[Hook] PlayerFaceFovaGate DoesNeedFaceFovaForAvatar: %s\n", okAvatar ? "OK" : "FAIL");
        g_InstalledAvatar = okAvatar;
    }

    return g_InstalledFace && g_InstalledAvatar;
}

bool Uninstall_PlayerFaceFovaGate_Hook()
{
    if (g_InstalledFace)
    {
        if (void* target = ResolveGameAddress(gAddr.ResourceTable_DoesNeedFaceFova))
            DisableAndRemoveHook(target);

        g_OrigDoesNeedFaceFova = nullptr;
        g_InstalledFace = false;
    }

    if (g_InstalledAvatar)
    {
        if (void* target = ResolveGameAddress(gAddr.ResourceTable_DoesNeedFaceFovaForAvatar))
            DisableAndRemoveHook(target);

        g_OrigDoesNeedFaceFovaForAvatar = nullptr;
        g_InstalledAvatar = false;
    }

    Log("[Hook] PlayerFaceFovaGate: removed\n");
    return true;
}