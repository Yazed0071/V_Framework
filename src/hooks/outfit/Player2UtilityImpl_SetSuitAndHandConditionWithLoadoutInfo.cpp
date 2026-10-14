#include "pch.h"

#include "Player2UtilityImpl_SetSuitAndHandConditionWithLoadoutInfo.h"
#include "OutfitRegistry.h"
#include "CustomHeadRegistry.h"

#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

static inline bool V_WriteOutfitSetSuit(std::uint8_t p, std::uint8_t c,
                                  std::uint8_t pt)
{
    return outfit::WriteLivePlayerOutfit(
        p, c, pt, outfit::OutfitWriteSource::SetSuit);
}
#include "MissionCodeGuard.h"
#include "UniqueCharacterPartsTypePin.h"
#include "UniqueCharacterDefaultOutfit.h"
#include "../player/FobPlayerCharacters.h"

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

    constexpr std::size_t kApplyOff_RealizeFlag = 0x190;
    constexpr std::uint32_t kApplyRealizeBit    = 0x8u;


    constexpr std::size_t kInfoOff_PartsType  = 0x00;
    constexpr std::size_t kInfoOff_CamoType   = 0x01;
    constexpr std::size_t kInfoOff_Variant    = 0x02;
    constexpr std::size_t kInfoOff_FaceId     = 0x03;
    constexpr std::size_t kInfoOff_Flags      = 0xBC;
    constexpr std::size_t kInfoOff_PlayerType = 0xC0;


    static bool InspectAndRewriteLoadout(void* info)
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
            LogWarn("OutfitSuitConditionApply: loadout read faulted, custom outfit not applied");
            return false;
        }

        fobchars::NoteLoadoutPlayerType(playerType, flags);


        if ((flags & 0x80u) != 0)
        {
            __try
            {
                const std::uint8_t curFaceSlot = base[kInfoOff_FaceId];
                if (curFaceSlot != 0)
                {
                    outfit::ClearPendingHeadOptionEquipId();
                }
                else
                {
                    const std::uint8_t livePT   = outfit::ReadLivePartsType();
                    const std::uint8_t liveType = outfit::ReadLivePlayerType();
                    const bool liveIsCustom =
                        (livePT >= outfit::kCustomPartsTypeStart
                         && livePT <= outfit::kCustomPartsTypeEnd);
                    const bool liveIsVanillaExt =
                        livePT < outfit::kCustomPartsTypeStart
                        && outfit::VanillaExtHasAnyHeadOptions(
                               livePT, liveType, outfit::ReadLiveSelectorCode());
                    bool headClickConsumed = false;
                    if (liveIsCustom || liveIsVanillaExt)
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
                            else if (pendingHead == 0x20E)
                            {
                                slot = 1;
                            }
                            else if (pendingHead == 0x20F)
                            {
                                slot = 2;
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
                                    if (liveIsVanillaExt
                                        && !outfit::VanillaExtHasHeadOption(
                                               livePT, pendingHead, liveType,
                                               outfit::ReadLiveSelectorCode()))
                                        slot = 0;
                                }
                                else
                                {
                                    slot = 0;
                                    LogWarn("OutfitSuitConditionApply: head equipId=0x%X not registered, head set to none",
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
                            headClickConsumed = (flags & 0x1u) == 0;
                            if (slot != 0
                                && (slot < outfit::kCustomHeadSlotBase
                                    || outfit::IsCustomHeadSlot(slot)))
                                outfit::SetWornCustomHeadSlot(slot);
                            else
                                outfit::ClearWornCustomHeadSlot();
                        }
                    }

                    if (!headClickConsumed
                        && outfit::GetPendingHeadOptionEquipId() == 0)
                    {
                        const std::uint8_t wornSlot =
                            static_cast<std::uint8_t>(
                                outfit::ReadLiveWornHeadCategory() & 0xFFu);

                        std::uint16_t wornEquipId = 0;
                        if (wornSlot != 0)
                        {
                            if (outfit::IsCustomHeadSlot(wornSlot))
                            {
                                if (const auto* worn =
                                    outfit::TryGetCustomHeadBySlot(wornSlot))
                                    wornEquipId = worn->equipId;
                            }
                            else if (wornSlot <= 5)
                            {
                                wornEquipId = static_cast<std::uint16_t>(
                                    0x20D + wornSlot);
                            }
                        }

                        const std::uint8_t livePlayerType =
                            outfit::ReadLivePlayerType();
                        const std::uint8_t carryPT =
                            ((flags & 0x100u) != 0 || livePlayerType == 0xFF)
                                ? playerType
                                : livePlayerType;

                        const bool carrySuit = (flags & 0x1u) != 0;
                        std::uint8_t carryParts = partsType;
                        std::uint8_t carryCamo =
                            carrySuit ? camoType : outfit::ReadLiveSelectorCode();
                        const outfit::OutfitEntry* carryOutfit = nullptr;
                        std::uint8_t carryVExtPt = 0, carryVExtIdx = 0;
                        if (carrySuit
                            && outfit::TryGetVanillaExtByVariantSelector(
                                   camoType, &carryVExtPt, &carryVExtIdx))
                        {
                            carryParts = carryVExtPt;
                            const std::uint8_t srcCamo =
                                outfit::VanillaExtGetVariantSourceCamo(
                                    carryVExtPt, carryVExtIdx);
                            if (srcCamo != 0xFF)
                                carryCamo = srcCamo;
                        }
                        else if (partsType >= outfit::kCustomPartsTypeStart
                                 && partsType <= outfit::kCustomPartsTypeEnd)
                        {
                            outfit::TryGetOutfitByPartsType(partsType,
                                                            &carryOutfit);
                        }
                        else if (carrySuit
                                 && camoType >= outfit::kCustomSelectorStart
                                 && camoType <= outfit::kCustomSelectorEnd)
                        {
                            std::uint8_t carryVariantIdx = 0;
                            outfit::TryGetOutfitByVariantSelector(
                                camoType, &carryOutfit, &carryVariantIdx);
                        }

                        bool targetDeclares = false;
                        if (wornEquipId != 0)
                        {
                            if (carryOutfit)
                            {
                                targetDeclares =
                                    carryOutfit->HasHeadOptionAnyVariant(
                                        wornEquipId, carryPT);
                            }
                            else if (carryParts < outfit::kCustomPartsTypeStart)
                            {
                                targetDeclares = outfit::VanillaExtHasHeadOption(
                                    carryParts, wornEquipId, carryPT,
                                    carryCamo);
                            }
                        }

                        if (targetDeclares)
                        {
                            base[kInfoOff_FaceId]     = wornSlot;
                            base[kInfoOff_FaceId + 1] = 0;
                            outfit::SetWornCustomHeadSlot(wornSlot);
                        }
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                LogWarn("OutfitSuitConditionApply: head option rewrite faulted, head keeps vanilla pick");
            }
        }

        __try
        {
            const std::uint8_t faceSlot = base[kInfoOff_FaceId];
            if (faceSlot >= outfit::kCustomHeadSlotBase
                && !outfit::IsCustomHeadSlot(faceSlot))
            {
                outfit::DrainPendingHeads();

                const bool nowRegistered =
                    outfit::IsCustomHeadSlot(faceSlot);

                if (!nowRegistered && !outfit::IsCustomHeadSlotReserved(faceSlot))
                {
                    base[kInfoOff_FaceId] = 0;
                    outfit::ClearWornCustomHeadSlot();
                    LogWarn("OutfitSuitConditionApply: worn custom head slot 0x%02X not registered, head set to none",
                        static_cast<unsigned>(faceSlot));
                }
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
                    V_WriteOutfitSetSuit(remParts, remSel, playerType);
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
                    V_WriteOutfitSetSuit(0x00, 0x00, playerType);
                    outfit::ClearWornCustomHeadSlot();
                    partsType = 0x00;
                    camoType  = 0x00;
                }
            }
        }

        __try
        {
            const std::uint8_t worn = base[kInfoOff_FaceId];
            if ((flags & 0x1u) != 0
                && partsType != 0x00
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
                    head
                    && (outfit::IsUniqueCharacterPlayerType(gatePT)
                        || (oe && oe->HasHeadOptionAnyVariant(head->equipId, gatePT))
                        || (!oe
                            && partsType < outfit::kCustomPartsTypeStart
                            && outfit::VanillaExtHasHeadOption(
                                   partsType, head->equipId, gatePT,
                                   outfit::kHeadOptionAnyCamo)));
                if (!offered)
                {
                    base[kInfoOff_FaceId] = 0;
                    *reinterpret_cast<std::uint32_t*>(base + kInfoOff_Flags) |= 0x80u;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        const bool applySuit = (flags & 0x1u) != 0;
        if (!applySuit) return false;

        {
            std::uint8_t vExtPt = 0, vExtIdx = 0;
            if (outfit::TryGetVanillaExtByVariantSelector(camoType, &vExtPt,
                                                          &vExtIdx))
            {
                outfit::ResetAllVanillaExtVariants(vExtPt);
                outfit::SetActiveVariant(vExtPt, vExtIdx);

                if (partsType != vExtPt)
                {
                    __try
                    {
                        base[kInfoOff_PartsType] = vExtPt;
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER) {}
                    partsType = vExtPt;
                }
                {
                    const std::uint8_t vExtSrcCamo =
                        outfit::VanillaExtGetVariantSourceCamo(vExtPt, vExtIdx);
                    if (vExtSrcCamo != 0xFF)
                    {
                        __try
                        {
                            base[kInfoOff_CamoType] = vExtSrcCamo;
                        }
                        __except (EXCEPTION_EXECUTE_HANDLER) {}
                    }
                }
                return true;
            }
            outfit::ResetAllVanillaExtVariants();
        }

        const outfit::OutfitEntry* chosen = nullptr;


        if (partsType >= outfit::kCustomPartsTypeStart
         && partsType <= outfit::kCustomPartsTypeEnd)
        {
            outfit::TryGetOutfitByPartsType(partsType, &chosen);
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
                resolvedVariantIdx   = vi;
                resolvedVariantValid = true;
            }
        }


        if (!chosen && partsType == 0x00 && camoType == 0xFF)
        {
            const std::uint16_t pendingDevId = outfit::GetPendingOutfitDevelopId();
            if (pendingDevId != 0)
            {
                if (!outfit::IsOutfitBound(pendingDevId))
                    outfit::BindOutfit(pendingDevId, true, "loadout-apply");
                const outfit::OutfitEntry* byPending = nullptr;
                if (outfit::TryGetOutfitByDevelopId(pendingDevId, &byPending)
                    && byPending && byPending->bound)
                {
                    chosen = byPending;
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
                if (!outfit::IsOutfitBound(pendingDevId))
                    outfit::BindOutfit(pendingDevId, true, "loadout-apply");
                const outfit::OutfitEntry* byPending = nullptr;
                if (outfit::TryGetOutfitByDevelopId(pendingDevId, &byPending)
                    && byPending && byPending->bound)
                {
                    chosen = byPending;
                    outfit::ClearPendingOutfitDevelopId();
                }
            }
        }


        if (!chosen && !MissionCodeGuard::ShouldBypassHooks())
        {
            const std::uint8_t liveParts = outfit::ReadLivePartsType();
            const bool liveIsCustom =
                liveParts >= outfit::kCustomPartsTypeStart
             && liveParts <= outfit::kCustomPartsTypeEnd;

            std::uint8_t pt = 0xFF;
            if ((flags & 0x100u) != 0
                && outfit::IsUniqueCharacterPlayerType(playerType))
                pt = playerType;
            else
            {
                const std::uint8_t livePT = outfit::ReadLivePlayerType();
                if (outfit::IsUniqueCharacterPlayerType(livePT))
                    pt = livePT;
                else
                {
                    const std::uint8_t sel = fobchars::GetSelectedPlayerType();
                    if (outfit::IsUniqueCharacterPlayerType(sel))
                        pt = sel;
                }
            }

            if (!liveIsCustom && pt != 0xFF)
            {
                const bool ocelot = (pt == outfit::kPlayerType_Ocelot);
                const std::uint8_t pinParts = ocelot ? 0x1Au : 0x1Bu;
                const std::uint8_t pinCamo  = ocelot ? 0x73u : 0x74u;

                const bool descriptorIsVanillaPin =
                    (partsType == 0x00u || partsType == pinParts)
                 && (camoType  == 0x00u || camoType  == pinCamo
                     || camoType == 0xFFu);

                std::uint8_t remParts = 0;
                std::uint8_t remSel   = 0;
                if (descriptorIsVanillaPin
                 && uniquecharpin::TryGetRemembered(pt, &remParts, &remSel))
                {
                    const outfit::OutfitEntry* byRemembered = nullptr;
                    if (outfit::TryGetOutfitByPartsType(remParts, &byRemembered)
                     && byRemembered && byRemembered->bound
                     && byRemembered->IsPlayerTypeSupported(pt))
                    {
                        chosen = byRemembered;
                    }
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
                    const std::uint8_t syncPT =
                        ((flags & 0x100u) != 0) ? playerType
                                                : outfit::ReadLivePlayerType();
                    const outfit::CustomHeadEntry* head =
                        outfit::TryGetCustomHeadBySlot(worn);
                    const bool syncPTKnown =
                        syncPT < outfit::kPlayerTypeMax
                        && !outfit::IsUniqueCharacterPlayerType(syncPT);
                    if (head
                        && (!syncPTKnown
                            || outfit::VanillaExtHasHeadOption(
                                   partsType, head->equipId, syncPT,
                                   outfit::kHeadOptionAnyCamo)))
                    {
                        base[kInfoOff_FaceId] = worn;
                        *reinterpret_cast<std::uint32_t*>(base + kInfoOff_Flags) |= 0x80u;
                        outfit::SetWornCustomHeadSlot(worn);
                    }
                    else
                    {
                        base[kInfoOff_FaceId] = 0;
                        *reinterpret_cast<std::uint32_t*>(base + kInfoOff_Flags) |= 0x80u;
                        outfit::ClearWornCustomHeadSlot();
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}


            if (partsType == 0x00 && camoType == 0xFF)
            {
                __try
                {
                    base[kInfoOff_CamoType] = 0x00;
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
                    LogWarn("OutfitSuitConditionApply: partsType=0x%02X selector=0x%02X not registered, vanilla suit applied",
                        static_cast<unsigned>(partsType),
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
                    uniquecharpin::ForgetCustom(outfit::ReadLivePlayerType());
                    V_WriteOutfitSetSuit(
                        partsType, camoType, outfit::ReadLivePlayerType());
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
                                    && (!chosen->IsPlayerTypeSupported(effectivePT)
                                     || !outfit::DeclaresBranchInPartsGroupOf(
                                            *chosen, effectivePT));


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
                        if (outfit::IsUniqueCharacterPlayerType(effectivePT))
                        {
                            base[kInfoOff_FaceId] = worn;
                            *reinterpret_cast<std::uint32_t*>(base + kInfoOff_Flags) |= 0x80u;
                            outfit::SetWornCustomHeadSlot(worn);
                        }
                        else
                        {
                            base[kInfoOff_FaceId] = 0;
                            *reinterpret_cast<std::uint32_t*>(base + kInfoOff_Flags) |= 0x80u;
                            outfit::ClearWornCustomHeadSlot();
                        }
                    }
                }

                LogWarn("OutfitSuitConditionApply: developId=%u does not support playerType %u, vanilla suit applied",
                    static_cast<unsigned>(chosen->developId),
                    static_cast<unsigned>(effectivePT));
                return true;
            }


            if (!chosen->bound)
            {
                base[kInfoOff_PartsType] = 0x00;
                base[kInfoOff_CamoType]  = 0x00;
                LogWarn("OutfitSuitConditionApply: developId=%u not bound to a partsType, vanilla suit applied",
                    static_cast<unsigned>(chosen->developId));
                return true;
            }

            base[kInfoOff_PartsType] = chosen->partsType;
            base[kInfoOff_CamoType]  = chosen->selectorCode;


            if (resolvedVariantValid)
            {
                base[kInfoOff_Variant] = resolvedVariantIdx;
                outfit::ClearCrateDeliveredVariant();
                outfit::ConsumePendingSupplyDropVariantIdx();
                outfit::ConsumePendingSupplyDropDevelopId();
                outfit::SetActiveVariant(chosen->partsType, resolvedVariantIdx);
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
                        !canCheckPT
                        || (wornIsCustom
                            && outfit::IsUniqueCharacterPlayerType(effectivePT))
                        || chosen->HasHeadOptionAnyVariant(wornEquipId, effectivePT);
                    if (!offered)
                    {
                        base[kInfoOff_FaceId] = 0;
                        *reinterpret_cast<std::uint32_t*>(base + kInfoOff_Flags) |= 0x80u;
                        outfit::ClearWornCustomHeadSlot();
                    }
                    else if (wornIsCustom)
                    {
                        base[kInfoOff_FaceId] = worn;
                        *reinterpret_cast<std::uint32_t*>(base + kInfoOff_Flags) |= 0x80u;
                        outfit::SetWornCustomHeadSlot(worn);
                    }
                    else if (wornIsVanilla)
                    {
                        if (base[kInfoOff_FaceId] != worn)
                        {
                            base[kInfoOff_FaceId] = worn;
                            *reinterpret_cast<std::uint32_t*>(
                                base + kInfoOff_Flags) |= 0x80u;
                        }
                        outfit::SetWornCustomHeadSlot(worn);
                    }
                }
                else
                {
                    outfit::ClearWornCustomHeadSlot();
                }
            }

            {
                static std::atomic<std::uint32_t> s_lastApplyStamp{ 0 };
                const std::uint32_t pendStamp =
                    outfit::GetPendingOutfitDevelopIdStamp();
                const std::uint32_t prevStamp =
                    s_lastApplyStamp.exchange(pendStamp,
                                              std::memory_order_relaxed);

                const std::uint16_t stalePending =
                    outfit::GetPendingOutfitDevelopId();
                if (stalePending != 0 && pendStamp == prevStamp)
                {
                    outfit::ClearPendingOutfitDevelopId();
                }
            }

            outfit::NoteOutfitApplied(chosen->developId);
            uniquecharpin::RememberCustom(
                static_cast<std::uint8_t>(effectivePT),
                chosen->partsType, chosen->selectorCode);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            LogWarn("OutfitSuitConditionApply: loadout rewrite faulted, developId=%u not applied",
                static_cast<unsigned>(chosen->developId));
            return false;
        }
    }

    static void UpdateMotionOutfitHint(void* info)
    {
        if (!info) return;
        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(info);
            const std::uint8_t partsType  = base[kInfoOff_PartsType];
            const std::uint8_t camoType   = base[kInfoOff_CamoType];
            const std::uint8_t playerType = base[kInfoOff_PlayerType];
            if (partsType >= outfit::kCustomPartsTypeStart
                && partsType <= outfit::kCustomPartsTypeEnd)
            {
                outfit::SetMotionOutfitHint(partsType, playerType);
                return;
            }
            if (camoType >= outfit::kCustomSelectorStart
                && camoType <= outfit::kCustomSelectorEnd)
            {
                const outfit::OutfitEntry* entry = nullptr;
                std::uint8_t variantIdx = 0;
                if (outfit::TryGetOutfitByVariantSelector(camoType, &entry,
                                                          &variantIdx)
                    && entry)
                {
                    outfit::SetMotionOutfitHint(entry->partsType, playerType);
                    return;
                }
            }
            outfit::ClearMotionOutfitHint();
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static void __fastcall hkSetSuit(void* self, void* info)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
        {
            if (info)
            {
                __try
                {
                    auto* base = reinterpret_cast<std::uint8_t*>(info);
                    const std::uint8_t fobCamo = base[kInfoOff_CamoType];
                    if (fobCamo >= outfit::kCustomSelectorStart
                        && fobCamo <= outfit::kCustomSelectorEnd)
                    {
                        std::uint8_t vExtPt = 0, vExtIdx = 0;
                        if (outfit::TryGetVanillaExtByVariantSelector(
                                fobCamo, &vExtPt, &vExtIdx))
                        {
                            const std::uint8_t src =
                                outfit::VanillaExtGetVariantSourceCamo(
                                    vExtPt, vExtIdx);
                            base[kInfoOff_PartsType] = vExtPt;
                            if (src != 0xFF)
                                base[kInfoOff_CamoType] = src;
                        }
                    }
                    const std::uint8_t fobParts =
                        base[kInfoOff_PartsType];
                    const std::uint8_t fobSel =
                        base[kInfoOff_CamoType];
                    std::uint8_t fobDefPt = 0;
                    if (uniquedefaultoutfit::TryGetPlayerTypeForBinding(
                            fobParts, fobSel, &fobDefPt)
                        && base[kInfoOff_PlayerType] == fobDefPt)
                    {
                        const bool ocelot =
                            (fobDefPt == outfit::kPlayerType_Ocelot);
                        base[kInfoOff_PartsType] = ocelot ? 0x1Au : 0x1Bu;
                        base[kInfoOff_CamoType]  = ocelot ? 0x73u : 0x74u;
                    }

                    const std::uint8_t fobHead = base[kInfoOff_FaceId];
                    if (fobHead >= outfit::kCustomHeadSlotBase)
                    {
                        base[kInfoOff_FaceId] = 0;
                        *reinterpret_cast<std::uint32_t*>(
                            base + kInfoOff_Flags) |= 0x80u;
                        outfit::ClearWornCustomHeadSlot();
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            if (g_Orig) g_Orig(self, info);
            return;
        }

        InspectAndRewriteLoadout(info);

        UpdateMotionOutfitHint(info);

        g_Orig(self, info);

        if (info)
        {
            __try
            {
                auto* base = reinterpret_cast<std::uint8_t*>(info);
                const std::uint32_t appliedFlags =
                    *reinterpret_cast<std::uint32_t*>(base + kInfoOff_Flags);
                if ((appliedFlags & 0x80u) != 0)
                {
                    const std::uint8_t appliedSlot = base[kInfoOff_FaceId];
                    if (appliedSlot != 0)
                        outfit::SetWornCustomHeadSlot(appliedSlot);
                    else
                        outfit::ClearWornCustomHeadSlot();
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        if (info)
        {
            __try
            {
                const std::uint8_t livePT   = outfit::ReadLivePartsType();
                const std::uint8_t liveType = outfit::ReadLivePlayerType();
                if (livePT < outfit::kCustomPartsTypeStart)
                {
                    auto* base = reinterpret_cast<std::uint8_t*>(info);
                    std::uint8_t slot = base[kInfoOff_FaceId];
                    if (slot < outfit::kCustomHeadSlotBase)
                        slot = outfit::GetWornCustomHeadSlot();

                    if (slot >= outfit::kCustomHeadSlotBase
                        && outfit::IsCustomHeadSlot(slot))
                    {
                        const outfit::CustomHeadEntry* head =
                            outfit::TryGetCustomHeadBySlot(slot);
                        if (head
                            && outfit::VanillaExtHasHeadOption(livePT, head->equipId,
                                                              liveType,
                                                              outfit::ReadLiveSelectorCode())
                            && outfit::ReadLiveWornHeadCategory() != slot)
                        {
                            outfit::WriteLiveWornHeadCategory(slot);
                        }
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }



    static void __fastcall hkLoadoutApplyAfterSetSuit(void* self, void* info)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigLoadoutApply, self, info);

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
            LogWarn("OutfitLoadoutPreserve: loadout read faulted, vanilla slot apply used");
            g_OrigLoadoutApply(self, info);
            return;
        }

        if (shouldSuppressSlotApply)
        {
            if (g_Orig)
                g_Orig(self, info);


            __try
            {
                auto* p1 = reinterpret_cast<std::uint8_t*>(self);
                if (p1)
                {
                    *reinterpret_cast<std::uint32_t*>(p1 + kApplyOff_RealizeFlag)
                        |= kApplyRealizeBit;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                LogWarn("OutfitLoadoutPreserve: realize flag write faulted, custom outfit may not realize");
            }

            return;
        }

        g_OrigLoadoutApply(self, info);
    }


    static void __fastcall hkSetInitialConditionWithLoadoutInfo(
        void* self, void* info, std::uint8_t preserve)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigSetInitial, self, info, preserve);

        if (!info || !g_OrigSetInitial)
        {
            if (g_OrigSetInitial) g_OrigSetInitial(self, info, preserve);
            return;
        }

        std::uint8_t  partsType    = 0;
        std::uint8_t  camoType     = 0;
        bool          isCustomOutfitEquip = false;

        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(info);
            partsType  = base[kInfoOff_PartsType];
            camoType   = base[kInfoOff_CamoType];


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
            LogWarn("OutfitSuitConditionApply: SetInitialCondition loadout read faulted, vanilla apply used");
            g_OrigSetInitial(self, info, preserve);
            return;
        }

        if (isCustomOutfitEquip && preserve == 0)
        {
            g_OrigSetInitial(self, info, 1);
            return;
        }


        g_OrigSetInitial(self, info, preserve);
    }
}

namespace outfit
{

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
                if (!g_Installed)
                    LogError("OutfitSuitConditionApply: SetSuitAndHandCondition hook failed, custom outfits not applied");
            }
            else
            {
                LogError("OutfitSuitConditionApply: SetSuitAndHandCondition address missing, custom outfits not applied");
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
                if (!g_InstalledLoadoutApply)
                    LogError("OutfitSuitConditionApply: LoadoutApplyAfterSetSuit hook failed, custom outfit slot apply vanilla");
            }
            else
            {
                LogError("OutfitSuitConditionApply: LoadoutApplyAfterSetSuit address missing, custom outfit slot apply vanilla");
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
                if (!g_InstalledSetInitial)
                    LogError("OutfitSuitConditionApply: SetInitialCondition hook failed, custom outfit arm reload off");
            }
            else
            {
                LogError("OutfitSuitConditionApply: SetInitialCondition address missing, custom outfit arm reload off");
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
    }
}
