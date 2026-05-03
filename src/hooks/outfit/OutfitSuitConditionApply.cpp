#include "pch.h"

#include "OutfitSuitConditionApply.h"
#include "OutfitRegistry.h"
#include "CustomHeadRegistry.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{


    using SetSuitAndHandConditionWithLoadoutInfo_t =
        void (__fastcall*)(void* self, void* loadoutInfo);

    static SetSuitAndHandConditionWithLoadoutInfo_t g_Orig = nullptr;
    static bool g_Installed = false;


    using RequestToChangeLoadout_t =
        void (__fastcall*)(void* self, void* loadoutInfo, std::uint8_t apply);

    static RequestToChangeLoadout_t g_OrigReqLoadout    = nullptr;
    static bool                     g_InstalledReqLoadout = false;


    using LoadoutApplyAfterSetSuit_t =
        void (__fastcall*)(void* self, void* loadoutInfo);

    static LoadoutApplyAfterSetSuit_t g_OrigLoadoutApply    = nullptr;
    static bool                       g_InstalledLoadoutApply = false;


    using SetInitialConditionWithLoadoutInfo_t =
        void (__fastcall*)(void* self, void* loadoutInfo, std::uint8_t preserve);

    static SetInitialConditionWithLoadoutInfo_t g_OrigSetInitial    = nullptr;
    static bool                                 g_InstalledSetInitial = false;


    constexpr std::size_t kInfoOff_PartsType  = 0x00;
    constexpr std::size_t kInfoOff_CamoType   = 0x01;
    constexpr std::size_t kInfoOff_Variant    = 0x02;
    constexpr std::size_t kInfoOff_FaceId     = 0x03;
    constexpr std::size_t kInfoOff_Flags      = 0xBC;
    constexpr std::size_t kInfoOff_PlayerType = 0xC0;


    static bool InspectAndRewriteLoadout(void* info, const char* tag)
    {
        if (!info) return false;

        auto* base = reinterpret_cast<std::uint8_t*>(info);

        std::uint8_t  partsType  = 0;
        std::uint8_t  camoType   = 0;
        std::uint8_t  playerType = 0;
        std::uint32_t flags      = 0;

        __try
        {
            partsType  = base[kInfoOff_PartsType];
            camoType   = base[kInfoOff_CamoType];
            playerType = base[kInfoOff_PlayerType];
            flags      = *reinterpret_cast<std::uint32_t*>(base + kInfoOff_Flags);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitSuitConditionApply:%s] SEH reading info\n", tag);
            return false;
        }

        Log("[OutfitSuitConditionApply:%s] fire: partsType=0x%02X "
            "camo=0x%02X playerType=%u flags=0x%X (info=%p)\n",
            tag,
            static_cast<unsigned>(partsType),
            static_cast<unsigned>(camoType),
            static_cast<unsigned>(playerType),
            flags, info);


        if ((flags & 0x80u) != 0)
        {
            __try
            {
                const std::uint8_t curFaceSlot = base[kInfoOff_FaceId];
                if (curFaceSlot == 0)
                {
                    const std::uint8_t livePT = outfit::ReadLivePartsType();
                    const bool liveIsCustom =
                        (livePT >= outfit::kCustomPartsTypeStart
                         && livePT <= outfit::kCustomPartsTypeEnd);
                    if (liveIsCustom)
                    {
                        const std::uint16_t pendingHead =
                            outfit::GetPendingHeadOptionEquipId();
                        if (pendingHead != 0)
                        {


                            std::uint8_t slot = 0;
                            if (pendingHead == outfit::kHeadOption_None)
                            {
                                slot = 0;
                            }
                            else if (pendingHead == 0x210)
                            {
                                slot = 3;
                            }
                            else if (pendingHead == 0x211)
                            {
                                slot = 4;


                            }
                            else if (pendingHead == 0x212)
                            {
                                slot = 5;
                            }
                            else if (outfit::IsCustomHeadEquipId(pendingHead))
                            {


                                if (const auto* head =
                                    outfit::TryGetCustomHeadByEquipId(
                                        pendingHead))
                                {
                                    slot = head->slotByte;
                                }
                                else
                                {
                                    slot = 0;
                                    Log("[OutfitSuitConditionApply:%s] "
                                        "pending custom head equipId 0x%X "
                                        "in framework range but not "
                                        "registered — falling back to "
                                        "NONE\n",
                                        tag,
                                        static_cast<unsigned>(pendingHead));
                                }
                            }
                            else if (pendingHead < 0x100)
                            {

                                slot = static_cast<std::uint8_t>(
                                    pendingHead & 0xFF);
                            }
                            else
                            {
                                slot = 0;
                            }


                            base[kInfoOff_FaceId]     = slot;
                            base[kInfoOff_FaceId + 1] = 0;
                            outfit::ClearPendingHeadOptionEquipId();
                            Log("[OutfitSuitConditionApply:%s] head-option "
                                "rewrite: info[3] = 0x%02X (translated from "
                                "equipId 0x%X via pending stash; live "
                                "partsType=0x%02X is custom and orig "
                                "dropped the click)\n",
                                tag,
                                static_cast<unsigned>(slot),
                                static_cast<unsigned>(pendingHead),
                                static_cast<unsigned>(livePT));
                        }
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                Log("[OutfitSuitConditionApply:%s] SEH in head-option "
                    "rewrite\n", tag);
            }
        }

        const bool applySuit = (flags & 0x1u) != 0;
        if (!applySuit) return false;

        const outfit::OutfitEntry* chosen = nullptr;
        const char*                via    = nullptr;


        if (partsType >= outfit::kCustomPartsTypeStart
         && partsType <= outfit::kCustomPartsTypeEnd)
        {
            outfit::TryGetOutfitByPartsType(partsType, &chosen);
            if (chosen) via = "by-partsType";
        }


        std::uint8_t resolvedVariantIdx = 0;
        bool         resolvedVariantValid = false;
        if (!chosen
         && camoType >= outfit::kCustomSelectorStart
         && camoType <= outfit::kCustomSelectorEnd)
        {
            std::uint8_t vi = 0;
            if (outfit::TryGetOutfitByVariantSelector(camoType, &chosen, &vi)
                && chosen)
            {
                via = "by-selectorCode";
                resolvedVariantIdx   = vi;
                resolvedVariantValid = true;
            }
        }


        if (!chosen && partsType == 0x00 && camoType == 0xFF)
        {
            const std::uint16_t pendingDevId = outfit::GetPendingOutfitDevelopId();
            if (pendingDevId != 0)
            {
                const outfit::OutfitEntry* byPending = nullptr;
                if (outfit::TryGetOutfitByDevelopId(pendingDevId, &byPending)
                    && byPending)
                {
                    chosen = byPending;
                    via = "broken-custom-pendingDevId";
                    outfit::ClearPendingOutfitDevelopId();
                }
            }
        }


        if (!chosen
         && (flags & 0x01u) != 0
         && partsType == 0x00
         && camoType  == 0x00)
        {
            const std::uint16_t pendingDevId = outfit::GetPendingOutfitDevelopId();
            if (pendingDevId != 0)
            {
                const outfit::OutfitEntry* byPending = nullptr;
                if (outfit::TryGetOutfitByDevelopId(pendingDevId, &byPending)
                    && byPending)
                {
                    chosen = byPending;
                    via = "supply-drop-request-pendingDevId";
                    outfit::ClearPendingOutfitDevelopId();
                }
            }
        }

        if (!chosen)
        {


            if (partsType == 0x00 && camoType == 0xFF)
            {
                __try
                {
                    base[kInfoOff_CamoType] = 0x00;
                    Log("[OutfitSuitConditionApply:%s] cleared stale "
                        "broken-custom signal (no pendingDevId) -> vanilla "
                        "NORMAL\n",
                        tag);
                }
                __except (EXCEPTION_EXECUTE_HANDLER) { }
            }
            return false;
        }

        __try
        {


            const bool playerTypeValid = (flags & 0x100u) != 0;
            const std::uint8_t livePT = outfit::ReadLivePlayerType();
            const std::uint8_t effectivePT =
                playerTypeValid ? playerType : livePT;
            const bool canCheckPT = playerTypeValid || (livePT != 0xFF);
            // Snake↔Avatar bridging: a Snake-registered outfit is acceptable
            // on the Avatar slot and vice versa, so don't scrub on that pair.
            const bool clearMismatch = canCheckPT
                                    && !outfit::IsPlayerTypeCompatible(
                                            chosen->playerType, effectivePT);


            if (clearMismatch)
            {


                base[kInfoOff_PartsType] = 0x00;
                base[kInfoOff_CamoType]  = 0x00;

                Log("[OutfitSuitConditionApply:%s] playerType mismatch "
                    "(effective=%u via=%s outfit-playerType=%u "
                    "developId=%u; livePT=%u info[0xC0]=%u "
                    "flags=0x%X 0x100=%s) — applied vanilla NORMAL "
                    "upfront\n",
                    tag,
                    static_cast<unsigned>(effectivePT),
                    via,
                    static_cast<unsigned>(chosen->playerType),
                    static_cast<unsigned>(chosen->developId),
                    static_cast<unsigned>(livePT),
                    static_cast<unsigned>(playerType),
                    flags,
                    playerTypeValid ? "set" : "unset");
                return true;
            }


            base[kInfoOff_PartsType] = chosen->partsType;
            base[kInfoOff_CamoType]  = chosen->selectorCode;


            if (resolvedVariantValid)
            {
                base[kInfoOff_Variant] = resolvedVariantIdx;
            }

            Log("[OutfitSuitConditionApply:%s] rewrote loadout (via %s) "
                "-> developId=%u partsType=0x%02X selector=0x%02X "
                "variant=%u (effective=%u outfit-playerType=%u; livePT=%u "
                "info[0xC0]=%u flags=0x%X 0x100=%s)\n",
                tag, via,
                static_cast<unsigned>(chosen->developId),
                static_cast<unsigned>(chosen->partsType),
                static_cast<unsigned>(chosen->selectorCode),
                static_cast<unsigned>(base[kInfoOff_Variant]),
                static_cast<unsigned>(effectivePT),
                static_cast<unsigned>(chosen->playerType),
                static_cast<unsigned>(livePT),
                static_cast<unsigned>(playerType),
                flags,
                playerTypeValid ? "set" : "unset");
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitSuitConditionApply:%s] SEH writing rewrite\n", tag);
            return false;
        }
    }

    static void __fastcall hkSetSuit(void* self, void* info)
    {
        InspectAndRewriteLoadout(info, "SetSuit");


        g_Orig(self, info);
    }

    static void __fastcall hkReqLoadout(void* self, void* info, std::uint8_t apply)
    {
        InspectAndRewriteLoadout(info, "ReqLoadout");
        g_OrigReqLoadout(self, info, apply);
    }


    static void __fastcall hkLoadoutApplyAfterSetSuit(void* self, void* info)
    {
        if (!info || !g_OrigLoadoutApply)
        {
            if (g_OrigLoadoutApply) g_OrigLoadoutApply(self, info);
            return;
        }

        bool shouldSuppressSlotApply = false;
        std::uint8_t  partsType = 0;
        std::uint32_t flags     = 0;

        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(info);
            partsType = base[kInfoOff_PartsType];
            flags     = *reinterpret_cast<std::uint32_t*>(base + kInfoOff_Flags);

            const bool isCustomPartsType =
                partsType >= outfit::kCustomPartsTypeStart
             && partsType <= outfit::kCustomPartsTypeEnd;


            const bool noSlotBits = (flags & 0x1Cu) == 0;
            const bool hasSuitBit = (flags & 0x01u) != 0;

            if (isCustomPartsType && noSlotBits && hasSuitBit)
                shouldSuppressSlotApply = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitLoadoutPreserve] SEH reading info — falling through to orig\n");
            g_OrigLoadoutApply(self, info);
            return;
        }

        if (shouldSuppressSlotApply)
        {
            Log("[OutfitLoadoutPreserve] SUPPRESSING slot-apply: custom partsType=0x%02X "
                "flags=0x%X (suit-equip without slot data — orig would zero "
                "player's weapon slots). Calling SetSuit directly so body "
                "change happens; player loadout untouched.\n",
                static_cast<unsigned>(partsType),
                flags);


            if (g_Orig)
                g_Orig(self, info);


            __try
            {
                auto* p1 = reinterpret_cast<std::uint8_t*>(self);
                if (p1)
                {
                    *reinterpret_cast<std::uint32_t*>(p1 + 0x190) |= 8u;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                Log("[OutfitLoadoutPreserve] SEH writing post-bit at p1+0x190\n");
            }

            return;
        }

        g_OrigLoadoutApply(self, info);
    }


    static void __fastcall hkSetInitialConditionWithLoadoutInfo(
        void* self, void* info, std::uint8_t preserve)
    {
        if (!info || !g_OrigSetInitial)
        {
            if (g_OrigSetInitial) g_OrigSetInitial(self, info, preserve);
            return;
        }

        std::uint8_t  partsType    = 0;
        std::uint8_t  camoType     = 0;
        std::uint32_t flags        = 0;
        bool          isCustomOutfitEquip = false;

        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(info);
            partsType  = base[kInfoOff_PartsType];
            camoType   = base[kInfoOff_CamoType];
            flags      = *reinterpret_cast<std::uint32_t*>(base + kInfoOff_Flags);


            const bool customPT =
                partsType >= outfit::kCustomPartsTypeStart
             && partsType <= outfit::kCustomPartsTypeEnd;
            const bool customSel =
                camoType >= outfit::kCustomSelectorStart
             && camoType <= outfit::kCustomSelectorEnd;


            const bool brokenCustom = (partsType == 0x00 && camoType == 0xFF);

            isCustomOutfitEquip = customPT || customSel || brokenCustom;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitSuitConditionApply:SetInitial] SEH reading info — "
                "passing through to orig untouched\n");
            g_OrigSetInitial(self, info, preserve);
            return;
        }

        if (isCustomOutfitEquip && preserve == 0)
        {
            Log("[OutfitSuitConditionApply:SetInitial] custom-outfit equip "
                "detected (partsType=0x%02X camo=0x%02X flags=0x%X) — "
                "spoofing preserve=1 to suppress slot-clear and keep "
                "the player's weapon-slot loadout intact\n",
                static_cast<unsigned>(partsType),
                static_cast<unsigned>(camoType),
                flags);

            g_OrigSetInitial(self, info, 1);
            return;
        }


        g_OrigSetInitial(self, info, preserve);
    }
}

namespace outfit
{
    bool ForceLiveSuitReload(std::uint8_t playerType,
                             std::uint8_t partsType,
                             std::uint8_t selectorCode,
                             std::uint8_t variantIndex)
    {
        if (!g_OrigReqLoadout)
        {
            Log("[OutfitSuitConditionApply] ForceLiveSuitReload: "
                "ReqLoadout trampoline not yet captured (hook not "
                "installed?) — skipping\n");
            return false;
        }


        alignas(8) std::uint8_t info[256] = {};

        info[kInfoOff_PartsType] = partsType;
        info[kInfoOff_CamoType]  = selectorCode;


        info[0x02] = variantIndex;


        (void)playerType;


        *reinterpret_cast<std::uint32_t*>(info + kInfoOff_Flags) =
            0x001u;

        Log("[OutfitSuitConditionApply] ForceLiveSuitReload: "
            "playerType=%u partsType=0x%02X selector=0x%02X variant=%u "
            "— calling 3-arg ReqLoadout trampoline (orig will dispatch "
            "vtable[0x140] → SetSuit → engine schedules natural "
            "LoadPartsNew ~400ms later)\n",
            static_cast<unsigned>(playerType),
            static_cast<unsigned>(partsType),
            static_cast<unsigned>(selectorCode),
            static_cast<unsigned>(variantIndex));

        __try
        {


            g_OrigReqLoadout(nullptr, info, 1);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitSuitConditionApply] ForceLiveSuitReload: SEH "
                "calling ReqLoadout trampoline\n");
            return false;
        }
    }

    bool Install_OutfitSuitConditionApply_Hook()
    {
        if (!g_Installed)
        {
            void* target = ResolveGameAddress(
                gAddr.Player2UtilityImpl_SetSuitAndHandConditionWithLoadoutInfo);
            if (target)
            {
                g_Installed = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkSetSuit),
                    reinterpret_cast<void**>(&g_Orig));
                Log("[OutfitSuitConditionApply] SetSuit installed: %s (target=%p)\n",
                    g_Installed ? "OK" : "FAIL", target);
            }
            else
            {
                Log("[OutfitSuitConditionApply] SetSuit target unresolved\n");
            }
        }

        if (!g_InstalledReqLoadout)
        {
            void* target = ResolveGameAddress(
                gAddr.Player2UtilityImpl_CommitWrapper);
            if (target)
            {
                g_InstalledReqLoadout = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkReqLoadout),
                    reinterpret_cast<void**>(&g_OrigReqLoadout));
                Log("[OutfitSuitConditionApply] ReqLoadout installed: %s (target=%p)\n",
                    g_InstalledReqLoadout ? "OK" : "FAIL", target);
            }
            else
            {
                Log("[OutfitSuitConditionApply] ReqLoadout target unresolved\n");
            }
        }

        if (!g_InstalledLoadoutApply)
        {
            void* target = ResolveGameAddress(
                gAddr.Player2UtilityImpl_LoadoutApplyAfterSetSuit);
            if (target)
            {
                g_InstalledLoadoutApply = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkLoadoutApplyAfterSetSuit),
                    reinterpret_cast<void**>(&g_OrigLoadoutApply));
                Log("[OutfitSuitConditionApply] LoadoutApplyAfterSetSuit "
                    "installed: %s (target=%p)\n",
                    g_InstalledLoadoutApply ? "OK" : "FAIL", target);
            }
            else
            {
                Log("[OutfitSuitConditionApply] LoadoutApplyAfterSetSuit "
                    "target unresolved\n");
            }
        }

        if (!g_InstalledSetInitial)
        {
            void* target = ResolveGameAddress(
                gAddr.Player2UtilityImpl_SetInitialConditionWithLoadoutInfo);
            if (target)
            {
                g_InstalledSetInitial = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkSetInitialConditionWithLoadoutInfo),
                    reinterpret_cast<void**>(&g_OrigSetInitial));
                Log("[OutfitSuitConditionApply] SetInitialConditionWithLoadoutInfo "
                    "installed: %s (target=%p)\n",
                    g_InstalledSetInitial ? "OK" : "FAIL", target);
            }
            else
            {
                Log("[OutfitSuitConditionApply] SetInitialConditionWithLoadoutInfo "
                    "target unresolved\n");
            }
        }

        return g_Installed || g_InstalledReqLoadout
            || g_InstalledLoadoutApply || g_InstalledSetInitial;
    }

    void Uninstall_OutfitSuitConditionApply_Hook()
    {
        if (g_Installed)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.Player2UtilityImpl_SetSuitAndHandConditionWithLoadoutInfo))
                DisableAndRemoveHook(t);
            g_Orig      = nullptr;
            g_Installed = false;
        }
        if (g_InstalledReqLoadout)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.Player2UtilityImpl_CommitWrapper))
                DisableAndRemoveHook(t);
            g_OrigReqLoadout      = nullptr;
            g_InstalledReqLoadout = false;
        }
        if (g_InstalledLoadoutApply)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.Player2UtilityImpl_LoadoutApplyAfterSetSuit))
                DisableAndRemoveHook(t);
            g_OrigLoadoutApply      = nullptr;
            g_InstalledLoadoutApply = false;
        }
        if (g_InstalledSetInitial)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.Player2UtilityImpl_SetInitialConditionWithLoadoutInfo))
                DisableAndRemoveHook(t);
            g_OrigSetInitial      = nullptr;
            g_InstalledSetInitial = false;
        }
        Log("[OutfitSuitConditionApply] removed\n");
    }
}
