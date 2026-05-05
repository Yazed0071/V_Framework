#include "pch.h"

#include "OutfitEquippedState.h"
#include "OutfitRegistry.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using IsEquipDeveloped_t =
        std::uint8_t (__fastcall*)(void* self, std::uint32_t flowIndex);

    using GetEquipIdFromLoadoutInfo_t =
        std::uint64_t (__fastcall*)(void* self, std::uint32_t slot);


    using IsEquipSuit_t =
        std::uint8_t (__fastcall*)(void* self, std::uint32_t pt, std::uint32_t flowIndex);

    static IsEquipDeveloped_t          g_OrigIsEquipDeveloped         = nullptr;
    static GetEquipIdFromLoadoutInfo_t g_OrigGetEquipIdFromLoadoutInfo = nullptr;
    static IsEquipSuit_t               g_OrigIsEquipSuit              = nullptr;

    static bool g_InstalledIsDeveloped = false;
    static bool g_InstalledGetEquipId  = false;
    static bool g_InstalledIsEquipSuit = false;

    static void* g_CachedEDC = nullptr;


    constexpr std::uint32_t kSlotUniforms = 1;

    static std::uint8_t __fastcall hkIsEquipDeveloped(void* self, std::uint32_t flowIndex)
    {
        if (!g_CachedEDC && self)
        {
            g_CachedEDC = self;
            Log("[OutfitEquippedState] cached EDC=%p\n", self);
        }


        constexpr std::uint32_t kEdcRowCapacity = 0x400;
        if (flowIndex >= kEdcRowCapacity)
            return 0;


        return g_OrigIsEquipDeveloped(self, flowIndex);
    }

    static std::uint64_t __fastcall hkGetEquipIdFromLoadoutInfo(
        void* self, std::uint32_t slot)
    {
        const std::uint64_t orig = g_OrigGetEquipIdFromLoadoutInfo(self, slot);

        if (slot != kSlotUniforms) return orig;

        const std::uint8_t pt = outfit::ReadLivePartsType();
        if (pt < outfit::kCustomPartsTypeStart || pt > outfit::kCustomPartsTypeEnd)
            return orig;

        const outfit::OutfitEntry* entry = nullptr;
        if (!outfit::TryGetOutfitByPartsType(pt, &entry) || !entry)
            return orig;


        const std::uint8_t livePlayer = outfit::ReadLivePlayerType();
        if (livePlayer != 0xFF
            && !entry->IsPlayerTypeSupported(livePlayer))
            return orig;

        return static_cast<std::uint64_t>(entry->flowIndex);
    }


    static std::uint8_t __fastcall hkIsEquipSuit(
        void* self, std::uint32_t pt, std::uint32_t flowIndex)
    {
        const outfit::OutfitEntry* entry = nullptr;
        if (outfit::TryGetOutfitByFlowIndex(
                static_cast<std::uint16_t>(flowIndex), &entry)
            && entry)
        {


            const std::uint8_t askPT = static_cast<std::uint8_t>(pt & 0xFFu);
            const bool match = entry->IsPlayerTypeSupported(askPT);
            return match ? std::uint8_t{1} : std::uint8_t{0};
        }


        if (g_OrigIsEquipSuit) return g_OrigIsEquipSuit(self, pt, flowIndex);
        return 1;
    }
}

namespace outfit
{
    bool Install_OutfitEquippedState_Hooks()
    {
        void* tDeveloped = ResolveGameAddress(gAddr.IsEquipDeveloped);
        void* tGetEquip  = ResolveGameAddress(gAddr.GetEquipIdFromLoadoutInfo);
        void* tIsSuit    = ResolveGameAddress(gAddr.IsEquipSuit);

        if (tDeveloped)
        {
            g_InstalledIsDeveloped = CreateAndEnableHook(
                tDeveloped,
                reinterpret_cast<void*>(&hkIsEquipDeveloped),
                reinterpret_cast<void**>(&g_OrigIsEquipDeveloped));
        }

        if (tGetEquip)
        {
            g_InstalledGetEquipId = CreateAndEnableHook(
                tGetEquip,
                reinterpret_cast<void*>(&hkGetEquipIdFromLoadoutInfo),
                reinterpret_cast<void**>(&g_OrigGetEquipIdFromLoadoutInfo));
        }

        if (tIsSuit)
        {
            g_InstalledIsEquipSuit = CreateAndEnableHook(
                tIsSuit,
                reinterpret_cast<void*>(&hkIsEquipSuit),
                reinterpret_cast<void**>(&g_OrigIsEquipSuit));
        }

        Log("[OutfitEquippedState] installed: isDeveloped=%s getEquipId=%s "
            "isEquipSuit=%s\n",
            g_InstalledIsDeveloped ? "OK" : "skip",
            g_InstalledGetEquipId  ? "OK" : "skip",
            g_InstalledIsEquipSuit ? "OK" : "skip");

        return g_InstalledIsDeveloped && g_InstalledGetEquipId;
    }

    void Uninstall_OutfitEquippedState_Hooks()
    {
        if (g_InstalledIsDeveloped)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.IsEquipDeveloped));
        if (g_InstalledGetEquipId)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.GetEquipIdFromLoadoutInfo));
        if (g_InstalledIsEquipSuit)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.IsEquipSuit));

        g_OrigIsEquipDeveloped         = nullptr;
        g_OrigGetEquipIdFromLoadoutInfo = nullptr;
        g_OrigIsEquipSuit              = nullptr;
        g_CachedEDC                    = nullptr;
        g_InstalledIsDeveloped = g_InstalledGetEquipId = g_InstalledIsEquipSuit = false;

        Log("[OutfitEquippedState] removed\n");
    }

    void* GetCachedEquipDevelopController()
    {
        return g_CachedEDC;
    }

    bool IsFlowIndexDevelopedByOrig(unsigned short flowIndex)
    {
        if (!g_CachedEDC || !g_OrigIsEquipDeveloped) return false;


        if (flowIndex >= 0x400) return false;
        return g_OrigIsEquipDeveloped(g_CachedEDC, flowIndex) != 0;
    }
}
