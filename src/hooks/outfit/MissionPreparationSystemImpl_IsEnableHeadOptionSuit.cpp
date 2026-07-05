#include "pch.h"

#include "MissionPreparationSystemImpl_IsEnableHeadOptionSuit.h"
#include "OutfitRegistry.h"
#include "CustomHeadRegistry.h"

#include <atomic>
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
                const std::uint8_t livePT = outfit::ReadLivePartsType();
                const outfit::OutfitEntry* oe = nullptr;
                const bool offered =
                    livePT >= outfit::kCustomPartsTypeStart
                    && livePT <= outfit::kCustomPartsTypeEnd
                    && outfit::TryGetOutfitByPartsType(livePT, &oe) && oe
                    && oe->HasHeadOptionAnyVariant(head->equipId, playerType);
                if (offered)
                {
                    const std::uint8_t pt =
                        (playerType < outfit::kPlayerTypeMax) ? playerType : 0;
                    std::uint16_t fid = head->TppEnemyFaceId[pt];
                    if (fid == 0) fid = head->TppEnemyFaceId[0];
                    return fid;
                }
            }
        }
        else if (playerFaceEquipId >= 1 && playerFaceEquipId <= 5)
        {
            const std::uint8_t livePT = outfit::ReadLivePartsType();
            const outfit::OutfitEntry* oe = nullptr;
            if (livePT >= outfit::kCustomPartsTypeStart
                && livePT <= outfit::kCustomPartsTypeEnd
                && outfit::TryGetOutfitByPartsType(livePT, &oe) && oe)
            {
                const std::uint16_t vanEquipId =
                    static_cast<std::uint16_t>(playerFaceEquipId + 0x20D);
                if (!oe->HasHeadOptionAnyVariant(vanEquipId, playerType))
                {
                    return g_OrigConverFaceId
                        ? g_OrigConverFaceId(playerType, faceId, 0)
                        : faceId;
                }
            }
        }

        return g_OrigConverFaceId
            ? g_OrigConverFaceId(playerType, faceId, playerFaceEquipId)
            : faceId;
    }

    using ConvertHeadEquipModelType_t = std::uint64_t (__fastcall*)(
        void* self, std::uint32_t soldierIndex, void* offerList,
        std::uint32_t offerCount);
    static ConvertHeadEquipModelType_t g_OrigConvertHeadEquip      = nullptr;
    static bool                        g_ConvertHeadEquipInstalled = false;
    constexpr std::uint64_t            kNoHeadEquipModelType       = 0x2e;

    static std::uint64_t __fastcall hkConvertHeadEquipModelType(
        void* self, std::uint32_t soldierIndex, void* offerList,
        std::uint32_t offerCount)
    {
        if (self && soldierIndex != 0x1ff)
        {
            __try
            {
                auto* container = *reinterpret_cast<std::uint8_t**>(
                    reinterpret_cast<std::uint8_t*>(self) + 0xF8);
                auto* arrHdr = container
                    ? *reinterpret_cast<std::uint8_t**>(container + 0x98)
                    : nullptr;
                auto* base = arrHdr
                    ? *reinterpret_cast<std::uint8_t**>(arrHdr + 0x8)
                    : nullptr;
                const std::uint32_t count = arrHdr
                    ? *reinterpret_cast<std::uint32_t*>(arrHdr + 0x10)
                    : 0;
                if (base && soldierIndex < count)
                {
                    auto* entry =
                        base + static_cast<std::size_t>(soldierIndex) * 0x70;
                    const std::uint16_t wornId =
                        *reinterpret_cast<std::uint16_t*>(entry + 0x4e);
                    if (wornId != 0)
                    {
                        const outfit::CustomHeadEntry* h =
                            outfit::TryGetCustomHeadByEquipId(wornId);
                        if (h)
                        {
                            const std::uint8_t livePT =
                                outfit::ReadLivePartsType();
                            const outfit::OutfitEntry* oe = nullptr;
                            const bool offered =
                                livePT >= outfit::kCustomPartsTypeStart
                                && livePT <= outfit::kCustomPartsTypeEnd
                                && outfit::TryGetOutfitByPartsType(livePT, &oe)
                                && oe
                                && oe->HasHeadOptionAnyVariant(
                                       h->equipId, outfit::ReadLivePlayerType());
                            if (!offered)
                                return kNoHeadEquipModelType;
                        }
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                static std::atomic<int> s_fault{0};
                if (int n = s_fault.load(std::memory_order_relaxed); n < 8)
                {
                    s_fault.store(n + 1, std::memory_order_relaxed);
                    Log("[OutfitHeadOption] HeadEquipType FAULT: sIdx=%u "
                        "(read chain bad -> offsets wrong)\n", soldierIndex);
                }
            }
        }
        return g_OrigConvertHeadEquip
            ? g_OrigConvertHeadEquip(self, soldierIndex, offerList, offerCount)
            : kNoHeadEquipModelType;
    }

    static std::uint8_t __fastcall hkIsEnableHeadOptionSuit(
        void* self, std::uint16_t param2)
    {
        const std::uint8_t pt     = outfit::ReadLivePartsType();
        const std::uint8_t livePT = outfit::ReadLivePlayerType();

        if (pt >= outfit::kCustomPartsTypeStart && pt <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry)
            {
                return entry->HasHeadOptionsForVariant(
                           livePT, outfit::GetActiveVariant(pt)) ? 1 : 0;
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
                return entry->HasHeadOptionsForVariant(
                           livePT, outfit::GetActiveVariant(pt)) ? 1 : 0;
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
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry)
            {
                return entry->HasHeadOptionsForVariant(
                           livePT, outfit::GetActiveVariant(pt)) ? 1 : 0;
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
        if (target)
        {
            g_Installed = CreateAndEnableHook(
                target,
                reinterpret_cast<void*>(&hkIsEnableCurrentHeadOption),
                reinterpret_cast<void**>(&g_OrigIsEnableHead));
            Log("[OutfitHeadOption] enable-gate hook: %s (target=%p)\n",
                g_Installed ? "OK" : "FAIL", target);
        }
        else
        {
            Log("[OutfitHeadOption] enable-gate unresolved; skipped (submenu may "
                "show greyed, head render still installs below)\n");
        }

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

        void* heTarget =
            ResolveGameAddress(gAddr.RealizedSoldier2Impl_ConvertHeadEquipModelType);
        if (heTarget)
        {
            g_ConvertHeadEquipInstalled = CreateAndEnableHook(
                heTarget,
                reinterpret_cast<void*>(&hkConvertHeadEquipModelType),
                reinterpret_cast<void**>(&g_OrigConvertHeadEquip));
            Log("[OutfitHeadOption:HeadEquipType] hook %s (target=%p)\n",
                g_ConvertHeadEquipInstalled ? "installed" : "FAILED", heTarget);
        }
        else
        {
            Log("[OutfitHeadOption:HeadEquipType] target unresolved; skipped\n");
        }

        return g_ConverFaceIdInstalled || g_Installed
            || g_IsEnableCurrentSuitInstalled || g_IsEnableHeadOptionSuitInstalled
            || g_ConvertHeadEquipInstalled;
    }

    void Uninstall_OutfitHeadOption_Hook()
    {
        if (g_ConvertHeadEquipInstalled)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.RealizedSoldier2Impl_ConvertHeadEquipModelType))
                DisableAndRemoveHook(t);
            g_OrigConvertHeadEquip      = nullptr;
            g_ConvertHeadEquipInstalled = false;
        }

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
#ifdef _DEBUG
        Log("[OutfitHeadOption] removed\n");
#endif
    }
}
