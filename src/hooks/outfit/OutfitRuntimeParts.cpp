#include "pch.h"

#include "OutfitRuntimeParts.h"
#include "OutfitRegistry.h"

#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{


    using FoxPath_Path_t = void* (__fastcall*)(void* outPath,
                                                std::uint64_t code64ext);

    using LoadPlayerPartsParts_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType);

    using LoadPlayerPartsFpk_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType);

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
        std::uint32_t playerPartsType, std::uint32_t playerFaceId,
        char playerFaceEquipId);


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

    using LoadPartsNew_t = void (__fastcall*)(
        void* self, std::uint32_t playerIndex,
        LoadPartsPlayerInfo* info, std::uint32_t flags);


    using DoesNeedFaceFova_t = std::uint8_t (__fastcall*)(std::uint32_t playerPartsType);

    using SetHandSlotEnabled_t = void (__fastcall*)(void* self, std::uint32_t slot, std::uint8_t enabled);

    using IsArtificialHandEnabled_t = std::uint8_t (__fastcall*)(std::uint32_t playerType, std::uint32_t playerPartsType);

    using IsArtificialHandEnabledForCurrent_t = std::uint8_t (__fastcall*)();

    using ProcessSignal_t = void (__fastcall*)(void* p1, void* p2, std::uint32_t slot, std::uint64_t* signalPtr);

    constexpr std::uint64_t kSignalRefreshFv2s = 0x8483a342fa61ull;
    constexpr std::size_t kP2GO_OffPerPlayerStruct = 0x80;
    constexpr std::size_t kP2GO_OffStateMachinePtr = 0xb0;
    constexpr std::size_t kPP_OffPlayerTypeArr = 0x40;
    constexpr std::size_t kPP_OffPartsTypeArr = 0x48;
    constexpr std::size_t kPP_OffCamoTypeArr  = 0x50;
    constexpr std::size_t kPP_OffArmTypeArr   = 0x58;
    constexpr std::size_t kPP_OffStateChangedBits = 0x180;
    constexpr std::size_t kPP_OffAltStateBits = 0x184;
    constexpr std::size_t kPP_OffLoadoutReq   = 0xc0;
    constexpr std::size_t kPP_LoadoutReqStride = 0x3a;
    constexpr std::size_t kPP_LoadoutReqEquipHashOff = 0x8;
    constexpr std::uint8_t kProcessSignalSpoofPartsType = 0x01;

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
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            result = 0;
        }
        return result;
    }

    using UpdatePartsStatus_t = void (__fastcall*)(void* self);

    using Player2ImplSetUpParts_t = bool (__fastcall*)(
        void* self,
        std::uint32_t slot,
        std::uint32_t playerType,
        std::uint32_t partsType,
        std::uint32_t camo,
        std::uint32_t armType,
        std::uint32_t faceId,
        void* avatarInfo);

    static FoxPath_Path_t                   g_FoxPath_Path                       = nullptr;
    static LoadPlayerPartsParts_t           g_OrigLoadPartsParts                 = nullptr;
    static LoadPlayerPartsFpk_t             g_OrigLoadPartsFpk                   = nullptr;
    static LoadPlayerCamoFpk_t              g_OrigLoadCamoFpk                    = nullptr;
    static LoadPlayerSnakeBlackDiamondFpk_t g_OrigLoadDiamondFpk                 = nullptr;
    static LoadPlayerBionicArm_t            g_OrigLoadBionicArmFv2               = nullptr;
    static LoadPlayerBionicArm_t            g_OrigLoadBionicArmFpk               = nullptr;
    static LoadPlayerSnakeFace_t            g_OrigLoadSnakeFaceFv2               = nullptr;
    static LoadPlayerSnakeFace_t            g_OrigLoadSnakeFaceFpk               = nullptr;
    static LoadPartsNew_t                   g_OrigLoadPartsNew                   = nullptr;
    static DoesNeedFaceFova_t               g_OrigDoesNeedFaceFova               = nullptr;
    static DoesNeedFaceFova_t               g_OrigDoesNeedFaceFovaForAvatar      = nullptr;
    static SetHandSlotEnabled_t             g_OrigSetHandSlotEnabled             = nullptr;
    static IsArtificialHandEnabled_t        g_OrigIsArtificialHandEnabled        = nullptr;
    static IsArtificialHandEnabledForCurrent_t g_OrigIsArtificialHandForCurrent  = nullptr;
    static ProcessSignal_t                  g_OrigProcessSignal                  = nullptr;
    static UpdatePartsStatus_t              g_OrigUpdatePartsStatus              = nullptr;
    static Player2ImplSetUpParts_t          g_OrigPlayer2ImplSetUpParts          = nullptr;

    static bool g_InstalledParts          = false;
    static bool g_InstalledFpk            = false;
    static bool g_InstalledCamo           = false;
    static bool g_InstalledDiamond        = false;
    static bool g_InstalledBionicArmFv2   = false;
    static bool g_InstalledBionicArmFpk   = false;
    static bool g_InstalledSnakeFaceFv2   = false;
    static bool g_InstalledSnakeFaceFpk   = false;
    static bool g_InstalledLpn                  = false;
    static bool g_InstalledDoesNeedFace         = false;
    static bool g_InstalledDoesNeedFaceForAvatar = false;
    static bool g_InstalledSetHandSlotEnabled   = false;
    static bool g_InstalledIsArtificialHand     = false;
    static bool g_InstalledIsArtHandForCurrent  = false;
    static bool g_InstalledProcessSignal        = false;
    static bool g_InstalledUpdatePartsStatus    = false;
    static bool g_InstalledPlayer2ImplSetUpParts = false;


    static void* g_CapturedBlockController = nullptr;


    static thread_local std::uint8_t tl_SpoofedRealPartsType = 0;


    static std::atomic<std::uint16_t> g_RecentForcePartsReloadDevId{0};


    static std::int16_t  g_LastInfoFaceId        = 0;
    static std::uint16_t g_LastInfoFaceEquipId   = 0;
    static std::uint8_t  g_LastInfoFaceUnk       = 0;
    static bool          g_LastInfoCaptured      = false;

    // Per-playerType (must NOT be global — slot 0 / slot 1 iteration in
    // UpdatePartsStatus would clobber each other otherwise).
    static constexpr std::size_t kArmTierByPlayerTypeMax = 4;
    static std::uint8_t  g_LastInfoArmType_byPT[kArmTierByPlayerTypeMax]      = {0,0,0,0};
    static bool          g_LastInfoArmCaptured_byPT[kArmTierByPlayerTypeMax]  = {false,false,false,false};

    static void GetCachedArmTierForPlayerType(
        std::uint32_t playerType, std::uint8_t* outTier, bool* outCaptured)
    {
        const std::uint32_t pt = playerType & 0xFF;
        if (pt < kArmTierByPlayerTypeMax)
        {
            if (outTier)     *outTier     = g_LastInfoArmType_byPT[pt];
            if (outCaptured) *outCaptured = g_LastInfoArmCaptured_byPT[pt];
        }
        else
        {
            if (outTier)     *outTier     = 0;
            if (outCaptured) *outCaptured = false;
        }
    }

    static void SetCachedArmTierForPlayerType(
        std::uint32_t playerType, std::uint8_t tier)
    {
        const std::uint32_t pt = playerType & 0xFF;
        if (pt < kArmTierByPlayerTypeMax)
        {
            g_LastInfoArmType_byPT[pt]     = tier;
            g_LastInfoArmCaptured_byPT[pt] = true;
        }
    }


    static std::uint32_t EffectivePartsType(std::uint32_t paramPartsType)
    {
        if (tl_SpoofedRealPartsType >= outfit::kCustomPartsTypeStart
         && tl_SpoofedRealPartsType <= outfit::kCustomPartsTypeEnd)
        {
            return tl_SpoofedRealPartsType;
        }
        return paramPartsType;
    }

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

    static bool ResolveCustomEntry(
        std::uint32_t playerType, std::uint32_t playerPartsType,
        const outfit::OutfitEntry** outEntry)
    {
        const auto pt = static_cast<std::uint8_t>(playerPartsType & 0xFF);
        const auto ply = static_cast<std::uint8_t>(playerType & 0xFF);

        if (pt < outfit::kCustomPartsTypeStart || pt > outfit::kCustomPartsTypeEnd)
            return false;

        const outfit::OutfitEntry* entry = nullptr;
        if (!outfit::TryGetOutfitByPartsType(pt, &entry) || !entry) return false;
        if (!outfit::IsPlayerTypeCompatible(entry->playerType, ply)) return false;

        if (outEntry) *outEntry = entry;
        return true;
    }

    static std::uint64_t* __fastcall hkLoadPlayerPartsParts(
        std::uint64_t* outPath, std::uint32_t playerType, std::uint32_t playerPartsType)
    {


        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);

        const outfit::OutfitEntry* entry = nullptr;
        if (ResolveCustomEntry(playerType, effectivePartsType, &entry))
        {
            const std::uint8_t v = entry->HasVariants()
                ? outfit::GetActiveVariant(entry->partsType)
                : 0;
            const std::uint64_t path = entry->GetVariantPartsPath(v);


            static std::uint32_t s_lastPlayerType   = 0xFFFFFFFFu;
            static std::uint32_t s_lastPartsType    = 0xFFFFFFFFu;
            static std::uint8_t  s_lastVariant      = 0xFFu;
            static std::uint64_t s_lastPath         = 0;
            if (s_lastPlayerType != playerType
                || s_lastPartsType != effectivePartsType
                || s_lastVariant   != v
                || s_lastPath      != path)
            {
                Log("[OutfitRuntimeParts] LoadPlayerPartsParts: playerType=%u "
                    "partsType=0x%02X variant=%u -> custom path=0x%016llX (developId=%u)%s\n",
                    playerType, effectivePartsType & 0xFFu,
                    static_cast<unsigned>(v),
                    static_cast<unsigned long long>(path),
                    static_cast<unsigned>(entry->developId),
                    (effectivePartsType != playerPartsType) ? " [via spoof]" : "");
                s_lastPlayerType = playerType;
                s_lastPartsType  = effectivePartsType;
                s_lastVariant    = v;
                s_lastPath       = path;
            }
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
            const std::uint8_t v = entry->HasVariants()
                ? outfit::GetActiveVariant(entry->partsType)
                : 0;
            const std::uint64_t path = entry->GetVariantFpkPath(v);
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
            const std::uint8_t v = entry->HasVariants()
                ? outfit::GetActiveVariant(entry->partsType)
                : 0;
            const std::uint64_t camo = entry->GetVariantCamoFpk(v);

            if (camo > outfit::kSubAssetUseVanilla)
                return WriteFoxPath(outPath, camo);


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
            const std::uint8_t v = entry->HasVariants()
                ? outfit::GetActiveVariant(entry->partsType)
                : 0;
            const std::uint64_t diamond = entry->GetVariantDiamondFpk(v);

            if (diamond == outfit::kSubAssetDisabled)
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
            if (diamond > outfit::kSubAssetUseVanilla)
                return WriteFoxPath(outPath, diamond);

        }
        return g_OrigLoadDiamondFpk(outPath, playerType, playerPartsType,
                                    applyBlackDiamond);
    }


    // Route custom outfits through 0x01 (STANDARD Snake) since the leaf
    // rejects partsType outside its whitelist; arm asset is selected by
    // handType*2, not partsType.
    constexpr std::uint32_t kBionicArmVanillaPartsTypeSubstitute = 0x01;

    static std::uint64_t* __fastcall hkLoadPlayerBionicArmFv2(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerHandType)
    {
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);

        if (effectivePartsType >= outfit::kCustomPartsTypeStart
         && effectivePartsType <= outfit::kCustomPartsTypeEnd)
        {
            const auto pt = static_cast<std::uint8_t>(effectivePartsType & 0xFF);
            const auto ply = static_cast<std::uint8_t>(playerType & 0xFF);
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry
                && outfit::IsPlayerTypeCompatible(entry->playerType, ply))
            {
                if (!entry->IsArmEnabled())
                {
                    return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
                }
                // Engine zeroes handType for custom partsType (the leaf
                // would then load NULL); recover from per-PT cache.
                std::uint32_t effectiveHandType = playerHandType;
                if (effectiveHandType == 0)
                {
                    std::uint8_t cachedTier = 0;
                    bool cachedFlag = false;
                    GetCachedArmTierForPlayerType(playerType, &cachedTier, &cachedFlag);
                    effectiveHandType = cachedFlag
                        ? static_cast<std::uint32_t>(cachedTier)
                        : 1u;
                }
                return g_OrigLoadBionicArmFv2(
                    outPath, playerType,
                    kBionicArmVanillaPartsTypeSubstitute,
                    effectiveHandType);
            }
        }
        return g_OrigLoadBionicArmFv2(outPath, playerType,
                                      playerPartsType, playerHandType);
    }

    static std::uint64_t* __fastcall hkLoadPlayerBionicArmFpk(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerHandType)
    {
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);

        if (effectivePartsType >= outfit::kCustomPartsTypeStart
         && effectivePartsType <= outfit::kCustomPartsTypeEnd)
        {
            const auto pt = static_cast<std::uint8_t>(effectivePartsType & 0xFF);
            const auto ply = static_cast<std::uint8_t>(playerType & 0xFF);
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry
                && outfit::IsPlayerTypeCompatible(entry->playerType, ply))
            {
                if (!entry->IsArmEnabled())
                {
                    return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
                }
                std::uint32_t effectiveHandType = playerHandType;
                if (effectiveHandType == 0)
                {
                    std::uint8_t cachedTier = 0;
                    bool cachedFlag = false;
                    GetCachedArmTierForPlayerType(playerType, &cachedTier, &cachedFlag);
                    effectiveHandType = cachedFlag
                        ? static_cast<std::uint32_t>(cachedTier)
                        : 1u;
                }
                std::uint64_t* result = g_OrigLoadBionicArmFpk(
                    outPath, playerType,
                    kBionicArmVanillaPartsTypeSubstitute,
                    effectiveHandType);
                Log("[OutfitRuntimeParts:BionicArmFpk] partsType=0x%02X "
                    "developId=%u IsArmEnabled=true -> orig(playerType=%u, "
                    "partsType=0x%02X[substitute], handType=%u%s) returned "
                    "path=0x%016llX\n",
                    static_cast<unsigned>(pt),
                    static_cast<unsigned>(entry->developId),
                    playerType,
                    static_cast<unsigned>(kBionicArmVanillaPartsTypeSubstitute),
                    effectiveHandType,
                    (effectiveHandType != playerHandType)
                        ? " [substituted from 0; engine zeroes armType for custom partsType]"
                        : "",
                    result ? static_cast<unsigned long long>(*result) : 0ull);
                return result;
            }
        }
        return g_OrigLoadBionicArmFpk(outPath, playerType,
                                      playerPartsType, playerHandType);
    }

    static std::uint64_t* __fastcall hkLoadPlayerSnakeFaceFv2(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerFaceId,
        char playerFaceEquipId)
    {
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);

        if (effectivePartsType >= outfit::kCustomPartsTypeStart
         && effectivePartsType <= outfit::kCustomPartsTypeEnd)
        {
            const auto pt  = static_cast<std::uint8_t>(effectivePartsType & 0xFF);
            const auto ply = static_cast<std::uint8_t>(playerType & 0xFF);
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry
                && outfit::IsPlayerTypeCompatible(entry->playerType, ply))
            {
                if (!entry->IsHeadEnabled())
                {
                    return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
                }
                return g_OrigLoadSnakeFaceFv2(
                    outPath, playerType,
                    kBionicArmVanillaPartsTypeSubstitute,
                    playerFaceId, playerFaceEquipId);
            }
        }
        return g_OrigLoadSnakeFaceFv2(outPath, playerType,
                                      playerPartsType, playerFaceId,
                                      playerFaceEquipId);
    }

    static std::uint64_t* __fastcall hkLoadPlayerSnakeFaceFpk(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerFaceId,
        char playerFaceEquipId)
    {
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);

        if (effectivePartsType >= outfit::kCustomPartsTypeStart
         && effectivePartsType <= outfit::kCustomPartsTypeEnd)
        {
            const auto pt  = static_cast<std::uint8_t>(effectivePartsType & 0xFF);
            const auto ply = static_cast<std::uint8_t>(playerType & 0xFF);
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry
                && outfit::IsPlayerTypeCompatible(entry->playerType, ply))
            {
                if (!entry->IsHeadEnabled())
                {
                    return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
                }
                return g_OrigLoadSnakeFaceFpk(
                    outPath, playerType,
                    kBionicArmVanillaPartsTypeSubstitute,
                    playerFaceId, playerFaceEquipId);
            }
        }
        return g_OrigLoadSnakeFaceFpk(outPath, playerType,
                                      playerPartsType, playerFaceId,
                                      playerFaceEquipId);
    }


    static std::uint8_t __fastcall hkDoesNeedFaceFova(std::uint32_t playerPartsType)
    {


        const std::uint32_t effective = EffectivePartsType(playerPartsType);

        if (effective >= outfit::kCustomPartsTypeStart
         && effective <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            const auto pt = static_cast<std::uint8_t>(effective & 0xFF);
            const bool found = outfit::TryGetOutfitByPartsType(pt, &entry)
                            && entry;
            const bool enabled = found && entry->IsHeadEnabled();


            const bool spoofActive = (effective != playerPartsType);
            static std::uint32_t s_lastPartsType = 0xFFFFFFFFu;
            static int           s_lastFound    = -1;
            static int           s_lastEnabled  = -1;
            static int           s_lastSpoof    = -1;
            if (s_lastPartsType != effective
                || s_lastFound  != (found       ? 1 : 0)
                || s_lastEnabled!= (enabled     ? 1 : 0)
                || s_lastSpoof  != (spoofActive ? 1 : 0))
            {
                Log("[OutfitRuntimeParts] hkDoesNeedFaceFova: partsType=0x%X "
                    "(effective=0x%X) found=%d enableHead=%d spoof=%d -> %s\n",
                    playerPartsType,
                    effective,
                    found ? 1 : 0,
                    enabled ? 1 : 0,
                    spoofActive ? 1 : 0,
                    found
                        ? "1 (registered outfit -> proceed with reload; "
                          "face suppressed at info layer for !enableHead)"
                        : "fall-through to orig");
                s_lastPartsType = effective;
                s_lastFound     = found   ? 1 : 0;
                s_lastEnabled   = enabled ? 1 : 0;
                s_lastSpoof     = spoofActive ? 1 : 0;
            }

            if (found)
            {


                return enabled ? std::uint8_t{1} : std::uint8_t{0};
            }


        }
        return g_OrigDoesNeedFaceFova
             ? g_OrigDoesNeedFaceFova(playerPartsType)
             : 0;
    }

    static std::uint8_t __fastcall hkDoesNeedFaceFovaForAvatar(
        std::uint32_t playerPartsType)
    {
        const std::uint32_t effective = EffectivePartsType(playerPartsType);

        if (effective >= outfit::kCustomPartsTypeStart
         && effective <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            const auto pt = static_cast<std::uint8_t>(effective & 0xFF);
            const bool found = outfit::TryGetOutfitByPartsType(pt, &entry)
                            && entry;
            const bool enabled = found && entry->IsHeadEnabled();


            const bool spoofActive = (effective != playerPartsType);
            static std::uint32_t s_lastPartsType = 0xFFFFFFFFu;
            static int           s_lastFound    = -1;
            static int           s_lastEnabled  = -1;
            static int           s_lastSpoof    = -1;
            if (s_lastPartsType != effective
                || s_lastFound  != (found       ? 1 : 0)
                || s_lastEnabled!= (enabled     ? 1 : 0)
                || s_lastSpoof  != (spoofActive ? 1 : 0))
            {
                Log("[OutfitRuntimeParts] hkDoesNeedFaceFovaForAvatar: "
                    "partsType=0x%X (effective=0x%X) found=%d enableHead=%d "
                    "spoof=%d -> %s\n",
                    playerPartsType, effective,
                    found       ? 1 : 0,
                    enabled     ? 1 : 0,
                    spoofActive ? 1 : 0,
                    found
                        ? "registered outfit -> proceed with Avatar face load"
                        : "fall-through to orig");
                s_lastPartsType = effective;
                s_lastFound     = found   ? 1 : 0;
                s_lastEnabled   = enabled ? 1 : 0;
                s_lastSpoof     = spoofActive ? 1 : 0;
            }

            if (found)
            {
                return enabled ? std::uint8_t{1} : std::uint8_t{0};
            }
        }
        return g_OrigDoesNeedFaceFovaForAvatar
             ? g_OrigDoesNeedFaceFovaForAvatar(playerPartsType)
             : 0;
    }

    static void __fastcall hkSetHandSlotEnabled(
        void* self_equipController,
        std::uint32_t slot,
        std::uint8_t  enabled)
    {
        if (enabled != 0)
        {
            if (g_OrigSetHandSlotEnabled)
                g_OrigSetHandSlotEnabled(self_equipController, slot, enabled);
            return;
        }

        const std::uint8_t livePT = outfit::ReadLivePlayerType();
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
                && outfit::IsPlayerTypeCompatible(entry->playerType, livePT)
                && entry->IsArmEnabled())
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
        std::uint32_t playerType,
        std::uint32_t playerPartsType)
    {
        if ((playerType == outfit::kPlayerType_Snake
              || playerType == outfit::kPlayerType_Avatar)
         && playerPartsType >= outfit::kCustomPartsTypeStart
         && playerPartsType <= outfit::kCustomPartsTypeEnd)
        {
            const auto pt  = static_cast<std::uint8_t>(playerPartsType & 0xFF);
            const auto ply = static_cast<std::uint8_t>(playerType & 0xFF);
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry
                && outfit::IsPlayerTypeCompatible(entry->playerType, ply)
                && entry->IsArmEnabled())
            {
                static std::uint32_t s_lastPlayerType = 0xFFFFFFFFu;
                static std::uint32_t s_lastPartsType  = 0xFFFFFFFFu;
                if (s_lastPlayerType != playerType
                 || s_lastPartsType  != playerPartsType)
                {
                    Log("[OutfitRuntimeParts:IsArtHand] playerType=%u "
                        "partsType=0x%02X developId=%u — overriding to 1 "
                        "[unblocks per-frame arm-equip dispatch in "
                        "FUN_1412a2f80, makes the bionic arm actually render]\n",
                        static_cast<unsigned>(playerType),
                        static_cast<unsigned>(playerPartsType),
                        static_cast<unsigned>(entry->developId));
                    s_lastPlayerType = playerType;
                    s_lastPartsType  = playerPartsType;
                }
                return 1;
            }
        }

        return g_OrigIsArtificialHandEnabled
             ? g_OrigIsArtificialHandEnabled(playerType, playerPartsType)
             : 0;
    }

    static std::uint8_t __fastcall hkIsArtificialHandEnabledForCurrentPlayerType()
    {
        const std::uint8_t livePT        = outfit::ReadLivePlayerType();
        const std::uint8_t livePartsType = outfit::ReadLivePartsType();

        if ((livePT == outfit::kPlayerType_Snake
              || livePT == outfit::kPlayerType_Avatar)
         && livePartsType >= outfit::kCustomPartsTypeStart
         && livePartsType <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(livePartsType, &entry) && entry
                && outfit::IsPlayerTypeCompatible(entry->playerType, livePT)
                && entry->IsArmEnabled())
            {
                static std::uint8_t s_lastLivePT       = 0xFFu;
                static std::uint8_t s_lastLivePartsT   = 0xFFu;
                if (s_lastLivePT != livePT || s_lastLivePartsT != livePartsType)
                {
                    Log("[OutfitRuntimeParts:IsArtHandLive] livePT=%u "
                        "livePartsType=0x%02X developId=%u — overriding "
                        "ForCurrentPlayerType to 1 [unblocks live-state "
                        "callers throughout engine, including iDroid UI's "
                        "arm-module compatibility check]\n",
                        static_cast<unsigned>(livePT),
                        static_cast<unsigned>(livePartsType),
                        static_cast<unsigned>(entry->developId));
                    s_lastLivePT     = livePT;
                    s_lastLivePartsT = livePartsType;
                }
                return 1;
            }
        }

        return g_OrigIsArtificialHandForCurrent
             ? g_OrigIsArtificialHandForCurrent()
             : 0;
    }

    static void __fastcall hkProcessSignal(
        void* param_1,
        void* param_2,
        std::uint32_t slot,
        std::uint64_t* signalPtr)
    {
        if (!g_OrigProcessSignal)
            return;

        if (signalPtr)
        {
            const std::uint64_t sig = *signalPtr;
            static struct { std::uint32_t slot; std::uint64_t sig; } seen[32] = {};
            static std::size_t seenCount = 0;
            bool already = false;
            for (std::size_t i = 0; i < seenCount; ++i)
            {
                if (seen[i].slot == slot && seen[i].sig == sig)
                {
                    already = true;
                    break;
                }
            }
            if (!already && seenCount < (sizeof(seen) / sizeof(seen[0])))
            {
                seen[seenCount].slot = slot;
                seen[seenCount].sig  = sig;
                ++seenCount;
                Log("[OutfitRuntimeParts:ProcessSignal] slot=%u signal=0x%016llX "
                    "(first occurrence)\n",
                    slot, static_cast<unsigned long long>(sig));
            }
        }

        if (!signalPtr || !param_1 || *signalPtr != kSignalRefreshFv2s)
        {
            g_OrigProcessSignal(param_1, param_2, slot, signalPtr);
            return;
        }

        std::uint8_t* partsTypeArr = nullptr;
        std::uint8_t  origPartsType = 0;
        bool          spoofed = false;

        __try
        {
            void* perPlayer = *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(param_1) + kP2GO_OffPerPlayerStruct);
            if (perPlayer)
            {
                partsTypeArr = *reinterpret_cast<std::uint8_t**>(
                    reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffPartsTypeArr);
                if (partsTypeArr)
                {
                    origPartsType = partsTypeArr[slot];
                    if (origPartsType >= outfit::kCustomPartsTypeStart
                     && origPartsType <= outfit::kCustomPartsTypeEnd)
                    {
                        const outfit::OutfitEntry* entry = nullptr;
                        if (outfit::TryGetOutfitByPartsType(origPartsType, &entry)
                            && entry)
                        {
                            // Set TLS so leaf hooks invoked inside orig recover
                            // the real custom partsType (otherwise they'd see
                            // the spoofed 0x01 and load vanilla assets).
                            tl_SpoofedRealPartsType = origPartsType;
                            partsTypeArr[slot] = kProcessSignalSpoofPartsType;
                            spoofed = true;

                            static std::uint32_t s_lastSlot      = 0xFFFFFFFFu;
                            static std::uint8_t  s_lastPartsType = 0xFFu;
                            if (s_lastSlot != slot
                             || s_lastPartsType != origPartsType)
                            {
                                Log("[OutfitRuntimeParts:ProcessSignal] "
                                    "signal=0x8483a342fa61 slot=%u partsType=0x%02X "
                                    "developId=%u — spoofing partsType byte to 0x%02X "
                                    "for the duration of orig (TLS=0x%02X) so the "
                                    "<0x1C gate passes and InitLoadPlayerFv2s wires "
                                    "the Fv2 attachments into the visible scene\n",
                                    slot,
                                    static_cast<unsigned>(origPartsType),
                                    static_cast<unsigned>(entry->developId),
                                    static_cast<unsigned>(kProcessSignalSpoofPartsType),
                                    static_cast<unsigned>(tl_SpoofedRealPartsType));
                                s_lastSlot      = slot;
                                s_lastPartsType = origPartsType;
                            }
                        }
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitRuntimeParts:ProcessSignal] SEH while pre-orig "
                "spoofing — falling through to orig untouched\n");
            spoofed = false;
        }

        g_OrigProcessSignal(param_1, param_2, slot, signalPtr);

        if (spoofed)
        {
            __try
            {
                if (partsTypeArr)
                    partsTypeArr[slot] = origPartsType;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                Log("[OutfitRuntimeParts:ProcessSignal] SEH while post-orig "
                    "restoring partsType byte\n");
            }
            tl_SpoofedRealPartsType = 0;
        }
    }

    static void __fastcall hkUpdatePartsStatus(void* self)
    {
        if (!g_OrigUpdatePartsStatus) return;
        if (!self)
        {
            return;
        }

        constexpr std::size_t kMaxSlots = 4;
        struct SlotOverride
        {
            bool         active;
            std::uint8_t restoreValue;
            // INCLUDES first-encounter (last=0xFF differs from any real tier).
            bool         tierJustChanged;
            // EXCLUDES first-encounter: gates the cascade-drive that must
            // not fire during the initial outfit-equip pass.
            bool         genuineTierChange;
        };
        SlotOverride overrides[kMaxSlots] = {};
        std::uint8_t* armTypeArr = nullptr;

        __try
        {
            void* perPlayer = *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(self) + kP2GO_OffPerPlayerStruct);
            std::uint8_t* stateMachineArr =
                *reinterpret_cast<std::uint8_t**>(
                    reinterpret_cast<std::uint8_t*>(self) + kP2GO_OffStateMachinePtr);
            if (perPlayer)
            {
                std::uint8_t* partsTypeArr =
                    *reinterpret_cast<std::uint8_t**>(
                        reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffPartsTypeArr);
                std::uint8_t* playerTypeArr =
                    *reinterpret_cast<std::uint8_t**>(
                        reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffPlayerTypeArr);
                armTypeArr =
                    *reinterpret_cast<std::uint8_t**>(
                        reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffArmTypeArr);
                std::uint32_t* stateChangedBits =
                    reinterpret_cast<std::uint32_t*>(
                        reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffStateChangedBits);
                std::uint32_t* altStateBits =
                    reinterpret_cast<std::uint32_t*>(
                        reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffAltStateBits);

                if (partsTypeArr && playerTypeArr && armTypeArr)
                {
                    for (std::size_t i = 0; i < kMaxSlots; ++i)
                    {
                        const std::uint8_t pt  = partsTypeArr[i];
                        const std::uint8_t ply = playerTypeArr[i];

                        if (pt < outfit::kCustomPartsTypeStart
                         || pt > outfit::kCustomPartsTypeEnd)
                            continue;
                        if (ply != outfit::kPlayerType_Snake
                         && ply != outfit::kPlayerType_Avatar)
                            continue;

                        const outfit::OutfitEntry* entry = nullptr;
                        if (!outfit::TryGetOutfitByPartsType(pt, &entry) || !entry)
                            continue;
                        if (!outfit::IsPlayerTypeCompatible(entry->playerType, ply))
                            continue;
                        if (!entry->IsArmEnabled())
                            continue;

                        // Tier resolution: live equipHash → cached → fallback 1.
                        // Live read also seeds the cache so cold-start saves
                        // get the right tier without a vanilla→custom round trip.
                        std::uint8_t liveTier =
                            ReadLiveArmTierFromLoadoutRequest(self, i);
                        std::uint8_t cachedTierForPT = 0;
                        bool         cachedFlagForPT = false;
                        GetCachedArmTierForPlayerType(
                            ply, &cachedTierForPT, &cachedFlagForPT);
                        std::uint8_t resolvedTier =
                            (liveTier > 0) ? liveTier
                          : (cachedFlagForPT ? cachedTierForPT
                                             : std::uint8_t{1});
                        // PER playerType: a global cache would let slot 1's
                        // default tier 1 clobber Snake's developed tier when
                        // SetUpParts later fires for slot 0.
                        if (liveTier > 0)
                        {
                            SetCachedArmTierForPlayerType(ply, liveTier);
                        }

                        overrides[i].active = true;
                        overrides[i].restoreValue = resolvedTier;

                        // Tier-change tick: don't pre-zero so orig's compare
                        // sees the change and fires exactly one reload.
                        static std::uint8_t s_lastSeenTier[kMaxSlots] =
                            {0xFFu, 0xFFu, 0xFFu, 0xFFu};
                        const bool firstEncounter =
                            (s_lastSeenTier[i] == 0xFFu);
                        const bool tierChanged =
                            (s_lastSeenTier[i] != resolvedTier);
                        overrides[i].tierJustChanged   = tierChanged;
                        overrides[i].genuineTierChange =
                            !firstEncounter && tierChanged;
                        if (!tierChanged)
                        {
                            armTypeArr[i] = 0;
                        }
                        s_lastSeenTier[i] = resolvedTier;

                        static std::uint8_t s_lastPartsType[kMaxSlots] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
                        static std::uint8_t s_lastTier     [kMaxSlots] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
                        if (s_lastPartsType[i] != pt
                         || s_lastTier[i] != overrides[i].restoreValue)
                        {
                            Log("[OutfitRuntimeParts:UpdatePartsStatus] slot=%zu "
                                "partsType=0x%02X (custom, developId=%u) "
                                "playerType=%u — armType=%u (liveEquipTier=%u, "
                                "cached_pt=%u/captured=%d) %s\n",
                                i,
                                static_cast<unsigned>(pt),
                                static_cast<unsigned>(entry->developId),
                                static_cast<unsigned>(ply),
                                static_cast<unsigned>(overrides[i].restoreValue),
                                static_cast<unsigned>(liveTier),
                                static_cast<unsigned>(cachedTierForPT),
                                cachedFlagForPT ? 1 : 0,
                                tierChanged
                                    ? "[TIER CHANGED -> letting orig "
                                      "state-changed fire ONE reload, then "
                                      "back to steady-state suppression]"
                                    : "[steady-state -> pre-orig zero "
                                      "suppresses cascade]");
                            s_lastPartsType[i] = pt;
                            s_lastTier[i]      = overrides[i].restoreValue;
                        }

                        if (overrides[i].genuineTierChange
                         && stateMachineArr
                         && stateChangedBits
                         && altStateBits)
                        {
                            const std::uint8_t  st0 =
                                stateMachineArr[0];
                            const std::uint8_t  st1 =
                                stateMachineArr[1];
                            const std::uint8_t  stI =
                                stateMachineArr[i];
                            const std::uint32_t bits180 =
                                *stateChangedBits;
                            const std::uint32_t bits184 =
                                *altStateBits;
                            const std::uint32_t actionState =
                                *reinterpret_cast<std::uint32_t*>(
                                    reinterpret_cast<std::uint8_t*>(self) + 0x204);
                            const std::uint32_t slotBit =
                                1u << static_cast<std::uint32_t>(i);

                            std::uint16_t outerGate_0x90 = 0xFFFF;
                            std::uint8_t  outerGate_0x98 = 0xFF;
                            std::uintptr_t base_0x398 = 0;
                            std::uintptr_t plVar21_computed = 0;
                            std::uint8_t  pp40 = 0xFF;
                            std::uint8_t  pp48 = 0xFF;
                            std::uint8_t  pp50 = 0xFF;
                            std::uint8_t  pp68 = 0xFF;
                            __try
                            {
                                std::uint16_t* arr90 =
                                    *reinterpret_cast<std::uint16_t**>(
                                        reinterpret_cast<std::uint8_t*>(self) + 0x90);
                                if (arr90)
                                    outerGate_0x90 = arr90[i];

                                std::uint8_t* arr98 =
                                    *reinterpret_cast<std::uint8_t**>(
                                        reinterpret_cast<std::uint8_t*>(self) + 0x98);
                                if (arr98)
                                    outerGate_0x98 = arr98[i];

                                base_0x398 = *reinterpret_cast<std::uintptr_t*>(
                                    reinterpret_cast<std::uint8_t*>(self) + 0x398);
                                plVar21_computed =
                                    static_cast<std::uintptr_t>(outerGate_0x98) * 0x6a8
                                    + base_0x398;

                                std::uint8_t* pp40Arr =
                                    *reinterpret_cast<std::uint8_t**>(
                                        reinterpret_cast<std::uint8_t*>(perPlayer)
                                        + kPP_OffPlayerTypeArr);
                                if (pp40Arr) pp40 = pp40Arr[i];

                                std::uint8_t* pp48Arr =
                                    *reinterpret_cast<std::uint8_t**>(
                                        reinterpret_cast<std::uint8_t*>(perPlayer)
                                        + kPP_OffPartsTypeArr);
                                if (pp48Arr) pp48 = pp48Arr[i];

                                std::uint8_t* pp50Arr =
                                    *reinterpret_cast<std::uint8_t**>(
                                        reinterpret_cast<std::uint8_t*>(perPlayer)
                                        + kPP_OffCamoTypeArr);
                                if (pp50Arr) pp50 = pp50Arr[i];

                                std::uint8_t* pp68Arr =
                                    *reinterpret_cast<std::uint8_t**>(
                                        reinterpret_cast<std::uint8_t*>(perPlayer)
                                        + 0x68);
                                if (pp68Arr) pp68 = pp68Arr[i];
                            }
                            __except (EXCEPTION_EXECUTE_HANDLER) {}

                            Log("[OutfitRuntimeParts:UpdatePartsStatus:Diag] "
                                "PRE-orig slot=%zu (genuine tier change, restoredTier=%u) | "
                                "state[0]=%u state[1]=%u state[i]=%u | "
                                "+0x180=0x%08X (slotBit=0x%X, set=%d) | "
                                "+0x184=0x%08X (slotBit=0x%X, set=%d) | "
                                "actionState[+0x204]=0x%08X (bit0xd=%d, bit0xe=%d, bit0x8=%d, bit0x12=%d) | "
                                "armTypeArr[i] (after pre-orig leave-alone)=%u\n",
                                i,
                                static_cast<unsigned>(overrides[i].restoreValue),
                                static_cast<unsigned>(st0),
                                static_cast<unsigned>(st1),
                                static_cast<unsigned>(stI),
                                static_cast<unsigned>(bits180),
                                static_cast<unsigned>(slotBit),
                                (bits180 & slotBit) ? 1 : 0,
                                static_cast<unsigned>(bits184),
                                static_cast<unsigned>(slotBit),
                                (bits184 & slotBit) ? 1 : 0,
                                static_cast<unsigned>(actionState),
                                static_cast<int>((actionState >> 0xd) & 1),
                                static_cast<int>((actionState >> 0xe) & 1),
                                static_cast<int>((actionState >> 8) & 1),
                                static_cast<int>((actionState >> 0x12) & 1),
                                static_cast<unsigned>(armTypeArr[i]));

                            Log("[OutfitRuntimeParts:UpdatePartsStatus:Diag] "
                                "PRE-orig slot=%zu (cont'd) | "
                                "*(self+0x90)[i*2]=0x%04X (outer-if cond1: !=0xffff -> %s) | "
                                "*(self+0x98)[i]=0x%02X (outer-if cond2: !=0xff -> %s) | "
                                "*(self+0x398)=0x%016llX | "
                                "plVar21=0x%016llX (outer-if cond3: !=0 -> %s) | "
                                "pp+0x40[i] playerType=%u | "
                                "pp+0x48[i] partsType=0x%02X | "
                                "pp+0x50[i] camo=0x%02X | "
                                "pp+0x68[i] faceUnk=0x%02X\n",
                                i,
                                static_cast<unsigned>(outerGate_0x90),
                                outerGate_0x90 != 0xFFFF ? "PASS" : "FAIL",
                                static_cast<unsigned>(outerGate_0x98),
                                outerGate_0x98 != 0xFF ? "PASS" : "FAIL",
                                static_cast<unsigned long long>(base_0x398),
                                static_cast<unsigned long long>(plVar21_computed),
                                plVar21_computed != 0 ? "PASS" : "FAIL",
                                static_cast<unsigned>(pp40),
                                static_cast<unsigned>(pp48),
                                static_cast<unsigned>(pp50),
                                static_cast<unsigned>(pp68));

                            // SAFETY: only set when state==3 — setting during
                            // states {0,1,2,4} can double-unload as the bit
                            // lingers into the next state==3 entry.
                            if (stateMachineArr[i] == 3)
                            {
                                *stateChangedBits |= slotBit;
                                Log("[OutfitRuntimeParts:UpdatePartsStatus] "
                                    "slot=%zu DRIVE CASCADE: state==3 + "
                                    "genuine tier change -> setting +0x180 "
                                    "slotBit=0x%X (orig's natural mechanism "
                                    "didn't fire for custom partsType)\n",
                                    i,
                                    static_cast<unsigned>(slotBit));
                            }
                            else
                            {
                                Log("[OutfitRuntimeParts:UpdatePartsStatus] "
                                    "slot=%zu SKIP cascade-drive: state[i]=%u "
                                    "(needs to be 3) — bit-set deferred until "
                                    "next tier change at steady state\n",
                                    i,
                                    static_cast<unsigned>(stI));
                            }
                        }
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitRuntimeParts:UpdatePartsStatus] SEH during pre-orig "
                "armType byte zero — falling through to orig untouched\n");
            for (std::size_t i = 0; i < kMaxSlots; ++i) overrides[i].active = false;
            armTypeArr = nullptr;
        }

        g_OrigUpdatePartsStatus(self);

        if (armTypeArr)
        {
            __try
            {
                for (std::size_t i = 0; i < kMaxSlots; ++i)
                {
                    if (overrides[i].active)
                        armTypeArr[i] = overrides[i].restoreValue;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                Log("[OutfitRuntimeParts:UpdatePartsStatus] SEH during post-orig "
                    "armType byte restore — gameplay arm-effects may flicker\n");
            }
        }

        __try
        {
            void* perPlayerPost = *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(self) + kP2GO_OffPerPlayerStruct);
            std::uint8_t* stateMachineArrPost =
                *reinterpret_cast<std::uint8_t**>(
                    reinterpret_cast<std::uint8_t*>(self) + kP2GO_OffStateMachinePtr);

            if (perPlayerPost && stateMachineArrPost)
            {
                std::uint32_t* bits180Post =
                    reinterpret_cast<std::uint32_t*>(
                        reinterpret_cast<std::uint8_t*>(perPlayerPost) + kPP_OffStateChangedBits);
                std::uint32_t* bits184Post =
                    reinterpret_cast<std::uint32_t*>(
                        reinterpret_cast<std::uint8_t*>(perPlayerPost) + kPP_OffAltStateBits);

                for (std::size_t i = 0; i < kMaxSlots; ++i)
                {
                    if (!overrides[i].active || !overrides[i].genuineTierChange)
                        continue;

                    const std::uint8_t  st0 = stateMachineArrPost[0];
                    const std::uint8_t  st1 = stateMachineArrPost[1];
                    const std::uint8_t  stI = stateMachineArrPost[i];
                    const std::uint32_t b180 = *bits180Post;
                    const std::uint32_t b184 = *bits184Post;
                    const std::uint32_t actionStatePost =
                        *reinterpret_cast<std::uint32_t*>(
                            reinterpret_cast<std::uint8_t*>(self) + 0x204);
                    const std::uint32_t slotBit =
                        1u << static_cast<std::uint32_t>(i);

                    Log("[OutfitRuntimeParts:UpdatePartsStatus:Diag] "
                        "POST-orig slot=%zu (genuine tier change, restoredTier=%u) | "
                        "state[0]=%u state[1]=%u state[i]=%u | "
                        "+0x180=0x%08X (slotBit=0x%X, set=%d) | "
                        "+0x184=0x%08X (slotBit=0x%X, set=%d) | "
                        "actionState[+0x204]=0x%08X (bit0xd=%d, bit0xe=%d, bit0x8=%d, bit0x12=%d) | "
                        "armTypeArr[i] (now restored to cached)=%u\n",
                        i,
                        static_cast<unsigned>(overrides[i].restoreValue),
                        static_cast<unsigned>(st0),
                        static_cast<unsigned>(st1),
                        static_cast<unsigned>(stI),
                        static_cast<unsigned>(b180),
                        static_cast<unsigned>(slotBit),
                        (b180 & slotBit) ? 1 : 0,
                        static_cast<unsigned>(b184),
                        static_cast<unsigned>(slotBit),
                        (b184 & slotBit) ? 1 : 0,
                        static_cast<unsigned>(actionStatePost),
                        static_cast<int>((actionStatePost >> 0xd) & 1),
                        static_cast<int>((actionStatePost >> 0xe) & 1),
                        static_cast<int>((actionStatePost >> 8) & 1),
                        static_cast<int>((actionStatePost >> 0x12) & 1),
                        armTypeArr ? static_cast<unsigned>(armTypeArr[i]) : 0xFFu);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitRuntimeParts:UpdatePartsStatus:Diag] SEH during post-orig "
                "diagnostic capture — skipping\n");
        }
    }

    // armType arrives as 0 (zeroed by our UpdatePartsStatus cascade-prevention)
    // → restore from cache so RegisterFilesForArm picks the right tier files.
    static bool __fastcall hkPlayer2ImplSetUpParts(
        void* self,
        std::uint32_t slot,
        std::uint32_t playerType,
        std::uint32_t partsType,
        std::uint32_t camo,
        std::uint32_t armType,
        std::uint32_t faceId,
        void* avatarInfo)
    {
        if (!g_OrigPlayer2ImplSetUpParts)
            return false;

        std::uint32_t effectiveArmType = armType;
        const bool needOverride =
               armType == 0
            && partsType >= outfit::kCustomPartsTypeStart
            && partsType <= outfit::kCustomPartsTypeEnd
            && (playerType == outfit::kPlayerType_Snake
                || playerType == outfit::kPlayerType_Avatar);

        if (needOverride)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(
                    static_cast<std::uint8_t>(partsType & 0xFF), &entry)
                && entry
                && outfit::IsPlayerTypeCompatible(
                       entry->playerType,
                       static_cast<std::uint8_t>(playerType & 0xFF))
                && entry->IsArmEnabled())
            {
                std::uint8_t cachedTierForPT = 0;
                bool         cachedFlagForPT = false;
                GetCachedArmTierForPlayerType(
                    playerType, &cachedTierForPT, &cachedFlagForPT);
                effectiveArmType = cachedFlagForPT
                    ? static_cast<std::uint32_t>(cachedTierForPT)
                    : 1u;

                static std::uint32_t s_lastSlot[4]      = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
                static std::uint8_t  s_lastPartsType[4] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
                static std::uint8_t  s_lastTier[4]      = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
                const std::size_t idx = (slot < 4) ? slot : 0;
                if (s_lastSlot[idx] != slot
                 || s_lastPartsType[idx] != static_cast<std::uint8_t>(partsType)
                 || s_lastTier[idx] != static_cast<std::uint8_t>(effectiveArmType))
                {
                    Log("[OutfitRuntimeParts:SetUpParts] slot=%u partsType=0x%02X "
                        "(custom, developId=%u) playerType=%u — armType arg "
                        "arrived as 0 (zeroed by our UpdatePartsStatus cascade-"
                        "prevention) -> overriding to %u (cached_pt=%u/captured=%d) "
                        "so RegisterFilesForArm registers the right tier's "
                        "Fv2 effect files\n",
                        slot,
                        static_cast<unsigned>(partsType & 0xFF),
                        static_cast<unsigned>(entry->developId),
                        playerType,
                        effectiveArmType,
                        static_cast<unsigned>(cachedTierForPT),
                        cachedFlagForPT ? 1 : 0);
                    s_lastSlot[idx]      = slot;
                    s_lastPartsType[idx] = static_cast<std::uint8_t>(partsType);
                    s_lastTier[idx]      = static_cast<std::uint8_t>(effectiveArmType);
                }
            }
        }

        return g_OrigPlayer2ImplSetUpParts(
            self, slot, playerType, partsType, camo,
            effectiveArmType, faceId, avatarInfo);
    }

    static void __fastcall hkLoadPartsNew(
        void* self, std::uint32_t playerIndex,
        LoadPartsPlayerInfo* info, std::uint32_t flags)
    {


        if (!g_CapturedBlockController && self)
            g_CapturedBlockController = self;

        if (info)
        {


            Log("[OutfitRuntimeParts] LoadPartsNew fire: playerIndex=%u flags=0x%X "
                "playerType=%u partsType=0x%02X camo=0x%02X arm=0x%02X "
                "faceEquipId=0x%02X soldierFace=%d faceUnk=0x%02X\n",
                playerIndex, flags,
                static_cast<unsigned>(info->playerType),
                static_cast<unsigned>(info->playerPartsType),
                static_cast<unsigned>(info->playerCamoType),
                static_cast<unsigned>(info->playerArmType),
                static_cast<unsigned>(info->playerFaceEquipId),
                static_cast<int>(info->playerFaceId),
                static_cast<unsigned>(info->playerFaceEquipUnk));


            if (info->playerFaceId != 0)
            {
                g_LastInfoFaceId      = info->playerFaceId;
                g_LastInfoFaceEquipId = info->playerFaceEquipId;
                g_LastInfoFaceUnk     = info->playerFaceEquipUnk;
                g_LastInfoCaptured    = true;
            }


            // Gate on armType > 0 — engine's commit-blob expansion zeroes it
            // for new-outfit applies; only remember values from natural calls.
            if (info->playerArmType != 0)
            {
                SetCachedArmTierForPlayerType(
                    info->playerType, info->playerArmType);
            }


            const bool isCustomSelectorRange =
                info->playerCamoType >= outfit::kCustomSelectorStart
             && info->playerCamoType <= outfit::kCustomSelectorEnd;

            if (info->playerPartsType == 0x00 && isCustomSelectorRange)
            {


                const outfit::OutfitEntry* bySel = nullptr;
                if (outfit::TryGetOutfitBySelectorCode(info->playerCamoType, &bySel)
                    && bySel
                    && outfit::IsPlayerTypeCompatible(bySel->playerType,
                                                       info->playerType))
                {
                    Log("[OutfitRuntimeParts] LoadPartsNew: partial-custom "
                        "(partsType=0,camo=0x%02X) for playerType=%u -> "
                        "selectorCode lookup developId=%u partsType=0x%02X\n",
                        static_cast<unsigned>(info->playerCamoType),
                        static_cast<unsigned>(info->playerType),
                        static_cast<unsigned>(bySel->developId),
                        static_cast<unsigned>(bySel->partsType));
                    info->playerPartsType = bySel->partsType;


                    outfit::WriteLivePlayerOutfit(bySel->partsType,
                                                   bySel->selectorCode,
                                                   bySel->playerType);
                }
                else
                {
                    Log("[OutfitRuntimeParts] LoadPartsNew: partial-custom "
                        "(partsType=0,camo=0x%02X) playerType=%u -> "
                        "no matching outfit, forcing vanilla NORMAL camo\n",
                        static_cast<unsigned>(info->playerCamoType),
                        static_cast<unsigned>(info->playerType));
                    info->playerCamoType = 0x00;
                }
            }
            else if (info->playerPartsType == 0x00 && info->playerCamoType == 0xFF)
            {


                const outfit::OutfitEntry* chosen = nullptr;
                const char*                via    = "no-match";

                const std::uint16_t pendingDevId = outfit::GetPendingOutfitDevelopId();
                const outfit::OutfitEntry* byPending = nullptr;
                if (pendingDevId != 0
                    && outfit::TryGetOutfitByDevelopId(pendingDevId, &byPending)
                    && byPending
                    && outfit::IsPlayerTypeCompatible(byPending->playerType,
                                                       info->playerType))
                {
                    chosen = byPending;
                    via = "pendingDevId";
                    outfit::ClearPendingOutfitDevelopId();
                }

                if (chosen)
                {


                    Log("[OutfitRuntimeParts] LoadPartsNew: supply-drop "
                        "broken-custom (partsType=0,camo=0xFF) for playerType=%u "
                        "-> resolved via %s developId=%u partsType=0x%02X selector=0x%02X\n",
                        static_cast<unsigned>(info->playerType),
                        via,
                        static_cast<unsigned>(chosen->developId),
                        static_cast<unsigned>(chosen->partsType),
                        static_cast<unsigned>(chosen->selectorCode));
                    info->playerPartsType = chosen->partsType;
                    info->playerCamoType  = chosen->selectorCode;
                    outfit::WriteLivePlayerOutfit(chosen->partsType,
                                                   chosen->selectorCode,
                                                   chosen->playerType);
                }
                else
                {
                    Log("[OutfitRuntimeParts] LoadPartsNew: supply-drop "
                        "broken-custom (partsType=0,camo=0xFF) for playerType=%u "
                        "-> no resolution (pendingDevId=%u), forcing vanilla NORMAL\n",
                        static_cast<unsigned>(info->playerType),
                        static_cast<unsigned>(pendingDevId));
                    info->playerCamoType = 0x00;
                }
            }

            const outfit::OutfitEntry* entry = nullptr;
            const bool isCustom =
                ResolveCustomEntry(info->playerType, info->playerPartsType, &entry);

            if (isCustom)
            {


                if (!entry->IsArmEnabled())
                {
                    info->playerArmType = 0;
                }
                else if (info->playerArmType == 0)
                {
                    // Engine zeroes playerArmType when expanding new commit
                    // blob → restore developed tier from per-PT cache.
                    std::uint8_t cachedTierForPT = 0;
                    bool         cachedFlagForPT = false;
                    GetCachedArmTierForPlayerType(
                        info->playerType, &cachedTierForPT, &cachedFlagForPT);
                    info->playerArmType = cachedFlagForPT
                        ? cachedTierForPT
                        : std::uint8_t{1};
                    Log("[OutfitRuntimeParts] enableArm restored armType=%u "
                        "(via cache_pt[%u], captured=%d) for partsType=0x%02X\n",
                        static_cast<unsigned>(info->playerArmType),
                        static_cast<unsigned>(info->playerType),
                        cachedFlagForPT ? 1 : 0,
                        static_cast<unsigned>(info->playerPartsType));
                }

                if (!entry->IsFaceEnabled())
                {
                    info->playerFaceEquipId = 0;
                    info->playerFaceEquipUnk =
                        static_cast<std::uint8_t>(info->playerFaceEquipUnk & 0xF8);
                }


                if (entry->IsHeadEnabled()
                 && entry->defaultSoldierFaceId != 0
                 && info->playerFaceId == 0)
                {
                    Log("[OutfitRuntimeParts] forcing playerFaceId %d -> %u "
                        "(enableHead + slot empty)\n",
                        static_cast<int>(info->playerFaceId),
                        static_cast<unsigned>(entry->defaultSoldierFaceId));
                    info->playerFaceId =
                        static_cast<std::int16_t>(entry->defaultSoldierFaceId);
                }
            }
            else if (info->playerPartsType >= outfit::kCustomPartsTypeStart
                  && info->playerPartsType <= outfit::kCustomPartsTypeEnd)
            {


                Log("[OutfitRuntimeParts] LoadPartsNew: stray custom partsType=0x%02X "
                    "playerType=%u — forcing to vanilla 0x00\n",
                    static_cast<unsigned>(info->playerPartsType),
                    static_cast<unsigned>(info->playerType));
                info->playerPartsType = 0x00;
                info->playerCamoType  = 0x00;
            }


            const bool spoofPartsType = isCustom && entry;
            const std::uint8_t origPartsType = info->playerPartsType;
            std::uint8_t* shellTypeInfoPtr = nullptr;
            std::uint8_t  prevShellPartsType = 0;
            bool          shellSentinelWritten = false;


            constexpr bool     suppressFace = false;
            const std::int16_t origFaceId =
                info ? info->playerFaceId : std::int16_t{0};
            (void)suppressFace;
            (void)origFaceId;

            if (spoofPartsType)
            {
                tl_SpoofedRealPartsType = origPartsType;

                // Use LIVE playerType (not entry's) so Snake↔Avatar bridging
                // still gets the right STANDARD partsType (Snake=0x01, else 0).
                std::uint8_t spoofTarget = 0x00;
                if (info->playerType == outfit::kPlayerType_Snake)
                {
                    spoofTarget = 0x01;
                }
                info->playerPartsType   = spoofTarget;


                __try
                {
                    shellTypeInfoPtr =
                        *reinterpret_cast<std::uint8_t**>(
                            reinterpret_cast<std::uint8_t*>(self)
                            + playerIndex * 8 + 0x1100);
                    if (shellTypeInfoPtr)
                    {
                        prevShellPartsType    = shellTypeInfoPtr[1];
                        shellTypeInfoPtr[1]   = 0xFE;
                        shellSentinelWritten  = true;
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    Log("[OutfitRuntimeParts] hkLoadPartsNew: SEH "
                        "clobbering BlockShell partsType pre-orig "
                        "(self=%p playerIndex=%u)\n",
                        self, playerIndex);
                    shellTypeInfoPtr = nullptr;
                }

                Log("[OutfitRuntimeParts] hkLoadPartsNew: spoofing partsType "
                    "0x%02X -> 0x%02X (camo=0x%02X soldierFace=%d, "
                    "shellPre=0x%02X -> 0xFE [%s]) — calling orig...\n",
                    static_cast<unsigned>(origPartsType),
                    static_cast<unsigned>(info->playerPartsType),
                    static_cast<unsigned>(info->playerCamoType),
                    static_cast<int>(info->playerFaceId),
                    static_cast<unsigned>(prevShellPartsType),
                    shellSentinelWritten ? "clobbered" : "shell-ptr-null");
            }

            g_OrigLoadPartsNew(self, playerIndex, info, flags);


            if (spoofPartsType)
            {
                Log("[OutfitRuntimeParts] hkLoadPartsNew: orig returned "
                    "after spoofed call (partsType=0x%02X[real] camo=0x%02X "
                    "shell=0x%02X) — restoring spoof state\n",
                    static_cast<unsigned>(origPartsType),
                    static_cast<unsigned>(info->playerCamoType),
                    shellTypeInfoPtr ? static_cast<unsigned>(shellTypeInfoPtr[1])
                                     : 0xFFFFu);

                info->playerPartsType   = origPartsType;
                tl_SpoofedRealPartsType = 0;
                __try
                {
                    if (shellTypeInfoPtr)
                        shellTypeInfoPtr[1] = origPartsType;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    Log("[OutfitRuntimeParts] hkLoadPartsNew: SEH restoring "
                        "BlockShell partsType (self=%p playerIndex=%u)\n",
                        self, playerIndex);
                }
            }

            return;
        }

        g_OrigLoadPartsNew(self, playerIndex, info, flags);
    }
}

namespace outfit
{
    bool Install_OutfitRuntimeParts_Hooks()
    {
        ResolveFoxPathApi();

        void* tParts        = ResolveGameAddress(gAddr.LoadPlayerPartsParts);
        void* tFpk          = ResolveGameAddress(gAddr.LoadPlayerPartsFpk);
        void* tCamo         = ResolveGameAddress(gAddr.LoadPlayerCamoFpk);
        void* tDiamond      = ResolveGameAddress(gAddr.LoadPlayerSnakeBlackDiamondFpk);
        void* tBionicArmFv2 = ResolveGameAddress(gAddr.LoadPlayerBionicArmFv2);
        void* tBionicArmFpk = ResolveGameAddress(gAddr.LoadPlayerBionicArmFpk);
        void* tSnakeFaceFv2 = ResolveGameAddress(gAddr.LoadPlayerSnakeFaceFv2);
        void* tSnakeFaceFpk = ResolveGameAddress(gAddr.LoadPlayerSnakeFaceFpk);
        void* tLpn          = ResolveGameAddress(gAddr.Player2BlockController_LoadPartsNew);
        void* tFaceFova     = ResolveGameAddress(gAddr.ResourceTable_DoesNeedFaceFova);
        void* tFaceFovaAvatar = ResolveGameAddress(gAddr.ResourceTable_DoesNeedFaceFovaForAvatar);
        void* tSetHandSlot  = ResolveGameAddress(gAddr.EquipController_SetHandSlotEnabled);
        void* tIsArtHand    = ResolveGameAddress(gAddr.Sys_IsArtificialHandEnabled);
        void* tIsArtHandLive = ResolveGameAddress(gAddr.Sys_IsArtificialHandEnabledForCurrentPlayerType);
        void* tProcessSignal = ResolveGameAddress(gAddr.Player2GameObjectImpl_ProcessSignal);
        void* tUpdatePartsStatus = ResolveGameAddress(gAddr.UpdatePartsStatus);
        void* tSetUpParts        = ResolveGameAddress(gAddr.Player2Impl_SetUpParts);

        if (tParts)
            g_InstalledParts = CreateAndEnableHook(
                tParts, reinterpret_cast<void*>(&hkLoadPlayerPartsParts),
                reinterpret_cast<void**>(&g_OrigLoadPartsParts));
        if (tFpk)
            g_InstalledFpk = CreateAndEnableHook(
                tFpk, reinterpret_cast<void*>(&hkLoadPlayerPartsFpk),
                reinterpret_cast<void**>(&g_OrigLoadPartsFpk));
        if (tCamo)
            g_InstalledCamo = CreateAndEnableHook(
                tCamo, reinterpret_cast<void*>(&hkLoadPlayerCamoFpk),
                reinterpret_cast<void**>(&g_OrigLoadCamoFpk));
        if (tDiamond)
            g_InstalledDiamond = CreateAndEnableHook(
                tDiamond, reinterpret_cast<void*>(&hkLoadPlayerSnakeBlackDiamondFpk),
                reinterpret_cast<void**>(&g_OrigLoadDiamondFpk));
        if (tBionicArmFv2)
            g_InstalledBionicArmFv2 = CreateAndEnableHook(
                tBionicArmFv2, reinterpret_cast<void*>(&hkLoadPlayerBionicArmFv2),
                reinterpret_cast<void**>(&g_OrigLoadBionicArmFv2));
        if (tBionicArmFpk)
            g_InstalledBionicArmFpk = CreateAndEnableHook(
                tBionicArmFpk, reinterpret_cast<void*>(&hkLoadPlayerBionicArmFpk),
                reinterpret_cast<void**>(&g_OrigLoadBionicArmFpk));
        if (tSnakeFaceFv2)
            g_InstalledSnakeFaceFv2 = CreateAndEnableHook(
                tSnakeFaceFv2, reinterpret_cast<void*>(&hkLoadPlayerSnakeFaceFv2),
                reinterpret_cast<void**>(&g_OrigLoadSnakeFaceFv2));
        if (tSnakeFaceFpk)
            g_InstalledSnakeFaceFpk = CreateAndEnableHook(
                tSnakeFaceFpk, reinterpret_cast<void*>(&hkLoadPlayerSnakeFaceFpk),
                reinterpret_cast<void**>(&g_OrigLoadSnakeFaceFpk));
        if (tLpn)
            g_InstalledLpn = CreateAndEnableHook(
                tLpn, reinterpret_cast<void*>(&hkLoadPartsNew),
                reinterpret_cast<void**>(&g_OrigLoadPartsNew));
        if (tFaceFova)
            g_InstalledDoesNeedFace = CreateAndEnableHook(
                tFaceFova, reinterpret_cast<void*>(&hkDoesNeedFaceFova),
                reinterpret_cast<void**>(&g_OrigDoesNeedFaceFova));
        if (tFaceFovaAvatar)
            g_InstalledDoesNeedFaceForAvatar = CreateAndEnableHook(
                tFaceFovaAvatar, reinterpret_cast<void*>(&hkDoesNeedFaceFovaForAvatar),
                reinterpret_cast<void**>(&g_OrigDoesNeedFaceFovaForAvatar));
        if (tSetHandSlot)
            g_InstalledSetHandSlotEnabled = CreateAndEnableHook(
                tSetHandSlot, reinterpret_cast<void*>(&hkSetHandSlotEnabled),
                reinterpret_cast<void**>(&g_OrigSetHandSlotEnabled));
        if (tIsArtHand)
            g_InstalledIsArtificialHand = CreateAndEnableHook(
                tIsArtHand, reinterpret_cast<void*>(&hkIsArtificialHandEnabled),
                reinterpret_cast<void**>(&g_OrigIsArtificialHandEnabled));
        if (tIsArtHandLive)
            g_InstalledIsArtHandForCurrent = CreateAndEnableHook(
                tIsArtHandLive,
                reinterpret_cast<void*>(&hkIsArtificialHandEnabledForCurrentPlayerType),
                reinterpret_cast<void**>(&g_OrigIsArtificialHandForCurrent));
        if (tProcessSignal)
            g_InstalledProcessSignal = CreateAndEnableHook(
                tProcessSignal, reinterpret_cast<void*>(&hkProcessSignal),
                reinterpret_cast<void**>(&g_OrigProcessSignal));
        if (tUpdatePartsStatus)
            g_InstalledUpdatePartsStatus = CreateAndEnableHook(
                tUpdatePartsStatus, reinterpret_cast<void*>(&hkUpdatePartsStatus),
                reinterpret_cast<void**>(&g_OrigUpdatePartsStatus));
        if (tSetUpParts)
            g_InstalledPlayer2ImplSetUpParts = CreateAndEnableHook(
                tSetUpParts, reinterpret_cast<void*>(&hkPlayer2ImplSetUpParts),
                reinterpret_cast<void**>(&g_OrigPlayer2ImplSetUpParts));

        Log("[OutfitRuntimeParts] installed: parts=%s fpk=%s camo=%s diamond=%s "
            "bionicArmFv2=%s bionicArmFpk=%s snakeFaceFv2=%s snakeFaceFpk=%s "
            "lpn=%s doesNeedFace=%s doesNeedFaceAvatar=%s setHandSlotEnabled=%s "
            "isArtificialHandEnabled=%s isArtHandForCurrent=%s "
            "processSignal=%s updatePartsStatus=%s setUpParts=%s\n",
            g_InstalledParts                  ? "OK" : "skip",
            g_InstalledFpk                    ? "OK" : "skip",
            g_InstalledCamo                   ? "OK" : "skip",
            g_InstalledDiamond                ? "OK" : "skip",
            g_InstalledBionicArmFv2           ? "OK" : "skip",
            g_InstalledBionicArmFpk           ? "OK" : "skip",
            g_InstalledSnakeFaceFv2           ? "OK" : "skip",
            g_InstalledSnakeFaceFpk           ? "OK" : "skip",
            g_InstalledLpn                    ? "OK" : "skip",
            g_InstalledDoesNeedFace           ? "OK" : "skip",
            g_InstalledDoesNeedFaceForAvatar  ? "OK" : "skip",
            g_InstalledSetHandSlotEnabled     ? "OK" : "skip",
            g_InstalledIsArtificialHand       ? "OK" : "skip",
            g_InstalledIsArtHandForCurrent    ? "OK" : "skip",
            g_InstalledProcessSignal          ? "OK" : "skip",
            g_InstalledUpdatePartsStatus      ? "OK" : "skip",
            g_InstalledPlayer2ImplSetUpParts  ? "OK" : "skip");

        return g_InstalledParts && g_InstalledFpk && g_InstalledLpn;
    }

    bool ForcePartsReload(unsigned char playerType,
                          unsigned char partsType,
                          unsigned char selectorCode)
    {
        if (!g_CapturedBlockController || !g_OrigLoadPartsNew)
        {
            Log("[OutfitRuntimeParts] ForcePartsReload: no captured "
                "BlockController or orig — call after at least one "
                "natural LoadPartsNew has fired (mission boot)\n");
            return false;
        }


        LoadPartsPlayerInfo info{};
        info.playerType         = playerType;
        info.playerPartsType    = partsType;
        info.playerCamoType     = selectorCode;
        {
            std::uint8_t cachedTierForPT = 0;
            bool         cachedFlagForPT = false;
            GetCachedArmTierForPlayerType(
                playerType, &cachedTierForPT, &cachedFlagForPT);
            info.playerArmType = cachedFlagForPT ? cachedTierForPT : std::uint8_t{0};
        }
        info.playerFaceId       = g_LastInfoCaptured    ? g_LastInfoFaceId      : std::int16_t{0};
        info.playerFaceEquipId  = g_LastInfoCaptured    ? g_LastInfoFaceEquipId : std::uint16_t{0};
        info.playerFaceEquipUnk = g_LastInfoCaptured    ? g_LastInfoFaceUnk     : std::uint8_t{0};


        constexpr std::uint32_t kFlagsP0 = 0x15F640;
        constexpr std::uint32_t kFlagsP1 = 0x15F600;


        const bool quarkOk =
            outfit::WriteLivePlayerOutfit(partsType, selectorCode, playerType);


        const outfit::OutfitEntry* entry = nullptr;
        const bool spoofPartsType =
            outfit::TryGetOutfitByPartsType(partsType, &entry)
         && entry;
        const std::uint8_t origPartsType = info.playerPartsType;


        std::uint8_t* shellTypeInfoPtr0 = nullptr;
        std::uint8_t* shellTypeInfoPtr1 = nullptr;
        std::uint8_t  prevShellPartsType0 = 0;
        std::uint8_t  prevShellPartsType1 = 0;

        if (spoofPartsType)
        {
            tl_SpoofedRealPartsType = origPartsType;

            std::uint8_t spoofTarget = 0x00;
            if (playerType == outfit::kPlayerType_Snake)
            {
                spoofTarget = 0x01;
            }
            info.playerPartsType    = spoofTarget;

            __try
            {
                shellTypeInfoPtr0 =
                    *reinterpret_cast<std::uint8_t**>(
                        reinterpret_cast<std::uint8_t*>(g_CapturedBlockController)
                        + 0u * 8 + 0x1100);
                if (shellTypeInfoPtr0)
                {
                    prevShellPartsType0 = shellTypeInfoPtr0[1];
                    shellTypeInfoPtr0[1] = 0xFE;
                }

                shellTypeInfoPtr1 =
                    *reinterpret_cast<std::uint8_t**>(
                        reinterpret_cast<std::uint8_t*>(g_CapturedBlockController)
                        + 1u * 8 + 0x1100);
                if (shellTypeInfoPtr1)
                {
                    prevShellPartsType1 = shellTypeInfoPtr1[1];
                    shellTypeInfoPtr1[1] = 0xFE;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                Log("[OutfitRuntimeParts] ForcePartsReload: SEH "
                    "clobbering BlockShell partsType pre-orig\n");
                shellTypeInfoPtr0 = nullptr;
                shellTypeInfoPtr1 = nullptr;
            }

            Log("[OutfitRuntimeParts] ForcePartsReload: spoofing partsType "
                "0x%02X -> 0x%02X for orig recognition "
                "(custom outfit, enableHead=%d, selector=0x%02X, "
                "shellPre=[0x%02X,0x%02X] -> 0xFE)\n",
                static_cast<unsigned>(origPartsType),
                static_cast<unsigned>(info.playerPartsType),
                entry && entry->IsHeadEnabled() ? 1 : 0,
                static_cast<unsigned>(selectorCode),
                static_cast<unsigned>(prevShellPartsType0),
                static_cast<unsigned>(prevShellPartsType1));
        }

        Log("[OutfitRuntimeParts] ForcePartsReload: playerType=%u "
            "partsType=0x%02X selector=0x%02X quark=%s (controller=%p)%s\n",
            static_cast<unsigned>(playerType),
            static_cast<unsigned>(partsType),
            static_cast<unsigned>(selectorCode),
            quarkOk ? "OK" : "FAIL",
            g_CapturedBlockController,
            spoofPartsType ? " [enableHead spoof active]" : "");

        __try
        {
            g_OrigLoadPartsNew(g_CapturedBlockController, 0u, &info, kFlagsP0);
            g_OrigLoadPartsNew(g_CapturedBlockController, 1u, &info, kFlagsP1);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitRuntimeParts] ForcePartsReload: SEH while calling "
                "orig LoadPartsNew — captured controller may be stale\n");


            if (spoofPartsType) tl_SpoofedRealPartsType = 0;


            __try
            {
                if (shellTypeInfoPtr0) shellTypeInfoPtr0[1] = origPartsType;
                if (shellTypeInfoPtr1) shellTypeInfoPtr1[1] = origPartsType;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            g_CapturedBlockController = nullptr;
            return false;
        }


        if (spoofPartsType)
        {
            info.playerPartsType    = origPartsType;
            tl_SpoofedRealPartsType = 0;
            __try
            {
                if (shellTypeInfoPtr0) shellTypeInfoPtr0[1] = origPartsType;
                if (shellTypeInfoPtr1) shellTypeInfoPtr1[1] = origPartsType;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                Log("[OutfitRuntimeParts] ForcePartsReload: SEH "
                    "restoring BlockShell partsType post-orig\n");
            }
        }


        if (entry)
        {
            g_RecentForcePartsReloadDevId.store(
                entry->developId, std::memory_order_release);
            Log("[OutfitRuntimeParts] ForcePartsReload: published "
                "developId=%u as recent-reload token (suppresses the "
                "orig pickup pipeline's redundant LoadPartsNew that "
                "fires ~100-200ms later)\n",
                static_cast<unsigned>(entry->developId));
        }
        return true;
    }

    void Uninstall_OutfitRuntimeParts_Hooks()
    {
        if (g_InstalledParts)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.LoadPlayerPartsParts));
        if (g_InstalledFpk)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.LoadPlayerPartsFpk));
        if (g_InstalledCamo)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.LoadPlayerCamoFpk));
        if (g_InstalledDiamond)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.LoadPlayerSnakeBlackDiamondFpk));
        if (g_InstalledBionicArmFv2)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.LoadPlayerBionicArmFv2));
        if (g_InstalledBionicArmFpk)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.LoadPlayerBionicArmFpk));
        if (g_InstalledSnakeFaceFv2)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.LoadPlayerSnakeFaceFv2));
        if (g_InstalledSnakeFaceFpk)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.LoadPlayerSnakeFaceFpk));
        if (g_InstalledLpn)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.Player2BlockController_LoadPartsNew));
        if (g_InstalledDoesNeedFace)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.ResourceTable_DoesNeedFaceFova));
        if (g_InstalledDoesNeedFaceForAvatar)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.ResourceTable_DoesNeedFaceFovaForAvatar));
        if (g_InstalledSetHandSlotEnabled)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.EquipController_SetHandSlotEnabled));
        if (g_InstalledIsArtificialHand)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.Sys_IsArtificialHandEnabled));
        if (g_InstalledIsArtHandForCurrent)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.Sys_IsArtificialHandEnabledForCurrentPlayerType));
        if (g_InstalledProcessSignal)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.Player2GameObjectImpl_ProcessSignal));
        if (g_InstalledUpdatePartsStatus)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.UpdatePartsStatus));
        if (g_InstalledPlayer2ImplSetUpParts)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.Player2Impl_SetUpParts));

        g_OrigLoadPartsParts   = nullptr;
        g_OrigLoadPartsFpk     = nullptr;
        g_OrigLoadCamoFpk      = nullptr;
        g_OrigLoadDiamondFpk   = nullptr;
        g_OrigLoadBionicArmFv2 = nullptr;
        g_OrigLoadBionicArmFpk = nullptr;
        g_OrigLoadSnakeFaceFv2 = nullptr;
        g_OrigLoadSnakeFaceFpk = nullptr;
        g_OrigLoadPartsNew              = nullptr;
        g_OrigDoesNeedFaceFova          = nullptr;
        g_OrigDoesNeedFaceFovaForAvatar = nullptr;
        g_OrigSetHandSlotEnabled        = nullptr;
        g_OrigIsArtificialHandEnabled   = nullptr;
        g_OrigIsArtificialHandForCurrent = nullptr;
        g_OrigProcessSignal             = nullptr;
        g_OrigUpdatePartsStatus         = nullptr;
        g_OrigPlayer2ImplSetUpParts     = nullptr;
        g_FoxPath_Path         = nullptr;

        g_InstalledParts        = false;
        g_InstalledFpk          = false;
        g_InstalledCamo         = false;
        g_InstalledDiamond      = false;
        g_InstalledBionicArmFv2 = false;
        g_InstalledBionicArmFpk = false;
        g_InstalledSnakeFaceFv2 = false;
        g_InstalledSnakeFaceFpk = false;
        g_InstalledLpn                   = false;
        g_InstalledDoesNeedFace          = false;
        g_InstalledDoesNeedFaceForAvatar = false;
        g_InstalledSetHandSlotEnabled    = false;
        g_InstalledIsArtificialHand      = false;
        g_InstalledIsArtHandForCurrent   = false;
        g_InstalledProcessSignal         = false;
        g_InstalledUpdatePartsStatus     = false;
        g_InstalledPlayer2ImplSetUpParts = false;

        g_CapturedBlockController = nullptr;

        Log("[OutfitRuntimeParts] removed\n");
    }
}
