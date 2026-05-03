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

    // Player2GameObjectImpl::ProcessSignal at mgsvtpp.exe.c:1322204, address
    // 0x1409C5D00. Big per-player signal dispatcher with a switch over signal
    // IDs. ONE specific signal — 0x8483a342fa61 — runs the Fv2 attachment
    // refresh:
    //   if (uVar33 < 0x1c) {                          // mgsvtpp.exe.c:1322328
    //       Fv2Info::Fv2Info(&local_188);
    //       InitLoadPlayerPartsParts(...);
    //       InitLoadPlayerFv2s(...);                  // wires Fv2 into scene
    //       signalPtr[1..6] = ... ;                   // populates output struct
    //       return;
    //   }
    //   goto LAB_1409c60a6;                           // failure path
    // For custom partsType (≥ 0x1C) the gate fails, the Fv2 attach never
    // happens, the renderer never gets the Fv2 wired into the visible scene,
    // and the arm/face assets we loaded via the leaf hooks just sit in memory
    // unused. We hook ProcessSignal entry, detect signal 0x8483a342fa61 with a
    // custom partsType in the slot, temporarily spoof the partsType byte at
    // *(this+0x80)+0x48+slot to 0x01 (vanilla Snake STANDARD — passes the
    // gate), set the framework's TLS so the leaf hooks called from inside
    // InitLoadPlayerPartsParts/Fv2s recover the real custom partsType, call
    // orig, then restore the byte. The byte is restored before this hook
    // returns so UpdatePartsStatus's state-changed cycling never sees the
    // spoofed value.
    //
    // KEY EXE FINDING: every sender of 0x8483a342fa61 in the binary lives in
    // the demo/cutscene namespace (tpp::gk::DemoCallback at
    // mgsvtpp.exe.c:859262 / 859631 / 5250030, tpp::gk::demo::GameObjectSendString
    // at 8381875). NONE of the senders fire during normal mission preparation
    // outfit-change flow. So while this signal IS the Fv2 refresh trigger
    // during cutscenes, the in-mission-prep Fv2 attach goes through a
    // SEPARATE non-signal path: Block::Activate callbacks dispatched via
    // ExecBlockControllerCallbacks (mgsvtpp.exe.c:5881811), one of which
    // ultimately calls Fova2ControllerImpl::Realize (sets bit 0x2 of the
    // per-slot field at +0x152) and FUN_146877140 / FUN_140aeca60 (the
    // actual Fv2 wire-up that sets bit 0x1 of +0x152). Both bits must be set
    // for Fova2ControllerImpl::PostUpdate (mgsvtpp.exe.c:1427853) to run the
    // mesh-visibility processing per-tick. Since the leaf hooks for arm Fv2/
    // Fpk are called UNCONDITIONALLY from LoadPlayerFv2s and
    // LoadPlayerFv2sSubsetUnk (no partsType gate at the parent), if the
    // user's diagnostic logs show our [BionicArmFv2] entry firing, the leaf
    // is being requested correctly and the issue is downstream of the leaf
    // (Block resolution, Fv2 file lookup, or mesh attach). If the diagnostic
    // logs don't fire, the parent function isn't being invoked — pointing
    // upstream to the Block activation pipeline.
    using ProcessSignal_t = void (__fastcall*)(void* p1, void* p2, std::uint32_t slot, std::uint64_t* signalPtr);

    constexpr std::uint64_t kSignalRefreshFv2s = 0x8483a342fa61ull;
    constexpr std::size_t kP2GO_OffPerPlayerStruct = 0x80;
    // Player2GameObjectImpl + 0xb0 = pointer to per-slot state-machine byte
    // array. Each byte is the cVar9 state (0=needs-LoadPartsNew, 1=loading,
    // 2=loaded steady, 3=unload pending, 4=waiting empty). Used by
    // UpdatePartsStatus's `cVar9 = *(char *)(uVar27 + lVar23)` at named EXE
    // line 2728697 (mgsvtpp.exe.c:1324659). Forcing state→3 here is what
    // wakes the state==3 ClearParts + UnloadPartsNew → state==4 → state==0
    // → LoadPartsNew cascade for arm-only changes that would otherwise stay
    // in steady-state==2 forever for custom partsType (the gate at
    // mgsvtpp:1325085 only runs when state ∉ {1,2}, so custom never trips
    // it and never reloads).
    constexpr std::size_t kP2GO_OffStateMachinePtr = 0xb0;
    constexpr std::size_t kPP_OffPlayerTypeArr = 0x40;
    constexpr std::size_t kPP_OffPartsTypeArr = 0x48;
    constexpr std::size_t kPP_OffArmTypeArr   = 0x58;
    // perPlayer + 0x180: 32-bit "state-changed/needHead" bitfield. Bit S set
    // means slot S needs a re-evaluation pass. State==3 branch reads
    // `needHead = (*(this+0x180) & uVar13) != 0` to gate the
    // ClearParts+UnloadPartsNew transition (named EXE line 2728693
    // / mgsvtpp.exe.c:1324642).
    constexpr std::size_t kPP_OffStateChangedBits = 0x180;
    // perPlayer + 0x184: 32-bit alternate-trigger bitfield (bVar31 source).
    // Set alongside +0x180 so the state==3 branch's `bVar31 || needHead`
    // disjunction fires regardless of which half of the engine's state-track
    // logic clears first.
    constexpr std::size_t kPP_OffAltStateBits = 0x184;
    constexpr std::size_t kPP_OffLoadoutReq   = 0xc0;
    constexpr std::size_t kPP_LoadoutReqStride = 0x3a;
    constexpr std::size_t kPP_LoadoutReqEquipHashOff = 0x8;
    constexpr std::uint8_t kProcessSignalSpoofPartsType = 0x01;
    // State machine value to force when arm tier changes for a custom slot.
    // The state==3 branch in UpdatePartsStatus reads `bVar31 || needHead`
    // (both bits set by us above) and runs Player2Impl::ClearParts +
    // Player2BlockController::UnloadPartsNew, then transitions to 4. State
    // 4 polls IsPartsBlockEmptyNew, transitions to 0 when empty. State 0
    // calls LoadPartsNew with the latest LoadoutRequest values. Our
    // hkLoadPartsNew handles the partsType + armType for custom outfits
    // (restoring armType from cache when it arrives as 0).
    constexpr std::uint8_t kForceCascadeState = 3;

    // Mirror of the engine's hardcoded equipHash → armTier translation, used
    // by every place in the binary that gates arm behavior on the partsType
    // whitelist (UpdatePartsStatus@1325105, EquipControllerImpl::
    // InitializePlayerAtIndex@2262691, SynchronizerImpl::GetSpawnCondition
    // @2397283, UnrealUpdaterImpl::ReceiveSyncState@2415985). The equipHash
    // values 0x203..0x209 correspond to the player's developed arm tier in
    // ascending order: 0x203=Tier 2, 0x204=Tier 3, 0x205=Tier 4, 0x206=Tier 5,
    // 0x208=Tier 6 (Hand of Jehuty), 0x209=Tier 7 (HoJ Camo). Anything else
    // (including 0x207 which is HoJ-base or 0 for unequipped) maps to Tier 1
    // (basic prosthetic).
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

    // Read the live equipped-arm hash for the given slot from the per-player
    // LoadoutRequest array at *(p2go+0x80)+0xc0+slot*0x3a+8, and translate it
    // to the engine's arm-tier byte. Returns 0 if the read can't be resolved
    // (caller falls back to its own default).
    //
    // This lets us seed `g_LastInfoArmType` even when the user loads directly
    // into a save with a custom outfit already equipped (no prior natural
    // LoadPartsNew with armType > 0 to sample from). Without this seeding,
    // hkLoadPlayerBionicArmFv2 substitutes handType=1 and the arm renders as
    // basic prosthetic regardless of the user's actual developed tier.
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

    // Player2GameObjectImpl::UpdatePartsStatus at mgsvtpp.exe.c:1324531, address
    // 0x1409CC380. The per-tick state syncer. For each player slot it reads
    // OLD bytes from byte_arrays at pIVar3+0x40..0x68 into locals at function
    // entry, computes NEW values, compares NEW vs OLD, and writes NEW back if
    // anything changed (also setting the reload-flag bit at pIVar3+0x180).
    //
    // Inside the function (mgsvtpp.exe.c:1325087) is THE partsType whitelist
    // that controls the arm-tier byte at byte_arrays+0x58+slot:
    //   if ((playerType == 0 || playerType == 3)) {
    //       switch (playerPartsType) {
    //       case 0..2,7..D,F..12,17..19:                          // vanilla
    //           switch (loadoutRequest[slot].equipHash at +0x8) {
    //               case 0x203: local_1a8 = 2; break;             // Bionic Arm Tier 2
    //               case 0x204: local_1a8 = 3; break;             // Tier 3
    //               case 0x205: local_1a8 = 4; break;             // Tier 4
    //               case 0x206: local_1a8 = 5; break;             // Tier 5
    //               case 0x208: local_1a8 = 6; break;             // Hand of Jehuty
    //               case 0x209: local_1a8 = 7; break;             // HoJ Camo
    //               default:    local_1a8 = 1; break;             // Tier 1 basic
    //           }
    //           break;
    //       default: goto caseD_3;                                 // <-- custom lands here
    //       }
    //   }
    //   else {
    //   caseD_3: uVar26 = 0;                                       // local_1a8 stays 0
    //   }
    //   ...later writes byte_arrays+0x58+slot = local_1a8;
    //
    // The byte at byte_arrays+0x58+slot is the SINGLE SOURCE OF TRUTH every
    // gameplay arm-effect consults — KnockActionPluginImpl::StateKnock at
    // FUN_1411e7c40 (mgsvtpp.exe.c:2293557) reads it directly to choose
    // arm-knock vs body-knock sound; the per-frame arm-equip dispatch
    // (FUN_1412a2f80) and many other systems do likewise. For custom
    // partsType the engine writes 0 → every arm-effect plays the no-arm
    // version regardless of the live arm being equipped and visually
    // rendered.
    //
    // STRAIGHTFORWARD POST-ORIG WRITES CASCADE — DON'T DO IT. A previous
    // attempt wrote byte=cachedTier post-orig. Next tick, orig reads OLD=
    // cachedTier into the local but recomputes NEW=0 (custom falls through
    // the same switch), state-changed triggers (NEW≠OLD), the reload-flag
    // bit at pIVar3+0x180 gets set. On the subsequent tick, the cVar9==3
    // branch sees the bit and runs Player2Impl::ClearParts +
    // UnloadPartsNew → cVar9=4 → cVar9=0 → LoadPartsNew. Combined with the
    // unconditional 0xFE BlockShell clobber in hkLoadPartsNew defeating
    // orig's dedupe at mgsvtpp.exe.c:1312714, custom assets reloaded every
    // tick → "Character does not load."
    //
    // CURRENT APPROACH (hkUpdatePartsStatus below): pre-orig write the byte
    // to 0 for slots that are custom-partsType + enableArm. Orig now reads
    // OLD=0 and computes NEW=0 — they match, no state-change, no reload-flag
    // bit, no cascade. Post-orig, write the byte back to the cached arm tier.
    // The byte is non-zero only OUTSIDE orig's run; since UpdatePartsStatus
    // is single-threaded with all gameplay queries, every consumer sees
    // byte=cachedTier whenever they read it. Vanilla slots are untouched.
    using UpdatePartsStatus_t = void (__fastcall*)(void* self);

    // Player2Impl::SetUpParts at named EXE Tpp_main_win64.exe.c:2727306 (live
    // address 0x1409CA560 per mgsvtpp_Addresses.exe.txt:7909319). Signature:
    //   bool __thiscall SetUpParts(Player2Impl* this, uint slot,
    //                              uint playerType, uint partsType,
    //                              uint camo, uint armType,
    //                              uint faceId, AvatarInfo* avatarInfo);
    //
    // Called from UpdatePartsStatus's cVar13==1 branch (named EXE line
    // 2728965) AFTER LoadPartsNew completes, to actually wire up the arm
    // assets for the slot via RegisterFilesForArm(this, slot, armType).
    // RegisterFilesForArm uses armType to index `g_armEffectInfos[armType]`
    // — armType=0 means "no arm files registered", so the visual stays at
    // whatever was last set (or empty).
    //
    // The armType arg here comes from byte_arrays+0x58+slot read at the
    // start of UpdatePartsStatus. Our hkUpdatePartsStatus zeros that byte
    // pre-orig to suppress the state-changed cascade — so when SetUpParts
    // runs INSIDE orig, it sees armType=0 and registers nothing for the
    // arm slot. That's why the arm visual doesn't update on tier swap
    // even though our tier-change detection correctly fires the asset
    // reload chain.
    //
    // Fix: hook SetUpParts and override armType=0 → g_LastInfoArmType (the
    // cached live tier kept fresh by hkUpdatePartsStatus's ReadLiveArmTier
    // FromLoadoutRequest seeding) when the slot has a custom partsType + a
    // registered outfit with enableArm=true. Vanilla slots and non-arm-
    // enabled custom outfits pass through untouched.
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

    // Per-playerType cache for the live arm tier. Indexed by playerType
    // (0=Snake, 1=DD_M, 2=DD_F, 3=Avatar). MUST be per-playerType, not
    // global — UpdatePartsStatus iterates slots, and slot 0 (Snake) and
    // slot 1 (Avatar) each have their own LoadoutRequest equip hash. A
    // global cache gets clobbered by whichever slot is iterated last,
    // making the OTHER slot's SetUpParts override use the wrong tier.
    // (Observed regression: Snake's HoJ tier 3 visual reverted to tier 1
    // because slot 1's "tier 1 default Avatar arm" iteration overwrote
    // the cache.)
    //
    // For backward compat, accessor helpers wrap the array.
    static constexpr std::size_t kArmTierByPlayerTypeMax = 4;
    static std::uint8_t  g_LastInfoArmType_byPT[kArmTierByPlayerTypeMax]      = {0,0,0,0};
    static bool          g_LastInfoArmCaptured_byPT[kArmTierByPlayerTypeMax]  = {false,false,false,false};

    // Look up the cached arm tier for a given playerType. Returns
    // {tier, captured} pair via out-params. Bounds-checks playerType.
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

        // Diagnostic: state-change-gated log per (playerType, partsType, handType)
        // tuple. Confirms whether the engine is actually requesting the arm Fv2
        // path for the live player. If we never see this fire after a custom-
        // outfit equip, the Fv2 attach pipeline isn't even being invoked — the
        // bug is upstream of the leaf (state machine, signal dispatch, or Block
        // activation never reaches this layer).
        {
            static std::uint32_t s_lastPT = 0xFFFFFFFFu;
            static std::uint32_t s_lastPartsType = 0xFFFFFFFFu;
            static std::uint32_t s_lastEffective = 0xFFFFFFFFu;
            static std::uint32_t s_lastHand = 0xFFFFFFFFu;
            if (s_lastPT != playerType || s_lastPartsType != playerPartsType
                || s_lastEffective != effectivePartsType || s_lastHand != playerHandType)
            {
                Log("[OutfitRuntimeParts:BionicArmFv2] entry: playerType=%u "
                    "partsType=0x%02X (effective=0x%02X) handType=%u\n",
                    playerType,
                    static_cast<unsigned>(playerPartsType & 0xFF),
                    static_cast<unsigned>(effectivePartsType & 0xFF),
                    playerHandType);
                s_lastPT = playerType;
                s_lastPartsType = playerPartsType;
                s_lastEffective = effectivePartsType;
                s_lastHand = playerHandType;
            }
        }

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
                    Log("[OutfitRuntimeParts:BionicArmFv2] partsType=0x%02X "
                        "developId=%u IsArmEnabled=false -> kSubAssetDisabled\n",
                        static_cast<unsigned>(pt),
                        static_cast<unsigned>(entry->developId));
                    return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
                }
                // CRITICAL: handType comes from the live byte arrays at
                // *(Player2GameObjectImpl+0x80)+0x58+slot, which the engine
                // populates as 0 for custom partsType (the engine doesn't
                // track arm tier for outfit slots outside its own whitelist).
                // BionicArmFv2Array[handType*2] uses handType to index — for
                // handType=0 the entry is a NULL FoxPath, which is exactly
                // what produces the invisible-arm symptom even with our
                // partsType substitute. Fall back to the cached real arm
                // tier (captured by hkLoadPartsNew on natural pre-outfit-
                // change calls) when the leaf is invoked with handType=0.
                // If no cache yet (cold-start with custom outfit already
                // equipped from save), use 1 (basic prosthetic) — the
                // user's developed tier may be higher but visually 1 is
                // a non-empty path so the arm renders, and SetActiveVariant/
                // outfit reload will re-cache the real tier later.
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
                std::uint64_t* result = g_OrigLoadBionicArmFv2(
                    outPath, playerType,
                    kBionicArmVanillaPartsTypeSubstitute,
                    effectiveHandType);
                Log("[OutfitRuntimeParts:BionicArmFv2] partsType=0x%02X "
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
        return g_OrigLoadBionicArmFv2(outPath, playerType,
                                      playerPartsType, playerHandType);
    }

    static std::uint64_t* __fastcall hkLoadPlayerBionicArmFpk(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerHandType)
    {
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);

        // Diagnostic: state-change-gated log to confirm Fpk leaf is requested.
        {
            static std::uint32_t s_lastPT = 0xFFFFFFFFu;
            static std::uint32_t s_lastPartsType = 0xFFFFFFFFu;
            static std::uint32_t s_lastEffective = 0xFFFFFFFFu;
            static std::uint32_t s_lastHand = 0xFFFFFFFFu;
            if (s_lastPT != playerType || s_lastPartsType != playerPartsType
                || s_lastEffective != effectivePartsType || s_lastHand != playerHandType)
            {
                Log("[OutfitRuntimeParts:BionicArmFpk] entry: playerType=%u "
                    "partsType=0x%02X (effective=0x%02X) handType=%u\n",
                    playerType,
                    static_cast<unsigned>(playerPartsType & 0xFF),
                    static_cast<unsigned>(effectivePartsType & 0xFF),
                    playerHandType);
                s_lastPT = playerType;
                s_lastPartsType = playerPartsType;
                s_lastEffective = effectivePartsType;
                s_lastHand = playerHandType;
            }
        }

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
                    Log("[OutfitRuntimeParts:BionicArmFpk] partsType=0x%02X "
                        "developId=%u IsArmEnabled=false -> kSubAssetDisabled\n",
                        static_cast<unsigned>(pt),
                        static_cast<unsigned>(entry->developId));
                    return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
                }
                // Mirror the Fv2 leaf's handType=0 fallback: the FPK leaf is
                // called from the same call sites as the Fv2 leaf, with the
                // same handType source. If handType is 0 here we'd load an
                // empty FPK path and the Fv2 file the Fv2 leaf returns won't
                // be findable later.
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

    // ProcessSignal hook — surgical: only intervenes for signal
    // 0x8483a342fa61 (the Fv2 refresh signal) and only when the slot's
    // partsType is in the custom range with a registered outfit. Pass-through
    // for all other signals (and there are dozens — corpse, vehicle, gimmick
    // signals etc.). The spoof window is bounded to a single orig call, with
    // a try/finally pattern via __try/__except to guarantee byte restoration
    // even if orig faults.
    static void __fastcall hkProcessSignal(
        void* param_1,
        void* param_2,
        std::uint32_t slot,
        std::uint64_t* signalPtr)
    {
        if (!g_OrigProcessSignal)
            return;

        // Diagnostic: log the FIRST occurrence of each (slot, signalId) tuple
        // so we can confirm the hook is firing and identify the actual set of
        // signals dispatched in the user's scenario. If signal 0x8483a342fa61
        // never appears here even after a custom-outfit equip, the engine
        // isn't using that signal in their flow — we need to find a different
        // Fv2 refresh trigger.
        if (signalPtr)
        {
            const std::uint64_t sig = *signalPtr;
            // Tiny dedupe table: the per-frame dispatch goes through a small
            // set of well-known signal IDs. Log each unique (slot, sig) pair
            // once; ignore subsequent repeats. Size 32 is plenty.
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

        // Fast-path: only consider intervention if the signal is the one we
        // care about and the params look sane.
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
                            // Set TLS so leaf hooks called from inside
                            // InitLoadPlayerPartsParts and InitLoadPlayerFv2s
                            // can recover the real custom partsType (otherwise
                            // they'd see the spoofed 0x01 and load vanilla
                            // assets instead of the outfit's custom paths).
                            tl_SpoofedRealPartsType = origPartsType;
                            partsTypeArr[slot] = kProcessSignalSpoofPartsType;
                            spoofed = true;

                            // State-change-gated log per (slot, partsType).
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

        // Always call orig, with or without spoof in place.
        g_OrigProcessSignal(param_1, param_2, slot, signalPtr);

        // Restore the byte and clear TLS, even if orig faulted (we won't
        // reach here if it did, but if SEH was caught above we still need
        // to restore).
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

    // Player2GameObjectImpl::UpdatePartsStatus hook. Bypasses the partsType
    // whitelist's zero-write of byte_arrays+0x58+slot for slots wearing a
    // registered custom outfit with enableArm=true. See the long comment at
    // `using UpdatePartsStatus_t = ...` above for the full mechanism. Short
    // version:
    //   - pre-orig: zero byte_arrays+0x58+slot so orig's state-changed compare
    //     sees OLD=0, NEW=0 (whitelist forces NEW=0 for custom partsType) →
    //     no state change → no reload-flag bit set → no cascade
    //   - post-orig: write byte_arrays+0x58+slot = cachedArmTier so every
    //     gameplay arm-effect (StateKnock at FUN_1411e7c40, per-frame arm-equip
    //     dispatch at FUN_1412a2f80, etc.) reads non-zero and treats the
    //     player as having the bionic arm
    //
    // Slot count is bounded by the engine's player2 controller — typically
    // 2 (Snake + buddy) but we iterate up to a small max defensively. We
    // intervene only when the slot's playerType is Snake/Avatar AND the slot's
    // partsType is in the custom range AND a registered outfit exists with
    // enableArm=true. Vanilla DD slots and non-arm-enabled custom outfits are
    // untouched, so vanilla behavior is preserved exactly.
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
            // True on the tick where resolvedTier differs from the previous
            // tick's value. Drives the post-orig force-cascade for custom
            // outfits in iDroid mission-prep, where steady-state==2 would
            // otherwise never reach the partsType gate at mgsvtpp:1325085
            // and the state machine never transitions out of 2 for arm-only
            // changes. Setting state→3 + the +0x180/+0x184 bits forces the
            // cascade to fire next tick.
            bool         tierJustChanged;
        };
        SlotOverride overrides[kMaxSlots] = {};
        std::uint8_t* armTypeArr = nullptr;

        __try
        {
            void* perPlayer = *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(self) + kP2GO_OffPerPlayerStruct);
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

                        // Determine the right arm-tier byte for this slot.
                        // Priority:
                        //   1. Live equip hash translated through the engine's
                        //      own hash→tier map (matches what UpdatePartsStatus
                        //      computes for vanilla partsType). This is the
                        //      "ground truth" — what the engine WOULD write if
                        //      partsType were in its whitelist.
                        //   2. The cached g_LastInfoArmType captured by
                        //      hkLoadPartsNew during a prior natural call.
                        //   3. Fallback to 1 (basic prosthetic) so the slot is
                        //      at least active.
                        // The live equipHash read also seeds the cache so other
                        // framework subsystems (LoadPartsNew restoration,
                        // ForcePartsReload) get the correct tier on cold-start
                        // saves without needing a vanilla→custom round trip.
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
                        // Keep the cache live PER PLAYERTYPE. Each slot in
                        // UpdatePartsStatus may have a different playerType
                        // (typically slot 0 = Snake/0 and slot 1 = Avatar/3
                        // in single-player). Storing into a single global
                        // cache made the second iteration overwrite the
                        // first — when SetUpParts later fired for slot 0,
                        // the cache had been clobbered by slot 1's "default
                        // basic prosthetic" tier 1, so Snake's HoJ tier 3
                        // visual reverted to tier 1. Per-playerType keeps
                        // each character's tier independently.
                        if (liveTier > 0)
                        {
                            SetCachedArmTierForPlayerType(ply, liveTier);
                        }

                        overrides[i].active = true;
                        overrides[i].restoreValue = resolvedTier;

                        // Per-slot persistent "last seen tier". We let orig's
                        // state-changed detection fire EXACTLY ONCE on the tick
                        // when the user actually swaps to a different arm tier,
                        // so the engine reloads the arm asset; on every other
                        // tick we suppress the comparison by pre-zeroing the
                        // byte.
                        //
                        // Mechanism:
                        //  - Steady state (resolvedTier == lastSeen):
                        //      pre-orig zero  -> orig reads OLD=0, NEW=0
                        //      compare passes -> no reload-flag bit -> no
                        //      cVar9 state machine cascade. Post-orig restores
                        //      byte = resolvedTier.
                        //  - Tier-change tick (resolvedTier != lastSeen):
                        //      DON'T pre-orig zero. byte still holds last
                        //      tick's restored value (= old resolvedTier =
                        //      lastSeen). Orig reads OLD=lastSeen, NEW=0.
                        //      0 != lastSeen -> state-changed=true -> orig
                        //      writes byte=0 and sets pIVar3+0x180 bit.
                        //      That bit drives ONE pass of UnloadPartsNew →
                        //      LoadPartsNew (proper arm asset reload). Post-
                        //      orig restores byte = new resolvedTier.
                        //
                        //   After the tier-change tick, lastSeen is updated to
                        //   the new tier. Subsequent ticks see equality →
                        //   steady-state path → cascade stays suppressed.
                        static std::uint8_t s_lastSeenTier[kMaxSlots] =
                            {0xFFu, 0xFFu, 0xFFu, 0xFFu};
                        const bool tierChanged =
                            (s_lastSeenTier[i] != resolvedTier);
                        overrides[i].tierJustChanged = tierChanged;
                        if (!tierChanged)
                        {
                            // Steady state: zero pre-orig to suppress cascade.
                            armTypeArr[i] = 0;
                        }
                        // else: leave armTypeArr[i] alone — it currently holds
                        // the last-tick restored value (old tier), so orig's
                        // state-changed compare detects the mismatch and
                        // schedules ONE clean asset reload.
                        s_lastSeenTier[i] = resolvedTier;

                        // State-change-gated log per slot — log when the
                        // (partsType, tier) tuple for this slot changes from
                        // the prior tick. Keeps the log signal-rich without
                        // every-tick spam.
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

        // Post-orig: write back the cached arm tier for each overridden slot.
        // Orig's state-changed compare already finished against the zeroed
        // value, so writing here doesn't trigger a cascade. The byte stays
        // non-zero until the next UpdatePartsStatus tick (where we zero it
        // again pre-orig). Gameplay queries reading the byte at any other
        // time observe the cached tier.
        //
        // Additionally on tier-change ticks: force the state machine to 3
        // and set the +0x180/+0x184 state-changed bits for custom slots.
        // This is the FIX for iDroid mission-prep arm cycling: the engine's
        // gate (mgsvtpp.exe.c:1325085) that recomputes arm tier from
        // LoadoutRequest equipHash and triggers the state cascade only runs
        // when state ∉ {1, 2}. For vanilla outfits in iDroid prep,
        // SetUpDirtyEffects (or the action-state-bit-8 path) transitions
        // state 2→3 by some side effect, which lets the gate fire on the
        // next tick (state==3 → gate runs → bit set → ClearParts). For
        // custom partsType, none of those side effects fire, so the engine
        // sits in state==2 forever after an arm cycle, and the model never
        // reloads. By setting state→3 + the +0x180/+0x184 bits ourselves,
        // we kick the state==3 branch's `bVar31 || needHead` disjunction
        // true on the very next tick. That triggers Player2Impl::ClearParts
        // + Player2BlockController::UnloadPartsNew → state→4 → IsPartsBlock-
        // EmptyNew → state→0 → Player2BlockController::LoadPartsNew (with
        // the new arm tier from byte_arrays+0x58, which we just restored
        // post-orig to resolvedTier). Our hkLoadPartsNew picks up the
        // armType=0 case (zeroed by orig's gate when it eventually does run
        // in state 0/3/4) and restores from the per-PT cache.
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

        // Force-cascade for custom slots whose arm tier changed this tick.
        // Done OUTSIDE the armTypeArr block so SEH around pointer derefs is
        // independent — even if perPlayer/stateMachine pointers are
        // momentarily NULL at game-load shutdown, we don't lose the byte
        // restore for other slots.
        __try
        {
            void* perPlayer = *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(self) + kP2GO_OffPerPlayerStruct);
            std::uint8_t* stateMachineArr =
                *reinterpret_cast<std::uint8_t**>(
                    reinterpret_cast<std::uint8_t*>(self) + kP2GO_OffStateMachinePtr);

            if (perPlayer && stateMachineArr)
            {
                std::uint32_t* stateChangedBits =
                    reinterpret_cast<std::uint32_t*>(
                        reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffStateChangedBits);
                std::uint32_t* altStateBits =
                    reinterpret_cast<std::uint32_t*>(
                        reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffAltStateBits);

                // FORCE-CASCADE DISABLED 2026-05-04
                //
                // Original idea (kept for reference): on tier-change tick
                // for state==2 or state==3, set state→3 + +0x180/+0x184 bits
                // to trigger ClearParts → UnloadPartsNew → IsPartsBlockEmpty
                // → state→0 → LoadPartsNew, picking up the new arm tier.
                //
                // Problem observed in iDroid mission-prep test:
                // ClearParts + UnloadPartsNew DOES fire (character vanishes),
                // but the cascade never completes the reload — no
                // LoadPartsNew follows. The engine's load infrastructure
                // appears to be gated on gameplay services that are paused
                // during prep mode, so the request gets dropped after
                // unload. Net effect: character permanently invisible.
                //
                // The actual iDroid arm preview must go through a different
                // pipeline — likely TppModelViewerManager or
                // EquipPreviewSystem, NOT Player2Impl::UpdatePartsStatus.
                // This needs further investigation. Until that's traced,
                // keep the rest of the hook (cache, post-orig restore) but
                // DON'T force the cascade — that way custom outfits still
                // load correctly at gameplay-time, and mission-prep arm
                // cycling has no visual effect (matches pre-fix behavior),
                // but the character stays loaded.
                for (std::size_t i = 0; i < kMaxSlots; ++i)
                {
                    if (!overrides[i].active || !overrides[i].tierJustChanged)
                        continue;

                    const std::uint8_t prevState = stateMachineArr[i];
                    Log("[OutfitRuntimeParts:UpdatePartsStatus] slot=%zu "
                        "TIER CHANGE detected, state=%u, restoredTier=%u "
                        "(force-cascade DISABLED — pending iDroid prep "
                        "pipeline investigation; gameplay-time arm changes "
                        "still work via natural state machine flow)\n",
                        i,
                        static_cast<unsigned>(prevState),
                        static_cast<unsigned>(overrides[i].restoreValue));
                }
                (void)stateMachineArr;
                (void)stateChangedBits;
                (void)altStateBits;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitRuntimeParts:UpdatePartsStatus] SEH during post-orig "
                "force-cascade — model may not reflect arm tier change this "
                "tick (will retry next tick if tier difference persists)\n");
        }
    }

    // Player2Impl::SetUpParts hook. Surgical override of the armType arg when
    // it arrives as 0 for a slot wearing a custom partsType + enableArm.
    //
    // Why armType arrives as 0: hkUpdatePartsStatus zeros byte_arrays+0x58+slot
    // pre-orig (cascade prevention), and orig UpdatePartsStatus reads bVar1 =
    // byte_arrays+0x58+slot at function entry — so it captures 0. That bVar1 is
    // then passed verbatim to SetUpParts in the cVar13==1 branch (named EXE
    // line 2728965). Inside SetUpParts, RegisterFilesForArm(this, slot,
    // armType=0) indexes g_armEffectInfos[0] which is null → no arm files
    // registered → arm visual stays empty/old.
    //
    // Fix: when SetUpParts is called and our state shows a custom-outfit slot
    // with enableArm but armType was zeroed by our own hook, substitute the
    // cached live tier (g_LastInfoArmType, kept fresh by hkUpdatePartsStatus's
    // ReadLiveArmTierFromLoadoutRequest seeding) so RegisterFilesForArm
    // registers the right tier's effect files. Vanilla outfits and non-arm
    // custom outfits pass through unchanged.
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

                // State-change-gated log per (slot, partsType, tier) tuple to
                // avoid every-tick spam when SetUpParts re-fires on tier
                // changes.
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


            // Capture arm tier per-playerType — gate on armType > 0 so we only
            // sample valid developed tiers (the engine's commit-blob expansion
            // zeroes armType for new-outfit applies; we only want to remember
            // the values from natural pre-outfit-change calls). Indexed by
            // info->playerType so Snake (0) and Avatar (3) each get their own
            // tier cached; without this split, slot 1's "default tier 1" arm
            // would clobber Snake's actual developed tier.
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
                    // The engine zeroes playerArmType when expanding the new
                    // commit blob into LoadPartsPlayerInfo, which would lose
                    // the player's developed prosthetic upgrade tier on every
                    // outfit swap. Restore from the cached tier captured per-
                    // playerType (Snake's HoJ tier 3 stays separate from
                    // Avatar's tier 1 default — without per-PT split they'd
                    // overwrite each other across slots).
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
        // Arm tier comes from the per-playerType cache (Snake/Avatar each
        // hold their own developed tier independently). Caller-supplied
        // playerType selects which slot of the cache to read.
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
