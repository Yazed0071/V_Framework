#include "pch.h"

#include "OutfitHeadOption.h"
#include "OutfitRegistry.h"
#include "CustomHeadRegistry.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{


    using IsEnableCurrentHeadOption_t = std::uint8_t (__fastcall*)(void* self);

    static IsEnableCurrentHeadOption_t g_OrigIsEnableHead = nullptr;
    static bool                        g_Installed        = false;


    using IsEnableCurrentSuit_t = std::uint8_t (__fastcall*)(void* self);
    static IsEnableCurrentSuit_t g_OrigIsEnableCurrentSuit       = nullptr;
    static bool                  g_IsEnableCurrentSuitInstalled  = false;


    using IsEnableHeadOptionSuit_t =
        std::uint8_t (__fastcall*)(void* self, std::uint16_t flowIndex);
    static IsEnableHeadOptionSuit_t g_OrigIsEnableHeadOptionSuit       = nullptr;
    static bool                     g_IsEnableHeadOptionSuitInstalled  = false;


    using ConverFaceId_t = std::uint16_t (__fastcall*)(
        std::uint8_t playerType, std::int16_t faceId,
        std::uint8_t playerFaceEquipId);
    static ConverFaceId_t g_OrigConverFaceId           = nullptr;
    static bool           g_ConverFaceIdInstalled      = false;

    static std::uint16_t __fastcall hkConverFaceIdWithFaceEquipId(
        std::uint8_t playerType, std::int16_t faceId,
        std::uint8_t playerFaceEquipId)
    {


        if (outfit::IsCustomHeadSlot(playerFaceEquipId))
        {
            if (const auto* head =
                outfit::TryGetCustomHeadBySlot(playerFaceEquipId))
            {
                return head->TppEnemyFaceId;
            }
        }

        return g_OrigConverFaceId
            ? g_OrigConverFaceId(playerType, faceId, playerFaceEquipId)
            : faceId;
    }

    static std::uint8_t __fastcall hkIsEnableHeadOptionSuit(
        void* self, std::uint16_t param2)
    {
        const std::uint8_t pt     = outfit::ReadLivePartsType();
        const std::uint8_t livePT = outfit::ReadLivePlayerType();

        if (pt >= outfit::kCustomPartsTypeStart && pt <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry
                && entry->HasHeadOptions(livePT))
            {
                return 1;
            }
        }

        return g_OrigIsEnableHeadOptionSuit(self, param2);
    }

    static std::uint8_t __fastcall hkIsEnableCurrentHeadOption(void* self)
    {
        const std::uint8_t pt     = outfit::ReadLivePartsType();
        const std::uint8_t livePT = outfit::ReadLivePlayerType();

        if (pt >= outfit::kCustomPartsTypeStart && pt <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry)
            {
                return entry->HasHeadOptions(livePT) ? 1 : 0;
            }
        }

        return g_OrigIsEnableHead(self);
    }

    static std::uint8_t __fastcall hkIsEnableCurrentSuit(void* self)
    {
        const std::uint8_t pt     = outfit::ReadLivePartsType();
        const std::uint8_t livePT = outfit::ReadLivePlayerType();

        if (pt >= outfit::kCustomPartsTypeStart && pt <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry
                && entry->HasHeadOptions(livePT))
            {
                return 1;
            }
        }

        return g_OrigIsEnableCurrentSuit(self);
    }
}

namespace outfit
{
    bool Install_OutfitHeadOption_Hook()
    {
        if (g_Installed) return true;

        void* target = ResolveGameAddress(gAddr.MissionPrep_IsEnableCurrentHeadOption);
        if (!target)
        {
            Log("[OutfitHeadOption] target unresolved; module disabled\n");
            return false;
        }

        g_Installed = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkIsEnableCurrentHeadOption),
            reinterpret_cast<void**>(&g_OrigIsEnableHead));

        Log("[OutfitHeadOption] installed: %s (target=%p)\n",
            g_Installed ? "OK" : "FAIL", target);

        void* suitTarget = ResolveGameAddress(gAddr.IsEnableCurrentSuit);
        if (suitTarget)
        {
            g_IsEnableCurrentSuitInstalled = CreateAndEnableHook(
                suitTarget,
                reinterpret_cast<void*>(&hkIsEnableCurrentSuit),
                reinterpret_cast<void**>(&g_OrigIsEnableCurrentSuit));
            Log("[OutfitHeadOption:SuitGate] override hook %s (target=%p)\n",
                g_IsEnableCurrentSuitInstalled ? "installed" : "FAILED",
                suitTarget);
        }
        else
        {
            Log("[OutfitHeadOption:SuitGate] target unresolved; skipped\n");
        }

        void* hosTarget = ResolveGameAddress(
            gAddr.MissionPrepSystem_IsEnableHeadOptionSuit);
        if (hosTarget)
        {
            g_IsEnableHeadOptionSuitInstalled = CreateAndEnableHook(
                hosTarget,
                reinterpret_cast<void*>(&hkIsEnableHeadOptionSuit),
                reinterpret_cast<void**>(&g_OrigIsEnableHeadOptionSuit));
            Log("[OutfitHeadOption:HOSuit] override hook %s (target=%p)\n",
                g_IsEnableHeadOptionSuitInstalled ? "installed" : "FAILED",
                hosTarget);
        }
        else
        {
            Log("[OutfitHeadOption:HOSuit] target unresolved; skipped\n");
        }


        void* cfTarget = ResolveGameAddress(gAddr.Player_ConverFaceIdWithFaceEquipId);
        if (cfTarget)
        {
            g_ConverFaceIdInstalled = CreateAndEnableHook(
                cfTarget,
                reinterpret_cast<void*>(&hkConverFaceIdWithFaceEquipId),
                reinterpret_cast<void**>(&g_OrigConverFaceId));
            Log("[OutfitHeadOption:ConverFace] hook %s (target=%p)\n",
                g_ConverFaceIdInstalled ? "installed" : "FAILED", cfTarget);
        }
        else
        {
            Log("[OutfitHeadOption:ConverFace] target unresolved; skipped\n");
        }

        return g_Installed;
    }

    void Uninstall_OutfitHeadOption_Hook()
    {
        if (g_ConverFaceIdInstalled)
        {
            if (void* t = ResolveGameAddress(gAddr.Player_ConverFaceIdWithFaceEquipId))
                DisableAndRemoveHook(t);
            g_OrigConverFaceId      = nullptr;
            g_ConverFaceIdInstalled = false;
        }

        if (g_IsEnableHeadOptionSuitInstalled)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.MissionPrepSystem_IsEnableHeadOptionSuit))
                DisableAndRemoveHook(t);
            g_OrigIsEnableHeadOptionSuit      = nullptr;
            g_IsEnableHeadOptionSuitInstalled = false;
        }

        if (g_IsEnableCurrentSuitInstalled)
        {
            if (void* t = ResolveGameAddress(gAddr.IsEnableCurrentSuit))
                DisableAndRemoveHook(t);
            g_OrigIsEnableCurrentSuit       = nullptr;
            g_IsEnableCurrentSuitInstalled  = false;
        }

        if (!g_Installed) return;
        if (void* t = ResolveGameAddress(gAddr.MissionPrep_IsEnableCurrentHeadOption))
            DisableAndRemoveHook(t);
        g_OrigIsEnableHead = nullptr;
        g_Installed        = false;
        Log("[OutfitHeadOption] removed\n");
    }
}
