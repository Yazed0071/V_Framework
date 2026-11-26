#pragma once

#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

inline void* InstallSupportHeliHook(const char* what, std::uintptr_t address,
                                    const std::uint8_t* entry, const std::uint8_t* mask, std::size_t size,
                                    void* detour, void** original, const char* consequence)
{
    void* target = address ? ResolveGameAddress(address) : nullptr;
    if (!target)
    {
        LogError("SupportHeliWeapon: %s address missing on this build, %s", what, consequence);
        return nullptr;
    }
    const HookEntry state = mask ? CheckHookEntryMasked(target, entry, mask, size)
                                 : CheckHookEntry(target, entry, size);
    if (state == HookEntry::Unknown)
    {
        char description[160] = {};
        DescribeFunctionEntry(target, description, sizeof(description));
        LogError("SupportHeliWeapon: %s code differs on this build (%s), %s", what, description, consequence);
        return nullptr;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop(what, target);
    if (!CreateAndEnableHook(target, detour, original))
    {
        LogError("SupportHeliWeapon: %s hook failed, %s", what, consequence);
        return nullptr;
    }
    return target;
}

inline void UninstallSupportHeliHook(void*& target)
{
    if (!target)
        return;
    DisableAndRemoveHook(target);
    target = nullptr;
}
