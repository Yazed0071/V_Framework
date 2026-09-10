#include "pch.h"

#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "OutfitRegistry.h"
#include "Player2Impl_UpdateVoiceType.h"
#include "ShadowState.h"
#include "log.h"

namespace
{
    using UpdateVoiceType_t =
        void (__fastcall*)(void*, std::uint32_t, void*);

    static UpdateVoiceType_t g_OrigUpdateVoiceType = nullptr;

    constexpr std::size_t kSelfSlotBaseOff  = 0x0C;
    constexpr std::size_t kSelfVoiceTypeOff = 0x650;
    constexpr std::size_t kDataTypeTableOff = 0x40;

    constexpr std::uint32_t kMaxPlayerType = 8;
    constexpr std::uint64_t kOverrideArmed = 0x100000000ull;

    static std::atomic<std::uint64_t> g_Overrides[kMaxPlayerType] = {};
    static std::atomic<std::uint32_t> g_LastSeen[kMaxPlayerType] = {};

    static uintptr_t UpdateVoiceTypeAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:  return 0x1409ce930ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:  return 0x1409ce830ull;
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:  return 0x1409cdc80ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:  return 0x1409cd740ull;
        default:                                           return 0;
        }
    }

    static std::uint32_t ArmedOverrideFor(std::uint32_t playerType)
    {
        if (playerType >= kMaxPlayerType)
            return outfit::kSoundSwitchUnset;

        const std::uint64_t slotValue =
            g_Overrides[playerType].load(std::memory_order_acquire);
        if ((slotValue & kOverrideArmed) == 0)
            return outfit::kSoundSwitchUnset;

        return static_cast<std::uint32_t>(slotValue & 0xFFFFFFFFull);
    }

    static std::uint32_t ResolveWornOutfitVoiceType(std::uint8_t playerType)
    {
        if (!outfit::shadow::HasCurrentSlot())
            return outfit::kSoundSwitchUnset;

        outfit::shadow::Slot s;
        if (!outfit::shadow::Get(outfit::shadow::GetCurrentSlot(), &s))
            return outfit::kSoundSwitchUnset;
        if (s.realPlayerType != playerType)
            return outfit::kSoundSwitchUnset;

        const outfit::OutfitEntry* entry = nullptr;
        if (!outfit::TryGetOutfitByPartsType(s.realPartsType, &entry) || !entry)
            return outfit::kSoundSwitchUnset;
        if (!entry->IsPlayerTypeSupported(playerType))
            return outfit::kSoundSwitchUnset;

        const std::uint8_t variant = entry->HasVariants()
            ? outfit::GetActiveVariant(entry->partsType) : std::uint8_t{ 0 };

        return entry->GetVariantVoiceType(playerType, variant);
    }

    static int ReadPlayerTypeAtIndex_SEH(void* data, std::uint32_t index)
    {
        int playerType = -1;
        __try
        {
            auto* typeTable = *reinterpret_cast<std::uint8_t**>(
                static_cast<std::uint8_t*>(data) + kDataTypeTableOff);
            if (typeTable)
                playerType = static_cast<int>(typeTable[index]);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            playerType = -1;
        }
        return playerType;
    }

    static bool ReadVoiceTypeAtSlot_SEH(
        void* self, std::uint32_t index, std::uint32_t& out)
    {
        bool ok = false;
        __try
        {
            auto* base = static_cast<std::uint8_t*>(self);
            const std::int32_t slotBase =
                *reinterpret_cast<std::int32_t*>(base + kSelfSlotBaseOff);
            const std::int32_t slot =
                static_cast<std::int32_t>(index) - slotBase;
            if (slot < 0)
                return false;

            auto* voiceTypes = *reinterpret_cast<std::uint32_t**>(
                base + kSelfVoiceTypeOff);
            if (!voiceTypes)
                return false;

            out = voiceTypes[slot];
            ok = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ok = false;
        }
        return ok;
    }

    static bool WriteVoiceType_SEH(
        void* self, std::uint32_t index, std::uint32_t voiceType)
    {
        bool written = false;
        __try
        {
            auto* base = static_cast<std::uint8_t*>(self);
            const std::int32_t slotBase =
                *reinterpret_cast<std::int32_t*>(base + kSelfSlotBaseOff);
            const std::int32_t slot =
                static_cast<std::int32_t>(index) - slotBase;
            if (slot < 0)
                return false;

            auto* voiceTypes = *reinterpret_cast<std::uint32_t**>(
                base + kSelfVoiceTypeOff);
            if (!voiceTypes)
                return false;

            voiceTypes[slot] = voiceType;
            written = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            written = false;
        }
        return written;
    }

    static void __fastcall hkUpdateVoiceType(
        void* self, std::uint32_t index, void* data)
    {
        g_OrigUpdateVoiceType(self, index, data);

        if (index == 0xFFFFFFFFu || !self || !data)
            return;
        if (MissionCodeGuard::ShouldBypassHooks())
            return;

        const int playerType = ReadPlayerTypeAtIndex_SEH(data, index);
        if (playerType < 0 || playerType >= static_cast<int>(kMaxPlayerType))
            return;

        std::uint32_t engineValue = outfit::kSoundSwitchUnset;
        if (ReadVoiceTypeAtSlot_SEH(self, index, engineValue)
            && engineValue != outfit::kSoundSwitchUnset)
            g_LastSeen[playerType].store(engineValue, std::memory_order_release);

        std::uint32_t voiceType =
            ResolveWornOutfitVoiceType(static_cast<std::uint8_t>(playerType));
        if (voiceType == outfit::kSoundSwitchUnset)
            voiceType = ArmedOverrideFor(static_cast<std::uint32_t>(playerType));
        if (voiceType == outfit::kSoundSwitchUnset)
            return;

        if (WriteVoiceType_SEH(self, index, voiceType))
            g_LastSeen[playerType].store(voiceType, std::memory_order_release);
    }
}


bool Install_PlayerVoiceType_Hook()
{
    void* target = ResolveGameAddress(UpdateVoiceTypeAddr());
    if (!target)
    {
        Log("[PlayerVoiceType] UpdateVoiceType is unknown on this build, so the "
            "player's Wwise voice type stays whatever the engine picks from the "
            "player type - a voice fpk belonging to a different soldier will "
            "load but answer no lines\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkUpdateVoiceType),
        reinterpret_cast<void**>(&g_OrigUpdateVoiceType));

    if (!ok)
        Log("[PlayerVoiceType] UpdateVoiceType hook install FAILED (target=%p) - "
            "the voice type cannot be overridden, so a cross-soldier voice fpk "
            "answers no lines\n", target);

    return ok;
}


bool Uninstall_PlayerVoiceType_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(UpdateVoiceTypeAddr()));
    g_OrigUpdateVoiceType = nullptr;
    Clear_AllPlayerVoiceTypeOverrides();
    return true;
}


void Set_PlayerVoiceTypeForType(std::uint32_t playerType, std::uint32_t voiceType)
{
    if (playerType >= kMaxPlayerType)
        return;

    g_Overrides[playerType].store(
        kOverrideArmed | static_cast<std::uint64_t>(voiceType),
        std::memory_order_release);
}


void Clear_PlayerVoiceTypeForType(std::uint32_t playerType)
{
    if (playerType >= kMaxPlayerType)
        return;

    g_Overrides[playerType].store(0, std::memory_order_release);
}


void Clear_AllPlayerVoiceTypeOverrides()
{
    for (std::uint32_t i = 0; i < kMaxPlayerType; ++i)
        g_Overrides[i].store(0, std::memory_order_release);
}


std::uint32_t Get_EffectivePlayerVoiceType(std::uint32_t playerType)
{
    if (playerType >= kMaxPlayerType)
        return outfit::kSoundSwitchUnset;

    std::uint32_t v =
        ResolveWornOutfitVoiceType(static_cast<std::uint8_t>(playerType));
    if (v == outfit::kSoundSwitchUnset)
        v = ArmedOverrideFor(playerType);
    if (v == outfit::kSoundSwitchUnset)
        v = g_LastSeen[playerType].load(std::memory_order_acquire);
    return v;
}
