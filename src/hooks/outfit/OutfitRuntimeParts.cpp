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

    // EquipControllerImpl::SetHandSlotEnabled(this, slot, enabled) — the leaf
    // function the partsType-translator inside Player2GameObjectImpl::
    // UpdatePartsStatus calls to enable/disable the bionic arm input slot
    // (mgsvtpp.exe.c:2263824, address 0x1411B0D10). For any custom partsType
    // the translator's outer switch falls through to caseD_3 which forces
    // `enabled=0`, disabling the arm button. We override the disable when the
    // live player is wearing a registered custom outfit with enableArm=true.
    using SetHandSlotEnabled_t = void (__fastcall*)(void* self, std::uint32_t slot, std::uint8_t enabled);

    // tpp::sys::IsArtificialHandEnabled(uint playerType, uint playerPartsType)
    // — mgsvtpp.exe.c:1321783, address 0x1409C45C0. A simple whitelist:
    //   if ((playerType == 0 || playerType == 3)
    //    && partsType in {0,1,2,7..D,F..12,17..19}) return 1; else return 0;
    // Two callers in the per-frame player-update loop FUN_1412a2f80 at
    // mgsvtpp.exe.c:2396152 and 2396298 each wrap their entire arm-equip-
    // render dispatch in `if (uVar12 & 2 && cVar5 != '\0')`. When this
    // returns 0 (custom partsType), the engine NEVER tells the renderer the
    // arm slot is active — vtable+0x100 (per-equip dispatch), vtable+0x118
    // (slot activation), vtable+0x2f0 (per-bullet-id dispatch), and
    // vtable+0x2d8 (per-frame finalize) all get skipped. Result: assets are
    // loaded but never rendered. The fix: override to 1 for custom partsType
    // with a registered outfit that has enableArm=true.
    using IsArtificialHandEnabled_t = std::uint8_t (__fastcall*)(std::uint32_t playerType, std::uint32_t playerPartsType);

    // tpp::sys::PlayerInfoService::IsArtificialHandEnabledForCurrentPlayerType()
    // — mgsvtpp.exe.c:3945377, address 0x141E02D80. Independent function with
    // the SAME hardcoded whitelist as the explicit-args variant above, but
    // reads playerType/partsType from QuarkSystemTable -> +0x98 -> +0x10 ->
    // [+0xfb]/[+0xf8] (the live player state) instead of from arguments. Many
    // callers across the engine consult this for "does the live player have
    // an artificial hand?" — including UI greying logic. Without this hook,
    // even with the explicit-args variant overridden, callers asking the
    // live-state variant get 0 for custom partsType and the arm-related
    // dispatch they gate stays disabled.
    using IsArtificialHandEnabledForCurrent_t = std::uint8_t (__fastcall*)();

    // NOTE: Player2GameObjectImpl::UpdatePartsStatus is the per-tick state
    // syncer that contains a partsType-whitelist gate which both disables the
    // bionic arm input slot and zeros the arm asset-index byte at
    // pIVar3+0x58+slot for any custom partsType. The input-gate side-effect is
    // covered by hkSetHandSlotEnabled below. The asset-index zero CANNOT be
    // safely overridden post-orig — every byte in pIVar3+0x40..0x68 is part
    // of UpdatePartsStatus's state-changed check, so writing a non-zero value
    // post-orig forces state-changed=true on the next tick, fires another
    // Player2BlockController::LoadPartsNew, and the framework's 0xFE BlockShell
    // clobber inside hkLoadPartsNew makes orig's dedupe at
    // mgsvtpp.exe.c:1312714 fail every time → asset reload every UPS tick →
    // character never settles → "Character does not load." Verified at
    // 04:30:11..04:30:19 in the test session log. The iDroid loadout UI's
    // greying of HAND OF JEHUTY on custom partsType is a cosmetic
    // consequence we accept rather than thrash the asset pipeline; a separate
    // surgical hook on whichever UI compatibility check reads from there is
    // the proper fix for that.

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


    static void* g_CapturedBlockController = nullptr;


    static thread_local std::uint8_t tl_SpoofedRealPartsType = 0;


    static std::atomic<std::uint16_t> g_RecentForcePartsReloadDevId{0};


    static std::int16_t  g_LastInfoFaceId        = 0;
    static std::uint16_t g_LastInfoFaceEquipId   = 0;
    static std::uint8_t  g_LastInfoFaceUnk       = 0;
    static std::uint8_t  g_LastInfoArmType       = 0;
    static bool          g_LastInfoCaptured      = false;
    // Separate capture flag for arm tier — face captures gate on
    // playerFaceId!=0 which doesn't fire for users whose soldierFace is 0
    // (default Snake face). Arm capture is independent: any natural
    // LoadPartsNew that arrives with armType > 0 is a valid sample of the
    // player's developed prosthetic tier.
    static bool          g_LastInfoArmCaptured   = false;


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
        // Snake-registered outfits accept Avatar live (and vice versa) so the
        // same registration covers both story and FOB/online characters.
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


    // Vanilla partsType used as the substitute when calling orig
    // LoadPlayerBionicArm{Fv2,Fpk} for any custom outfit. The leaf functions
    // hardcode a partsType whitelist (case 0,1,2,7,8,9,A,B,C,D,F,10,11,12,17,
    // 18,19) and reject anything else with a null FoxPath. The arm asset path
    // itself is selected by `playerHandType * 2`, NOT by partsType, so the
    // whitelist case we route through is irrelevant — pick a stable, always-
    // present vanilla value (0x01 = STANDARD Snake suit).
    constexpr std::uint32_t kBionicArmVanillaPartsTypeSubstitute = 0x01;

    // Detours for the bionic-arm leaf loaders.
    //
    // Why this exists: the engine's leaf functions reject any partsType outside
    // the hardcoded whitelist {0,1,2,7..0xD except 0xE,0xF..0x12,0x17..0x19}
    // — V_FrameWork's custom partsType range (`outfit::kCustomPartsTypeStart`..
    // `kCustomPartsTypeEnd`) has zero overlap with that whitelist, so for every
    // custom outfit the leaf returns a null FoxPath and the arm Fv2/Fpk slot
    // ends up empty. The Fpk path is also called from inside LoadPartsNew
    // while the framework's spoof window is active (info->playerPartsType=0x01)
    // so it would already work, but hooking both for symmetry is defensive.
    //
    // Both detours: scale O(1) per call, lookup is by partsType through the
    // existing registry — works for any number of registered outfits.
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
                return g_OrigLoadBionicArmFv2(
                    outPath, playerType,
                    kBionicArmVanillaPartsTypeSubstitute,
                    playerHandType);
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
                return g_OrigLoadBionicArmFpk(
                    outPath, playerType,
                    kBionicArmVanillaPartsTypeSubstitute,
                    playerHandType);
            }
        }
        return g_OrigLoadBionicArmFpk(outPath, playerType,
                                      playerPartsType, playerHandType);
    }

    // Snake face FOVA leaves — same shape as bionic arm. Both
    // LoadPlayerSnakeFaceFv2 and LoadPlayerSnakeFaceFpk are gated by
    // `if (playerType == 0)` (Snake only) followed by a hardcoded partsType
    // whitelist {0..2, 7..9, 0xB..0xE, 0xF..0x16, 0x17..0x19}. Custom range
    // 0x40..0x7F falls into `default: snakeFaceFv2Path = 0;` → null FoxPath
    // → invisible head. We substitute partsType=0x01 (Snake STANDARD) when
    // the registered outfit has `enableHead = true`, so the engine returns
    // the vanilla Snake face path. When enableHead is false, write null
    // (head not loaded — same effect the framework already enforces via
    // hkDoesNeedFaceFova).
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

    // Avatar variant of the face-needed gate. Vanilla
    // ResourceTable::DoesNeedFaceFovaForAvatar @ 0x140AE8500 has the same
    // hardcoded partsType whitelist as the Snake/DD variant, so it returns
    // false for any custom partsType — preventing the engine from loading
    // the Avatar's procedural face when Snake↔Avatar bridging puts a custom
    // outfit's partsType into the Avatar slot. Mirror the Snake/DD hook:
    // when a registered outfit with `enableHead = true` is the live one,
    // force-return 1 so the engine proceeds with the Avatar face load
    // (which uses BlockShell+0xF7/+0xF8 customization indices, not partsType,
    // so no further whitelist substitution is needed).
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

    // EquipControllerImpl::SetHandSlotEnabled is the leaf the partsType
    // translator inside UpdatePartsStatus calls. For custom partsType the
    // translator's outer switch falls into caseD_3 (uVar26=0) and calls this
    // with `enabled=0`, disabling the bionic arm input slot. We override the
    // disable in that specific case (custom outfit registered with
    // enableArm=true). Vanilla DD slots (which legitimately have no arm) are
    // left alone — we only act when the live player is Snake/Avatar AND
    // wearing a registered custom outfit AND that outfit's enableArm is true.
    //
    // PREVIOUS APPROACH (reverted): we hooked Player2GameObjectImpl::
    // UpdatePartsStatus and spoofed the partsType byte array to 0x01 before
    // orig ran. That CASCADED into the orig's internal LoadPartsNew call
    // (mgsvtpp.exe.c:1324794) which builds playerInfo from the same byte
    // arrays — LoadPartsNew received partsType=0x01 instead of the real
    // custom value, so our LoadPartsNew leaf hook saw it as vanilla and the
    // custom body assets never loaded. The spoof was ABI-toxic at that layer.
    //
    // CURRENT APPROACH (this hook): leave the byte arrays untouched so
    // LoadPartsNew receives the real custom partsType and our existing
    // LoadPartsNew leaf hook substitutes the custom asset paths correctly.
    // Address only the SetHandSlotEnabled side-effect by overriding its
    // `enabled` argument — purely a gameplay-input fix, no asset-load impact.
    static void __fastcall hkSetHandSlotEnabled(
        void* self_equipController,
        std::uint32_t slot,
        std::uint8_t  enabled)
    {
        if (enabled != 0)
        {
            // Engine wants enabled=1; nothing to override. Pass through.
            if (g_OrigSetHandSlotEnabled)
                g_OrigSetHandSlotEnabled(self_equipController, slot, enabled);
            return;
        }

        // Engine is calling with enabled=0. This is either a legitimate
        // disable (DD slot, dead player, demo state, etc.) or the custom-
        // partsType translator miss we want to override. Distinguish by
        // checking the live player's outfit state.
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
                // State-change-gated log to avoid every-frame spam. We log on
                // first override per (slot, partsType) transition only.
                static std::uint32_t s_lastSlot       = 0xFFFFFFFFu;
                static std::uint8_t  s_lastPartsType  = 0xFFu;
                if (s_lastSlot != slot || s_lastPartsType != livePartsType)
                {
                    Log("[OutfitRuntimeParts:SetHandSlot] slot=%u partsType=0x%02X "
                        "(livePT=%u developId=%u) enabled=0 -> 1 [custom outfit "
                        "with enableArm=true; overriding translator's "
                        "whitelist-miss disable]\n",
                        slot,
                        static_cast<unsigned>(livePartsType),
                        static_cast<unsigned>(livePT),
                        static_cast<unsigned>(entry->developId));
                    s_lastSlot      = slot;
                    s_lastPartsType = livePartsType;
                }

                if (g_OrigSetHandSlotEnabled)
                    g_OrigSetHandSlotEnabled(self_equipController, slot, 1);
                return;
            }
        }

        // Legitimate disable — pass through.
        if (g_OrigSetHandSlotEnabled)
            g_OrigSetHandSlotEnabled(self_equipController, slot, enabled);
    }

    // Override the engine's per-frame "should the artificial hand be active?"
    // gate. Vanilla returns 1 only for the hardcoded vanilla partsType
    // whitelist; for custom partsType (0x40+) it returns 0 and the per-player
    // update loop FUN_1412a2f80 at mgsvtpp.exe.c:2396151-2396188 and
    // 2396297-2396324 skips the entire arm-equip-render dispatch (vtable
    // calls +0x100, +0x118, +0x2f0, +0x2d8 are gated by `cVar5 != '\0'`).
    // Even though our leaf hooks load the correct arm Fpk/Fv2 paths, this
    // gate prevents the renderer from being told the arm slot is active,
    // resulting in a visually-invisible bionic arm despite all asset loads
    // succeeding. Override to 1 when the live partsType is a registered
    // custom outfit with enableArm=true. Pass through everything else
    // (vanilla partsTypes, DD player types, custom outfits with enableArm
    // intentionally false).
    static std::uint8_t __fastcall hkIsArtificialHandEnabled(
        std::uint32_t playerType,
        std::uint32_t playerPartsType)
    {
        // Only consider Snake (0) or Avatar (3) — same restriction the engine
        // applies. DD player types never have a bionic arm, vanilla or custom.
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
                // State-change-gated log per (playerType, partsType) pair.
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

        // Pass through to orig for anything we don't override.
        return g_OrigIsArtificialHandEnabled
             ? g_OrigIsArtificialHandEnabled(playerType, playerPartsType)
             : 0;
    }

    // The "ForCurrentPlayerType" variant — reads live state from
    // QuarkSystemTable instead of accepting explicit args. Same whitelist,
    // separate function, separate set of callers. We use the framework's
    // existing live-state readers (which read the same QuarkSystemTable
    // path) to decide whether to override.
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


            // Capture arm tier independently of face — gate on armType > 0 so
            // we only sample valid developed tiers (the engine's commit-blob
            // expansion zeroes armType for new-outfit applies; we only want to
            // remember the values from natural pre-outfit-change calls).
            if (info->playerArmType != 0)
            {
                g_LastInfoArmType     = info->playerArmType;
                g_LastInfoArmCaptured = true;
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
                    // The engine zeroes playerArmType when expanding the new
                    // commit blob into LoadPartsPlayerInfo, which would lose
                    // the player's developed prosthetic upgrade tier on every
                    // outfit swap. Restore from the cached tier captured by
                    // the previous natural LoadPartsNew (when armType was > 0).
                    // Fall back to 1 (basic prosthetic) if no prior natural
                    // call has been observed yet (first-load / cold-start).
                    //
                    // NOTE: a previous version of this code read directly from
                    // the BlockShell pointer at self+0x1100+idx*8 (offset +3),
                    // but that pointer's layout beyond [1] is not stable —
                    // returned values like 10 and 66 that crashed the bionic
                    // arm leaf when used as playerHandType. Cache-based
                    // recovery is the safe path.
                    info->playerArmType = g_LastInfoArmCaptured
                        ? g_LastInfoArmType
                        : std::uint8_t{1};
                    Log("[OutfitRuntimeParts] enableArm restored armType=%u "
                        "(via cache, captured=%d) for partsType=0x%02X\n",
                        static_cast<unsigned>(info->playerArmType),
                        g_LastInfoArmCaptured ? 1 : 0,
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


                // Spoof target = the live player's vanilla STANDARD partsType.
                // Snake STANDARD = 0x01, Avatar/DD STANDARD = 0x00. Using the
                // LIVE playerType (not the entry's) keeps the spoof valid when
                // a Snake outfit is applied on the Avatar slot or vice versa.
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

        Log("[OutfitRuntimeParts] installed: parts=%s fpk=%s camo=%s diamond=%s "
            "bionicArmFv2=%s bionicArmFpk=%s snakeFaceFv2=%s snakeFaceFpk=%s "
            "lpn=%s doesNeedFace=%s doesNeedFaceAvatar=%s setHandSlotEnabled=%s "
            "isArtificialHandEnabled=%s isArtHandForCurrent=%s\n",
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
            g_InstalledIsArtHandForCurrent    ? "OK" : "skip");

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
        // Arm uses its own capture flag (gated on armType>0 instead of faceId>0)
        // so it survives setups where the user's soldierFace is 0 (default Snake).
        info.playerArmType      = g_LastInfoArmCaptured ? g_LastInfoArmType     : std::uint8_t{0};
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


            // Spoof target = live player's vanilla STANDARD partsType (see
            // hkLoadPartsNew comment). Use the caller-supplied live `playerType`
            // so Snake↔Avatar bridging works in ForcePartsReload too.
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

        g_CapturedBlockController = nullptr;

        Log("[OutfitRuntimeParts] removed\n");
    }
}
