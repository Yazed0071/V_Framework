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

    // After ForcePartsReload completes, the orig supply-drop pickup
    // pipeline still fires its OWN follow-up LoadPartsNew with the
    // broken-custom transient signal (partsType=0, camo=0xFF). The
    // broken-custom resolver in hkLoadPartsNew then resolves it via
    // pendingOutfitDevelopId and would re-issue the orig load — a
    // SECOND load 100-200ms after our ForcePartsReload's first load
    // is still mid-progress. Two concurrent loads of the same outfit
    // race in the asset loader's state machine and hang the body
    // change indefinitely (user-visible "infinite loading").
    //
    // To prevent the redundant second load, ForcePartsReload publishes
    // the developId it just loaded into this atomic. The broken-custom
    // resolver checks the atomic: if the resolved developId matches,
    // skip the orig LoadPartsNew (return without calling it). The
    // first ForcePartsReload load is still in flight; the asset loader
    // completes that one cleanly, and we don't pile a duplicate on top.
    //
    // The atomic is one-shot: cleared after the first broken-custom
    // suppression so a subsequent legitimate broken-custom signal
    // (e.g. for a different outfit, or a reload of the same outfit
    // later) takes the normal resolve-and-call-orig path.
    static std::atomic<std::uint16_t> g_RecentForcePartsReloadDevId{0};

    // Snapshot of the last natural LoadPartsNew's info struct values.
    // ForcePartsReload uses these to populate the FULL info struct
    // it passes to orig — empirically, orig short-circuits the load
    // when fields like playerFaceId are zero (the stripped-info
    // ForcePartsReload was building). Capturing the user's actual
    // soldierFace / faceUnk / armType / faceEquipId from the most
    // recent natural fire and reusing them gives orig a recognizable
    // "real load" call so it dispatches to the per-asset loaders.
    static std::int16_t  g_LastInfoFaceId      = 0;     // soldier face ID (e.g. 378)
    static std::uint16_t g_LastInfoFaceEquipId = 0;
    static std::uint8_t  g_LastInfoFaceUnk     = 0;
    static std::uint8_t  g_LastInfoArmType     = 0;
    static bool          g_LastInfoCaptured    = false;

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
        // Resolve the EFFECTIVE partsType — `playerPartsType` is the
        // value orig is calling us with. When `hkLoadPartsNew` has
        // spoofed `info->playerPartsType` to 0x00 (so orig recognizes
        // it and proceeds with the load), `playerPartsType` arrives
        // here as 0x00 even for our custom outfits. EffectivePartsType
        // reads the thread-local stash and returns the REAL custom
        // partsType, so we can look up the registered outfit and
        // decide whether the face should actually be loaded.
        const std::uint32_t effective = EffectivePartsType(playerPartsType);

        if (effective >= outfit::kCustomPartsTypeStart
         && effective <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            const auto pt = static_cast<std::uint8_t>(effective & 0xFF);
            const bool found = outfit::TryGetOutfitByPartsType(pt, &entry)
                            && entry;
            const bool enabled = found && entry->IsHeadEnabled();

            // The non-inlined call sites (LOD / shadow / etc.) poll this
            // every frame; only log when the resolved (partsType, found,
            // enabled, spoofActive) tuple changes to avoid flooding the
            // log at 60Hz.
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
                // RESTORED 2026-04-28: !enableHead → 0 (suppress face).
                //
                // Per MEMORY note 5 (the working architecture):
                //   "Counter-fix in hkDoesNeedFaceFova: read
                //    EffectivePartsType (returns the real custom
                //    partsType during spoof scope), look up the entry;
                //    if IsHeadEnabled() return 1 (load face for FROG-
                //    style), else return 0 (suppress face for Jill-
                //    style integrated-head outfits)."
                //
                // For enableHead=true (FROG): return 1, orig loads
                // user's face on top of the headless body parts.
                //
                // For enableHead=false (Jill): return 0, orig skips
                // the face-load branch, body parts file's integrated
                // head renders alone — no double-head artifact.
                //
                // Earlier today (this session) this was incorrectly
                // reversed to always return 1 based on a misdiagnosed
                // body-load test. The body-load issue at that time was
                // actually caused by SettledHandler-related state
                // corruption, NOT by this hook returning 0. Reverting
                // back to the documented working behavior.
                return enabled ? std::uint8_t{1} : std::uint8_t{0};
            }
            // Custom range but no registered entry — stale partsType.
            // Defer to orig so the game's own behavior decides.
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

            // Snapshot user-context fields when present so ForcePartsReload
            // can pass full info to orig (zero-init "stripped info" makes
            // orig short-circuit the load — see g_LastInfo* declaration
            // for full explanation). Only capture when soldierFace is
            // populated (i.e. the call has the user's character info,
            // not a degenerate zero-everything flag=0x40 fire).
            if (info->playerFaceId != 0)
            {
                g_LastInfoFaceId      = info->playerFaceId;
                g_LastInfoFaceEquipId = info->playerFaceEquipId;
                g_LastInfoFaceUnk     = info->playerFaceEquipUnk;
                g_LastInfoArmType     = info->playerArmType;
                g_LastInfoCaptured    = true;
            }

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
                    // 2026-04-28 — REMOVED the "skip orig if recent
                    // ForcePartsReload" suppression.
                    //
                    // Empirical evidence (iDroid Supply-Drop pickup
                    // log 03:53:18): LoadPlayerPartsParts dispatches
                    // from ORIG's redundant LoadPartsNew call (the one
                    // arriving ~100-200ms after ForcePartsReload), NOT
                    // from ForcePartsReload's trampoline call directly.
                    // ForcePartsReload primes asset-load state; orig's
                    // redundant call drives the actual parts dispatch.
                    //
                    // Skipping orig in the resolver was therefore wrong
                    // — it bypassed the only call that actually fires
                    // LoadPlayerPartsParts. The body never loaded
                    // because we suppressed the call that loads it.
                    //
                    // Always rewrite bytes + write Quark + fall through
                    // to the main-flow partsType-spoof block + orig.
                    // The token (still published by ForcePartsReload
                    // for telemetry) is no longer consumed here.
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
                // For custom outfits, ensure the donor sub-asset bytes
                // are sane before the orig dispatches the per-asset
                // virtual loaders. Phase 2 conservative defaults:
                //   - If outfit doesn't enable arm: zero playerArmType
                //   - If outfit DOES enable arm AND it's currently zero
                //     (typical for custom partsType on Snake, since the
                //     orig SUIT→ARM lookup table only knows vanilla
                //     partsTypes): force to 1 (BIONIC ARM). Otherwise
                //     the orig spoof-to-NORMAL path makes orig think
                //     Snake-NORMAL is equipped — Snake-NORMAL has the
                //     arm integrated into the body parts file, so no
                //     separate arm FPK is dispatched. Result on
                //     SSD-port "armless" body geometry: invisible arms.
                //     Forcing 1 makes orig dispatch the arm load.
                //   - If outfit doesn't enable face: zero playerFaceEquipId
                //   - Camo type is left as-is; our hkLoadPlayerCamoFpk
                //     handles the path.
                if (!entry->IsArmEnabled())
                {
                    info->playerArmType = 0;
                }
                else if (info->playerArmType == 0)
                {
                    info->playerArmType = 1;
                }

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
            // Gated on isCustom only — the spoof is needed for ANY
            // custom partsType, not just enableHead ones. Originally
            // gated on `entry->IsHeadEnabled()` because the inlined
            // DoesNeedFaceFova gate was the known motivator. But
            // 2026-04-28 runtime testing of supply-drop pickup of a
            // non-enableHead custom outfit (Jill, partsType=0x41,
            // enableHead=0) revealed orig LoadPartsNew SILENTLY BAILS
            // when given an unrecognized partsType in the custom range
            // (0x40..0x7F). It doesn't dispatch to the per-asset
            // loaders (LoadPlayerPartsParts etc.), so the body never
            // loads → infinite loading screen on supply-drop crate
            // pickup. Spoofing to 0x00 makes orig recognize a vanilla
            // partsType and proceed normally; per-asset hooks route
            // back to the custom outfit's assets via EffectivePartsType.
            //
            // Side-effect for non-enableHead outfits: orig sees
            // partsType=0x00 → DoesNeedFaceFova(0x00) returns true →
            // orig wants to load a face. Without intervention, that
            // would put the user's face on top of (e.g.) Jill's
            // integrated head. `hkDoesNeedFaceFova` was extended in
            // the same change to detect spoofed-non-enableHead via
            // EffectivePartsType + entry->IsHeadEnabled() and return
            // 0, suppressing the orig face load.
            //
            // Vanilla suits and non-custom outfits are untouched.
            //
            // Spoof to 0x00 (vanilla NORMAL), not 0x07 or other, because:
            //   (a) DoesNeedFaceFova(0x00) returns true (relevant for
            //       enableHead outfits where we want the face load).
            //   (b) The orig BlockShell equality short-circuit at line
            //       1310891-1310900 compares shell.partsType to spoofed
            //       0x00 — for re-equips of the same outfit, the previous
            //       restore left shell.partsType=real(0x40), so 0x40!=0x00
            //       and reload proceeds correctly.
            //   (c) Other partsType-keyed lookups (asset arrays etc.) in
            //       orig route via param to our per-asset hooks, which
            //       use EffectivePartsType to reach the real custom range.
            const bool spoofPartsType = isCustom && entry;
            const std::uint8_t origPartsType = info->playerPartsType;
            std::uint8_t* shellTypeInfoPtr = nullptr;
            std::uint8_t  prevShellPartsType = 0;
            bool          shellSentinelWritten = false;

            // 2026-04-28 — DISABLED (TEMPORARILY) playerFaceId=0 face
            // suppression for !enableHead outfits.
            //
            // Re-enabled in a previous iteration to fix Jill (enableHead
            // =false) showing the user's face on top of her integrated
            // head. Caused a game freeze during DLL init / first equip
            // — symptoms exactly like the earlier disabled-attempt
            // (log 04:06:14): something in the spoof+face-zero
            // combination interferes with orig dispatch in a way that
            // hangs the game.
            //
            // Tracking comments retained; suppressFace is computed but
            // never applied (constexpr false). When we have a safer
            // suppression mechanism (face-FPK loader interception,
            // shell.face PathId zero post-load, etc.) re-enable.
            constexpr bool     suppressFace = false;
            const std::int16_t origFaceId =
                info ? info->playerFaceId : std::int16_t{0};
            (void)suppressFace;
            (void)origFaceId;

            if (spoofPartsType)
            {
                tl_SpoofedRealPartsType = origPartsType;  // stash real

                // playerType-conditional spoof target.
                //
                // The orig BlockControllerImpl::LoadPartsNew has THREE
                // face/head load branches keyed on partsType (verified
                // retail mgsvtpp.exe.c:2714901-2714974):
                //   (a) partsType in {1, 2}      → Soldier2FaceSystem
                //                                  face load (Snake-style:
                //                                  separate face/hair/
                //                                  hairDeco/faceDeco
                //                                  paths via vtable[0x20])
                //   (b) partsType == 3           → Avatar face load
                //   (c) anything else            → ResourceTable::
                //                                  GetFaceFpkPath generic
                //                                  (DDMale-style; works
                //                                  for DDMale-NORMAL)
                //
                // Spoofing to 0x00 lands in branch (c). For DDMale (FROGS
                // style) GetFaceFpkPath returns a real path → face/head
                // appears. For Snake-PT custom outfits the same generic
                // path returns nothing for our custom selectorCode →
                // head + arm don't render even though the .parts file
                // has the geometry.
                //
                // Fix: when entry->playerType == Snake, spoof to 0x01
                // (Snake's first variant in the {1,2} range) so the
                // Soldier2FaceSystem branch fires and writes the head/
                // hair paths to the BlockShell. The hand path is also
                // partsType-keyed (GetHandFpkPath at line 2714895), so
                // partsType=0x01 routes through Snake's hand pipeline
                // too instead of the empty NORMAL slot.
                //
                // Per-asset hooks (LoadPlayerPartsParts/Fpk/CamoFpk) use
                // EffectivePartsType to recover the REAL custom partsType
                // from the thread-local, so our outfit's custom paths
                // still resolve correctly regardless of the spoof value.
                std::uint8_t spoofTarget = 0x00;  // default for DDMale/F
                if (entry->playerType == outfit::kPlayerType_Snake)
                {
                    spoofTarget = 0x01;
                }
                info->playerPartsType   = spoofTarget;

                // PRE-ORIG SHELL CLOBBER (added 2026-04-28).
                //
                // Orig BlockControllerImpl::LoadPartsNew has an early
                // equality short-circuit at named-build line 1310891-1310900
                // that compares shell.partsType to info.playerPartsType
                // and bails the entire reload if they match — no dispatch
                // to LoadPlayerPartsParts/Fpk/CamoFpk, body never loads.
                //
                // Vulnerable case (verified in log 03:13:16.198): user
                // wearing vanilla NORMAL (shell.partsType=0x00) requests
                // a custom outfit via supply drop. At pickup time:
                //   - resolver rewrites info.partsType=0x41 (real custom)
                //   - spoof rewrites info.partsType=0x00 (vanilla NORMAL)
                //   - shell.partsType is still 0x00 from the previous
                //     vanilla equip
                //   - orig sees info==shell on partsType, short-circuits,
                //     skips body load → infinite loading screen.
                //
                // Re-equip-of-same-custom case worked previously because
                // the previous LoadPartsNew's post-orig restore left
                // shell.partsType=0x40 (the real custom value), and 0x40
                // != 0x00 (spoofed) → orig proceeded with reload. But
                // the first-equip-from-vanilla case wasn't covered.
                //
                // Fix: pre-orig, save the current shell.partsType and
                // clobber it to 0xFE (sentinel value never written by
                // legitimate code paths). Now orig sees info.partsType=
                // 0x00 != shell.partsType=0xFE on every spoof-scope call,
                // proceeds with the load, dispatches LoadPlayerPartsParts
                // via per-asset hooks. After orig: restore shell to the
                // REAL custom partsType (origPartsType) so future
                // equality checks see the correct value.
                __try
                {
                    shellTypeInfoPtr =
                        *reinterpret_cast<std::uint8_t**>(
                            reinterpret_cast<std::uint8_t*>(self)
                            + playerIndex * 8 + 0x1100);
                    if (shellTypeInfoPtr)
                    {
                        prevShellPartsType    = shellTypeInfoPtr[1];
                        shellTypeInfoPtr[1]   = 0xFE;  // sentinel
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

            // Diagnostic: if this fires, orig RETURNED (didn't hang).
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
                tl_SpoofedRealPartsType = 0;  // clear spoof state
                __try
                {
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

        // Build a LoadPartsPlayerInfo populated with the target outfit
        // bytes AND the user-context fields (soldierFace, faceUnk, arm,
        // faceEquipId) snapshotted from the most recent natural
        // LoadPartsNew fire. Empirically (2026-04-28 runtime testing),
        // calling orig LoadPartsNew via trampoline with stripped info
        // (playerFaceId=0 etc.) makes orig short-circuit the load —
        // it doesn't dispatch to the per-asset loaders (LoadPlayerPartsParts
        // doesn't fire), the body never appears, infinite-loading
        // screen on supply-drop pickup. Reusing the captured user
        // context gives orig a recognizable "real load" call so it
        // dispatches normally; per-asset hooks then route the
        // partsType-keyed lookups to the custom outfit's assets via
        // EffectivePartsType.
        // 2026-04-28 — DISABLED playerFaceId=0 face suppression for
        // !enableHead (again). See hkLoadPartsNew spoof block comment.
        // Body load takes priority over the head-cosmetic-overlay fix.
        LoadPartsPlayerInfo info{};
        info.playerType         = playerType;
        info.playerPartsType    = partsType;
        info.playerCamoType     = selectorCode;
        info.playerArmType      = g_LastInfoCaptured ? g_LastInfoArmType     : std::uint8_t{0};
        info.playerFaceId       = g_LastInfoCaptured ? g_LastInfoFaceId      : std::int16_t{0};
        info.playerFaceEquipId  = g_LastInfoCaptured ? g_LastInfoFaceEquipId : std::uint16_t{0};
        info.playerFaceEquipUnk = g_LastInfoCaptured ? g_LastInfoFaceUnk     : std::uint8_t{0};

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

        // Apply the partsType-spoof we do in hkLoadPartsNew. ForcePartsReload
        // calls the trampoline directly (NOT through our hook), so the spoof
        // must happen here too. Originally gated on `IsHeadEnabled()` because
        // the inlined DoesNeedFaceFova gate was the known motivator. But
        // 2026-04-28 runtime testing of supply-drop pickup of a non-enableHead
        // outfit (Jill, partsType=0x41) revealed orig LoadPartsNew SILENTLY
        // BAILS when it doesn't recognize a custom-range partsType — no
        // dispatch to per-asset loaders, body never loads, infinite loading
        // screen. Spoofing to 0x00 (vanilla NORMAL) makes orig accept the
        // partsType and proceed; per-asset hooks route back to the custom
        // outfit's assets via EffectivePartsType (the thread-local that
        // reads `tl_SpoofedRealPartsType`). hkDoesNeedFaceFova was extended
        // in the same change to detect spoofed-non-enableHead via Effective-
        // PartsType + IsHeadEnabled() and return 0, suppressing orig's face
        // load (Jill has integrated head; we don't want a separate face on
        // top).
        //
        // So: spoof for ALL custom outfits, regardless of enableHead.
        const outfit::OutfitEntry* entry = nullptr;
        const bool spoofPartsType =
            outfit::TryGetOutfitByPartsType(partsType, &entry)
         && entry;
        const std::uint8_t origPartsType = info.playerPartsType;

        // Pre-orig shell clobber for the equality short-circuit (same
        // motivation as the hkLoadPartsNew spoof block — see the long
        // comment there). Orig BlockControllerImpl::LoadPartsNew bails
        // the reload entirely if shell.partsType matches the spoofed
        // info.playerPartsType (0x00). Most ForcePartsReload paths run
        // shortly after a fresh natural LoadPartsNew that left the
        // shells in some valid state, but on a freshly-vanilla shell
        // (player was wearing NORMAL, partsType=0x00) the spoofed 0x00
        // would equal shell's 0x00 → bail. Clobber to 0xFE pre-orig.
        std::uint8_t* shellTypeInfoPtr0 = nullptr;
        std::uint8_t* shellTypeInfoPtr1 = nullptr;
        std::uint8_t  prevShellPartsType0 = 0;
        std::uint8_t  prevShellPartsType1 = 0;

        if (spoofPartsType)
        {
            tl_SpoofedRealPartsType = origPartsType;

            // playerType-conditional spoof target. See the long comment
            // in hkLoadPartsNew about the three orig face-load branches
            // (partsType {1,2} → Soldier2FaceSystem; partsType 3 →
            // Avatar; else → generic GetFaceFpkPath). Snake (PT=0)
            // needs the {1,2} branch to fire for head/hand to load
            // properly; spoofing to 0x00 falls into the generic branch
            // which returns empty paths for our custom selectorCode.
            std::uint8_t spoofTarget = 0x00;
            if (entry->playerType == outfit::kPlayerType_Snake)
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
            // Clear spoof state on exception to avoid leaking into the
            // next legitimate LoadPartsNew fire on this thread.
            if (spoofPartsType) tl_SpoofedRealPartsType = 0;
            // Best-effort restore of clobbered shell bytes so a stale
            // 0xFE doesn't leak into the next natural LoadPartsNew.
            __try
            {
                if (shellTypeInfoPtr0) shellTypeInfoPtr0[1] = origPartsType;
                if (shellTypeInfoPtr1) shellTypeInfoPtr1[1] = origPartsType;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            g_CapturedBlockController = nullptr;
            return false;
        }

        // Restore spoofed bytes after the two slot fires. Restore the
        // shell.partsType cache to the REAL custom partsType so future
        // equality checks see the correct value (and a stale 0xFE
        // sentinel can't leak into the next natural LoadPartsNew).
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

        // Publish the developId we just loaded so the broken-custom
        // resolver in hkLoadPartsNew can suppress the orig pickup
        // pipeline's redundant follow-up LoadPartsNew. Without this,
        // the follow-up resolves via pendingOutfitDevelopId and
        // re-issues the orig load while ours is still in progress —
        // double-load race hangs the asset loader (user-visible
        // "infinite loading on supply-drop pickup").
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
