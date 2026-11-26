#include <pch.h>
#include "MessageArgs_GetValueAsDouble.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "EquipAliasLease.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using GetValueAsDouble_t = double(__fastcall*)(const std::uint8_t* message, unsigned index);

    constexpr std::size_t  kMessageId     = 0x28;
    constexpr std::size_t  kMessageSender = 0x2C;
    constexpr std::int64_t kLastEngineId  = 0x7FF;

    const std::uint8_t kPrologue[] = { 0x4C, 0x8B, 0xC9, 0x83, 0xFA, 0x04 };

    struct EquipIdMessage
    {
        const char*   sender;
        const char*   message;
        unsigned      arg;
        std::uint32_t senderHash;
        std::uint32_t messageHash;
    };

    EquipIdMessage g_EquipIdMessages[] = {
        { "Placed",   "OnActivatePlaced",            0, 0, 0 },
        { "Placed",   "OnBreakPlaced",               1, 0, 0 },
        { "Player",   "OnAmmoLessInMagazine",        2, 0, 0 },
        { "Player",   "OnAmmoStackEmpty",            2, 0, 0 },
        { "Player",   "OnEquipHudClosed",            1, 0, 0 },
        { "Player",   "OnEquipItem",                 1, 0, 0 },
        { "Player",   "OnEquipWeapon",               1, 0, 0 },
        { "Player",   "OnPickUpPlaced",              1, 0, 0 },
        { "Player",   "OnPickUpWeapon",              1, 0, 0 },
        { "Player",   "PlayerHoldWeapon",            0, 0, 0 },
        { "Player",   "WeaponPutPlaced",             1, 0, 0 },
        { "Terminal", "MbDvcActSelectItemDropPoint", 0, 0, 0 },
    };

    bool               g_Prepared = false;
    GetValueAsDouble_t g_Orig = nullptr;
    void*              g_Target = nullptr;

    double __fastcall hkGetValueAsDouble(const std::uint8_t* message, unsigned index)
    {
        const double value = g_Orig(message, index);
        if (MissionCodeGuard::ShouldBypassHooks())
            return value;

        const std::int64_t engineId = static_cast<std::int64_t>(value);
        std::int32_t realId = 0;
        if (static_cast<double>(engineId) == value && WideEquipMessages_RealEquipId(message, index, engineId, realId))
            return realId;   // only the two Lua message bridges call this, so the message itself keeps the stand-in
        return value;
    }
}

bool WideEquipMessages_Prepare()
{
    if (g_Prepared)
        return true;
    for (EquipIdMessage& known : g_EquipIdMessages)
    {
        known.senderHash  = FoxHashes::StrCode32(known.sender);
        known.messageHash = FoxHashes::StrCode32(known.message);
        if (!known.senderHash || !known.messageHash)
            return false;
    }
    g_Prepared = true;
    return true;
}

bool WideEquipMessages_RealEquipId(const std::uint8_t* message, unsigned index, std::int64_t engineId, std::int32_t& realId)
{
    if (!g_Prepared || !message || engineId < 0 || engineId > kLastEngineId)
        return false;

    std::uint32_t idAndSender[2] = {};
    if (!ReadEntryBytesSEH(message + kMessageId, idAndSender, sizeof(idAndSender)))
        return false;
    static_assert(kMessageSender == kMessageId + sizeof(std::uint32_t), "sender follows the message id");

    for (const EquipIdMessage& known : g_EquipIdMessages)
    {
        if (known.messageHash != idAndSender[0] || known.senderHash != idAndSender[1] || known.arg != index)
            continue;
        realId = equip::FromEngine(static_cast<std::int32_t>(engineId));
        return realId != engineId;
    }
    return false;
}

bool Install_MessageArgs_GetValueAsDouble_Hook()
{
    if (!gAddr.MessageArgs_GetValueAsDouble)
    {
        LogError("WideEquipMessages: GetValueAsDouble address missing for this build, mission scripts see stand-in equipIds for ids from 2048");
        return false;
    }
    if (!WideEquipMessages_Prepare())
    {
        LogError("WideEquipMessages: message names could not be hashed, Lua messages keep stand-in equipIds for ids from 2048");
        return false;
    }

    auto* fn = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.MessageArgs_GetValueAsDouble));
    const HookEntry entry = CheckHookEntry(fn, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(fn, found, sizeof(found));
        LogError("WideEquipMessages: GetValueAsDouble at 0x%llX is not the expected code (%s), mission scripts see stand-in equipIds for ids from 2048",
                 static_cast<unsigned long long>(gAddr.MessageArgs_GetValueAsDouble), found);
        return false;
    }

    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkGetValueAsDouble), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("WideEquipMessages: GetValueAsDouble hook failed, mission scripts see stand-in equipIds for ids from 2048");
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("WideEquipMessages: GetValueAsDouble", fn);
    g_Target = fn;
    return true;
}

void Uninstall_MessageArgs_GetValueAsDouble_Hook()
{
    if (g_Target)
    {
        DisableAndRemoveHook(g_Target);
        g_Target = nullptr;
    }
    g_Orig = nullptr;
}
