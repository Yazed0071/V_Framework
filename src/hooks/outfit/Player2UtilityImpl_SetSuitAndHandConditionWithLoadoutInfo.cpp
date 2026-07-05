#include "pch.h"

#include "Player2UtilityImpl_SetSuitAndHandConditionWithLoadoutInfo.h"
#include "OutfitRegistry.h"
#include "CustomHeadRegistry.h"

#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{


    using SetSuitAndHandConditionWithLoadoutInfo_t =
        void (__fastcall*)(void* self, void* loadoutInfo);

    static SetSuitAndHandConditionWithLoadoutInfo_t g_Orig = nullptr;
    static bool g_Installed = false;




    using LoadoutApplyAfterSetSuit_t =
        void (__fastcall*)(void* self, void* loadoutInfo);

    static LoadoutApplyAfterSetSuit_t g_OrigLoadoutApply    = nullptr;
    static bool                       g_InstalledLoadoutApply = false;


    using SetInitialConditionWithLoadoutInfo_t =
        void (__fastcall*)(void* self, void* loadoutInfo, std::uint8_t preserve);

    static SetInitialConditionWithLoadoutInfo_t g_OrigSetInitial    = nullptr;
    static bool                                 g_InstalledSetInitial = false;

    static bool                                 g_HaveCapturedSuit   = false;
    static void*                                g_CapturedSuitSelf   = nullptr;
    alignas(8) static std::uint8_t              g_CapturedSuitInfo[256] = {};
    static bool                                 g_InForcedReload     = false;
    static bool                                 g_HaveCapturedApply  = false;
    static void*                                g_CapturedApplySelf  = nullptr;
    constexpr std::size_t kApplyOff_RealizeFlag = 0x190;
    constexpr std::uint32_t kApplyRealizeBit    = 0x8u;


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

#ifdef _DEBUG
        Log("[OutfitSuitConditionApply:%s] fire: partsType=0x%02X "
            "camo=0x%02X playerType=%u flags=0x%X (info=%p)\n",
            tag,
            static_cast<unsigned>(partsType),
            static_cast<unsigned>(camoType),
            static_cast<unsigned>(playerType),
            flags, info);
#endif


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
                            if (slot != 0
                                && (slot < outfit::kCustomHeadSlotBase
                                    || outfit::IsCustomHeadSlot(slot)))
                                outfit::SetWornCustomHeadSlot(slot);
                            else
                                outfit::ClearWornCustomHeadSlot();
#ifdef _DEBUG
                            Log("[OutfitSuitConditionApply:%s] head-option "
                                "rewrite: info[3] = 0x%02X (translated from "
                                "equipId 0x%X via pending stash; live "
                                "partsType=0x%02X is custom and orig "
                                "dropped the click)\n",
                                tag,
                                static_cast<unsigned>(slot),
                                static_cast<unsigned>(pendingHead),
                                static_cast<unsigned>(livePT));
#endif
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

        __try
        {
            const std::uint8_t faceSlot = base[kInfoOff_FaceId];
            if (faceSlot >= outfit::kCustomHeadSlotBase
                && !outfit::IsCustomHeadSlot(faceSlot))
            {
                base[kInfoOff_FaceId] = 0;
                outfit::ClearWornCustomHeadSlot();
                Log("[OutfitSuitConditionApply:%s] dangling custom head slot "
                    "0x%02X not registered — reset to vanilla NONE\n",
                    tag, static_cast<unsigned>(faceSlot));
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        if ((flags & 0x100u) != 0
            && partsType >= outfit::kCustomPartsTypeStart
            && partsType <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* cur = nullptr;
            const bool curSupports =
                outfit::TryGetOutfitByPartsType(partsType, &cur) && cur
                && cur->IsPlayerTypeSupported(playerType);
            if (!curSupports)
            {
                std::uint8_t remParts = 0, remSel = 0;
                const outfit::OutfitEntry* rem = nullptr;
                const bool haveRemembered =
                    outfit::GetRememberedPlayerTypeOutfit(playerType, &remParts, &remSel)
                    && remParts != partsType
                    && outfit::TryGetOutfitByPartsType(remParts, &rem) && rem
                    && rem->IsPlayerTypeSupported(playerType);
                if (haveRemembered)
                {
                    __try
                    {
                        base[kInfoOff_PartsType] = remParts;
                        base[kInfoOff_CamoType]  = remSel;
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER) {}
                    outfit::WriteLivePlayerOutfit(remParts, remSel, playerType);
#ifdef _DEBUG
                    Log("[OutfitSuitConditionApply:%s] PT-RESTORE: partsType=0x%02X "
                        "unsupported for pt=%u — restored this PT's last outfit "
                        "0x%02X selector=0x%02X (real equip)\n",
                        tag, static_cast<unsigned>(partsType),
                        static_cast<unsigned>(playerType),
                        static_cast<unsigned>(remParts), static_cast<unsigned>(remSel));
#endif
                    partsType = remParts;
                    camoType  = remSel;
                }
                else if (playerType == outfit::kPlayerType_DDMale
                      || playerType == outfit::kPlayerType_DDFemale)
                {
                    __try
                    {
                        base[kInfoOff_PartsType] = 0x00;
                        base[kInfoOff_CamoType]  = 0x00;
                        base[kInfoOff_FaceId]    = 0x00;
                        *reinterpret_cast<std::uint32_t*>(base + kInfoOff_Flags)
                            |= 0x80u;
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER) {}
                    outfit::WriteLivePlayerOutfit(0x00, 0x00, playerType);
                    outfit::WriteLiveHeadSlot(0);
                    outfit::ClearWornCustomHeadSlot();
#ifdef _DEBUG
                    Log("[OutfitSuitConditionApply:%s] PT-RELEASE: partsType=0x%02X "
                        "unsupported for pt=%u, nothing remembered — released to "
                        "vanilla default 0x00/0x00 + head cleared (matches vanilla "
                        "cross-body)\n",
                        tag, static_cast<unsigned>(partsType),
                        static_cast<unsigned>(playerType));
#endif
                    partsType = 0x00;
                    camoType  = 0x00;
                }
            }
        }

        __try
        {
            const std::uint8_t worn = base[kInfoOff_FaceId];
            if (partsType != 0x00
                && worn >= outfit::kCustomHeadSlotBase
                && outfit::IsCustomHeadSlot(worn))
            {
                const std::uint8_t gatePT =
                    ((flags & 0x100u) != 0) ? playerType : outfit::ReadLivePlayerType();
                const outfit::CustomHeadEntry* head =
                    outfit::TryGetCustomHeadBySlot(worn);
                const outfit::OutfitEntry* oe = nullptr;
                if (partsType >= outfit::kCustomPartsTypeStart
                    && partsType <= outfit::kCustomPartsTypeEnd)
                {
                    outfit::TryGetOutfitByPartsType(partsType, &oe);
                }
                else if (camoType >= outfit::kCustomSelectorStart
                         && camoType <= outfit::kCustomSelectorEnd)
                {
                    std::uint8_t vi = 0;
                    outfit::TryGetOutfitByVariantSelector(camoType, &oe, &vi);
                }
                const bool offered =
                    head && oe
                    && oe->HasHeadOptionAnyVariant(head->equipId, gatePT);
                if (!offered)
                {
                    base[kInfoOff_FaceId] = 0;
                    outfit::WriteLiveHeadSlot(0);
                    *reinterpret_cast<std::uint32_t*>(base + kInfoOff_Flags) |= 0x80u;
#ifdef _DEBUG
                    Log("[OutfitSuitConditionApply:%s] HEAD-GATE: worn custom head "
                        "slot 0x%02X not offered by current outfit "
                        "(partsType=0x%02X pt=%u) — dropped + head-apply flag set\n",
                        tag, static_cast<unsigned>(worn),
                        static_cast<unsigned>(partsType),
                        static_cast<unsigned>(gatePT));
#endif
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

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
            __try
            {
                std::uint8_t worn = base[kInfoOff_FaceId];
                if (worn == 0) worn = outfit::GetWornCustomHeadSlot();
                if (worn >= outfit::kCustomHeadSlotBase
                    && outfit::IsCustomHeadSlot(worn))
                {
                    base[kInfoOff_FaceId] = 0;
                    *reinterpret_cast<std::uint32_t*>(base + kInfoOff_Flags) |= 0x80u;
                    outfit::WriteLiveHeadSlot(0);
                    outfit::ClearWornCustomHeadSlot();
#ifdef _DEBUG
                    Log("[OutfitSuitConditionApply:%s] head-sync: vanilla suit "
                        "apply — dropped worn custom head slot 0x%02X + cleared "
                        "tracker\n", tag, static_cast<unsigned>(worn));
#endif
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}


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

            const bool danglingPT =
                partsType >= outfit::kCustomPartsTypeStart
             && partsType <= outfit::kCustomPartsTypeEnd;
            const bool danglingSel =
                camoType >= outfit::kCustomSelectorStart
             && camoType <= outfit::kCustomSelectorEnd;
            if (danglingPT || danglingSel)
            {
                __try
                {
                    base[kInfoOff_PartsType] = 0x00;
                    base[kInfoOff_CamoType]  = 0x00;
                    Log("[OutfitSuitConditionApply:%s] BRICK-GUARD: dangling "
                        "custom suit partsType=0x%02X selector=0x%02X not "
                        "registered — scrubbed to vanilla NORMAL\n",
                        tag, static_cast<unsigned>(partsType),
                        static_cast<unsigned>(camoType));
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
                return true;
            }

            {
                const std::uint8_t liveParts = outfit::ReadLivePartsType();
                if (liveParts >= outfit::kCustomPartsTypeStart
                 && liveParts <= outfit::kCustomPartsTypeEnd)
                {
                    outfit::WriteLivePlayerOutfit(
                        partsType, camoType, outfit::ReadLivePlayerType());
                    Log("[OutfitSuitConditionApply:%s] vanilla suit "
                        "(partsType=0x%02X camo=0x%02X) picked while live override "
                        "was custom 0x%02X — released override to engine "
                        "(anti-brick)\n",
                        tag, static_cast<unsigned>(partsType),
                        static_cast<unsigned>(camoType),
                        static_cast<unsigned>(liveParts));
                }
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
            const bool clearMismatch = canCheckPT
                                    && !chosen->IsPlayerTypeSupported(effectivePT);


            if (clearMismatch)
            {


                base[kInfoOff_PartsType] = 0x00;
                base[kInfoOff_CamoType]  = 0x00;
                {
                    std::uint8_t worn = base[kInfoOff_FaceId];
                    if (worn == 0) worn = outfit::GetWornCustomHeadSlot();
                    if (worn >= outfit::kCustomHeadSlotBase
                        && outfit::IsCustomHeadSlot(worn))
                    {
                        base[kInfoOff_FaceId] = 0;
                        *reinterpret_cast<std::uint32_t*>(base + kInfoOff_Flags) |= 0x80u;
                        outfit::WriteLiveHeadSlot(0);
                        outfit::ClearWornCustomHeadSlot();
                    }
                }

                Log("[OutfitSuitConditionApply:%s] playerType not supported "
                    "(effective=%u via=%s developId=%u; "
                    "livePT=%u info[0xC0]=%u flags=0x%X 0x100=%s) — applied "
                    "vanilla NORMAL upfront\n",
                    tag,
                    static_cast<unsigned>(effectivePT),
                    via,
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

            {
                std::uint8_t worn = base[kInfoOff_FaceId];
                if (worn == 0)
                    worn = outfit::GetWornCustomHeadSlot();

                const bool wornIsCustom =
                    worn >= outfit::kCustomHeadSlotBase && outfit::IsCustomHeadSlot(worn);
                const bool wornIsVanilla = (worn >= 1 && worn <= 5);

                std::uint16_t wornEquipId = 0;
                if (wornIsCustom)
                {
                    if (const outfit::CustomHeadEntry* head =
                            outfit::TryGetCustomHeadBySlot(worn))
                        wornEquipId = head->equipId;
                }
                else if (wornIsVanilla)
                {
                    wornEquipId = static_cast<std::uint16_t>(worn + 0x20D);
                }

                if (wornEquipId != 0)
                {
                    const bool offered =
                        chosen->HasHeadOptionAnyVariant(wornEquipId, effectivePT);
                    if (!offered)
                    {
                        base[kInfoOff_FaceId] = 0;
                        *reinterpret_cast<std::uint32_t*>(base + kInfoOff_Flags) |= 0x80u;
                        outfit::WriteLiveHeadSlot(0);
                        outfit::ClearWornCustomHeadSlot();
#ifdef _DEBUG
                        Log("[OutfitSuitConditionApply:%s] head-sync: worn head slot "
                            "0x%02X (equipId 0x%X, %s) NOT in headOptions of "
                            "partsType=0x%02X (pt=%u) — dropped\n",
                            tag, static_cast<unsigned>(worn),
                            static_cast<unsigned>(wornEquipId),
                            wornIsCustom ? "custom" : "vanilla",
                            static_cast<unsigned>(chosen->partsType),
                            static_cast<unsigned>(effectivePT));
#endif
                    }
                    else if (wornIsCustom)
                    {
                        base[kInfoOff_FaceId] = worn;
                        *reinterpret_cast<std::uint32_t*>(base + kInfoOff_Flags) |= 0x80u;
                        outfit::SetWornCustomHeadSlot(worn);
#ifdef _DEBUG
                        Log("[OutfitSuitConditionApply:%s] head-sync: re-applied "
                            "worn custom head slot 0x%02X onto partsType=0x%02X "
                            "(offered; pt=%u)\n",
                            tag, static_cast<unsigned>(worn),
                            static_cast<unsigned>(chosen->partsType),
                            static_cast<unsigned>(effectivePT));
#endif
                    }
                    else if (wornIsVanilla)
                    {
                        if (base[kInfoOff_FaceId] != worn)
                        {
                            base[kInfoOff_FaceId] = worn;
                            *reinterpret_cast<std::uint32_t*>(
                                base + kInfoOff_Flags) |= 0x80u;
#ifdef _DEBUG
                            Log("[OutfitSuitConditionApply:%s] head-sync: "
                                "re-applied worn VANILLA head slot 0x%02X onto "
                                "partsType=0x%02X (offered; pt=%u)\n",
                                tag, static_cast<unsigned>(worn),
                                static_cast<unsigned>(chosen->partsType),
                                static_cast<unsigned>(effectivePT));
#endif
                        }
                        outfit::SetWornCustomHeadSlot(worn);
                    }
                }
            }

#ifdef _DEBUG
            Log("[OutfitSuitConditionApply:%s] rewrote loadout (via %s) "
                "-> developId=%u partsType=0x%02X selector=0x%02X "
                "variant=%u (effective=%u; livePT=%u info[0xC0]=%u "
                "flags=0x%X 0x100=%s)\n",
                tag, via,
                static_cast<unsigned>(chosen->developId),
                static_cast<unsigned>(chosen->partsType),
                static_cast<unsigned>(chosen->selectorCode),
                static_cast<unsigned>(base[kInfoOff_Variant]),
                static_cast<unsigned>(effectivePT),
                static_cast<unsigned>(livePT),
                static_cast<unsigned>(playerType),
                flags,
                playerTypeValid ? "set" : "unset");
#endif
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

        if (!g_InForcedReload && info)
        {
            __try
            {
                const std::uint8_t pt =
                    reinterpret_cast<std::uint8_t*>(info)[kInfoOff_PartsType];
                if (pt >= outfit::kCustomPartsTypeStart
                 && pt <= outfit::kCustomPartsTypeEnd)
                {
                    std::memcpy(g_CapturedSuitInfo, info, sizeof(g_CapturedSuitInfo));
                    g_HaveCapturedSuit = true;
#ifdef _DEBUG
                    Log("[OutfitSuitConditionApply:SetSuit] captured POST-rewrite "
                        "custom descriptor for arm replay (partsType=0x%02X)\n",
                        static_cast<unsigned>(pt));
#endif
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        g_Orig(self, info);
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
#ifdef _DEBUG
            Log("[OutfitLoadoutPreserve] SUPPRESSING slot-apply: custom partsType=0x%02X "
                "flags=0x%X\n",
                static_cast<unsigned>(partsType),
                flags);
#endif

            if (!g_InForcedReload && self)
            {
                g_CapturedApplySelf = self;
                g_HaveCapturedApply = true;
            }

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

        if (isCustomOutfitEquip && !g_InForcedReload && self)
        {
            g_CapturedSuitSelf = self;
#ifdef _DEBUG
            Log("[OutfitSuitConditionApply:SetInitial] captured loadout self=%p "
                "(descriptor captured post-rewrite in SetSuit)\n", self);
#endif
        }

        if (isCustomOutfitEquip && preserve == 0)
        {
#ifdef _DEBUG
            Log("[OutfitSuitConditionApply:SetInitial] custom-outfit equip "
                "(partsType=0x%02X camo=0x%02X flags=0x%X) — spoofing "
                "preserve=1\n",
                static_cast<unsigned>(partsType),
                static_cast<unsigned>(camoType),
                flags);
#endif

            g_OrigSetInitial(self, info, 1);
            return;
        }


        g_OrigSetInitial(self, info, preserve);
    }
}

namespace outfit
{

    bool ReplayCapturedSuitEquip()
    {
        if (g_InForcedReload) return false;
        if (!g_HaveCapturedSuit || !g_Orig || !g_CapturedSuitSelf)
        {
#ifdef _DEBUG
            Log("[OutfitSuitConditionApply] ReplayCapturedSuitEquip: no captured "
                "custom-suit descriptor yet (equip a custom suit once first) — "
                "skipping arm reload\n");
#endif
            return false;
        }

#ifdef _DEBUG
        Log("[OutfitSuitConditionApply] ReplayCapturedSuitEquip: re-realizing via "
            "full SetSuit (self=%p)\n", g_CapturedSuitSelf);
#endif

        g_InForcedReload = true;
        bool ok = false;
        __try
        {
            g_Orig(g_CapturedSuitSelf, g_CapturedSuitInfo);
            ok = true;
#ifdef _DEBUG
            Log("[OutfitSuitConditionApply] ReplayCapturedSuitEquip: SetSuit realize "
                "issued -> engine re-realizes the custom outfit + new arm\n");
#endif
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitSuitConditionApply] ReplayCapturedSuitEquip: SEH calling "
                "SetInitialConditionWithLoadoutInfo — replay aborted (no crash); "
                "dropping the captured descriptor\n");
            g_HaveCapturedSuit = false;
            g_CapturedSuitSelf = nullptr;
        }
        g_InForcedReload = false;
        return ok;
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
#ifdef _DEBUG
                Log("[OutfitSuitConditionApply] SetSuit installed: %s (target=%p)\n",
                    g_Installed ? "OK" : "FAIL", target);
#endif
            }
            else
            {
                Log("[OutfitSuitConditionApply] SetSuit target unresolved\n");
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
#ifdef _DEBUG
                Log("[OutfitSuitConditionApply] LoadoutApplyAfterSetSuit "
                    "installed: %s (target=%p)\n",
                    g_InstalledLoadoutApply ? "OK" : "FAIL", target);
#endif
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
#ifdef _DEBUG
                Log("[OutfitSuitConditionApply] SetInitialConditionWithLoadoutInfo "
                    "installed: %s (target=%p)\n",
                    g_InstalledSetInitial ? "OK" : "FAIL", target);
#endif
            }
            else
            {
                Log("[OutfitSuitConditionApply] SetInitialConditionWithLoadoutInfo "
                    "target unresolved\n");
            }
        }

        return g_Installed
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
#ifdef _DEBUG
        Log("[OutfitSuitConditionApply] removed\n");
#endif
    }
}
