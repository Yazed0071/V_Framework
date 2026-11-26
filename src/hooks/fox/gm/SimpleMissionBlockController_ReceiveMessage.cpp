#include <pch.h>
#include "SimpleMissionBlockController_ReceiveMessage.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>

#include "AddressSet.h"
#include "FeatureSwitch.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "log.h"
#include "../../ui/MbDvcSoundControllerImpl_Update.h"

namespace
{
    using ReceiveMessage_t      = void(__fastcall*)(void* self, void* messageBox, const std::uint8_t* message);
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();

    constexpr std::size_t  kMessageArgTypes     = 0x20;   // 3 bits per argument: 0 none, 1 int64, 2 uint64, 3 double
    constexpr std::size_t  kMessageId           = 0x28;
    constexpr std::size_t  kMessageSender       = 0x2C;
    constexpr std::size_t  kMessageBytes        = 0x30;
    constexpr unsigned     kArgInt64            = 1;
    constexpr unsigned     kArgUInt64           = 2;
    constexpr unsigned     kArgDouble           = 3;
    constexpr int          kAnyArgs             = -1;
    constexpr std::int64_t kBombardment         = 1;      // supportStrikeId: 1 bombardment, 2 smoke, 3 sleeping gas, 4 chaff, 5 weather modification
    constexpr std::int64_t kWeatherModification = 5;
    constexpr std::int64_t kNoStrike            = -1;
    constexpr std::uint32_t kAfterGameVoiceMs   = 1500;   // the game's own strike voices go first

    constexpr std::size_t   kQuark_Systems   = 0x98;
    constexpr std::size_t   kSystems_Strikes = 0x250;   // SupportAttackSystemImpl's request interface, null where strikes do not exist
    constexpr std::size_t   kStrikes_Kind    = 0x80;    // supportStrikeId of the last request
    constexpr std::size_t   kStrikes_State   = 0x240;
    constexpr std::uint32_t kStrikeOnItsWay  = 1;       // from the request until its last shell or the weather change

    constexpr std::uint32_t kLinePleaseWait        = 1200944481;   // "Please wait."
    constexpr std::uint32_t kLineWeatherInProgress = 1184166791;   // "Weather modification in progress."

    const std::uint8_t kPrologue[] = { 0x4C, 0x8B, 0xDC, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x50, 0x49, 0xC7, 0x43, 0xB8, 0xFE,
                                       0xFF, 0xFF, 0xFF, 0x49, 0x89, 0x5B, 0x10, 0x49, 0x89, 0x6B, 0x18, 0x49, 0x8B, 0xE8, 0x4C, 0x8B,
                                       0xF2, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x41, 0x58 };   // ends on mov rax, [rcx+0x58]: the mission script it calls OnMessage on

    struct LineTrigger
    {
        const char*   sender;
        const char*   message;
        int           arg;
        std::int64_t  value;
        std::uint32_t delayMs;
        std::uint32_t condition;
        std::uint32_t senderHash;
        std::uint32_t messageHash;
    };

    LineTrigger g_Triggers[] = {
        { "Terminal",      "MbDvcActOpenStaffList",   kAnyArgs, 0, 0, 1167389211, 0, 0 },   // "Please select staff."
        { "Terminal",      "MbDvcActRunDevelopEquip", kAnyArgs, 0, 0, 2996443848, 0, 0 },   // "Commencing development."
        { "SupportAttack", "OnRequested", 1, kWeatherModification, kAfterGameVoiceMs, kLineWeatherInProgress, 0, 0 },
        { "SupportAttack", "OnRequested", 1, kBombardment,         kAfterGameVoiceMs, 1116909294, 0, 0 },   // "Danger, maintain safe distance from strike point."
    };

    const char* const kStrikeEntries[] = { "MSN_ATTACK_ARTILLERY", "MSN_ATTACK_SMOKE", "MSN_ATTACK_SLEEP", "MSN_ATTACK_CHAFF",
                                           "MSN_ATTACK_WEATHER", "MSN_ATTACK_WEATHER_SANDSTORM", "MSN_ATTACK_WEATHER_STORM",
                                           "MSN_ATTACK_WEATHER_CLEAR" };   // TppTerminal.MBDVCMENU

    std::uint32_t         g_StrikeEntryHashes[std::size(kStrikeEntries)] = {};
    std::uint32_t         g_TerminalHash        = 0;
    std::uint32_t         g_LockedEntryHash     = 0;
    GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;
    ReceiveMessage_t      g_Orig                = nullptr;
    void*                 g_Target              = nullptr;

    bool ArgValue(const std::uint8_t* message, int index, std::int64_t& value)
    {
        const std::uint32_t types = *reinterpret_cast<const std::uint32_t*>(message + kMessageArgTypes);
        const unsigned type = (types >> (index * 3)) & 7;
        const std::uint8_t* at = message + index * sizeof(std::uint64_t);
        if (type == kArgInt64 || type == kArgUInt64)
            value = *reinterpret_cast<const std::int64_t*>(at);
        else if (type == kArgDouble)
            value = static_cast<std::int64_t>(*reinterpret_cast<const double*>(at));
        else
            return false;
        return true;
    }

    std::int64_t StrikeOnItsWay()
    {
        const std::uint8_t* table   = g_GetQuarkSystemTable ? g_GetQuarkSystemTable() : nullptr;
        const std::uint8_t* systems = nullptr;
        const std::uint8_t* strikes = nullptr;
        std::uint32_t       state   = 0;
        std::int32_t        kind    = 0;
        if (!table || !ReadEntryBytesSEH(table + kQuark_Systems, &systems, sizeof(systems)) || !systems
            || !ReadEntryBytesSEH(systems + kSystems_Strikes, &strikes, sizeof(strikes)) || !strikes
            || !ReadEntryBytesSEH(strikes + kStrikes_State, &state, sizeof(state)) || !(state & kStrikeOnItsWay)
            || !ReadEntryBytesSEH(strikes + kStrikes_Kind, &kind, sizeof(kind)))
            return kNoStrike;
        return kind;
    }

    // the game locks every strike entry while a strike is on its way: the one lock that ends by itself
    void RequestLockedStrikeEntryLine(const std::uint8_t* message)
    {
        std::int64_t entry = 0;
        if (!ArgValue(message, 0, entry))
            return;
        const auto entryHash = static_cast<std::uint32_t>(entry);
        if (std::find(std::begin(g_StrikeEntryHashes), std::end(g_StrikeEntryHashes), entryHash) == std::end(g_StrikeEntryHashes))
            return;
        const std::int64_t strike = StrikeOnItsWay();
        if (strike == kNoStrike)
            return;
        const std::uint32_t line = strike == kWeatherModification ? kLineWeatherInProgress : kLinePleaseWait;
        LogDebug("IdroidLines: locked strike entry 0x%08X while strike %lld is on its way -> line %u", entryHash, strike, line);
        IdroidVoice_Request(line);
    }

    void RequestTriggeredLines(const std::uint8_t* message)
    {
        alignas(16) std::uint8_t copy[kMessageBytes] = {};
        if (!message || !ReadEntryBytesSEH(message, copy, sizeof(copy)))
            return;
        const std::uint32_t id     = *reinterpret_cast<const std::uint32_t*>(copy + kMessageId);
        const std::uint32_t sender = *reinterpret_cast<const std::uint32_t*>(copy + kMessageSender);
        if (id == g_LockedEntryHash && sender == g_TerminalHash)
        {
            RequestLockedStrikeEntryLine(copy);
            return;
        }
        for (const LineTrigger& trigger : g_Triggers)
        {
            if (trigger.messageHash != id || trigger.senderHash != sender)
                continue;
            std::int64_t value = 0;
            if (trigger.arg != kAnyArgs && (!ArgValue(copy, trigger.arg, value) || value != trigger.value))
                continue;
            LogDebug("IdroidLines: %s %s -> line %u", trigger.sender, trigger.message, trigger.condition);
            IdroidVoice_Request(trigger.condition, 0, 0, trigger.delayMs);
        }
    }

    // mission_main.OnMessage: every game message the mission scripts get (GameMessage only carries UI, Radio, Video, Network, Nt)
    void __fastcall hkReceiveMessage(void* self, void* messageBox, const std::uint8_t* message)
    {
        if (FeatureSwitch::IsOn(FeatureSwitch::Feature::RestoredIdroidLines))
            RequestTriggeredLines(message);
        g_Orig(self, messageBox, message);
    }
}

bool Install_SimpleMissionBlockController_ReceiveMessage_Hook()
{
    if (g_Target)
        return true;
    if (!gAddr.SimpleMissionBlockController_ReceiveMessage)
    {
        LogError("IdroidLines: no mission message address for this build, iDroid menu actions stay silent");
        return false;
    }
    for (LineTrigger& trigger : g_Triggers)
    {
        trigger.senderHash  = FoxHashes::StrCode32(trigger.sender);
        trigger.messageHash = FoxHashes::StrCode32(trigger.message);
        if (!trigger.senderHash || !trigger.messageHash)
        {
            LogError("IdroidLines: message names could not be hashed, iDroid menu actions stay silent");
            return false;
        }
    }
    g_TerminalHash    = FoxHashes::StrCode32("Terminal");
    g_LockedEntryHash = FoxHashes::StrCode32("MbDvcActSelectNonActiveMenu");
    for (std::size_t i = 0; i < std::size(kStrikeEntries); ++i)
        g_StrikeEntryHashes[i] = FoxHashes::StrCode32(kStrikeEntries[i]);
    if (!g_TerminalHash || !g_LockedEntryHash
        || std::find(std::begin(g_StrikeEntryHashes), std::end(g_StrikeEntryHashes), 0u) != std::end(g_StrikeEntryHashes))
    {
        LogError("IdroidLines: iDroid entry names could not be hashed, iDroid menu actions stay silent");
        return false;
    }
    if (gAddr.GetQuarkSystemTable)
        g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
    if (!g_GetQuarkSystemTable)
        LogError("IdroidLines: no system table address for this build, locked strike entries stay silent");

    void* target = ResolveGameAddress(gAddr.SimpleMissionBlockController_ReceiveMessage);
    const HookEntry state = CheckHookEntry(target, kPrologue, sizeof(kPrologue));
    if (state == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(target, found, sizeof(found));
        LogError("IdroidLines: SimpleMissionBlockController::ReceiveMessage is not the code we know (%s), iDroid menu actions stay silent", found);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("IdroidLines: SimpleMissionBlockController::ReceiveMessage", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkReceiveMessage), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("IdroidLines: SimpleMissionBlockController::ReceiveMessage hook failed at %p, iDroid menu actions stay silent", target);
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_SimpleMissionBlockController_ReceiveMessage_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target              = nullptr;
    g_Orig                = nullptr;
    g_GetQuarkSystemTable = nullptr;
}
