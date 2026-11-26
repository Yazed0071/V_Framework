#include <pch.h>

#include "BuddyServiceImpl_InitBuddyCommand.h"

#include <cstdint>
#include <cstdio>

#include "AddressSet.h"
#include "BattleGearBuddy.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using InitBuddyCommand_t = void(__fastcall*)(void* service, std::uint8_t buddyType);

    const std::uint8_t kInitBuddyCommandEntry[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x0F, 0xB6, 0xCA, 0x48, 0x8B, 0x03, 0xFF, 0xC9
    };
    const std::uint8_t kInitBuddyCommandForStatusEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x44, 0x0F, 0xB6, 0xC2, 0x48, 0x8B, 0xD9
    };

    InitBuddyCommand_t g_OrigCommand = nullptr;
    InitBuddyCommand_t g_OrigForStatus = nullptr;
    void*              g_CommandTarget = nullptr;
    void*              g_ForStatusTarget = nullptr;

    std::uint8_t HostedType(std::uint8_t buddyType)
    {
        return battleGear::IsHostedType(buddyType) ? battleGear::kHostBuddyType : buddyType;
    }

    void __fastcall hkInitBuddyCommand(void* service, std::uint8_t buddyType)
    {
        g_OrigCommand(service, HostedType(buddyType));
    }

    void __fastcall hkInitBuddyCommandForStatus(void* service, std::uint8_t buddyType)
    {
        g_OrigForStatus(service, HostedType(buddyType));
    }

    bool HookOne(std::uintptr_t address, const std::uint8_t* entry, std::size_t entrySize, const char* name,
                 void* detour, void** original, void*& installed)
    {
        void* target = ResolveGameAddress(address);
        if (!target)
        {
            LogError("BattleGear: %s address missing on this build, Battle Gear is off", name);
            return false;
        }
        const HookEntry state = CheckHookEntry(target, entry, entrySize);
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("BattleGear: %s code differs on this build (%s), Battle Gear is off", name, desc);
            return false;
        }
        if (state == HookEntry::ForeignHook)
        {
            char label[96] = {};
            std::snprintf(label, sizeof(label), "BattleGear: %s", name);
            LogHookedOnTop(label, target);
        }
        if (!CreateAndEnableHook(target, detour, original))
        {
            LogError("BattleGear: %s hook failed, Battle Gear is off", name);
            return false;
        }
        installed = target;
        return true;
    }
}

bool Install_BuddyServiceImpl_InitBuddyCommand_Hooks()
{
    if (g_CommandTarget && g_ForStatusTarget)
        return true;
    const bool ok =
        HookOne(gAddr.BuddyServiceImpl_InitBuddyCommand, kInitBuddyCommandEntry, sizeof(kInitBuddyCommandEntry),
                "InitBuddyCommand", reinterpret_cast<void*>(&hkInitBuddyCommand),
                reinterpret_cast<void**>(&g_OrigCommand), g_CommandTarget)
        && HookOne(gAddr.BuddyServiceImpl_InitBuddyCommandForStatus, kInitBuddyCommandForStatusEntry,
                   sizeof(kInitBuddyCommandForStatusEntry), "InitBuddyCommandForStatus",
                   reinterpret_cast<void*>(&hkInitBuddyCommandForStatus),
                   reinterpret_cast<void**>(&g_OrigForStatus), g_ForStatusTarget);
    if (!ok)
        Uninstall_BuddyServiceImpl_InitBuddyCommand_Hooks();
    return ok;
}

void Uninstall_BuddyServiceImpl_InitBuddyCommand_Hooks()
{
    if (g_ForStatusTarget)
    {
        DisableAndRemoveHook(g_ForStatusTarget);
        g_ForStatusTarget = nullptr;
        g_OrigForStatus = nullptr;
    }
    if (g_CommandTarget)
    {
        DisableAndRemoveHook(g_CommandTarget);
        g_CommandTarget = nullptr;
        g_OrigCommand = nullptr;
    }
}
