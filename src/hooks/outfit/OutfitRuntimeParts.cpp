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

    using LoadPlayerBionicArmFpk_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerHandType);

    using LoadPlayerSnakeFaceFpk_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerFaceId,
        std::uint8_t playerFaceEquipId);

    using InitLoadPlayerPartsParts_t = std::uint64_t (__fastcall*)(
        void* blockController, std::uint32_t playerSlot,
        std::uint32_t playerType, std::uint32_t playerPartsType);

    // Player2Impl::SetUpParts — named-build returns bool, retail decompiled as
    // void (likely Ghidra mistype). Returns true if body bound (vtable[0x4f8]
    // succeeded), false if vtable[0x4f8] returned -1 (body NOT bound, state
    // stays at 1 — body never appears).
    //
    // Args (named-build): (Player2Impl*, slot, playerType, partsType,
    //                       param_5, param_6, param_7=int)
    using Player2Impl_SetUpParts_t = bool (__fastcall*)(
        void* this_ptr, std::uint32_t slot, std::uint32_t playerType,
        std::uint32_t partsType, std::uint64_t param_5, std::uint64_t param_6,
        std::int32_t param_7);


    // Player2GameObjectImpl::UpdatePartsStatus — the orchestrator function
    // (mgsvtpp.exe.c:1322700, retail 0x1409CC380). Per-frame state machine
    // driving body/arm/head load + bind for each player+buddy slot.
    //
    // Reads per-slot byte arrays at *(pIVar3+0x40..0x68) where
    // pIVar3 = *(param_1+0x80). These arrays hold the loadout for each slot:
    //   +0x40 = playerType[]
    //   +0x48 = playerPartsType[]   ← we patch this 0x40 → 0x00
    //   +0x50 = playerCamoType[]
    //   +0x58 = playerArmType[]
    //   +0x60 = playerFaceId[] (ushort)
    //   +0x68 = playerFaceEquipId[]
    //
    // For Snake (PT=0) custom outfits with our spoofed body bind succeeding
    // but arm/head not rendering, the cause is downstream calls in the
    // orchestrator (LoadAdditionalFacialBlock, SetEmblemTexture, vtable[0xb8])
    // reading partsType=0x40 and either bailing or not initializing arm/head
    // bind state for unknown partsType.
    //
    // Fix: patch the per-slot partsType byte array to 0x00 BEFORE orig
    // UpdatePartsStatus runs, restore after. Orchestrator and all downstream
    // calls see vanilla 0x00 → arm/head bind/render normally. Outside of
    // this per-frame call, the byte stays at 0x40 so framework's outfit
    // tracking (UI filters, equip checks, etc.) still sees the custom value.
    using UpdatePartsStatus_t = void (__fastcall*)(void* gameObjectImpl);


    struct LoadPartsPlayerInfo
    {
        std::uint8_t  playerType;
        std::uint8_t  playerPartsType;
        std::uint8_t  playerCamoType;
        std::uint8_t  playerArmType;
        std::int16_t  playerFaceId;
        std::uint8_t  playerFaceEquipId;
        std::uint8_t  reserved07;
        std::uint8_t  reserved08[0x4C];    // +0x08..+0x53
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

    static FoxPath_Path_t                   g_FoxPath_Path                       = nullptr;
    static LoadPlayerPartsParts_t           g_OrigLoadPartsParts                 = nullptr;
    static LoadPlayerPartsFpk_t             g_OrigLoadPartsFpk                   = nullptr;
    static LoadPlayerCamoFpk_t              g_OrigLoadCamoFpk                    = nullptr;
    static LoadPlayerSnakeBlackDiamondFpk_t g_OrigLoadDiamondFpk                 = nullptr;
    static LoadPartsNew_t                   g_OrigLoadPartsNew                   = nullptr;
    static DoesNeedFaceFova_t               g_OrigDoesNeedFaceFova               = nullptr;
    static LoadPlayerSnakeFaceFpk_t         g_OrigLoadSnakeFaceFpk               = nullptr;

    static LoadPlayerBionicArmFpk_t         g_LoadPlayerBionicArmFpk             = nullptr;
    static InitLoadPlayerPartsParts_t       g_InitLoadPlayerPartsParts           = nullptr;
    static InitLoadPlayerPartsParts_t       g_OrigInitLoadPlayerPartsParts       = nullptr;
    static Player2Impl_SetUpParts_t         g_OrigSetUpParts                     = nullptr;
    static UpdatePartsStatus_t              g_OrigUpdatePartsStatus              = nullptr;

    static bool g_InstalledParts          = false;
    static bool g_InstalledFpk            = false;
    static bool g_InstalledCamo           = false;
    static bool g_InstalledDiamond        = false;
    static bool g_InstalledLpn            = false;
    static bool g_InstalledDoesNeedFace   = false;
    static bool g_InstalledSnakeFace      = false;
    static bool g_InstalledInitLoadParts  = false;
    static bool g_InstalledSetUpParts     = false;
    static bool g_InstalledUpdateParts    = false;


    static void* g_CapturedBlockController = nullptr;


    static thread_local std::uint8_t tl_SpoofedRealPartsType = 0;
    static thread_local bool tl_InsideLoadPartsNewSpoof = false;


    static std::atomic<std::uint16_t> g_RecentForcePartsReloadDevId{0};


    static std::int16_t  g_LastInfoFaceId      = 0;
    static std::uint16_t g_LastInfoFaceEquipId = 0;
    static std::uint8_t  g_LastInfoFaceUnk     = 0;
    static std::uint8_t  g_LastInfoArmType     = 0;
    static bool          g_LastInfoCaptured    = false;


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

    static void* GetBlockControllerImpl(void* wrapperSelf)
    {
        if (!wrapperSelf) return nullptr;
        __try
        {
            return *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(wrapperSelf) + 0x10);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
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


        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);

        const outfit::OutfitEntry* entry = nullptr;
        if (ResolveCustomEntry(playerType, effectivePartsType, &entry))
        {
            const std::uint8_t v = entry->HasVariants()
                ? outfit::GetActiveVariant(entry->partsType)
                : 0;
            const std::uint64_t path = entry->GetVariantPartsPath(v);

            if (tl_InsideLoadPartsNewSpoof)
            {
                Log("[OutfitRuntimeParts] LoadPlayerPartsParts [INSIDE spoof] "
                    "outPath=%p playerType=%u partsType=0x%02X[spoofed] -> "
                    "effective=0x%02X variant=%u path=0x%016llX (developId=%u)\n",
                    outPath, playerType,
                    static_cast<unsigned>(playerPartsType & 0xFF),
                    static_cast<unsigned>(effectivePartsType & 0xFF),
                    static_cast<unsigned>(v),
                    static_cast<unsigned long long>(path),
                    static_cast<unsigned>(entry->developId));
            }


            static std::uint32_t s_lastPlayerType   = 0xFFFFFFFFu;
            static std::uint32_t s_lastPartsType    = 0xFFFFFFFFu;
            static std::uint8_t  s_lastVariant      = 0xFFu;
            static std::uint64_t s_lastPath         = 0;
            static void*         s_lastReturnAddr   = nullptr;
            void* retAddr = _ReturnAddress();
            if (s_lastPlayerType != playerType
                || s_lastPartsType != effectivePartsType
                || s_lastVariant   != v
                || s_lastPath      != path
                || s_lastReturnAddr != retAddr)
            {
                Log("[OutfitRuntimeParts] LoadPlayerPartsParts: playerType=%u "
                    "partsType=0x%02X variant=%u -> custom path=0x%016llX (developId=%u)%s "
                    "retAddr=%p\n",
                    playerType, effectivePartsType & 0xFFu,
                    static_cast<unsigned>(v),
                    static_cast<unsigned long long>(path),
                    static_cast<unsigned>(entry->developId),
                    (effectivePartsType != playerPartsType) ? " [via spoof]" : "",
                    retAddr);
                s_lastPlayerType  = playerType;
                s_lastPartsType   = effectivePartsType;
                s_lastVariant     = v;
                s_lastPath        = path;
                s_lastReturnAddr  = retAddr;
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


    static std::uint64_t* __fastcall hkLoadPlayerSnakeFaceFpk(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerFaceId,
        std::uint8_t playerFaceEquipId)
    {
        const std::uint32_t effectivePartsType =
            EffectivePartsType(playerPartsType);

        const outfit::OutfitEntry* entry = nullptr;
        if (ResolveCustomEntry(playerType, effectivePartsType, &entry)
            && entry)
        {
            const std::uint64_t faceCode = entry->faceFpk;

            if (!entry->IsHeadEnabled() || faceCode == outfit::kSubAssetDisabled)
            {
                static std::atomic<bool> s_loggedSuppress{false};
                if (!s_loggedSuppress.exchange(true))
                {
                    Log("[OutfitRuntimeParts] hkLoadPlayerSnakeFaceFpk: "
                        "suppressing face for PT=%u partsType=0x%02X "
                        "(enableHead=%d faceFpk=0x%016llX) - body integrated "
                        "head renders alone\n",
                        playerType,
                        static_cast<unsigned>(effectivePartsType),
                        entry->IsHeadEnabled() ? 1 : 0,
                        static_cast<unsigned long long>(faceCode));
                }
                return WriteFoxPath(outPath, 0);
            }

            if (faceCode != outfit::kSubAssetUseVanilla)
            {
                static std::atomic<bool> s_loggedCustom{false};
                if (!s_loggedCustom.exchange(true))
                {
                    Log("[OutfitRuntimeParts] hkLoadPlayerSnakeFaceFpk: "
                        "routing custom face for PT=%u partsType=0x%02X "
                        "(faceFpk=0x%016llX)\n",
                        playerType,
                        static_cast<unsigned>(effectivePartsType),
                        static_cast<unsigned long long>(faceCode));
                }
                return WriteFoxPath(outPath, faceCode);
            }
        }

        if (g_OrigLoadSnakeFaceFpk)
            return g_OrigLoadSnakeFaceFpk(
                outPath, playerType, playerPartsType,
                playerFaceId, playerFaceEquipId);
        return WriteFoxPath(outPath, 0);
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

    // 2026-05-01: Diagnostic hook on InitLoadPlayerPartsParts to verify
    // whether the .parts file is found in the BlockGroupOperator's block.
    //
    // InitLoadPlayerPartsParts:
    //   1. Calls LoadPlayerPartsParts(playerType, partsType) to resolve path
    //      (our LoadPlayerPartsParts hook substitutes user's path here)
    //   2. Calls GetPartsBlockNew(impl, slot) to get the block
    //   3. Calls fox::Block::GetFileForPathId(block, &path) to look up file
    //   4. Returns DataSetFile2* (NULL if not found)
    //
    // If return value is 0/NULL, file is not in block (FPK content issue:
    // user's FPK doesn't contain a file at the user's parts path hash).
    // If return value is non-NULL, file found, vtable[0x4f8] should succeed.
    static std::uint64_t __fastcall hkInitLoadPlayerPartsParts(
        void* blockController, std::uint32_t playerSlot,
        std::uint32_t playerType, std::uint32_t playerPartsType)
    {
        const std::uint64_t result = g_OrigInitLoadPlayerPartsParts(
            blockController, playerSlot, playerType, playerPartsType);


        if (playerType == 0
            || (playerPartsType >= outfit::kCustomPartsTypeStart
                && playerPartsType <= outfit::kCustomPartsTypeEnd))
        {
            static std::uint32_t s_lastPlayerType   = 0xFFFFFFFFu;
            static std::uint32_t s_lastPlayerSlot   = 0xFFFFFFFFu;
            static std::uint32_t s_lastPartsType    = 0xFFFFFFFFu;
            static std::uint64_t s_lastResult       = 0xDEADBEEFDEADBEEFull;
            static void*         s_lastReturnAddr   = nullptr;
            void* retAddr = _ReturnAddress();
            if (s_lastPlayerType != playerType
                || s_lastPlayerSlot != playerSlot
                || s_lastPartsType != playerPartsType
                || s_lastResult != result
                || s_lastReturnAddr != retAddr)
            {
                Log("[OutfitRuntimeParts] hkInitLoadPlayerPartsParts: "
                    "slot=%u PT=%u partsType=0x%02X -> result=0x%016llX %s "
                    "retAddr=%p\n",
                    playerSlot, playerType,
                    static_cast<unsigned>(playerPartsType & 0xFF),
                    static_cast<unsigned long long>(result),
                    (result == 0)
                        ? "[NULL DataSetFile2* — file NOT found in block, "
                          "vtable[0x4f8] WILL bail with -1]"
                        : "[non-NULL DataSetFile2* — file found, "
                          "vtable[0x4f8] should succeed]",
                    retAddr);
                s_lastPlayerType  = playerType;
                s_lastPlayerSlot  = playerSlot;
                s_lastPartsType   = playerPartsType;
                s_lastResult      = result;
                s_lastReturnAddr  = retAddr;
            }
        }

        return result;
    }

    // 2026-05-01: Diagnostic hook on Player2Impl::SetUpParts to definitively
    // determine if vtable[0x4f8] is bailing.
    //
    // Decomp line 1322985-1322986 (named-build line 2731951):
    //   bool SetUpParts(Player2Impl*, slot, playerType, partsType, ...);
    //
    //   SetUpParts internals:
    //     local_128 = InitLoadPlayerPartsParts(s_instance, slot, playerType, partsType);
    //     piVar4 = vtable[0x4f8](plVar1, &local_150, slot, &local_128);
    //     if (*piVar4 == -1) return false;  // BAIL — body NOT bound
    //     // ... post-bind setup ...
    //     return true;  // body bound, state advances 1 → 3
    //
    // Orchestrator at line 1322985:
    //   cVar9 = SetUpParts(...); if (cVar9 != '\0') { /* advance state to 3 */ }
    //
    // If SetUpParts returns false, post-bind setup skipped, state stays at 1,
    // body never appears. This hook captures the return value to confirm.
    static bool __fastcall hkSetUpParts(
        void* this_ptr, std::uint32_t slot, std::uint32_t playerType,
        std::uint32_t partsType, std::uint64_t param_5, std::uint64_t param_6,
        std::int32_t param_7)
    {

        // 2026-05-01: For unregistered (PT, partsType) combos (e.g. buddy
        // slot when player wears a custom Snake outfit), SKIP orig entirely
        // and return TRUE. This:
        //   1. Tells orchestrator "succeeded", state advances 1 → 3
        //   2. Doesn't actually call orig (no vtable[0x4f8] bail)
        //   3. Buddy keeps its previous bind (whatever was loaded before)
        //
        // Substituting to 0x00 (previous attempt) didn't work because DDog's
        // (PT=3) parts block doesn't have a file at vanilla NORMAL hash
        // either — DDog has its own content loaded.
        //
        // The skip-and-return-true approach:
        //   - Player slot (PT=0, registered): orig runs normally, returns TRUE
        //   - Buddy slot (PT=3, not registered for partsType=0x40): skipped,
        //     return TRUE to satisfy orchestrator. Buddy's previous parts
        //     remain bound (from before the player switched outfit).
        //
        // Hypothesis: buddy's state-stuck-at-1 may have been invalidating
        // player rendering through cross-slot dependency. If body becomes
        // visible after this fix, confirmed.
        bool   skippedOrig = false;
        if (partsType >= outfit::kCustomPartsTypeStart
            && partsType <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            const std::uint8_t pt = static_cast<std::uint8_t>(partsType & 0xFF);
            const bool registeredForPT =
                outfit::TryGetOutfitByPartsType(pt, &entry)
             && entry
             && entry->playerType == static_cast<std::uint8_t>(playerType & 0xFF);
            if (!registeredForPT)
            {
                skippedOrig = true;
            }
        }

        const bool result = skippedOrig
            ? true  // skip orig, force TRUE so orchestrator advances state
            : g_OrigSetUpParts(this_ptr, slot, playerType, partsType,
                               param_5, param_6, param_7);


        if (playerType == 0
            || (partsType >= outfit::kCustomPartsTypeStart
                && partsType <= outfit::kCustomPartsTypeEnd))
        {
            static std::uint32_t s_lastSlot          = 0xFFFFFFFFu;
            static std::uint32_t s_lastPT            = 0xFFFFFFFFu;
            static std::uint32_t s_lastPartsType     = 0xFFFFFFFFu;
            static int           s_lastResult        = -1;
            static int           s_lastSkipped       = -1;
            if (s_lastSlot != slot
                || s_lastPT != playerType
                || s_lastPartsType != partsType
                || s_lastResult != (result ? 1 : 0)
                || s_lastSkipped != (skippedOrig ? 1 : 0))
            {
                Log("[OutfitRuntimeParts] hkSetUpParts: slot=%u PT=%u "
                    "partsType=0x%02X%s -> %s\n",
                    slot, playerType,
                    static_cast<unsigned>(partsType & 0xFF),
                    skippedOrig
                        ? " [SKIPPED orig (no registered outfit for this PT) — forcing TRUE to advance orchestrator state]"
                        : "",
                    result
                        ? "TRUE [body bound, state advances 1 -> 3]"
                        : "FALSE [vtable[0x4f8] BAILED with -1, state STUCK at 1, body invisible]");
                s_lastSlot         = slot;
                s_lastPT           = playerType;
                s_lastPartsType    = partsType;
                s_lastResult       = result ? 1 : 0;
                s_lastSkipped      = skippedOrig ? 1 : 0;
            }
        }

        return result;
    }

    // 2026-05-01: hkUpdatePartsStatus — patches per-slot partsType byte array
    // BEFORE orchestrator runs so all downstream calls see vanilla.
    //
    // For Snake (PT=0) custom outfits, body bind succeeds via SetUpParts
    // but arm/head don't render. Root cause: post-bind setup calls in the
    // orchestrator (LoadAdditionalFacialBlock, SetEmblemTexture, multiple
    // vtable calls on Player2Impl sub-objects) read partsType=0x40 from
    // the per-slot loadout array and either bail or fail to initialize
    // arm/head bind state for unknown partsType.
    //
    // The orchestrator at line 1322797-1322802 reads:
    //   playerPartsType_00 = *(byte*)(slot + *(longlong*)(pIVar3 + 0x48));
    // We patch this byte to 0x00 (vanilla NORMAL) before orig runs. Inside
    // orig, every partsType read returns 0x00, so post-bind setup runs as
    // if it's a vanilla outfit. Arm/head bind correctly.
    //
    // After orig returns, we restore the original 0x40 byte so framework
    // state (outfit equipped tracking, UI filters, R&D state) still
    // reflects the custom outfit. The patch is invisible outside the
    // per-frame UpdatePartsStatus window.
    //
    // Safety:
    //   - Only patches bytes already in custom range (0x40..0x7F)
    //   - Vanilla bytes (0x00..0x1B) untouched
    //   - SEH-wrapped reads/writes
    //   - Per-call save/restore (no leak across frames)
    static void __fastcall hkUpdatePartsStatus(void* gameObjectImpl)
    {
        constexpr std::size_t kMaxSlots = 16;  // safety cap on slot iteration
        std::uint8_t* partsTypeArr = nullptr;
        std::uint8_t  origBytes[kMaxSlots] = {};
        std::uint8_t  patchedMask = 0;
        std::uint32_t numSlots    = 0;


        __try
        {
            if (gameObjectImpl)
            {
                void* pIVar3 = *reinterpret_cast<void**>(
                    reinterpret_cast<std::uint8_t*>(gameObjectImpl) + 0x80);

                std::uint32_t count = *reinterpret_cast<std::uint32_t*>(
                    reinterpret_cast<std::uint8_t*>(gameObjectImpl) + 0x228);

                if (pIVar3 && count > 0 && count <= kMaxSlots)
                {
                    partsTypeArr = *reinterpret_cast<std::uint8_t**>(
                        reinterpret_cast<std::uint8_t*>(pIVar3) + 0x48);

                    if (partsTypeArr)
                    {
                        numSlots = count;
                        for (std::uint32_t i = 0; i < count; ++i)
                        {
                            origBytes[i] = partsTypeArr[i];
                            if (origBytes[i] >= outfit::kCustomPartsTypeStart
                                && origBytes[i] <= outfit::kCustomPartsTypeEnd)
                            {
                                partsTypeArr[i] = 0x00;
                                patchedMask |= static_cast<std::uint8_t>(1u << i);
                            }
                        }
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            partsTypeArr = nullptr;
            patchedMask  = 0;
        }


        if (patchedMask != 0)
        {
            static std::uint8_t s_lastMask = 0xFFu;
            static std::uint8_t s_lastByte0 = 0xFFu;
            const std::uint8_t b0 = (numSlots > 0) ? origBytes[0] : 0xFF;
            if (s_lastMask != patchedMask || s_lastByte0 != b0)
            {
                Log("[OutfitRuntimeParts] hkUpdatePartsStatus: patched per-slot "
                    "partsType byte array (mask=0x%02X numSlots=%u origByte[0]=0x%02X) "
                    "-> orchestrator + downstream calls see 0x00 for this frame\n",
                    static_cast<unsigned>(patchedMask),
                    numSlots,
                    static_cast<unsigned>(b0));
                s_lastMask  = patchedMask;
                s_lastByte0 = b0;
            }
        }


        g_OrigUpdatePartsStatus(gameObjectImpl);


        if (patchedMask != 0 && partsTypeArr != nullptr)
        {
            __try
            {
                for (std::uint32_t i = 0; i < numSlots; ++i)
                {
                    if (patchedMask & (1u << i))
                        partsTypeArr[i] = origBytes[i];
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
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
                g_LastInfoArmType     = info->playerArmType;
                g_LastInfoCaptured    = true;
            }


            const bool isCustomSelectorRange =
                info->playerCamoType >= outfit::kCustomSelectorStart
             && info->playerCamoType <= outfit::kCustomSelectorEnd;

            if (info->playerPartsType == 0x00 && isCustomSelectorRange)
            {


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

                if (entry->armType != 0xFF)
                {
                    info->playerArmType = entry->armType;
                }
                else if (!entry->IsArmEnabled())
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

            const std::uint8_t origCamoType = info->playerCamoType;
            if (spoofPartsType)
            {
                tl_SpoofedRealPartsType = origPartsType;


                // 2026-05-01: spoofTarget changed from 0x07 → 0x00 for Snake/Avatar.
                //
                // The spoof target determines which vanilla partsType's .parts
                // file gets async-loaded into the BlockGroupOperator's block.
                // Then SetUpParts (state machine state 1) queries that block
                // by HASH using `LoadPlayerPartsParts(playerType, partsType)`
                // → our hook returns user's parts path hash → query block.
                //
                // If block contains a DIFFERENT parts file than our user's
                // hash, fox::Block::GetFileForPathId returns NULL → SetUpParts'
                // vtable[0x4f8] returns piVar4 with *piVar4 == -1 → BAIL →
                // body never registered → invisible.
                //
                // For Snake (PT=0) with user's partsPath = vanilla Snake NORMAL
                // (sna4_main0_def_v00.parts), spoofing to 0x00 causes the block
                // to load partsType=0x00's file = vanilla Snake NORMAL = same
                // hash as user's path → match → vtable[0x4f8] succeeds.
                //
                // Previous spoof target 0x07 caused mismatch: block loaded v07
                // (sna4_main7_def or similar) while user requested v00 hash →
                // NULL → BAIL → invisible.
                //
                // For Female (PT=1/2), spoofTarget was already 0x00 (worked).
                // Avatar (PT=3) untested but pattern same as Snake → 0x00.
                std::uint8_t spoofTarget = 0x00;
                if (entry->playerType == outfit::kPlayerType_Snake
                 || entry->playerType == outfit::kPlayerType_Avatar)
                {
                    info->playerCamoType = 0x00;
                }
                info->playerPartsType   = spoofTarget;


                __try
                {
                    void* impl = GetBlockControllerImpl(self);
                    if (impl)
                    {
                        shellTypeInfoPtr =
                            *reinterpret_cast<std::uint8_t**>(
                                reinterpret_cast<std::uint8_t*>(impl)
                                + playerIndex * 8 + 0x1100);
                    }
                    if (shellTypeInfoPtr)
                    {
                        std::uint64_t preSlots[30] = {};
                        std::uint8_t* preBase = shellTypeInfoPtr - 0xF0;
                        for (int i = 0; i < 30; ++i)
                        {
                            preSlots[i] = *reinterpret_cast<std::uint64_t*>(
                                preBase + (i * 8));
                        }
                        Log("[OutfitRuntimeParts] BlockShell PRE-spoof "
                            "(prev equip's leftover state):\n");
                        for (int i = 0; i < 30; i += 4)
                        {
                            Log("  +%02X=0x%016llX  +%02X=0x%016llX  "
                                "+%02X=0x%016llX  +%02X=0x%016llX\n",
                                i * 8,     static_cast<unsigned long long>(preSlots[i]),
                                (i+1) * 8, static_cast<unsigned long long>(preSlots[i+1]),
                                (i+2) * 8, static_cast<unsigned long long>(preSlots[i+2]),
                                (i+3) * 8, static_cast<unsigned long long>(preSlots[i+3]));
                        }

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
                    "0x%02X -> 0x%02X (PT=%u armType=0x%02X camo=0x%02X "
                    "soldierFace=%d enableHead=%d enableArm=%d entryArmType=0x%02X, "
                    "shellPre=0x%02X -> 0xFE [%s]) — calling orig...\n",
                    static_cast<unsigned>(origPartsType),
                    static_cast<unsigned>(info->playerPartsType),
                    static_cast<unsigned>(info->playerType),
                    static_cast<unsigned>(info->playerArmType),
                    static_cast<unsigned>(info->playerCamoType),
                    static_cast<int>(info->playerFaceId),
                    entry->IsHeadEnabled() ? 1 : 0,
                    entry->IsArmEnabled() ? 1 : 0,
                    static_cast<unsigned>(entry->armType),
                    static_cast<unsigned>(prevShellPartsType),
                    shellSentinelWritten ? "clobbered" : "shell-ptr-null");
            }

            if (spoofPartsType) tl_InsideLoadPartsNewSpoof = true;
            g_OrigLoadPartsNew(self, playerIndex, info, flags);
            if (spoofPartsType) tl_InsideLoadPartsNewSpoof = false;


            if (spoofPartsType)
            {
                constexpr int kSlotCount = 30;
                std::uint64_t slots[kSlotCount] = {};
                std::uint8_t  shellPartsTypePost = 0xFF;
                bool          shellSlotsRead = false;
                __try
                {
                    if (shellTypeInfoPtr)
                    {
                        std::uint8_t* base = shellTypeInfoPtr - 0xF0;
                        for (int i = 0; i < kSlotCount; ++i)
                        {
                            slots[i] = *reinterpret_cast<std::uint64_t*>(
                                base + (i * 8));
                        }
                        shellPartsTypePost = shellTypeInfoPtr[1];
                        shellSlotsRead = true;
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    shellSlotsRead = false;
                }

                Log("[OutfitRuntimeParts] hkLoadPartsNew: orig returned "
                    "(partsType=0x%02X[real] PT=%u armType=0x%02X camo=0x%02X) "
                    "— restoring spoof state\n",
                    static_cast<unsigned>(origPartsType),
                    static_cast<unsigned>(info->playerType),
                    static_cast<unsigned>(info->playerArmType),
                    static_cast<unsigned>(info->playerCamoType));

                if (shellSlotsRead)
                {
                    Log("[OutfitRuntimeParts] BlockShell post-orig (developId=%u "
                        "PT=%u regParts=0x%016llX regFpk=0x%016llX shell.partsType=0x%02X):\n",
                        static_cast<unsigned>(entry ? entry->developId : 0u),
                        static_cast<unsigned>(entry ? entry->playerType : 0xFFu),
                        static_cast<unsigned long long>(entry ? entry->partsPathCode64 : 0u),
                        static_cast<unsigned long long>(entry ? entry->fpkPathCode64 : 0u),
                        static_cast<unsigned>(shellPartsTypePost));
                    for (int i = 0; i < kSlotCount; i += 4)
                    {
                        Log("  +%02X=0x%016llX  +%02X=0x%016llX  "
                            "+%02X=0x%016llX  +%02X=0x%016llX\n",
                            i * 8,     static_cast<unsigned long long>(slots[i]),
                            (i+1) * 8, static_cast<unsigned long long>(slots[i+1]),
                            (i+2) * 8, static_cast<unsigned long long>(slots[i+2]),
                            (i+3) * 8, static_cast<unsigned long long>(slots[i+3]));
                    }
                }
                else
                {
                    Log("[OutfitRuntimeParts] BlockShell post-orig slots: "
                        "(unable to read — shellTypeInfoPtr null or SEH)\n");
                }


                if (entry && shellTypeInfoPtr
                 && entry->armFpk != outfit::kSubAssetUseVanilla)
                {
                    __try
                    {
                        std::uint8_t* armSlot = shellTypeInfoPtr - 0xF0 + 0x08;
                        if (entry->armFpk == outfit::kSubAssetDisabled)
                        {
                            WriteFoxPath(reinterpret_cast<std::uint64_t*>(armSlot), 0);
                            Log("[OutfitRuntimeParts] hkLoadPartsNew: "
                                "armFpk=disabled override -> wrote 0 to "
                                "BlockShell+0x08 (developId=%u)\n",
                                static_cast<unsigned>(entry->developId));
                        }
                        else
                        {
                            WriteFoxPath(reinterpret_cast<std::uint64_t*>(armSlot),
                                         entry->armFpk);
                            Log("[OutfitRuntimeParts] hkLoadPartsNew: "
                                "armFpk=0x%016llX override -> wrote to "
                                "BlockShell+0x08 (developId=%u)\n",
                                static_cast<unsigned long long>(entry->armFpk),
                                static_cast<unsigned>(entry->developId));
                        }
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        Log("[OutfitRuntimeParts] hkLoadPartsNew: SEH "
                            "writing armFpk override to BlockShell+0x08\n");
                    }
                }

                // 2026-05-01: Manual InitLoadPlayerPartsParts call REMOVED.
                //
                // Decomp investigation revealed the asset-load pipeline is a
                // multi-frame state machine at mgsvtpp.exe.c:1322940+:
                //   State 0: calls Player2BlockController::LoadPartsNew
                //            (sets per-slot state to 5, arms FPK paths via
                //            ResourceTable::Get*FpkPath, BlockShell+0x68 set
                //            to 0x50 — verified in our post-orig dump)
                //   State 1: polls IsPartsBlockActiveNew(slot) until per-slot
                //            state advances 5 → 3 (BlockGroupOperator's Update
                //            drives this over multiple frames)
                //   State 1: once active, calls Player2Impl::SetUpParts(slot,
                //            PT, partsType) which internally calls
                //            InitLoadPlayerPartsParts to resolve the body
                //            parts path, then registers it via vtable[0x4f8].
                //
                // Our LoadPlayerPartsParts hook is permanently installed and
                // will intercept the natural call from SetUpParts on a later
                // frame (no [via spoof] tag — uses real partsType=0x40 from
                // the suit info bytes that ItemSelector wrote pre-SetSuit).
                //
                // Manual invocation here was racing the state machine:
                //   - LoadPartsNew armed FPK state=5
                //   - Manual InitLoadPlayerPartsParts queued body path on
                //     a block that wasn't ready (state still 5, not 3)
                //   - Body never registered because vtable[0x4f8] never ran
                //     (it's the orchestrator's job, not ours)

                // 2026-05-01: DO NOT restore info->playerPartsType or
                // shellTypeInfoPtr[1] to origPartsType (0x40).
                //
                // Diagnostic confirmed InitLoadPlayerPartsParts returns a
                // non-NULL DataSetFile2* for the user's parts hash — meaning
                // the file IS in the block. But body still invisible.
                // The remaining failure point is vtable[0x4f8] which appears
                // to internally read playerPartsType from the cached state
                // (Player2Impl's tracking) and bail for partsType >= 0x1c.
                //
                // The orchestrator state machine reads partsType from the
                // shell's TypeInfo[1] OR from the suit info bytes to call
                // SetUpParts(slot, PT, partsType). If we leave the spoof
                // (partsType=0x00) in place, the orchestrator passes 0x00
                // to SetUpParts. vtable[0x4f8] sees 0x00 (vanilla NORMAL),
                // doesn't bail. Body binds with the user's parts file
                // (from the user's FPK at the vanilla NORMAL hash).
                //
                // Restoring origPartsType=0x40 here was undoing the spoof
                // for downstream consumers and causing vtable[0x4f8] to
                // bail with -1.
                //
                // Trade-off: other game systems that read TypeInfo[1] now
                // see 0x00 instead of 0x40. Our outfit registry's separate
                // live-state (Quark[0x98][0x10][0xFB] via WriteLivePlayerOutfit)
                // still records 0x40, so the framework's UI/equip filters
                // continue to know the player has the custom outfit equipped.
                //
                // Just restore camo and clear thread-local — leave partsType
                // as the spoof (0x00):
                info->playerCamoType    = origCamoType;
                tl_SpoofedRealPartsType = 0;
                // info->playerPartsType stays at spoofTarget (0x00)
                // shellTypeInfoPtr[1] stays at spoofTarget (0x00)
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
        void* tSnakeFace = ResolveGameAddress(gAddr.LoadPlayerSnakeFaceFpk);

        g_LoadPlayerBionicArmFpk =
            reinterpret_cast<LoadPlayerBionicArmFpk_t>(
                ResolveGameAddress(gAddr.LoadPlayerBionicArmFpk));

        g_InitLoadPlayerPartsParts =
            reinterpret_cast<InitLoadPlayerPartsParts_t>(
                ResolveGameAddress(gAddr.InitLoadPlayerPartsParts));

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
        if (tSnakeFace)
            g_InstalledSnakeFace = CreateAndEnableHook(
                tSnakeFace, reinterpret_cast<void*>(&hkLoadPlayerSnakeFaceFpk),
                reinterpret_cast<void**>(&g_OrigLoadSnakeFaceFpk));


        // 2026-05-01: diagnostic hook on InitLoadPlayerPartsParts to verify
        // whether the .parts file is actually present in the block.
        if (g_InitLoadPlayerPartsParts)
        {
            g_InstalledInitLoadParts = CreateAndEnableHook(
                reinterpret_cast<void*>(g_InitLoadPlayerPartsParts),
                reinterpret_cast<void*>(&hkInitLoadPlayerPartsParts),
                reinterpret_cast<void**>(&g_OrigInitLoadPlayerPartsParts));
        }


        // 2026-05-01: diagnostic hook on Player2Impl::SetUpParts to capture
        // the bool return value — definitively shows if vtable[0x4f8] bails.
        void* tSetUpParts = ResolveGameAddress(gAddr.Player2Impl_SetUpParts);
        if (tSetUpParts)
        {
            g_InstalledSetUpParts = CreateAndEnableHook(
                tSetUpParts,
                reinterpret_cast<void*>(&hkSetUpParts),
                reinterpret_cast<void**>(&g_OrigSetUpParts));
        }


        // 2026-05-01: hkUpdatePartsStatus DISABLED.
        //
        // The patch (substituting per-slot partsType byte 0x40 → 0x00 for the
        // duration of orchestrator's run) BROKE both Snake and Female outfits.
        // Why: when orchestrator passes partsType=0x00 to LoadPartsNew, our
        // hook's partial-custom recovery rewrites info → 0x40 → spoofs to 0x00
        // → orig runs identically as before. BUT then SetUpParts is called with
        // partsType=0x00 (orchestrator reads patched array) → InitLoadPlayerPartsParts
        // → LoadPlayerPartsParts(0, 0x00) → falls through to orig → orig reads
        // the static parts hash table at index [(playerType*0x1C + partsType)*2]
        // = vanilla Snake NORMAL hash 0xE0AA66F9F8CDED9B. Same hash as user's path
        // BUT GetFileForPathId still returns NULL for partsType=0x00 — likely
        // because fox::Block's GetFileForPathId is not pure hash lookup, it may
        // also key on the partsType context internally (block uses partsType as
        // slot index for file storage).
        //
        // Body bind FAILS → state stuck at 1 → infinite loading → also broke
        // Female outfits which DID work before this patch.
        //
        // Hook code retained above for future iteration; install disabled.
        // void* tUpdateParts = ResolveGameAddress(gAddr.UpdatePartsStatus);
        // if (tUpdateParts) { g_InstalledUpdateParts = CreateAndEnableHook(...) }

        Log("[OutfitRuntimeParts] installed: parts=%s fpk=%s camo=%s diamond=%s "
            "lpn=%s doesNeedFace=%s snakeFace=%s bionicArmFn=%s "
            "initLoadPartsFn=%s initLoadHook=%s setUpPartsHook=%s "
            "updatePartsHook=%s\n",
            g_InstalledParts        ? "OK" : "skip",
            g_InstalledFpk          ? "OK" : "skip",
            g_InstalledCamo         ? "OK" : "skip",
            g_InstalledDiamond      ? "OK" : "skip",
            g_InstalledLpn          ? "OK" : "skip",
            g_InstalledDoesNeedFace ? "OK" : "skip",
            g_InstalledSnakeFace    ? "OK" : "skip",
            g_LoadPlayerBionicArmFpk ? "OK" : "unresolved",
            g_InitLoadPlayerPartsParts ? "OK" : "unresolved",
            g_InstalledInitLoadParts ? "OK" : "skip",
            g_InstalledSetUpParts ? "OK" : "skip",
            g_InstalledUpdateParts ? "OK" : "skip");

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
        info.playerArmType      = g_LastInfoCaptured ? g_LastInfoArmType     : std::uint8_t{0};
        info.playerFaceId       = g_LastInfoCaptured ? g_LastInfoFaceId      : std::int16_t{0};
        info.playerFaceEquipId  = g_LastInfoCaptured ? g_LastInfoFaceEquipId : std::uint16_t{0};
        info.playerFaceEquipUnk = g_LastInfoCaptured ? g_LastInfoFaceUnk     : std::uint8_t{0};


        constexpr std::uint32_t kFlagsP0 = 0x15F640;
        constexpr std::uint32_t kFlagsP1 = 0x15F600;


        const bool quarkOk =
            outfit::WriteLivePlayerOutfit(partsType, selectorCode, playerType);


        const outfit::OutfitEntry* entry = nullptr;
        const bool spoofPartsType =
            outfit::TryGetOutfitByPartsType(partsType, &entry)
         && entry;
        const std::uint8_t origPartsType = info.playerPartsType;
        const std::uint8_t origCamoTypeFR = info.playerCamoType;


        std::uint8_t* shellTypeInfoPtr0 = nullptr;
        std::uint8_t* shellTypeInfoPtr1 = nullptr;
        std::uint8_t  prevShellPartsType0 = 0;
        std::uint8_t  prevShellPartsType1 = 0;

        if (spoofPartsType)
        {
            tl_SpoofedRealPartsType = origPartsType;

            if (entry->armType != 0xFF)
            {
                info.playerArmType = entry->armType;
            }
            else if (!entry->IsArmEnabled())
            {
                info.playerArmType = 0;
            }
            else if (info.playerArmType == 0)
            {
                info.playerArmType = 1;
            }


            // 2026-05-01: spoofTargetFR changed 0x07 → 0x00 for Snake/Avatar
            // (matches hkLoadPartsNew's fix — see comment there for rationale).
            std::uint8_t spoofTargetFR = 0x00;
            if (entry->playerType == outfit::kPlayerType_Snake
             || entry->playerType == outfit::kPlayerType_Avatar)
            {
                info.playerCamoType = 0x00;
            }
            info.playerPartsType    = spoofTargetFR;

            __try
            {
                void* impl = GetBlockControllerImpl(g_CapturedBlockController);
                if (impl)
                {
                    shellTypeInfoPtr0 =
                        *reinterpret_cast<std::uint8_t**>(
                            reinterpret_cast<std::uint8_t*>(impl)
                            + 0u * 8 + 0x1100);
                    if (shellTypeInfoPtr0)
                    {
                        prevShellPartsType0 = shellTypeInfoPtr0[1];
                        shellTypeInfoPtr0[1] = 0xFE;
                    }

                    shellTypeInfoPtr1 =
                        *reinterpret_cast<std::uint8_t**>(
                            reinterpret_cast<std::uint8_t*>(impl)
                            + 1u * 8 + 0x1100);
                    if (shellTypeInfoPtr1)
                    {
                        prevShellPartsType1 = shellTypeInfoPtr1[1];
                        shellTypeInfoPtr1[1] = 0xFE;
                    }
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
            // 2026-05-01: same fix as hkLoadPartsNew — don't restore
            // info.playerPartsType / shellTypeInfoPtr[1] to origPartsType.
            // Leave them at the spoof value (0x00) so the orchestrator's
            // SetUpParts → vtable[0x4f8] sees 0x00 and doesn't bail.
            tl_SpoofedRealPartsType = 0;
            // info.playerPartsType, shellTypeInfoPtr0[1], shellTypeInfoPtr1[1]
            // stay at spoofTargetFR (0x00 for Snake/Avatar)
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
        if (g_InstalledLpn)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.Player2BlockController_LoadPartsNew));
        if (g_InstalledDoesNeedFace)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.ResourceTable_DoesNeedFaceFova));
        if (g_InstalledSnakeFace)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.LoadPlayerSnakeFaceFpk));
        if (g_InstalledInitLoadParts)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.InitLoadPlayerPartsParts));
        if (g_InstalledSetUpParts)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.Player2Impl_SetUpParts));
        if (g_InstalledUpdateParts)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.UpdatePartsStatus));

        g_OrigLoadPartsParts   = nullptr;
        g_OrigLoadPartsFpk     = nullptr;
        g_OrigLoadCamoFpk      = nullptr;
        g_OrigLoadDiamondFpk   = nullptr;
        g_OrigLoadPartsNew     = nullptr;
        g_OrigDoesNeedFaceFova = nullptr;
        g_OrigLoadSnakeFaceFpk = nullptr;
        g_LoadPlayerBionicArmFpk = nullptr;
        g_InitLoadPlayerPartsParts = nullptr;
        g_OrigInitLoadPlayerPartsParts = nullptr;
        g_OrigSetUpParts       = nullptr;
        g_OrigUpdatePartsStatus = nullptr;
        g_FoxPath_Path         = nullptr;

        g_InstalledParts        = false;
        g_InstalledFpk          = false;
        g_InstalledCamo         = false;
        g_InstalledDiamond      = false;
        g_InstalledLpn          = false;
        g_InstalledDoesNeedFace = false;
        g_InstalledSnakeFace    = false;
        g_InstalledInitLoadParts = false;
        g_InstalledSetUpParts   = false;
        g_InstalledUpdateParts  = false;

        g_CapturedBlockController = nullptr;

        Log("[OutfitRuntimeParts] removed\n");
    }
}
