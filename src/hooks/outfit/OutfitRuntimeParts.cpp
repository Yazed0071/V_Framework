#include "pch.h"

#include "OutfitRuntimeParts.h"
#include "OutfitRegistry.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    // FoxPath_Path(out, code64ext) writes a FoxPath into `out` and
    // returns it. This is the same primitive the game uses internally
    // to materialize a path from a 64-bit code+extension hash.
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

    // LoadPartsPlayerInfo struct — verified field offsets from
    // mgsvtpp.exe.c (struct size ~0x58 bytes, fields used here):
    struct LoadPartsPlayerInfo
    {
        std::uint8_t  playerType;          // +0x00
        std::uint8_t  playerPartsType;     // +0x01
        std::uint8_t  playerCamoType;      // +0x02
        std::uint8_t  playerArmType;       // +0x03
        std::int16_t  playerFaceId;        // +0x04
        std::uint8_t  playerFaceEquipId;   // +0x06
        std::uint8_t  reserved07;          // +0x07
        std::uint8_t  reserved08[0x4C];    // +0x08..+0x53
        std::uint8_t  reserved54;          // +0x54
        std::uint8_t  reserved55;          // +0x55
        std::uint8_t  playerFaceEquipUnk;  // +0x56
        std::uint8_t  reserved57;          // +0x57
    };
    static_assert(sizeof(LoadPartsPlayerInfo) == 0x58,
                  "LoadPartsPlayerInfo size must match retail layout");

    using LoadPartsNew_t = void (__fastcall*)(
        void* self, std::uint32_t playerIndex,
        LoadPartsPlayerInfo* info, std::uint32_t flags);

    // tpp::gm::player::ResourceTable::DoesNeedFaceFova(uint playerPartsType)
    // — verified at retail 0x140AE84B0 (gAddr.ResourceTable_DoesNeedFaceFova).
    // Static function, single uint param, returns bool.
    //
    // CORRECTED 2026-04-27: takes the PARTSTYPE byte, NOT camo. Confirmed
    // by named-build line 1429508 (function body) + every call site
    // (1310939 in BlockControllerImpl::LoadPartsNew, plus 1309597,
    // 1310421, 1310486, 1323246) all passing playerPartsType.
    //
    // Switch returns true for partsType in {0,1,2,7,8,9, 0xB..0x19} —
    // vanilla "real" outfit slots that have integrated head loading.
    // Returns false for everything else, INCLUDING our custom partsType
    // range (0x40..0x7F) → no face/head FPK loaded → headless body for
    // any custom outfit whose body parts file doesn't ship an integrated
    // head.
    //
    // This hook DOES NOT catch the call site inside LoadPartsNew itself
    // — MSVC inlines the small switch body at the LoadPartsNew call
    // site (verified by user testing: hook installs OK but never fires
    // during outfit equip). The actual fix for enableHead is in
    // hkLoadPartsNew which spoofs info->playerPartsType to a vanilla
    // value to engage the inlined gate; see tl_SpoofedRealPartsType
    // and the spoof block in hkLoadPartsNew below.
    //
    // The hook here is kept as diagnostic-only — if any caller does
    // reach the function-address version, we'll see it logged.
    using DoesNeedFaceFova_t = std::uint8_t (__fastcall*)(std::uint32_t playerPartsType);

    static FoxPath_Path_t                   g_FoxPath_Path                       = nullptr;
    static LoadPlayerPartsParts_t           g_OrigLoadPartsParts                 = nullptr;
    static LoadPlayerPartsFpk_t             g_OrigLoadPartsFpk                   = nullptr;
    static LoadPlayerCamoFpk_t              g_OrigLoadCamoFpk                    = nullptr;
    static LoadPlayerSnakeBlackDiamondFpk_t g_OrigLoadDiamondFpk                 = nullptr;
    static LoadPartsNew_t                   g_OrigLoadPartsNew                   = nullptr;
    static DoesNeedFaceFova_t               g_OrigDoesNeedFaceFova               = nullptr;

    static bool g_InstalledParts          = false;
    static bool g_InstalledFpk            = false;
    static bool g_InstalledCamo           = false;
    static bool g_InstalledDiamond        = false;
    static bool g_InstalledLpn            = false;
    static bool g_InstalledDoesNeedFace   = false;

    // Captured Player2BlockController instance from the first
    // hkLoadPartsNew fire. Used by outfit::ForcePartsReload to drive
    // an immediate equip when an out-of-band path (e.g. supply drop)
    // needs to apply a custom outfit without going through the normal
    // commit pipeline.
    static void* g_CapturedBlockController = nullptr;

    // ----------------------------------------------------------------
    // Thread-local partsType spoof state (added 2026-04-27).
    //
    // hkLoadPartsNew sets this to the REAL custom partsType (0x40..0x7F)
    // when it spoofs info->playerPartsType to a vanilla value (0x00) for
    // the duration of the orig call. The spoof is needed because the
    // orig BlockControllerImpl::LoadPartsNew has DoesNeedFaceFova INLINED
    // by MSVC and the gate function takes the PARTSTYPE byte (NOT camo,
    // contrary to earlier assumption). Verified at named-build line
    // 1310939: `isHeadNeeded = ResourceTable::DoesNeedFaceFova(playerPartsTypeUint);`
    // and function body 1429508: switch(playerPartsType) returns true
    // only for {0,1,2,7,8,9, 0xB..0x19} — our custom range 0x40..0x7F
    // returns false → no face/head FPK queued → headless body.
    //
    // While this thread-local is non-zero, the per-asset hooks
    // (LoadPlayerPartsParts/Fpk, LoadPlayerCamoFpk, Diamond) ignore the
    // (spoofed) param partsType and use this real partsType for registry
    // lookup, so the custom outfit's parts/fpk/camo paths still resolve
    // correctly even though orig is dispatching with vanilla partsType.
    //
    // thread_local because LoadPartsNew can in principle be called from
    // any thread; we don't want a worker-thread spoof to leak into a
    // main-thread hook fire.
    // ----------------------------------------------------------------
    static thread_local std::uint8_t tl_SpoofedRealPartsType = 0;  // 0 = no spoof active

    // Returns the partsType to use for OutfitEntry lookup. When a
    // hkLoadPartsNew partsType-spoof is active, returns the stashed REAL
    // custom partsType so the per-asset hooks route to the registered
    // custom outfit; otherwise returns the param passed by the caller.
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
        if (entry->playerType != ply) return false;

        if (outEntry) *outEntry = entry;
        return true;
    }

    static std::uint64_t* __fastcall hkLoadPlayerPartsParts(
        std::uint64_t* outPath, std::uint32_t playerType, std::uint32_t playerPartsType)
    {
        // Honor the partsType spoof state: when hkLoadPartsNew has
        // spoofed info->playerPartsType to a vanilla value (to engage
        // the inlined DoesNeedFaceFova gate for enableHead outfits),
        // the param here is also the spoofed value. Use the stashed
        // REAL custom partsType for registry lookup so we still route
        // to the custom outfit's assets.
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);

        const outfit::OutfitEntry* entry = nullptr;
        if (ResolveCustomEntry(playerType, effectivePartsType, &entry))
        {
            const std::uint8_t v = entry->HasVariants()
                ? outfit::GetActiveVariant(entry->partsType)
                : 0;
            const std::uint64_t path = entry->GetVariantPartsPath(v);

            // Game polls this every frame; only log when the resolved
            // (playerType, partsType, variant, path) tuple changes —
            // otherwise we'd flood the log at 60Hz for no new info.
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
        // Honor partsType spoof state — see hkLoadPlayerPartsParts.
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
        // Honor partsType spoof state. NOTE: this is critical for the
        // enableHead path — orig LoadPartsNew calls this with spoofed
        // partsType=0x00 BUT real (non-zero) camo. Without overriding
        // partsType to the real custom value, this hook would fall
        // through to the vanilla orig with (partsType=0x00, camo=0x80),
        // which OOB-indexes vanilla camo arrays and corrupts state.
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

            // Custom outfit, no custom camo: write a null FoxPath so
            // the game's camo system gets a defined empty path instead
            // of OOB-indexing its per-playerType camo array with our
            // out-of-range partsType.
            return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
        }
        return g_OrigLoadCamoFpk(outPath, playerType, playerPartsType, playerCamoType);
    }

    static std::uint64_t* __fastcall hkLoadPlayerSnakeBlackDiamondFpk(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t applyBlackDiamond)
    {
        // Honor partsType spoof state — see hkLoadPlayerPartsParts.
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
            // kSubAssetUseVanilla → fall through.
        }
        return g_OrigLoadDiamondFpk(outPath, playerType, playerPartsType,
                                    applyBlackDiamond);
    }

    // ResourceTable::DoesNeedFaceFova diagnostic hook.
    //
    // CORRECTED 2026-04-27: this function takes PARTSTYPE, not camo.
    // For our custom partsType range (0x40..0x7F), if found in registry
    // and enableHead=true, return 1 to make any non-inlined caller
    // see "yes, load face for this partsType."
    //
    // The PRIMARY enableHead mechanism is the partsType-spoof in
    // hkLoadPartsNew (which engages the INLINED gate inside orig
    // LoadPartsNew). This function-level hook is a belt-and-suspenders
    // — it catches any other (non-inlined) call site in case some code
    // path hits the function via address. In testing it never fired
    // during a normal equip (the LoadPartsNew call site is inlined),
    // but leaving it in place is harmless and gives diagnostic coverage
    // for any future call site that isn't inlined.
    static std::uint8_t __fastcall hkDoesNeedFaceFova(std::uint32_t playerPartsType)
    {
        // Custom partsType range only. Vanilla partsTypes defer to orig
        // immediately so unrelated callers see vanilla behavior.
        if (playerPartsType >= outfit::kCustomPartsTypeStart
         && playerPartsType <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            const auto pt = static_cast<std::uint8_t>(playerPartsType & 0xFF);
            const bool found = outfit::TryGetOutfitByPartsType(pt, &entry)
                            && entry;
            const bool enabled = found && entry->IsHeadEnabled();

            // The non-inlined call sites (LOD / shadow / etc.) poll this
            // every frame; only log when the resolved (partsType, found,
            // enabled) tuple changes to avoid flooding the log at 60Hz.
            static std::uint32_t s_lastPartsType = 0xFFFFFFFFu;
            static int           s_lastFound    = -1;
            static int           s_lastEnabled  = -1;
            if (s_lastPartsType != playerPartsType
                || s_lastFound  != (found   ? 1 : 0)
                || s_lastEnabled!= (enabled ? 1 : 0))
            {
                Log("[OutfitRuntimeParts] hkDoesNeedFaceFova: partsType=0x%X "
                    "found=%d enableHead=%d -> %s\n",
                    playerPartsType,
                    found ? 1 : 0,
                    enabled ? 1 : 0,
                    enabled ? "1 (override -> load face)" : "fall-through to orig");
                s_lastPartsType = playerPartsType;
                s_lastFound     = found   ? 1 : 0;
                s_lastEnabled   = enabled ? 1 : 0;
            }

            if (enabled) return 1;
        }
        return g_OrigDoesNeedFaceFova
             ? g_OrigDoesNeedFaceFova(playerPartsType)
             : 0;
    }

    static void __fastcall hkLoadPartsNew(
        void* self, std::uint32_t playerIndex,
        LoadPartsPlayerInfo* info, std::uint32_t flags)
    {
        // Capture the controller on first fire so outfit::ForcePartsReload
        // can drive an out-of-band equip later (used by the supply-drop
        // shortcut to apply a custom outfit immediately).
        if (!g_CapturedBlockController && self)
            g_CapturedBlockController = self;

        if (info)
        {
            // Log every fire so we see when the player parts pipeline
            // runs and what it's asked to load. faceId here = the u8
            // playerFaceEquipId at +0x06 (head equipment selector,
            // 3/4/5 = balaclava overrides per ConverFaceIdWithFaceEquipId).
            // soldierFace = the i16 playerFaceId at +0x04 (soldier face
            // index 0..899; what Soldier2FaceSystem::GetFaceFova reads
            // when DoesNeedFaceFova returns true). faceUnk = the u8 at
            // +0x56 (bit 0 = chicken-mask variant; bits 1/2 = cache
            // invalidation flags).
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

            // Supply-drop / non-MissionPrep equip path arrives at
            // LoadPartsNew with a broken-custom or partial-custom
            // signal because it bypasses the MissionPrep commit hook.
            // Two patterns observed:
            //   - (partsType=0x00, camo=0xFF) — pure broken-custom:
            //     the supply-drop pipeline failed to resolve to any
            //     selector, signals "unknown custom" via 0xFF camo.
            //   - (partsType=0x00, camo in 0x80..0xFE) — partial-custom:
            //     OutfitSupplyDropSetup hook wrote the custom selector
            //     to state[0x8], pipeline propagated it as camo, but
            //     partsType lookup didn't happen so it stays 0. We
            //     can resolve directly via selectorCode.
            //
            // Both patterns route through this rewrite. Resolution order
            // for the 0xFF case:
            //   (a) Pending developId published by OutfitItemSelector's
            //       supply-drop hook (precise: knows exactly what user
            //       clicked).
            //   (b) Live-PT fallback when exactly ONE registered outfit
            //       matches info->playerType.
            //   (c) Force vanilla NORMAL if neither resolves.
            // For the 0x80..0xFE case: direct selectorCode lookup.
            const bool isCustomSelectorRange =
                info->playerCamoType >= outfit::kCustomSelectorStart
             && info->playerCamoType <= outfit::kCustomSelectorEnd;

            if (info->playerPartsType == 0x00 && isCustomSelectorRange)
            {
                // Partial-custom: camo in custom range identifies the
                // outfit directly. Look up by selectorCode.
                const outfit::OutfitEntry* bySel = nullptr;
                if (outfit::TryGetOutfitBySelectorCode(info->playerCamoType, &bySel)
                    && bySel
                    && bySel->playerType == info->playerType)
                {
                    Log("[OutfitRuntimeParts] LoadPartsNew: partial-custom "
                        "(partsType=0,camo=0x%02X) for playerType=%u -> "
                        "selectorCode lookup developId=%u partsType=0x%02X\n",
                        static_cast<unsigned>(info->playerCamoType),
                        static_cast<unsigned>(info->playerType),
                        static_cast<unsigned>(bySel->developId),
                        static_cast<unsigned>(bySel->partsType));
                    info->playerPartsType = bySel->partsType;
                    // Camo already correct (it's the selectorCode).

                    // Sync Quark live state too — orig LoadPartsNew
                    // dispatches based on Quark, not just info, so an
                    // info-only rewrite doesn't actually equip. Without
                    // this, the broken-custom signal at supply-drop
                    // pickup gets bytes-rewritten but the asset never
                    // loads (no LoadPlayerPartsParts).
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
                // Broken-custom signal — resolve ONLY via pendingDevId.
                //
                // The "live-PT-unique" fallback (auto-pick the only
                // registered outfit matching live playerType) was
                // removed 2026-04-26 because it misfired on vanilla
                // selections: when the user picks vanilla in mission
                // prep while a custom is currently equipped, the orig
                // pipeline emits camo=0xFF transients and the fallback
                // would silently re-equip the custom, swapping the
                // player's character class behind the user's back.
                //
                // pendingDevId is the only legitimate "the user just
                // selected a custom outfit" signal. Supply-drop pickup
                // doesn't need a fallback either — OutfitSupplyDropPickup
                // writes Quark + calls LoadPartsNew directly with the
                // resolved bytes, bypassing this signal entirely.
                const outfit::OutfitEntry* chosen = nullptr;
                const char*                via    = "no-match";

                const std::uint16_t pendingDevId = outfit::GetPendingOutfitDevelopId();
                const outfit::OutfitEntry* byPending = nullptr;
                if (pendingDevId != 0
                    && outfit::TryGetOutfitByDevelopId(pendingDevId, &byPending)
                    && byPending
                    && byPending->playerType == info->playerType)
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

                    // Sync Quark live state — see comment in the
                    // partial-custom branch above. Without this the
                    // rewritten info passes through but the orig
                    // doesn't actually load the asset.
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
                // For custom outfits, ensure the donor sub-asset bytes
                // are sane before the orig dispatches the per-asset
                // virtual loaders. Phase 2 conservative defaults:
                //   - If outfit doesn't enable arm: zero playerArmType
                //   - If outfit doesn't enable face: zero playerFaceEquipId
                //   - Camo type is left as-is; our hkLoadPlayerCamoFpk
                //     handles the path.
                if (!entry->IsArmEnabled())
                    info->playerArmType = 0;

                if (!entry->IsFaceEnabled())
                {
                    info->playerFaceEquipId = 0;
                    info->playerFaceEquipUnk =
                        static_cast<std::uint8_t>(info->playerFaceEquipUnk & 0xF8);
                }

                // Optional soldier-face-id override. When enableHead is on
                // and the user has no manual face chosen (playerFaceId=0),
                // force the slot to the registry-supplied index so the
                // orig face loader reads a populated FaceUnit entry. See
                // the defaultSoldierFaceId field comment in OutfitRegistry.h
                // for why this can be necessary.
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
                // Stray custom-range partsType with no registered entry
                // (e.g. left over in Quark from a previous session).
                // Force back to vanilla NORMAL so the game doesn't
                // dispatch with an unresolvable type.
                Log("[OutfitRuntimeParts] LoadPartsNew: stray custom partsType=0x%02X "
                    "playerType=%u — forcing to vanilla 0x00\n",
                    static_cast<unsigned>(info->playerPartsType),
                    static_cast<unsigned>(info->playerType));
                info->playerPartsType = 0x00;
                info->playerCamoType  = 0x00;
            }

            // -------------------------------------------------------------
            // PARTSTYPE SPOOF for face-load gate (added 2026-04-27)
            // -------------------------------------------------------------
            // Orig BlockControllerImpl::LoadPartsNew gates face/head FPK
            // loading on
            //   isHeadNeeded = ResourceTable::DoesNeedFaceFova(playerPartsType);
            // — verified at named-build line 1310939 (PARTSTYPE, not camo).
            // The function body (line 1429508) is a switch returning true
            // for {0,1,2,7,8,9, 0xB..0x19} only. Our custom partsType
            // range (0x40..0x7F) hits the default case → returns false
            // → no face/head FPK queued → headless body for outfits whose
            // body parts don't ship an integrated head (FROG/SSD ports).
            //
            // The function-address hook (hkDoesNeedFaceFova) doesn't
            // catch this call site because MSVC INLINED the small switch
            // body inside LoadPartsNew (verified by user testing: hook
            // installs OK but never fires during equip).
            //
            // Fix: spoof info->playerPartsType to a vanilla value (0x00 =
            // NORMAL) before calling orig, restore after. The inlined
            // switch sees 0x00 → returns true → orig's face-load branch
            // runs (line 1310939-1310972: gets Soldier2FaceSystem instance,
            // calls vtable[0x20] with playerFaceId, writes face/hair/
            // hairDeco/faceDeco PathIds to the shell).
            //
            // To keep custom assets routed correctly despite the spoofed
            // partsType, we stash the REAL partsType in tl_SpoofedRealPartsType
            // (thread-local). The per-asset hooks (LoadPlayerPartsParts,
            // LoadPlayerPartsFpk, LoadPlayerCamoFpk, LoadPlayerSnakeBlackDiamondFpk)
            // call EffectivePartsType() which reads the thread-local and
            // overrides the spoofed param for registry lookup, so the
            // custom outfit's parts/fpk/camo paths still resolve correctly.
            //
            // After orig returns: restore info->playerPartsType, clear the
            // thread-local, AND restore the BlockShell's TypeInfo cache
            // byte (orig's TypeInfo::operator= at line 1310914 copied the
            // spoofed 0x00 to shell+0xF0+0x01). The BlockShell pointer
            // lives at self+playerIndex*8+0x1100 (verified named-build
            // line 1310936); the pointer points to shell+0xF0 (the
            // TypeInfo block, where byte 1 is partsType).
            //
            // Gated on isCustom + IsHeadEnabled, so only registered
            // outfits with enableHead=true trigger the spoof. Vanilla
            // suits and outfits without enableHead are untouched.
            //
            // Spoof to 0x00 (vanilla NORMAL), not 0x07 or other, because:
            //   (a) DoesNeedFaceFova(0x00) returns true.
            //   (b) The orig BlockShell equality short-circuit at line
            //       1310891-1310900 compares shell.partsType to spoofed
            //       0x00 — for re-equips of the same outfit, the previous
            //       restore left shell.partsType=real(0x40), so 0x40!=0x00
            //       and reload proceeds correctly.
            //   (c) Other partsType-keyed lookups (asset arrays etc.) in
            //       orig route via param to our per-asset hooks, which
            //       use EffectivePartsType to reach the real custom range.
            const bool spoofPartsType =
                isCustom && entry && entry->IsHeadEnabled();
            const std::uint8_t origPartsType = info->playerPartsType;
            if (spoofPartsType)
            {
                tl_SpoofedRealPartsType = origPartsType;  // stash real
                info->playerPartsType   = 0x00;           // spoof to NORMAL
                Log("[OutfitRuntimeParts] hkLoadPartsNew: spoofing partsType "
                    "0x%02X -> 0x00 for inlined DoesNeedFaceFova "
                    "(enableHead path, camo=0x%02X soldierFace=%d)\n",
                    static_cast<unsigned>(origPartsType),
                    static_cast<unsigned>(info->playerCamoType),
                    static_cast<int>(info->playerFaceId));
            }

            g_OrigLoadPartsNew(self, playerIndex, info, flags);

            if (spoofPartsType)
            {
                info->playerPartsType   = origPartsType;
                tl_SpoofedRealPartsType = 0;  // clear spoof state
                __try
                {
                    auto* shellTypeInfoPtr =
                        *reinterpret_cast<std::uint8_t**>(
                            reinterpret_cast<std::uint8_t*>(self)
                            + playerIndex * 8 + 0x1100);
                    if (shellTypeInfoPtr)
                        shellTypeInfoPtr[1] = origPartsType;  // playerPartsType offset
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

        void* tParts    = ResolveGameAddress(gAddr.LoadPlayerPartsParts);
        void* tFpk      = ResolveGameAddress(gAddr.LoadPlayerPartsFpk);
        void* tCamo     = ResolveGameAddress(gAddr.LoadPlayerCamoFpk);
        void* tDiamond  = ResolveGameAddress(gAddr.LoadPlayerSnakeBlackDiamondFpk);
        void* tLpn      = ResolveGameAddress(gAddr.Player2BlockController_LoadPartsNew);
        void* tFaceFova = ResolveGameAddress(gAddr.ResourceTable_DoesNeedFaceFova);

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
        if (tLpn)
            g_InstalledLpn = CreateAndEnableHook(
                tLpn, reinterpret_cast<void*>(&hkLoadPartsNew),
                reinterpret_cast<void**>(&g_OrigLoadPartsNew));
        if (tFaceFova)
            g_InstalledDoesNeedFace = CreateAndEnableHook(
                tFaceFova, reinterpret_cast<void*>(&hkDoesNeedFaceFova),
                reinterpret_cast<void**>(&g_OrigDoesNeedFaceFova));

        Log("[OutfitRuntimeParts] installed: parts=%s fpk=%s camo=%s diamond=%s "
            "lpn=%s doesNeedFace=%s\n",
            g_InstalledParts        ? "OK" : "skip",
            g_InstalledFpk          ? "OK" : "skip",
            g_InstalledCamo         ? "OK" : "skip",
            g_InstalledDiamond      ? "OK" : "skip",
            g_InstalledLpn          ? "OK" : "skip",
            g_InstalledDoesNeedFace ? "OK" : "skip");

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

        // Build a minimal LoadPartsPlayerInfo populated with the target
        // outfit bytes. The other fields (camo materials, face, etc.)
        // are left zero — the orig + the per-asset hooks (LoadPlayerPartsParts,
        // LoadPlayerPartsFpk, LoadPlayerCamoFpk) will resolve sub-assets
        // from the registered OutfitEntry.
        LoadPartsPlayerInfo info{};
        info.playerType      = playerType;
        info.playerPartsType = partsType;
        info.playerCamoType  = selectorCode;
        info.playerArmType   = 0;
        info.playerFaceId    = 0;
        info.playerFaceEquipId = 0;

        // Flags 0x15F640 / 0x15F600 observed in normal LoadPartsNew
        // fires for player-slot 0 and 1 respectively (see prior runtime
        // logs). Replicate them here.
        constexpr std::uint32_t kFlagsP0 = 0x15F640;
        constexpr std::uint32_t kFlagsP1 = 0x15F600;

        // CRITICAL: write Quark live player state FIRST. The supply-
        // drop submission immediately fires another LoadPartsNew that
        // reads the persistent player state — if we only call orig
        // LoadPartsNew with our bytes (without updating Quark), the
        // follow-up natural fire reads stale vanilla state and undoes
        // our equip. Updating Quark first means the follow-up sees
        // partsType=0x40/camo=0x80 and our hkLoadPartsNew handlers
        // route the asset load to the custom outfit normally.
        const bool quarkOk =
            outfit::WriteLivePlayerOutfit(partsType, selectorCode, playerType);

        // Apply the same enableHead partsType-spoof we do in hkLoadPartsNew.
        // ForcePartsReload calls the trampoline directly (NOT through our
        // hook), so the spoof must happen here too — otherwise supply-drop
        // pickups of enableHead outfits land headless.
        const outfit::OutfitEntry* entry = nullptr;
        const bool spoofPartsType =
            outfit::TryGetOutfitByPartsType(partsType, &entry)
         && entry
         && entry->IsHeadEnabled();
        const std::uint8_t origPartsType = info.playerPartsType;
        if (spoofPartsType)
        {
            tl_SpoofedRealPartsType = origPartsType;
            info.playerPartsType    = 0x00;
            Log("[OutfitRuntimeParts] ForcePartsReload: spoofing partsType "
                "0x%02X -> 0x00 for inlined DoesNeedFaceFova "
                "(enableHead path, selector=0x%02X)\n",
                static_cast<unsigned>(origPartsType),
                static_cast<unsigned>(selectorCode));
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
            // Clear spoof state on exception to avoid leaking into the
            // next legitimate LoadPartsNew fire on this thread.
            if (spoofPartsType) tl_SpoofedRealPartsType = 0;
            g_CapturedBlockController = nullptr;
            return false;
        }

        // Restore spoofed bytes after the two slot fires. We don't try
        // to restore each shell's TypeInfo cache here because the next
        // natural hkLoadPartsNew fire will rewrite both shells with the
        // real partsType=0x40 (Quark already has the real value, so the
        // follow-up fire applies the real bytes). Clearing the
        // thread-local is the only thing that's strictly required.
        if (spoofPartsType)
        {
            info.playerPartsType    = origPartsType;
            tl_SpoofedRealPartsType = 0;
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
        if (g_InstalledLpn)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.Player2BlockController_LoadPartsNew));
        if (g_InstalledDoesNeedFace)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.ResourceTable_DoesNeedFaceFova));

        g_OrigLoadPartsParts   = nullptr;
        g_OrigLoadPartsFpk     = nullptr;
        g_OrigLoadCamoFpk      = nullptr;
        g_OrigLoadDiamondFpk   = nullptr;
        g_OrigLoadPartsNew     = nullptr;
        g_OrigDoesNeedFaceFova = nullptr;
        g_FoxPath_Path         = nullptr;

        g_InstalledParts        = false;
        g_InstalledFpk          = false;
        g_InstalledCamo         = false;
        g_InstalledDiamond      = false;
        g_InstalledLpn          = false;
        g_InstalledDoesNeedFace = false;

        g_CapturedBlockController = nullptr;

        Log("[OutfitRuntimeParts] removed\n");
    }
}
