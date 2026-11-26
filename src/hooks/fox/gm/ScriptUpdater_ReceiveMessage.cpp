#include <pch.h>
#include "ScriptUpdater_ReceiveMessage.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MessageArgs_GetValueAsDouble.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using ReceiveMessage_t = void(__fastcall*)(void* self, void* messageBox, const std::uint8_t* message);

    constexpr std::size_t kMessageArgTypes = 0x20;   // 3 bits per argument: 0 none, 1 int64, 2 uint64, 3 double
    constexpr std::size_t kMessageBytes    = 0x30;   // all the ScriptUpdater reads
    constexpr unsigned    kArgCount        = 4;
    constexpr unsigned    kArgInt64        = 1;
    constexpr unsigned    kArgUInt64       = 2;
    constexpr unsigned    kArgDouble       = 3;

    const std::uint8_t kPrologue[] = { 0x48, 0x8B, 0xC4, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x50 };

    ReceiveMessage_t g_Orig = nullptr;
    void*            g_Target = nullptr;

    bool PutRealEquipId(std::uint8_t* message, unsigned index)
    {
        const std::uint32_t types = *reinterpret_cast<const std::uint32_t*>(message + kMessageArgTypes);
        const unsigned type = (types >> (index * 3)) & 7;
        std::uint8_t* value = message + index * sizeof(std::uint64_t);

        std::int64_t engineId = -1;
        if (type == kArgInt64 || type == kArgUInt64)
            engineId = *reinterpret_cast<const std::int64_t*>(value);
        else if (type == kArgDouble)
            engineId = static_cast<std::int64_t>(*reinterpret_cast<const double*>(value));
        else
            return false;

        std::int32_t realId = 0;
        if (!WideEquipMessages_RealEquipId(message, index, engineId, realId))
            return false;

        if (type == kArgDouble)
            *reinterpret_cast<double*>(value) = static_cast<double>(realId);
        else
            *reinterpret_cast<std::int64_t*>(value) = realId;
        return true;
    }

    void __fastcall hkReceiveMessage(void* self, void* messageBox, const std::uint8_t* message)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_Orig, self, messageBox, message);

        alignas(16) std::uint8_t copy[kMessageBytes] = {};
        if (!message || !ReadEntryBytesSEH(message, copy, sizeof(copy)))
        {
            g_Orig(self, messageBox, message);
            return;
        }

        bool changed = false;
        for (unsigned index = 0; index < kArgCount; ++index)
            changed = PutRealEquipId(copy, index) || changed;
        g_Orig(self, messageBox, changed ? copy : message);   // the ScriptUpdater only reads the message, it never hands it back to the game
    }
}

bool Install_ScriptUpdater_ReceiveMessage_Hook()
{
    if (!gAddr.ScriptUpdater_ReceiveMessage)
    {
        LogError("WideEquipMessages: ScriptUpdater address missing for this build, its Lua handlers see stand-in equipIds for ids from 2048");
        return false;
    }
    if (!WideEquipMessages_Prepare())
    {
        LogError("WideEquipMessages: message names could not be hashed, Lua messages keep stand-in equipIds for ids from 2048");
        return false;
    }

    auto* fn = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.ScriptUpdater_ReceiveMessage));
    const HookEntry entry = CheckHookEntry(fn, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(fn, found, sizeof(found));
        LogError("WideEquipMessages: ScriptUpdater at 0x%llX is not the expected code (%s), its Lua handlers see stand-in equipIds for ids from 2048",
                 static_cast<unsigned long long>(gAddr.ScriptUpdater_ReceiveMessage), found);
        return false;
    }

    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkReceiveMessage), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("WideEquipMessages: ScriptUpdater hook failed, its Lua handlers see stand-in equipIds for ids from 2048");
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("WideEquipMessages: ScriptUpdater", fn);
    g_Target = fn;
    return true;
}

void Uninstall_ScriptUpdater_ReceiveMessage_Hook()
{
    if (g_Target)
    {
        DisableAndRemoveHook(g_Target);
        g_Target = nullptr;
    }
    g_Orig = nullptr;
}
