#include "pch.h"

#include "OutfitPrepDiagnostic.h"

#include <atomic>
#include <cstdint>
#include <intrin.h>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    // Fova2ControllerImpl::ApplyFormVariationWithFile virtual wrapper at
    // mgsvtpp.exe.c:1427752 / address 0x140aed510 (verified by mgsvtpp_
    // Addresses.exe.txt:8866750: MOV [s_instance] / CALL thunk_FUN_146231f10 /
    // JMP [R10+0xb0]). param_2 is the per-slot index, param_3 is either the
    // FormVariationFile2* pointer or the path-hash uint64 depending on the
    // overload, param_4 is the bool flag. Every Fv2 swap funnels through this
    // entry; whoever invokes it during iDroid mission-prep is the prep-time
    // arm-swap caller we're hunting.
    using ApplyFormVariationWithFile_t = void (__fastcall*)(
        void*               self,
        std::uint32_t       slot,
        std::uint64_t       fv2OrHash,
        std::uint8_t        flag);

    // Player2Impl::RegisterFilesForArm at mgsvtpp.exe.c:5892417 / FUN_1462701b0
    // (named EXE Tpp_main_win64.exe.c:2725716). Reads g_armEffectInfos[armType]
    // and registers the per-slot Fv2/mtar files for the arm tier. armType=0
    // means "no arm files registered". Logs every armType actually wired up
    // for the live slot so we can diff vanilla vs custom prep-time behavior.
    using RegisterFilesForArm_t = void (__fastcall*)(
        void*               self,
        std::uint32_t       slot,
        std::uint32_t       armType);

    // player::appearance::LoadPlayerFv2sSubsetUnk at mgsvtpp.exe.c:1312150 /
    // address 0x1409B2B00. Secondary Fv2 path builder that reads partsType /
    // armType / faceId from the BlockShell at +0xf0..+0xf6 (NOT byte_arrays).
    // Calls LoadPlayerBionicArmFv2(out, playerType, partsType, shell[0xf3]).
    // Hypothesis: this is the prep-time arm-refresh path for VANILLA outfits;
    // for CUSTOM outfits the BlockShell never gets re-populated when the user
    // cycles arm tiers (because no Block::Activate fires for arm-only changes
    // after the initial outfit equip), so this function reads stale data and
    // re-loads the same arm Fv2.
    //
    // Log-only hook to confirm/refute. Signature inferred from the call site:
    // (longlong selfOrParent, BlockGroup *blockGroup, uint slotOrFlag).
    using LoadPlayerFv2sSubsetUnk_t = void (__fastcall*)(
        void*               param_1,
        void*               blockGroup,
        std::uint32_t       param_3);


    static ApplyFormVariationWithFile_t g_OrigApplyFormVariationWithFile = nullptr;
    static RegisterFilesForArm_t        g_OrigRegisterFilesForArm        = nullptr;
    static LoadPlayerFv2sSubsetUnk_t    g_OrigLoadPlayerFv2sSubsetUnk    = nullptr;

    static bool g_InstalledApplyFvF        = false;
    static bool g_InstalledRegisterArm     = false;
    static bool g_InstalledFv2sSubsetUnk   = false;


    // Per-hook call counter so we can correlate "fired N times during prep"
    // against the user's reproduction steps. Atomic so concurrent firings
    // (which shouldn't happen for these per-tick functions, but defensively)
    // don't lose increments.
    static std::atomic<std::uint64_t> g_ApplyFvFCallCount{0};
    static std::atomic<std::uint64_t> g_RegisterArmCallCount{0};
    static std::atomic<std::uint64_t> g_Fv2sSubsetUnkCallCount{0};


    static void __fastcall hkApplyFormVariationWithFile(
        void* self,
        std::uint32_t slot,
        std::uint64_t fv2OrHash,
        std::uint8_t flag)
    {
        const std::uint64_t n = g_ApplyFvFCallCount.fetch_add(1, std::memory_order_relaxed) + 1;

        // Caller return address — narrows down which engine system invoked
        // this Fv2 swap (input handler vs per-tick poller vs Block::Activate).
        // Different return addresses for vanilla vs custom in the same prep
        // session would prove the dispatcher takes different branches based
        // on a partsType filter upstream.
        void* const ret = _ReturnAddress();

        Log("[PrepDiag:ApplyFvF] #%llu self=%p slot=%u fv2/hash=0x%016llX flag=%u ret=%p\n",
            static_cast<unsigned long long>(n),
            self,
            slot,
            static_cast<unsigned long long>(fv2OrHash),
            static_cast<unsigned>(flag),
            ret);

        if (g_OrigApplyFormVariationWithFile)
            g_OrigApplyFormVariationWithFile(self, slot, fv2OrHash, flag);
    }


    static void __fastcall hkRegisterFilesForArm(
        void* self,
        std::uint32_t slot,
        std::uint32_t armType)
    {
        const std::uint64_t n = g_RegisterArmCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
        void* const ret = _ReturnAddress();

        Log("[PrepDiag:RegArm] #%llu self=%p slot=%u armType=%u ret=%p\n",
            static_cast<unsigned long long>(n),
            self,
            slot,
            armType,
            ret);

        if (g_OrigRegisterFilesForArm)
            g_OrigRegisterFilesForArm(self, slot, armType);
    }


    static void __fastcall hkLoadPlayerFv2sSubsetUnk(
        void* param_1,
        void* blockGroup,
        std::uint32_t param_3)
    {
        // CRITICAL: this function is called per-block in a tight per-frame
        // loop (observed 8000+ calls per second from a single caller during
        // game startup, all with identical params). Logging every call
        // crashes the game via log-write I/O blocking. State-change-gate
        // the log so it only fires when the (caller, params) tuple
        // changes — which is what we actually want for diagnostic purposes
        // anyway (we want to know when prep-cycling makes new-shape calls,
        // not see the per-frame block iteration spam).
        static thread_local void*         s_lastParam1   = nullptr;
        static thread_local void*         s_lastBlock    = nullptr;
        static thread_local std::uint32_t s_lastParam3   = 0xFFFFFFFFu;
        static thread_local void*         s_lastRet      = nullptr;
        static thread_local std::uint64_t s_runLength    = 0;

        void* const ret = _ReturnAddress();
        const std::uint64_t total =
            g_Fv2sSubsetUnkCallCount.fetch_add(1, std::memory_order_relaxed) + 1;

        const bool changed =
              s_lastParam1 != param_1
           || s_lastBlock  != blockGroup
           || s_lastParam3 != param_3
           || s_lastRet    != ret;

        if (changed)
        {
            // Log the previous run-length (helps confirm steady-state spam vs
            // signal), then start a new run.
            if (s_runLength > 0)
            {
                Log("[PrepDiag:Fv2sSubsetUnk] (prev tuple repeated %llu more times before this change)\n",
                    static_cast<unsigned long long>(s_runLength));
            }

            Log("[PrepDiag:Fv2sSubsetUnk] #%llu (NEW TUPLE) param_1=%p blockGroup=%p param_3=%u ret=%p\n",
                static_cast<unsigned long long>(total),
                param_1,
                blockGroup,
                param_3,
                ret);

            s_lastParam1 = param_1;
            s_lastBlock  = blockGroup;
            s_lastParam3 = param_3;
            s_lastRet    = ret;
            s_runLength  = 0;
        }
        else
        {
            ++s_runLength;
        }

        if (g_OrigLoadPlayerFv2sSubsetUnk)
            g_OrigLoadPlayerFv2sSubsetUnk(param_1, blockGroup, param_3);
    }
}

namespace outfit
{
    bool Install_OutfitPrepDiagnostic_Hooks()
    {
        void* tApplyFvF = ResolveGameAddress(gAddr.Diag_ApplyFormVariationWithFile);
        void* tRegArm   = ResolveGameAddress(gAddr.Diag_RegisterFilesForArm);
        void* tFv2sSub  = ResolveGameAddress(gAddr.Diag_LoadPlayerFv2sSubsetUnk);

        if (tApplyFvF)
            g_InstalledApplyFvF = CreateAndEnableHook(
                tApplyFvF,
                reinterpret_cast<void*>(&hkApplyFormVariationWithFile),
                reinterpret_cast<void**>(&g_OrigApplyFormVariationWithFile));

        if (tRegArm)
            g_InstalledRegisterArm = CreateAndEnableHook(
                tRegArm,
                reinterpret_cast<void*>(&hkRegisterFilesForArm),
                reinterpret_cast<void**>(&g_OrigRegisterFilesForArm));

        if (tFv2sSub)
            g_InstalledFv2sSubsetUnk = CreateAndEnableHook(
                tFv2sSub,
                reinterpret_cast<void*>(&hkLoadPlayerFv2sSubsetUnk),
                reinterpret_cast<void**>(&g_OrigLoadPlayerFv2sSubsetUnk));

        Log("[OutfitPrepDiagnostic] installed: applyFormVariationWithFile=%s "
            "registerFilesForArm=%s loadPlayerFv2sSubsetUnk=%s\n",
            g_InstalledApplyFvF      ? "OK" : "skip",
            g_InstalledRegisterArm   ? "OK" : "skip",
            g_InstalledFv2sSubsetUnk ? "OK" : "skip");

        return g_InstalledApplyFvF || g_InstalledRegisterArm || g_InstalledFv2sSubsetUnk;
    }

    void Uninstall_OutfitPrepDiagnostic_Hooks()
    {
        if (g_InstalledApplyFvF)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.Diag_ApplyFormVariationWithFile));
        if (g_InstalledRegisterArm)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.Diag_RegisterFilesForArm));
        if (g_InstalledFv2sSubsetUnk)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.Diag_LoadPlayerFv2sSubsetUnk));

        g_OrigApplyFormVariationWithFile = nullptr;
        g_OrigRegisterFilesForArm        = nullptr;
        g_OrigLoadPlayerFv2sSubsetUnk    = nullptr;
        g_InstalledApplyFvF              = false;
        g_InstalledRegisterArm           = false;
        g_InstalledFv2sSubsetUnk         = false;

        Log("[OutfitPrepDiagnostic] removed\n");
    }
}
