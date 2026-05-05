#include "pch.h"

#include "OutfitRuntimeParts.h"
#include "OutfitRegistry.h"
#include "ShadowState.h"

#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using FoxPath_Path_t = void* (__fastcall*)(void* outPath, std::uint64_t code64ext);

    using LoadPlayerPartsParts_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t playerType, std::uint32_t playerPartsType);
    using LoadPlayerPartsFpk_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t playerType, std::uint32_t playerPartsType);
    using LoadPlayerCamoFpk_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerCamoType);
    using LoadPlayerSnakeBlackDiamondFpk_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t applyBlackDiamond);
    using LoadPlayerBionicArm_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerHandType);
    using LoadPlayerSnakeFace_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerFaceId, char playerFaceEquipId);

    struct LoadPartsPlayerInfo
    {
        std::uint8_t  playerType;
        std::uint8_t  playerPartsType;
        std::uint8_t  playerCamoType;
        std::uint8_t  playerArmType;
        std::int16_t  playerFaceId;
        std::uint8_t  playerFaceEquipId;
        std::uint8_t  reserved07;
        std::uint8_t  reserved08[0x4C];
        std::uint8_t  reserved54;
        std::uint8_t  reserved55;
        std::uint8_t  playerFaceEquipUnk;
        std::uint8_t  reserved57;
    };
    static_assert(sizeof(LoadPartsPlayerInfo) == 0x58,
                  "LoadPartsPlayerInfo size must match retail layout");

    using LoadPartsNew_t           = void (__fastcall*)(void*, std::uint32_t, LoadPartsPlayerInfo*, std::uint32_t);
    using DoesNeedFaceFova_t       = std::uint8_t (__fastcall*)(std::uint32_t);
    using SetHandSlotEnabled_t     = void (__fastcall*)(void*, std::uint32_t, std::uint8_t);
    using IsArtificialHandEnabled_t            = std::uint8_t (__fastcall*)(std::uint32_t, std::uint32_t);
    using IsArtificialHandEnabledForCurrent_t  = std::uint8_t (__fastcall*)();
    using ProcessSignal_t          = void (__fastcall*)(void*, void*, std::uint32_t, std::uint64_t*);
    using UpdatePartsStatus_t      = void (__fastcall*)(void*);
    using Player2ImplSetUpParts_t  = bool (__fastcall*)(void*, std::uint32_t, std::uint32_t,
                                                       std::uint32_t, std::uint32_t, std::uint32_t,
                                                       std::uint32_t, void*);

    constexpr std::uint64_t kSignalRefreshFv2s             = 0x8483a342fa61ull;
    constexpr std::size_t   kP2GO_OffPerPlayerStruct       = 0x80;
    constexpr std::size_t   kP2GO_OffStateMachinePtr       = 0xb0;
    constexpr std::size_t   kPP_OffPlayerTypeArr           = 0x40;
    constexpr std::size_t   kPP_OffPartsTypeArr            = 0x48;
    constexpr std::size_t   kPP_OffCamoTypeArr             = 0x50;
    constexpr std::size_t   kPP_OffArmTypeArr              = 0x58;
    constexpr std::size_t   kPP_OffStateChangedBits        = 0x180;
    constexpr std::size_t   kPP_OffAltStateBits            = 0x184;
    constexpr std::size_t   kPP_OffLoadoutReq              = 0xc0;
    constexpr std::size_t   kPP_LoadoutReqStride           = 0x3a;
    constexpr std::size_t   kPP_LoadoutReqEquipHashOff     = 0x8;
    constexpr std::uint8_t  kProcessSignalSpoofPartsType   = 0x01;
    constexpr std::uint32_t kBionicArmVanillaPartsTypeSubstitute = 0x01;

    static FoxPath_Path_t                       g_FoxPath_Path                       = nullptr;
    static LoadPlayerPartsParts_t               g_OrigLoadPartsParts                 = nullptr;
    static LoadPlayerPartsFpk_t                 g_OrigLoadPartsFpk                   = nullptr;
    static LoadPlayerCamoFpk_t                  g_OrigLoadCamoFpk                    = nullptr;
    static LoadPlayerSnakeBlackDiamondFpk_t     g_OrigLoadDiamondFpk                 = nullptr;
    static LoadPlayerBionicArm_t                g_OrigLoadBionicArmFv2               = nullptr;
    static LoadPlayerBionicArm_t                g_OrigLoadBionicArmFpk               = nullptr;
    static LoadPlayerSnakeFace_t                g_OrigLoadSnakeFaceFv2               = nullptr;
    static LoadPlayerSnakeFace_t                g_OrigLoadSnakeFaceFpk               = nullptr;
    static LoadPartsNew_t                       g_OrigLoadPartsNew                   = nullptr;
    static DoesNeedFaceFova_t                   g_OrigDoesNeedFaceFova               = nullptr;
    static DoesNeedFaceFova_t                   g_OrigDoesNeedFaceFovaForAvatar      = nullptr;
    static SetHandSlotEnabled_t                 g_OrigSetHandSlotEnabled             = nullptr;
    static IsArtificialHandEnabled_t            g_OrigIsArtificialHandEnabled        = nullptr;
    static IsArtificialHandEnabledForCurrent_t  g_OrigIsArtificialHandForCurrent     = nullptr;
    static ProcessSignal_t                      g_OrigProcessSignal                  = nullptr;
    static UpdatePartsStatus_t                  g_OrigUpdatePartsStatus              = nullptr;
    static Player2ImplSetUpParts_t              g_OrigPlayer2ImplSetUpParts          = nullptr;

    static bool g_InstalledParts                 = false;
    static bool g_InstalledFpk                   = false;
    static bool g_InstalledCamo                  = false;
    static bool g_InstalledDiamond               = false;
    static bool g_InstalledBionicArmFv2          = false;
    static bool g_InstalledBionicArmFpk          = false;
    static bool g_InstalledSnakeFaceFv2          = false;
    static bool g_InstalledSnakeFaceFpk          = false;
    static bool g_InstalledLpn                   = false;
    static bool g_InstalledDoesNeedFace          = false;
    static bool g_InstalledDoesNeedFaceForAvatar = false;
    static bool g_InstalledSetHandSlotEnabled    = false;
    static bool g_InstalledIsArtificialHand      = false;
    static bool g_InstalledIsArtHandForCurrent   = false;
    static bool g_InstalledProcessSignal         = false;
    static bool g_InstalledUpdatePartsStatus     = false;
    static bool g_InstalledPlayer2ImplSetUpParts = false;

    static void* g_CapturedBlockController = nullptr;

    // ---------- helpers ----------

    static bool ResolveFoxPathApi()
    {
        if (!g_FoxPath_Path)
        {
            g_FoxPath_Path = reinterpret_cast<FoxPath_Path_t>(
                ResolveGameAddress(gAddr.FoxPath_Path));
        }
        return g_FoxPath_Path != nullptr;
    }

    static std::uint64_t* WriteFoxPath(std::uint64_t* outPath, std::uint64_t code64ext)
    {
        if (!outPath || !ResolveFoxPathApi()) return outPath;
        g_FoxPath_Path(outPath, code64ext);
        return outPath;
    }

    // Recover the real custom partsType. During an active LoadPartsNew spoof
    // window, ShadowState::GetCurrentSlot() points to the slot whose partsType
    // we substituted. Looking up the slot's shadow gives us the original.
    static std::uint32_t EffectivePartsType(std::uint32_t paramPartsType)
    {
        if (outfit::shadow::HasCurrentSlot())
        {
            outfit::shadow::Slot s;
            if (outfit::shadow::Get(outfit::shadow::GetCurrentSlot(), &s))
                return s.realPartsType;
        }
        return paramPartsType;
    }

    static bool ResolveCustomEntry(std::uint32_t playerType, std::uint32_t playerPartsType,
                                   const outfit::OutfitEntry** outEntry)
    {
        const auto pt  = static_cast<std::uint8_t>(playerPartsType & 0xFF);
        const auto ply = static_cast<std::uint8_t>(playerType & 0xFF);

        if (pt < outfit::kCustomPartsTypeStart || pt > outfit::kCustomPartsTypeEnd)
            return false;

        const outfit::OutfitEntry* entry = nullptr;
        if (!outfit::TryGetOutfitByPartsType(pt, &entry) || !entry) return false;
        if (!entry->IsPlayerTypeSupported(ply)) return false;

        if (outEntry) *outEntry = entry;
        return true;
    }

    static std::uint8_t TranslateEquipHashToArmTier(std::uint16_t equipHash)
    {
        switch (equipHash)
        {
        case 0x203: return 2;
        case 0x204: return 3;
        case 0x205: return 4;
        case 0x206: return 5;
        case 0x208: return 6;
        case 0x209: return 7;
        default:    return 1;
        }
    }

    static std::uint8_t ReadLiveArmTierFromLoadoutRequest(void* p2go, std::size_t slot)
    {
        if (!p2go) return 0;
        std::uint8_t result = 0;
        __try
        {
            void* perPlayer = *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(p2go) + kP2GO_OffPerPlayerStruct);
            if (!perPlayer) return 0;

            void* loadoutReqArr = *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffLoadoutReq);
            if (!loadoutReqArr) return 0;

            std::uint16_t equipHash = *reinterpret_cast<std::uint16_t*>(
                reinterpret_cast<std::uint8_t*>(loadoutReqArr)
                + slot * kPP_LoadoutReqStride
                + kPP_LoadoutReqEquipHashOff);
            result = TranslateEquipHashToArmTier(equipHash);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { result = 0; }
        return result;
    }

    static std::uint8_t ResolveSlotPartsTypeByteFromShadow(std::size_t slot,
                                                          std::uint8_t fallback)
    {
        outfit::shadow::Slot s;
        if (outfit::shadow::Get(slot, &s)) return s.realPartsType;
        return fallback;
    }

    // ---------- leaf hooks (asset path substitution) ----------

    static std::uint64_t* __fastcall hkLoadPlayerPartsParts(
        std::uint64_t* outPath, std::uint32_t playerType, std::uint32_t playerPartsType)
    {
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);

        const outfit::OutfitEntry* entry = nullptr;
        if (ResolveCustomEntry(playerType, effectivePartsType, &entry))
        {
            const auto pt = static_cast<std::uint8_t>(playerType & 0xFF);
            const std::uint8_t v = entry->HasVariants()
                ? outfit::GetActiveVariant(entry->partsType) : 0;
            const std::uint64_t path = entry->GetVariantPartsPath(pt, v);
            if (path != 0) return WriteFoxPath(outPath, path);
        }
        return g_OrigLoadPartsParts(outPath, playerType, playerPartsType);
    }

    static std::uint64_t* __fastcall hkLoadPlayerPartsFpk(
        std::uint64_t* outPath, std::uint32_t playerType, std::uint32_t playerPartsType)
    {
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);

        const outfit::OutfitEntry* entry = nullptr;
        if (ResolveCustomEntry(playerType, effectivePartsType, &entry))
        {
            const auto pt = static_cast<std::uint8_t>(playerType & 0xFF);
            const std::uint8_t v = entry->HasVariants()
                ? outfit::GetActiveVariant(entry->partsType) : 0;
            const std::uint64_t path = entry->GetVariantFpkPath(pt, v);
            if (path != 0) return WriteFoxPath(outPath, path);
        }
        return g_OrigLoadPartsFpk(outPath, playerType, playerPartsType);
    }

    static std::uint64_t* __fastcall hkLoadPlayerCamoFpk(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerCamoType)
    {
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);

        const outfit::OutfitEntry* entry = nullptr;
        if (ResolveCustomEntry(playerType, effectivePartsType, &entry))
        {
            const auto pt = static_cast<std::uint8_t>(playerType & 0xFF);
            const std::uint8_t v = entry->HasVariants()
                ? outfit::GetActiveVariant(entry->partsType) : 0;
            const std::uint64_t camo = entry->GetVariantCamoFpk(pt, v);
            if (camo > outfit::kSubAssetUseVanilla) return WriteFoxPath(outPath, camo);
            return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
        }
        return g_OrigLoadCamoFpk(outPath, playerType, playerPartsType, playerCamoType);
    }

    static std::uint64_t* __fastcall hkLoadPlayerSnakeBlackDiamondFpk(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t applyBlackDiamond)
    {
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);

        const outfit::OutfitEntry* entry = nullptr;
        if (ResolveCustomEntry(playerType, effectivePartsType, &entry))
        {
            const auto pt = static_cast<std::uint8_t>(playerType & 0xFF);
            const std::uint8_t v = entry->HasVariants()
                ? outfit::GetActiveVariant(entry->partsType) : 0;
            const std::uint64_t diamond = entry->GetVariantDiamondFpk(pt, v);
            if (diamond == outfit::kSubAssetDisabled)
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
            if (diamond > outfit::kSubAssetUseVanilla)
                return WriteFoxPath(outPath, diamond);
        }
        return g_OrigLoadDiamondFpk(outPath, playerType, playerPartsType, applyBlackDiamond);
    }

    // Engine zeroes handType for custom partsType (the leaf would then load
    // NULL); recover from per-PT cache.
    static std::uint32_t RecoverArmTierForLeaf(std::uint32_t playerType,
                                               std::uint32_t passedHandType)
    {
        if (passedHandType != 0) return passedHandType;
        bool captured = false;
        std::uint8_t cachedTier = outfit::shadow::GetArmTier(playerType, &captured);
        return captured ? static_cast<std::uint32_t>(cachedTier) : 1u;
    }

    static std::uint64_t* __fastcall hkLoadPlayerBionicArmFv2(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerHandType)
    {
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);
        const auto pt = static_cast<std::uint8_t>(playerType & 0xFF);

        const outfit::OutfitEntry* entry = nullptr;
        if (ResolveCustomEntry(playerType, effectivePartsType, &entry))
        {
            if (!entry->IsArmEnabled(pt))
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
            const std::uint32_t hand = RecoverArmTierForLeaf(playerType, playerHandType);
            return g_OrigLoadBionicArmFv2(outPath, playerType,
                                          kBionicArmVanillaPartsTypeSubstitute, hand);
        }
        return g_OrigLoadBionicArmFv2(outPath, playerType, playerPartsType, playerHandType);
    }

    static std::uint64_t* __fastcall hkLoadPlayerBionicArmFpk(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerHandType)
    {
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);
        const auto pt = static_cast<std::uint8_t>(playerType & 0xFF);

        const outfit::OutfitEntry* entry = nullptr;
        if (ResolveCustomEntry(playerType, effectivePartsType, &entry))
        {
            if (!entry->IsArmEnabled(pt))
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
            const std::uint32_t hand = RecoverArmTierForLeaf(playerType, playerHandType);
            return g_OrigLoadBionicArmFpk(outPath, playerType,
                                          kBionicArmVanillaPartsTypeSubstitute, hand);
        }
        return g_OrigLoadBionicArmFpk(outPath, playerType, playerPartsType, playerHandType);
    }

    static std::uint64_t* __fastcall hkLoadPlayerSnakeFaceFv2(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerFaceId, char playerFaceEquipId)
    {
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);
        const auto pt = static_cast<std::uint8_t>(playerType & 0xFF);
        const outfit::OutfitEntry* entry = nullptr;
        if (ResolveCustomEntry(playerType, effectivePartsType, &entry))
        {
            if (!entry->IsHeadEnabled(pt))
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
            return g_OrigLoadSnakeFaceFv2(outPath, playerType,
                                          kBionicArmVanillaPartsTypeSubstitute,
                                          playerFaceId, playerFaceEquipId);
        }
        return g_OrigLoadSnakeFaceFv2(outPath, playerType, playerPartsType,
                                      playerFaceId, playerFaceEquipId);
    }

    static std::uint64_t* __fastcall hkLoadPlayerSnakeFaceFpk(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerFaceId, char playerFaceEquipId)
    {
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);
        const auto pt = static_cast<std::uint8_t>(playerType & 0xFF);
        const outfit::OutfitEntry* entry = nullptr;
        if (ResolveCustomEntry(playerType, effectivePartsType, &entry))
        {
            if (!entry->IsHeadEnabled(pt))
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
            return g_OrigLoadSnakeFaceFpk(outPath, playerType,
                                          kBionicArmVanillaPartsTypeSubstitute,
                                          playerFaceId, playerFaceEquipId);
        }
        return g_OrigLoadSnakeFaceFpk(outPath, playerType, playerPartsType,
                                      playerFaceId, playerFaceEquipId);
    }

    // ---------- predicate hooks (face / arm whitelist gates) ----------

    static std::uint8_t __fastcall hkDoesNeedFaceFova(std::uint32_t playerPartsType)
    {
        const std::uint32_t effective = EffectivePartsType(playerPartsType);
        if (effective >= outfit::kCustomPartsTypeStart && effective <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            const auto pt     = static_cast<std::uint8_t>(effective & 0xFF);
            const auto livePT = outfit::ReadLivePlayerType();
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry)
                return entry->IsHeadEnabled(livePT) ? std::uint8_t{1} : std::uint8_t{0};
        }
        return g_OrigDoesNeedFaceFova ? g_OrigDoesNeedFaceFova(playerPartsType) : 0;
    }

    static std::uint8_t __fastcall hkDoesNeedFaceFovaForAvatar(std::uint32_t playerPartsType)
    {
        const std::uint32_t effective = EffectivePartsType(playerPartsType);
        if (effective >= outfit::kCustomPartsTypeStart && effective <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            const auto pt     = static_cast<std::uint8_t>(effective & 0xFF);
            const auto livePT = outfit::ReadLivePlayerType();
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry)
                return entry->IsHeadEnabled(livePT) ? std::uint8_t{1} : std::uint8_t{0};
        }
        return g_OrigDoesNeedFaceFovaForAvatar
             ? g_OrigDoesNeedFaceFovaForAvatar(playerPartsType) : 0;
    }

    static void __fastcall hkSetHandSlotEnabled(void* self_equipController,
                                                std::uint32_t slot, std::uint8_t enabled)
    {
        if (enabled != 0)
        {
            if (g_OrigSetHandSlotEnabled)
                g_OrigSetHandSlotEnabled(self_equipController, slot, enabled);
            return;
        }

        // Engine wants enabled=0; check if our custom outfit overrides that.
        const std::uint8_t livePT        = outfit::ReadLivePlayerType();
        const std::uint8_t livePartsType = outfit::ReadLivePartsType();

        const bool liveIsSnakeOrAvatar =
               (livePT == outfit::kPlayerType_Snake)
            || (livePT == outfit::kPlayerType_Avatar);
        const bool liveIsCustomPartsType =
               (livePartsType >= outfit::kCustomPartsTypeStart
             && livePartsType <= outfit::kCustomPartsTypeEnd);

        if (liveIsSnakeOrAvatar && liveIsCustomPartsType)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(livePartsType, &entry) && entry
                && entry->IsPlayerTypeSupported(livePT)
                && entry->IsArmEnabled(livePT))
            {
                if (g_OrigSetHandSlotEnabled)
                    g_OrigSetHandSlotEnabled(self_equipController, slot, 1);
                return;
            }
        }
        if (g_OrigSetHandSlotEnabled)
            g_OrigSetHandSlotEnabled(self_equipController, slot, enabled);
    }

    static std::uint8_t __fastcall hkIsArtificialHandEnabled(
        std::uint32_t playerType, std::uint32_t playerPartsType)
    {
        if (playerPartsType >= outfit::kCustomPartsTypeStart
         && playerPartsType <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            const auto pt  = static_cast<std::uint8_t>(playerPartsType & 0xFF);
            const auto ply = static_cast<std::uint8_t>(playerType & 0xFF);
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry
                && entry->IsPlayerTypeSupported(ply)
                && entry->IsArmEnabled(ply))
                return 1;
        }
        return g_OrigIsArtificialHandEnabled
             ? g_OrigIsArtificialHandEnabled(playerType, playerPartsType) : 0;
    }

    static std::uint8_t __fastcall hkIsArtificialHandEnabledForCurrentPlayerType()
    {
        const std::uint8_t livePT        = outfit::ReadLivePlayerType();
        const std::uint8_t livePartsType = outfit::ReadLivePartsType();

        if (livePartsType >= outfit::kCustomPartsTypeStart
         && livePartsType <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(livePartsType, &entry) && entry
                && entry->IsPlayerTypeSupported(livePT)
                && entry->IsArmEnabled(livePT))
                return 1;
        }
        return g_OrigIsArtificialHandForCurrent ? g_OrigIsArtificialHandForCurrent() : 0;
    }

    // ---------- ProcessSignal (Fv2 refresh) ----------

    static void __fastcall hkProcessSignal(void* p1, void* p2,
                                           std::uint32_t slot, std::uint64_t* signalPtr)
    {
        if (!signalPtr || *signalPtr != kSignalRefreshFv2s)
        {
            if (g_OrigProcessSignal) g_OrigProcessSignal(p1, p2, slot, signalPtr);
            return;
        }

        // Spoof partsType byte through the orig call so the signal's <0x1c
        // whitelist gate passes for our custom outfits.
        std::uint8_t* partsTypeArr = nullptr;
        std::uint8_t  origByte     = 0;
        bool          spoofWritten = false;

        __try
        {
            void* perPlayer = *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(p1) + kP2GO_OffPerPlayerStruct);
            if (perPlayer)
            {
                partsTypeArr = *reinterpret_cast<std::uint8_t**>(
                    reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffPartsTypeArr);
                if (partsTypeArr)
                {
                    origByte = partsTypeArr[slot];
                    if (origByte >= outfit::kCustomPartsTypeStart
                     && origByte <= outfit::kCustomPartsTypeEnd)
                    {
                        // Tell leaf hooks which slot is being processed.
                        outfit::shadow::SetCurrentSlot(slot);
                        partsTypeArr[slot] = kProcessSignalSpoofPartsType;
                        spoofWritten = true;
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { partsTypeArr = nullptr; }

        if (g_OrigProcessSignal) g_OrigProcessSignal(p1, p2, slot, signalPtr);

        if (spoofWritten)
        {
            __try { partsTypeArr[slot] = origByte; }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            outfit::shadow::ClearCurrentSlot();
        }
    }

    // ---------- UpdatePartsStatus (per-tick reconciliation) ----------
    //
    // Vanilla diff-gate at mgsvtpp:1325162 computes NEW arm tier from the
    // inner switch (whitelist case → equipHash → 1..7). For custom partsType
    // the switch hits the default branch and NEW=0. Comparing NEW=0 vs
    // OLD=byte_arrays+0x58 (= cached tier) fires the bit every tick.
    //
    // To stop the per-tick fire, we set armTypeArr[i] = 0 pre-orig in
    // steady state; orig sees match (0==0) and doesn't fire. On a genuine
    // tier change, we additionally OR the +0x180 bit ourselves so the next
    // state==3 tick fires the natural ClearParts → state→4 → state→0 →
    // LoadPartsNew cascade.
    //
    // This is the documented "force-cascade-once-per-tier-change" pattern.

    static void __fastcall hkUpdatePartsStatus(void* self)
    {
        struct SlotOverride
        {
            bool         active;
            std::uint8_t restoreValue;       // real arm tier
            bool         tierJustChanged;
            bool         genuineTierChange;
        };
        SlotOverride overrides[outfit::shadow::kMaxSlots] = {};
        std::uint8_t* armTypeArr = nullptr;

        __try
        {
            void* perPlayer = *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(self) + kP2GO_OffPerPlayerStruct);
            std::uint8_t* stateMachineArr = *reinterpret_cast<std::uint8_t**>(
                reinterpret_cast<std::uint8_t*>(self) + kP2GO_OffStateMachinePtr);

            if (perPlayer)
            {
                std::uint8_t* partsTypeArr = *reinterpret_cast<std::uint8_t**>(
                    reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffPartsTypeArr);
                std::uint8_t* playerTypeArr = *reinterpret_cast<std::uint8_t**>(
                    reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffPlayerTypeArr);
                armTypeArr = *reinterpret_cast<std::uint8_t**>(
                    reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffArmTypeArr);
                std::uint32_t* stateChangedBits = reinterpret_cast<std::uint32_t*>(
                    reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffStateChangedBits);

                if (partsTypeArr && playerTypeArr && armTypeArr)
                {
                    static std::uint8_t s_lastSeenTier[outfit::shadow::kMaxSlots] =
                        {0xFFu, 0xFFu, 0xFFu, 0xFFu};

                    for (std::size_t i = 0; i < outfit::shadow::kMaxSlots; ++i)
                    {
                        const std::uint8_t pt  = partsTypeArr[i];
                        const std::uint8_t ply = playerTypeArr[i];

                        if (pt < outfit::kCustomPartsTypeStart
                         || pt > outfit::kCustomPartsTypeEnd) continue;

                        const outfit::OutfitEntry* entry = nullptr;
                        if (!outfit::TryGetOutfitByPartsType(pt, &entry) || !entry) continue;
                        if (!entry->IsPlayerTypeSupported(ply)) continue;
                        if (!entry->IsArmEnabled(ply)) continue;

                        const std::uint8_t liveTier =
                            ReadLiveArmTierFromLoadoutRequest(self, i);

                        bool cachedFlag = false;
                        const std::uint8_t cachedTier =
                            outfit::shadow::GetArmTier(ply, &cachedFlag);

                        const std::uint8_t resolvedTier =
                            (liveTier > 0) ? liveTier
                                           : (cachedFlag ? cachedTier : std::uint8_t{1});

                        // Keep cache live; per-PT split avoids slot-iteration clobber.
                        if (liveTier > 0)
                            outfit::shadow::SetArmTier(ply, liveTier);

                        // Sync ShadowState slot record (idempotent if values unchanged)
                        outfit::shadow::Slot ss;
                        if (!outfit::shadow::Get(i, &ss))
                            ss = outfit::shadow::Slot{};
                        ss.realPartsType  = pt;
                        ss.realCamoType   = entry->selectorCode;
                        ss.realArmType    = resolvedTier;
                        ss.realPlayerType = ply;
                        ss.developId      = entry->developId;
                        ss.variantIdx     = entry->HasVariants()
                                          ? outfit::GetActiveVariant(pt) : 0;
                        outfit::shadow::Set(i, ss);

                        const bool firstEncounter = (s_lastSeenTier[i] == 0xFFu);
                        const bool tierChanged    = (s_lastSeenTier[i] != resolvedTier);
                        overrides[i].tierJustChanged   = tierChanged;
                        overrides[i].genuineTierChange = !firstEncounter && tierChanged;
                        overrides[i].active            = true;
                        overrides[i].restoreValue      = resolvedTier;

                        // Steady state: zero pre-orig to suppress vanilla compare's
                        // every-tick reload.
                        if (!tierChanged)
                            armTypeArr[i] = 0;
                        // else: leave armTypeArr[i] as last tick's restored value
                        // so the natural compare fires once.
                        s_lastSeenTier[i] = resolvedTier;

                        // Drive the natural cascade for arm-only tier changes:
                        // orig's whitelist switch never fires the +0x180 bit
                        // for custom partsType. Set it ourselves, but ONLY when
                        // state is already 3 (steady-waiting) — setting it during
                        // states {0,1,2,4} can double-unload as the bit lingers
                        // into the next state==3 entry.
                        if (overrides[i].genuineTierChange
                         && stateMachineArr && stateChangedBits
                         && stateMachineArr[i] == 3)
                        {
                            const std::uint32_t slotBit = 1u << static_cast<std::uint32_t>(i);
                            *stateChangedBits |= slotBit;
                        }
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            for (std::size_t i = 0; i < outfit::shadow::kMaxSlots; ++i)
                overrides[i].active = false;
            armTypeArr = nullptr;
        }

        g_OrigUpdatePartsStatus(self);

        // Restore real arm tiers for gameplay queries.
        if (armTypeArr)
        {
            __try
            {
                for (std::size_t i = 0; i < outfit::shadow::kMaxSlots; ++i)
                {
                    if (overrides[i].active)
                        armTypeArr[i] = overrides[i].restoreValue;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    // ---------- Player2Impl::SetUpParts (re-arm if vanilla zeroed) ----------

    static bool __fastcall hkPlayer2ImplSetUpParts(
        void* self, std::uint32_t slot,
        std::uint32_t playerType, std::uint32_t partsType,
        std::uint32_t camo, std::uint32_t armType,
        std::uint32_t faceId, void* avatarInfo)
    {
        if (!g_OrigPlayer2ImplSetUpParts) return false;

        std::uint32_t effectiveArmType = armType;
        if (armType == 0
         && partsType >= outfit::kCustomPartsTypeStart
         && partsType <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(
                    static_cast<std::uint8_t>(partsType & 0xFF), &entry)
                && entry
                && entry->IsPlayerTypeSupported(
                       static_cast<std::uint8_t>(playerType & 0xFF))
                && entry->IsArmEnabled(static_cast<std::uint8_t>(playerType & 0xFF)))
            {
                bool cachedFlag = false;
                std::uint8_t cachedTier = outfit::shadow::GetArmTier(playerType, &cachedFlag);
                effectiveArmType = cachedFlag ? static_cast<std::uint32_t>(cachedTier) : 1u;
            }
        }
        return g_OrigPlayer2ImplSetUpParts(self, slot, playerType, partsType,
                                           camo, effectiveArmType, faceId, avatarInfo);
    }

    // ---------- LoadPartsNew (asset-load entry; spoof + slot tracking) ----------

    static void __fastcall hkLoadPartsNew(
        void* self, std::uint32_t playerIndex,
        LoadPartsPlayerInfo* info, std::uint32_t flags)
    {
        if (!g_CapturedBlockController && self)
            g_CapturedBlockController = self;

        if (!info)
        {
            g_OrigLoadPartsNew(self, playerIndex, info, flags);
            return;
        }

        // Resolve broken-custom patterns the engine emits when a custom
        // outfit was selected: (partsType=0, camo=customSelector) and
        // (partsType=0, camo=0xFF, pendingDevId stash).
        auto feedShadow = [&](const outfit::OutfitEntry* e, std::uint8_t variantIdx) {
            outfit::shadow::Slot ss{};
            ss.realPartsType  = e->partsType;
            ss.realCamoType   = e->selectorCode;
            ss.realArmType    = info->playerArmType;
            ss.realPlayerType = info->playerType;
            ss.developId      = e->developId;
            ss.variantIdx     = variantIdx;
            outfit::shadow::Set(playerIndex, ss);
        };

        if (info->playerPartsType == 0)
        {
            const std::uint8_t camo = info->playerCamoType;
            if (camo >= outfit::kCustomSelectorStart && camo <= outfit::kCustomSelectorEnd)
            {
                const outfit::OutfitEntry* entry = nullptr;
                std::uint8_t variantIdx = 0;
                if (outfit::TryGetOutfitByVariantSelector(camo, &entry, &variantIdx)
                    && entry
                    && entry->IsPlayerTypeSupported(info->playerType))
                {
                    info->playerPartsType = entry->partsType;
                    info->playerCamoType  = entry->selectorCode;
                    outfit::SetActiveVariant(entry->partsType, variantIdx);
                    outfit::WriteLivePlayerOutfit(entry->partsType,
                                                  entry->selectorCode,
                                                  info->playerType);
                    feedShadow(entry, variantIdx);
                }
                else
                {
                    info->playerCamoType = 0;
                }
            }
            else if (camo == 0xFF)
            {
                const outfit::OutfitEntry* byPending = nullptr;
                const std::uint16_t pendingDevId = outfit::GetPendingOutfitDevelopId();
                if (pendingDevId != 0
                    && outfit::TryGetOutfitByDevelopId(pendingDevId, &byPending) && byPending
                    && byPending->IsPlayerTypeSupported(info->playerType))
                {
                    info->playerPartsType = byPending->partsType;
                    info->playerCamoType  = byPending->selectorCode;
                    outfit::WriteLivePlayerOutfit(byPending->partsType,
                                                  byPending->selectorCode,
                                                  info->playerType);
                    feedShadow(byPending, byPending->HasVariants()
                                          ? outfit::GetActiveVariant(byPending->partsType) : 0);
                    outfit::ClearPendingOutfitDevelopId();
                }
                else
                {
                    info->playerCamoType = 0;
                }
            }
        }

        const outfit::OutfitEntry* entry = nullptr;
        const bool isCustom = ResolveCustomEntry(info->playerType,
                                                 info->playerPartsType, &entry);

        if (isCustom)
        {
            const auto livePT = info->playerType;

            // Restore arm tier if engine zeroed it.
            if (!entry->IsArmEnabled(livePT))
            {
                info->playerArmType = 0;
            }
            else if (info->playerArmType == 0)
            {
                bool cachedFlag = false;
                std::uint8_t cachedTier = outfit::shadow::GetArmTier(livePT, &cachedFlag);
                info->playerArmType = cachedFlag ? cachedTier : std::uint8_t{1};
            }

            // Suppress face fields if this PT branch's faceFpk is disabled.
            if (!entry->IsFaceEnabled(livePT))
            {
                info->playerFaceEquipId = 0;
                info->playerFaceEquipUnk =
                    static_cast<std::uint8_t>(info->playerFaceEquipUnk & 0xF8);
            }

            const std::uint16_t defaultFaceId = entry->GetDefaultSoldierFaceIdFor(livePT);
            if (entry->IsHeadEnabled(livePT) && defaultFaceId != 0
             && info->playerFaceId == 0)
            {
                info->playerFaceId = static_cast<std::int16_t>(defaultFaceId);
            }

            // Feed shadow so leaf hooks (and same-tick downstream queries)
            // see correct slot data even before the next UpdatePartsStatus.
            feedShadow(entry, entry->HasVariants()
                              ? outfit::GetActiveVariant(entry->partsType) : 0);
        }

        // Capture per-PT arm tier from natural calls (armType > 0) for later
        // refill when the engine zeroes it.
        if (info->playerArmType != 0)
            outfit::shadow::SetArmTier(info->playerType, info->playerArmType);

        // Spoof: route orig through vanilla 0x01 (Snake STANDARD) so the leaf
        // whitelists pass; clobber BlockShell partsType to 0xFE so orig's
        // dedup compare doesn't short-circuit; tell leaf hooks which slot is
        // being processed via ShadowState.
        const bool             spoofPartsType = isCustom && entry;
        const std::uint8_t     origPartsType  = info->playerPartsType;
        std::uint8_t*          shellTypeInfoPtr     = nullptr;
        std::uint8_t           prevShellPartsType   = 0;
        bool                   shellSentinelWritten = false;

        if (spoofPartsType)
        {
            outfit::shadow::SetCurrentSlot(playerIndex);

            std::uint8_t spoofTarget = 0x00;
            if (info->playerType == outfit::kPlayerType_Snake) spoofTarget = 0x01;
            info->playerPartsType = spoofTarget;

            __try
            {
                shellTypeInfoPtr = *reinterpret_cast<std::uint8_t**>(
                    reinterpret_cast<std::uint8_t*>(self)
                    + playerIndex * 8 + 0x1100);
                if (shellTypeInfoPtr)
                {
                    prevShellPartsType   = shellTypeInfoPtr[1];
                    shellTypeInfoPtr[1]  = 0xFE;
                    shellSentinelWritten = true;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { shellTypeInfoPtr = nullptr; }
        }

        g_OrigLoadPartsNew(self, playerIndex, info, flags);

        if (spoofPartsType)
        {
            info->playerPartsType = origPartsType;
            outfit::shadow::ClearCurrentSlot();
            __try
            {
                if (shellTypeInfoPtr) shellTypeInfoPtr[1] = origPartsType;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            (void)prevShellPartsType;
            (void)shellSentinelWritten;
        }
    }
}

namespace outfit
{
    bool ForcePartsReload(unsigned char playerType,
                          unsigned char partsType,
                          unsigned char selectorCode)
    {
        if (!g_CapturedBlockController || !g_OrigLoadPartsNew) return false;

        const auto pt  = static_cast<std::uint8_t>(playerType);
        const auto ppt = static_cast<std::uint8_t>(partsType);
        const auto sel = static_cast<std::uint8_t>(selectorCode);

        const outfit::OutfitEntry* entry = nullptr;
        if (!ResolveCustomEntry(pt, ppt, &entry)) return false;

        bool cachedFlag = false;
        std::uint8_t cachedTier = outfit::shadow::GetArmTier(pt, &cachedFlag);

        LoadPartsPlayerInfo info{};
        info.playerType         = pt;
        info.playerPartsType    = ppt;
        info.playerCamoType     = sel;
        info.playerArmType      = (entry->IsArmEnabled(pt) && cachedFlag) ? cachedTier : 0;
        info.playerFaceId       = 0;
        info.playerFaceEquipId  = 0;
        info.playerFaceEquipUnk = 0;

        // Drive both slots so heli / FOB-ghost / dual-loadout setups all see
        // the new outfit (matches vanilla behavior in iDroid prep).
        constexpr std::uint32_t kFlags = 0x15EC40u;
        for (std::uint32_t slot : {0u, 1u})
        {
            hkLoadPartsNew(g_CapturedBlockController, slot, &info, kFlags);
        }

        outfit::shadow::Slot ss{};
        ss.realPartsType  = ppt;
        ss.realCamoType   = sel;
        ss.realArmType    = info.playerArmType;
        ss.realPlayerType = pt;
        ss.developId      = entry->developId;
        ss.variantIdx     = entry->HasVariants() ? outfit::GetActiveVariant(ppt) : 0;
        outfit::shadow::Set(0, ss);
        outfit::shadow::Set(1, ss);

        Log("[OutfitRuntimeParts] ForcePartsReload pt=%u partsType=0x%02X "
            "selector=0x%02X developId=%u\n",
            static_cast<unsigned>(pt),
            static_cast<unsigned>(ppt),
            static_cast<unsigned>(sel),
            static_cast<unsigned>(entry->developId));
        return true;
    }

    bool Install_OutfitRuntimeParts_Hooks()
    {
        ResolveFoxPathApi();

        struct H { void* tgt; void* hk; void** orig; bool* installed; };
        H hooks[] = {
            { ResolveGameAddress(gAddr.LoadPlayerPartsParts),
              reinterpret_cast<void*>(&hkLoadPlayerPartsParts),
              reinterpret_cast<void**>(&g_OrigLoadPartsParts), &g_InstalledParts },
            { ResolveGameAddress(gAddr.LoadPlayerPartsFpk),
              reinterpret_cast<void*>(&hkLoadPlayerPartsFpk),
              reinterpret_cast<void**>(&g_OrigLoadPartsFpk), &g_InstalledFpk },
            { ResolveGameAddress(gAddr.LoadPlayerCamoFpk),
              reinterpret_cast<void*>(&hkLoadPlayerCamoFpk),
              reinterpret_cast<void**>(&g_OrigLoadCamoFpk), &g_InstalledCamo },
            { ResolveGameAddress(gAddr.LoadPlayerSnakeBlackDiamondFpk),
              reinterpret_cast<void*>(&hkLoadPlayerSnakeBlackDiamondFpk),
              reinterpret_cast<void**>(&g_OrigLoadDiamondFpk), &g_InstalledDiamond },
            { ResolveGameAddress(gAddr.LoadPlayerBionicArmFv2),
              reinterpret_cast<void*>(&hkLoadPlayerBionicArmFv2),
              reinterpret_cast<void**>(&g_OrigLoadBionicArmFv2), &g_InstalledBionicArmFv2 },
            { ResolveGameAddress(gAddr.LoadPlayerBionicArmFpk),
              reinterpret_cast<void*>(&hkLoadPlayerBionicArmFpk),
              reinterpret_cast<void**>(&g_OrigLoadBionicArmFpk), &g_InstalledBionicArmFpk },
            { ResolveGameAddress(gAddr.LoadPlayerSnakeFaceFv2),
              reinterpret_cast<void*>(&hkLoadPlayerSnakeFaceFv2),
              reinterpret_cast<void**>(&g_OrigLoadSnakeFaceFv2), &g_InstalledSnakeFaceFv2 },
            { ResolveGameAddress(gAddr.LoadPlayerSnakeFaceFpk),
              reinterpret_cast<void*>(&hkLoadPlayerSnakeFaceFpk),
              reinterpret_cast<void**>(&g_OrigLoadSnakeFaceFpk), &g_InstalledSnakeFaceFpk },
            { ResolveGameAddress(gAddr.Player2BlockController_LoadPartsNew),
              reinterpret_cast<void*>(&hkLoadPartsNew),
              reinterpret_cast<void**>(&g_OrigLoadPartsNew), &g_InstalledLpn },
            { ResolveGameAddress(gAddr.ResourceTable_DoesNeedFaceFova),
              reinterpret_cast<void*>(&hkDoesNeedFaceFova),
              reinterpret_cast<void**>(&g_OrigDoesNeedFaceFova), &g_InstalledDoesNeedFace },
            { ResolveGameAddress(gAddr.ResourceTable_DoesNeedFaceFovaForAvatar),
              reinterpret_cast<void*>(&hkDoesNeedFaceFovaForAvatar),
              reinterpret_cast<void**>(&g_OrigDoesNeedFaceFovaForAvatar), &g_InstalledDoesNeedFaceForAvatar },
            { ResolveGameAddress(gAddr.EquipController_SetHandSlotEnabled),
              reinterpret_cast<void*>(&hkSetHandSlotEnabled),
              reinterpret_cast<void**>(&g_OrigSetHandSlotEnabled), &g_InstalledSetHandSlotEnabled },
            { ResolveGameAddress(gAddr.Sys_IsArtificialHandEnabled),
              reinterpret_cast<void*>(&hkIsArtificialHandEnabled),
              reinterpret_cast<void**>(&g_OrigIsArtificialHandEnabled), &g_InstalledIsArtificialHand },
            { ResolveGameAddress(gAddr.Sys_IsArtificialHandEnabledForCurrentPlayerType),
              reinterpret_cast<void*>(&hkIsArtificialHandEnabledForCurrentPlayerType),
              reinterpret_cast<void**>(&g_OrigIsArtificialHandForCurrent), &g_InstalledIsArtHandForCurrent },
            { ResolveGameAddress(gAddr.Player2GameObjectImpl_ProcessSignal),
              reinterpret_cast<void*>(&hkProcessSignal),
              reinterpret_cast<void**>(&g_OrigProcessSignal), &g_InstalledProcessSignal },
            { ResolveGameAddress(gAddr.UpdatePartsStatus),
              reinterpret_cast<void*>(&hkUpdatePartsStatus),
              reinterpret_cast<void**>(&g_OrigUpdatePartsStatus), &g_InstalledUpdatePartsStatus },
            { ResolveGameAddress(gAddr.Player2Impl_SetUpParts),
              reinterpret_cast<void*>(&hkPlayer2ImplSetUpParts),
              reinterpret_cast<void**>(&g_OrigPlayer2ImplSetUpParts), &g_InstalledPlayer2ImplSetUpParts },
        };
        for (auto& h : hooks)
        {
            if (h.tgt) *h.installed = CreateAndEnableHook(h.tgt, h.hk, h.orig);
        }

        Log("[OutfitRuntimeParts] installed: parts=%s fpk=%s camo=%s diamond=%s "
            "bionicArmFv2=%s bionicArmFpk=%s snakeFaceFv2=%s snakeFaceFpk=%s "
            "lpn=%s doesNeedFace=%s doesNeedFaceAvatar=%s setHandSlotEnabled=%s "
            "isArtificialHandEnabled=%s isArtHandForCurrent=%s processSignal=%s "
            "updatePartsStatus=%s setUpParts=%s\n",
            g_InstalledParts                 ? "OK" : "skip",
            g_InstalledFpk                   ? "OK" : "skip",
            g_InstalledCamo                  ? "OK" : "skip",
            g_InstalledDiamond               ? "OK" : "skip",
            g_InstalledBionicArmFv2          ? "OK" : "skip",
            g_InstalledBionicArmFpk          ? "OK" : "skip",
            g_InstalledSnakeFaceFv2          ? "OK" : "skip",
            g_InstalledSnakeFaceFpk          ? "OK" : "skip",
            g_InstalledLpn                   ? "OK" : "skip",
            g_InstalledDoesNeedFace          ? "OK" : "skip",
            g_InstalledDoesNeedFaceForAvatar ? "OK" : "skip",
            g_InstalledSetHandSlotEnabled    ? "OK" : "skip",
            g_InstalledIsArtificialHand      ? "OK" : "skip",
            g_InstalledIsArtHandForCurrent   ? "OK" : "skip",
            g_InstalledProcessSignal         ? "OK" : "skip",
            g_InstalledUpdatePartsStatus     ? "OK" : "skip",
            g_InstalledPlayer2ImplSetUpParts ? "OK" : "skip");

        return g_InstalledParts || g_InstalledFpk || g_InstalledCamo
            || g_InstalledDiamond || g_InstalledBionicArmFv2 || g_InstalledBionicArmFpk
            || g_InstalledSnakeFaceFv2 || g_InstalledSnakeFaceFpk || g_InstalledLpn;
    }

    void Uninstall_OutfitRuntimeParts_Hooks()
    {
        struct U { bool* installed; void* tgt; };
        U hooks[] = {
            { &g_InstalledParts,                 ResolveGameAddress(gAddr.LoadPlayerPartsParts) },
            { &g_InstalledFpk,                   ResolveGameAddress(gAddr.LoadPlayerPartsFpk) },
            { &g_InstalledCamo,                  ResolveGameAddress(gAddr.LoadPlayerCamoFpk) },
            { &g_InstalledDiamond,               ResolveGameAddress(gAddr.LoadPlayerSnakeBlackDiamondFpk) },
            { &g_InstalledBionicArmFv2,          ResolveGameAddress(gAddr.LoadPlayerBionicArmFv2) },
            { &g_InstalledBionicArmFpk,          ResolveGameAddress(gAddr.LoadPlayerBionicArmFpk) },
            { &g_InstalledSnakeFaceFv2,          ResolveGameAddress(gAddr.LoadPlayerSnakeFaceFv2) },
            { &g_InstalledSnakeFaceFpk,          ResolveGameAddress(gAddr.LoadPlayerSnakeFaceFpk) },
            { &g_InstalledLpn,                   ResolveGameAddress(gAddr.Player2BlockController_LoadPartsNew) },
            { &g_InstalledDoesNeedFace,          ResolveGameAddress(gAddr.ResourceTable_DoesNeedFaceFova) },
            { &g_InstalledDoesNeedFaceForAvatar, ResolveGameAddress(gAddr.ResourceTable_DoesNeedFaceFovaForAvatar) },
            { &g_InstalledSetHandSlotEnabled,    ResolveGameAddress(gAddr.EquipController_SetHandSlotEnabled) },
            { &g_InstalledIsArtificialHand,      ResolveGameAddress(gAddr.Sys_IsArtificialHandEnabled) },
            { &g_InstalledIsArtHandForCurrent,   ResolveGameAddress(gAddr.Sys_IsArtificialHandEnabledForCurrentPlayerType) },
            { &g_InstalledProcessSignal,         ResolveGameAddress(gAddr.Player2GameObjectImpl_ProcessSignal) },
            { &g_InstalledUpdatePartsStatus,     ResolveGameAddress(gAddr.UpdatePartsStatus) },
            { &g_InstalledPlayer2ImplSetUpParts, ResolveGameAddress(gAddr.Player2Impl_SetUpParts) },
        };
        for (auto& h : hooks)
        {
            if (*h.installed && h.tgt) DisableAndRemoveHook(h.tgt);
            *h.installed = false;
        }

        g_OrigLoadPartsParts            = nullptr;
        g_OrigLoadPartsFpk              = nullptr;
        g_OrigLoadCamoFpk               = nullptr;
        g_OrigLoadDiamondFpk            = nullptr;
        g_OrigLoadBionicArmFv2          = nullptr;
        g_OrigLoadBionicArmFpk          = nullptr;
        g_OrigLoadSnakeFaceFv2          = nullptr;
        g_OrigLoadSnakeFaceFpk          = nullptr;
        g_OrigLoadPartsNew              = nullptr;
        g_OrigDoesNeedFaceFova          = nullptr;
        g_OrigDoesNeedFaceFovaForAvatar = nullptr;
        g_OrigSetHandSlotEnabled        = nullptr;
        g_OrigIsArtificialHandEnabled   = nullptr;
        g_OrigIsArtificialHandForCurrent = nullptr;
        g_OrigProcessSignal             = nullptr;
        g_OrigUpdatePartsStatus         = nullptr;
        g_OrigPlayer2ImplSetUpParts     = nullptr;
        g_FoxPath_Path                  = nullptr;
        g_CapturedBlockController       = nullptr;

        outfit::shadow::ResetAll("Uninstall_OutfitRuntimeParts_Hooks");
        outfit::shadow::ResetArmTierCache();

        Log("[OutfitRuntimeParts] removed\n");
    }
}
