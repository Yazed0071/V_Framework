#include "pch.h"

#include <atomic>
#include <cstdint>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "OutfitRegistry.h"
#include "BlockControllerImpl_LoadPartsNew.h"
#include "Player2Impl_UpdateVoiceType.h"
#include "ShadowState.h"
#include "log.h"

namespace
{
    using UpdateVoiceType_t =
        void (__fastcall*)(void*, std::uint32_t, void*);

    static UpdateVoiceType_t g_OrigUpdateVoiceType = nullptr;

    constexpr std::size_t kSelfSlotBaseOff        = 0x0C;
    constexpr std::size_t kSelfDialogueEventOff   = 0x640;
    constexpr std::size_t kSelfDialogueEvent2Off  = 0x648;
    constexpr std::size_t kSelfVoiceTypeOff       = 0x650;

    struct VanillaVoiceEvents
    {
        std::uint32_t voiceType;
        std::uint32_t dialogueEvent;
        std::uint32_t dialogueEvent2;
    };

    constexpr VanillaVoiceEvents kVanillaVoiceEvents[] =
    {
        { 0x3CF677C6u, 0x6DAFA2D7u, 0xE76B3178u },
        { 0x2EED93D9u, 0x5E29E20Cu, 0x1A2F6442u },
        { 0x2EED93DAu, 0x5E29E20Fu, 0x16B798C3u },
        { 0x2EED93DBu, 0x5E29E20Eu, 0x3779E444u },
        { 0x2EED93DCu, 0x5E29E209u, 0x3748111Du },
        { 0x80400AFAu, 0xB2788B57u, 0x81316ADBu },
        { 0x80400AF9u, 0xB2788B54u, 0xF6769B1Au },
        { 0x80400AF8u, 0xB2788B55u, 0x6E44BB79u },
        { 0x80400AFFu, 0xB2788B52u, 0xF5757188u },
        { 0x1BF9CBC1u, 0xC2ED45DEu, 0x0BCE8B94u },
        { 0x5D5262DFu, 0x24CFC5CEu, 0xAD1D7E04u },
    };

    static const VanillaVoiceEvents* FindVanillaVoiceEvents(std::uint32_t voiceType)
    {
        for (const VanillaVoiceEvents& e : kVanillaVoiceEvents)
            if (e.voiceType == voiceType)
                return &e;
        return nullptr;
    }
    constexpr std::size_t kDataTypeTableOff = 0x40;

    constexpr std::uint32_t kMaxPlayerType = 8;
    constexpr std::uint64_t kOverrideArmed = 0x100000000ull;

    static std::atomic<std::uint64_t> g_Overrides[kMaxPlayerType] = {};
    static std::atomic<std::uint64_t> g_OverrideEvents[kMaxPlayerType] = {};
    static std::atomic<std::uint32_t> g_LastSeen[kMaxPlayerType] = {};

    struct VoiceWrite
    {
        std::uint32_t voiceType = outfit::kSoundSwitchUnset;
        std::uint32_t event1    = 0;
        std::uint32_t event2    = 0;
    };

    struct LiveVoiceSlot
    {
        void*         self       = nullptr;
        std::uint32_t index      = 0;
        std::uint32_t playerType = kMaxPlayerType;
        std::uint32_t engine[3]  = {};
    };

    static std::mutex    g_LiveMutex;
    static LiveVoiceSlot g_Live;

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

    static std::uint32_t ResolveVanillaExtVoiceType(std::uint8_t playerType)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return outfit::kSoundSwitchUnset;
        if (outfit::ReadLivePlayerType() != playerType)
            return outfit::kSoundSwitchUnset;

        const std::uint8_t vpt = outfit::ReadLivePartsType();
        if (vpt == 0xFF || vpt >= outfit::kCustomPartsTypeStart)
            return outfit::kSoundSwitchUnset;

        if (const std::uint8_t v = outfit::GetActiveVariant(vpt); v != 0)
        {
            if (const outfit::VanillaSuitVariantAsset* var =
                    outfit::VanillaExtGetVariantBridged(vpt, playerType, v))
            {
                if (var->voiceType != outfit::kSoundSwitchUnset)
                    return var->voiceType;
            }
        }

        return outfit::VanillaExtGetSuitVoiceType(
            vpt, playerType, outfit::ReadLiveSelectorCode());
    }

    static std::uint32_t ResolveWornOutfitVoiceType(std::uint8_t playerType)
    {
        if (!outfit::shadow::HasCurrentSlot())
            return ResolveVanillaExtVoiceType(playerType);

        outfit::shadow::Slot s;
        if (!outfit::shadow::Get(outfit::shadow::GetCurrentSlot(), &s))
            return ResolveVanillaExtVoiceType(playerType);
        if (s.realPlayerType != playerType)
            return ResolveVanillaExtVoiceType(playerType);

        const outfit::OutfitEntry* entry = nullptr;
        if (!outfit::TryGetOutfitByPartsType(s.realPartsType, &entry) || !entry)
            return ResolveVanillaExtVoiceType(playerType);
        if (!entry->IsPlayerTypeSupported(playerType))
            return ResolveVanillaExtVoiceType(playerType);

        const std::uint8_t variant = entry->HasVariants()
            ? outfit::GetActiveVariant(entry->partsType) : std::uint8_t{ 0 };

        const std::uint32_t declared =
            entry->GetVariantVoiceType(playerType, variant);
        if (declared != outfit::kSoundSwitchUnset)
            return declared;
        return ResolveVanillaExtVoiceType(playerType);
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

    static bool AccessVoiceSlot_SEH(
        void* self, std::uint32_t index, std::uint32_t* values, bool write)
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
            auto* events = *reinterpret_cast<std::uint32_t**>(
                base + kSelfDialogueEventOff);
            auto* events2 = *reinterpret_cast<std::uint32_t**>(
                base + kSelfDialogueEvent2Off);
            if (!voiceTypes || !events || !events2)
                return false;

            if (write)
            {
                events[slot]     = values[1];
                events2[slot]    = values[2];
                voiceTypes[slot] = values[0];
            }
            else
            {
                values[0] = voiceTypes[slot];
                values[1] = events[slot];
                values[2] = events2[slot];
            }
            ok = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ok = false;
        }
        return ok;
    }

    static bool WriteVoice_SEH(void* self, std::uint32_t index, const VoiceWrite& w)
    {
        std::uint32_t values[3] = {};
        if (!AccessVoiceSlot_SEH(self, index, values, false))
            return false;
        values[0] = w.voiceType;
        if (const VanillaVoiceEvents* e = FindVanillaVoiceEvents(w.voiceType))
        {
            values[1] = e->dialogueEvent;
            values[2] = e->dialogueEvent2;
        }
        if (w.event1 != 0) values[1] = w.event1;
        if (w.event2 != 0) values[2] = w.event2;
        return AccessVoiceSlot_SEH(self, index, values, true);
    }

    static VoiceWrite ResolveVoiceWrite(std::uint32_t playerType)
    {
        VoiceWrite w;
        w.voiceType = ResolveWornOutfitVoiceType(static_cast<std::uint8_t>(playerType));
        if (w.voiceType != outfit::kSoundSwitchUnset)
            return w;
        w.voiceType = ArmedOverrideFor(playerType);
        if (w.voiceType == outfit::kSoundSwitchUnset)
            return w;
        const std::uint64_t events =
            g_OverrideEvents[playerType].load(std::memory_order_acquire);
        w.event1 = static_cast<std::uint32_t>(events >> 32);
        w.event2 = static_cast<std::uint32_t>(events & 0xFFFFFFFFull);
        return w;
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

        std::uint32_t engine[3] = {};
        if (AccessVoiceSlot_SEH(self, index, engine, false))
        {
            if (engine[0] != outfit::kSoundSwitchUnset)
                g_LastSeen[playerType].store(engine[0], std::memory_order_release);
            if (index == outfit::GetLocalPartsSlot())
            {
                std::lock_guard<std::mutex> lock(g_LiveMutex);
                g_Live.self       = self;
                g_Live.index      = index;
                g_Live.playerType = static_cast<std::uint32_t>(playerType);
                for (int i = 0; i < 3; ++i) g_Live.engine[i] = engine[i];
            }
        }

        const VoiceWrite w = ResolveVoiceWrite(static_cast<std::uint32_t>(playerType));
        if (w.voiceType == outfit::kSoundSwitchUnset)
            return;

        if (WriteVoice_SEH(self, index, w))
            g_LastSeen[playerType].store(w.voiceType, std::memory_order_release);
    }

    static void ApplyLiveVoice(std::uint32_t playerType)
    {
        if (playerType >= kMaxPlayerType || MissionCodeGuard::ShouldBypassHooks())
            return;

        const VoiceWrite w = ResolveVoiceWrite(playerType);

        std::lock_guard<std::mutex> lock(g_LiveMutex);
        if (!g_Live.self || g_Live.playerType != playerType)
            return;

        if (w.voiceType != outfit::kSoundSwitchUnset)
        {
            if (WriteVoice_SEH(g_Live.self, g_Live.index, w))
                g_LastSeen[playerType].store(w.voiceType, std::memory_order_release);
        }
        else if (AccessVoiceSlot_SEH(g_Live.self, g_Live.index, g_Live.engine, true))
        {
            g_LastSeen[playerType].store(g_Live.engine[0], std::memory_order_release);
        }
    }
}


bool Install_PlayerVoiceType_Hook()
{
    void* target = ResolveGameAddress(UpdateVoiceTypeAddr());
    if (!target)
    {
        LogError("PlayerVoiceType: UpdateVoiceType address missing on this build, voice type override off");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkUpdateVoiceType),
        reinterpret_cast<void**>(&g_OrigUpdateVoiceType));

    if (!ok)
        LogError("PlayerVoiceType: UpdateVoiceType hook failed, voice type override off");

    return ok;
}


bool Uninstall_PlayerVoiceType_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(UpdateVoiceTypeAddr()));
    g_OrigUpdateVoiceType = nullptr;
    Reset_PlayerVoiceLiveSlot();
    Clear_AllPlayerVoiceTypeOverrides();
    return true;
}


void Set_PlayerVoiceTypeForType(std::uint32_t playerType, std::uint32_t voiceType,
                                std::uint32_t dialogueEvent, std::uint32_t dialogueEvent2)
{
    if (playerType >= kMaxPlayerType)
        return;

    g_OverrideEvents[playerType].store(
        (static_cast<std::uint64_t>(dialogueEvent) << 32) | dialogueEvent2,
        std::memory_order_release);
    g_Overrides[playerType].store(
        kOverrideArmed | static_cast<std::uint64_t>(voiceType),
        std::memory_order_release);
    ApplyLiveVoice(playerType);
}


void Clear_PlayerVoiceTypeForType(std::uint32_t playerType)
{
    if (playerType >= kMaxPlayerType)
        return;

    g_Overrides[playerType].store(0, std::memory_order_release);
    g_OverrideEvents[playerType].store(0, std::memory_order_release);
    ApplyLiveVoice(playerType);
}


void Reset_PlayerVoiceLiveSlot()
{
    std::lock_guard<std::mutex> lock(g_LiveMutex);
    g_Live = LiveVoiceSlot{};
}


void Clear_AllPlayerVoiceTypeOverrides()
{
    for (std::uint32_t i = 0; i < kMaxPlayerType; ++i)
    {
        g_Overrides[i].store(0, std::memory_order_release);
        g_OverrideEvents[i].store(0, std::memory_order_release);
    }
    for (std::uint32_t i = 0; i < kMaxPlayerType; ++i)
        ApplyLiveVoice(i);
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
