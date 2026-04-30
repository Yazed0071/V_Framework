#include "pch.h"

#include "OutfitCamoBonus.h"
#include "OutfitRegistry.h"

#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

// ===========================================================================
// CamouflageControllerImpl::ExecSuitCorrect hook (retail 0x140FDC5D0).
//
// What the orig does (verified from retail prologue at
// mgsvtpp.exe_Addresses.txt:12186509-12186613):
//
//   void ExecSuitCorrect(CamouflageControllerImpl* self, Info* info)
//   {
//       uint  slot      = *(u32*)((u8*)self + 0x78);   // player slot index
//       u8*   byteBuf   = *(u8**)((u8*)info + 0x50);   // per-slot camo array
//       u8    cVar13    = byteBuf[slot];   // PlayerCamoType, READ ONCE here
//       ...
//       // for vanilla equipIds 0x1c8..0x1d8 the orig has a switch
//       // that overrides cVar13 (the LOCAL) to a hardcoded
//       // PlayerCamoType (e.g. 0x1d1/0x1d2 → 2 = SQUARE). Custom
//       // equipIds fall through.
//       ...
//       // bonus calc: vtable[0x18](this, cVar13, materialType)
//       // adds the lookup result to a running float bonus accumulator.
//   }
//
// Our hook (SAVE / WRITE / ORIG / RESTORE):
//   1. Read slot index out of self+0x78
//   2. Read the byte buffer pointer out of info+0x50
//   3. Look up the live equipped outfit for this slot
//   4. If outfit is a registered custom with camoBonusType != 0:
//        a. SAVE byteBuf[slot] (the original PlayerCamoType byte)
//        b. WRITE byteBuf[slot] = entry->camoBonusType
//        c. Call ORIG — orig reads byteBuf[slot] into its local
//           cVar13 at function entry, uses that for the bonus
//           lookup throughout the rest of the function
//        d. RESTORE byteBuf[slot] = original
//   5. Otherwise just forward to orig untouched
//
// Why save/restore, not just write: that same byte at Info+0x50[slot]
// is ALSO read by an upstream caller of LoadPartsNew that uses it as
// the `camo` parameter (verified empirically 2026-04-30 when leaving
// the pin in place caused LoadPartsNew to fire with camo == our pin
// value, triggering body asset reloads with wrong-camo assets every
// camo-update tick → visible character flicker / disappear-reappear).
// By restoring after orig completes, the next reader (LoadPartsNew
// caller) sees the original asset-routing value untouched.
//
// Net effect: while wearing a custom outfit with camoBonusType set,
// the engine's surface-bonus calculation indexes the IH-style 117x82
// table at the modder's chosen PlayerCamoType row (e.g. BATTLEDRESS=16
// → row 16) — but the asset-routing byte stays at whatever the iDroid
// camo picker last wrote, so visuals and body loads aren't disturbed.
//
// Edge cases handled:
//   - SEH guards on every dereference; bail to orig on fault
//   - 0xFF or out-of-range camoBonusType is treated as "no override"
//   - If the orig faults mid-execution, the restore in our finally-
//     equivalent block (post-orig) doesn't run — byteBuf[slot] would
//     stay at our pin until the next ExecSuitCorrect call. Acceptable:
//     orig faulting is already a fatal-game-state condition.
// ===========================================================================

namespace
{
    using ExecSuitCorrect_t = void (__fastcall*)(void* self, void* info);
    static ExecSuitCorrect_t g_OrigExecSuitCorrect = nullptr;
    static bool              g_Installed           = false;

    // Cached entry-pointer offset for the per-slot byte buffer inside Info
    // and for the player-slot index inside CamouflageControllerImpl. Pulled
    // straight from the retail prologue disasm; same in named build.
    constexpr std::size_t kInfoCamoBufferPtrOffset    = 0x50;
    constexpr std::size_t kControllerSlotIndexOffset  = 0x78;

    // Diagnostic dedup — first override fire per process gets a chatty
    // log line so we can confirm the hook is wired correctly without
    // flooding the log at 60Hz.
    static std::atomic<bool> g_FirstOverrideLogged{ false };

    // Apply the SAVE+WRITE step inside SEH; on success returns the
    // address of the byte we wrote and writes the pre-image into
    // *outSaved so the caller can restore it after orig returns.
    // Returns nullptr if no override should apply (no live custom,
    // no pin, deref fault, etc.) — caller skips the restore step.
    static std::uint8_t* TryApplyPin_SEH(void* self, void* info,
                                         std::uint8_t* outSaved)
    {
        if (!self || !info || !outSaved) return nullptr;

        // 1) Pull the pin from the live equipped outfit. No SEH needed
        //    here — these are framework-internal lookups against our
        //    own data structures, not orig pointer derefs.
        const std::uint8_t livePT = outfit::ReadLivePartsType();
        if (livePT < outfit::kCustomPartsTypeStart
            || livePT > outfit::kCustomPartsTypeEnd)
            return nullptr;

        const outfit::OutfitEntry* entry = nullptr;
        if (!outfit::TryGetOutfitByPartsType(livePT, &entry) || !entry)
            return nullptr;

        // Skip if no pin is set. 0xFF = unset sentinel; values > 116
        // are out of PlayerCamoType range. Note that 0 (OLIVEDRAB) IS
        // a valid pin and must NOT be skipped.
        const std::uint8_t pin = entry->camoBonusType;
        if (pin == 0xFF || pin > 116)
            return nullptr;

        // 2) SEH-guarded deref of orig structures, then save+write the
        //    byte. Returns the byte address on success so the caller
        //    can restore post-orig.
        std::uint8_t* writtenSlot = nullptr;
        __try
        {
            auto* selfBytes = reinterpret_cast<std::uint8_t*>(self);
            auto* infoBytes = reinterpret_cast<std::uint8_t*>(info);

            const std::uint32_t slotIdx =
                *reinterpret_cast<std::uint32_t*>(
                    selfBytes + kControllerSlotIndexOffset);

            auto* byteBuf =
                *reinterpret_cast<std::uint8_t**>(
                    infoBytes + kInfoCamoBufferPtrOffset);

            if (byteBuf)
            {
                writtenSlot = &byteBuf[slotIdx];
                *outSaved = *writtenSlot;
                *writtenSlot = pin;

                if (!g_FirstOverrideLogged.exchange(true))
                {
                    Log("[OutfitCamoBonus] FIRST OVERRIDE: livePT=0x%02X "
                        "slot=%u byteBuf=%p saved=%u pinned=%u "
                        "(developId=%u flowIndex=%u)\n",
                        static_cast<unsigned>(livePT),
                        static_cast<unsigned>(slotIdx),
                        byteBuf,
                        static_cast<unsigned>(*outSaved),
                        static_cast<unsigned>(pin),
                        static_cast<unsigned>(entry->developId),
                        static_cast<unsigned>(entry->flowIndex));
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (!g_FirstOverrideLogged.exchange(true))
            {
                Log("[OutfitCamoBonus] SEH fault during peek — hook "
                    "no-op'd; check Info+0x%zx / Controller+0x%zx "
                    "offsets for build drift\n",
                    kInfoCamoBufferPtrOffset,
                    kControllerSlotIndexOffset);
            }
            writtenSlot = nullptr;
        }
        return writtenSlot;
    }

    // Restore the pre-image after orig runs. SEH-guarded for symmetry
    // (the orig might in theory have unmapped the page; in practice
    // this never happens but the guard is essentially free).
    static void RestorePin_SEH(std::uint8_t* writtenSlot, std::uint8_t saved)
    {
        if (!writtenSlot) return;
        __try { *writtenSlot = saved; }
        __except (EXCEPTION_EXECUTE_HANDLER) { /* tolerate */ }
    }

    static void __fastcall hkExecSuitCorrect(void* self, void* info)
    {
        // SAVE+WRITE → ORIG → RESTORE.
        //
        // Why save/restore and not just write: that same byte at
        // Info+0x50[slot] is ALSO read by an upstream caller of
        // LoadPartsNew that passes it through as the `camo` parameter
        // (verified empirically 2026-04-30: leaving the pin in place
        // caused LoadPartsNew to fire with camo == our pin every camo-
        // update tick → body asset reload loop → visible character
        // disappear-reappear flicker). The orig's ExecSuitCorrect
        // loads byteBuf[slot] into a local at function entry, so the
        // pin only needs to be live for the duration of the orig call;
        // restoring after avoids poisoning the asset-routing path.
        std::uint8_t saved = 0;
        std::uint8_t* writtenSlot = TryApplyPin_SEH(self, info, &saved);

        if (g_OrigExecSuitCorrect)
            g_OrigExecSuitCorrect(self, info);

        RestorePin_SEH(writtenSlot, saved);
    }
}

namespace outfit
{
    bool Install_OutfitCamoBonus_Hook()
    {
        if (g_Installed) return true;

        void* target = ResolveGameAddress(gAddr.CamouflageController_ExecSuitCorrect);
        if (!target)
        {
            Log("[OutfitCamoBonus] target unresolved; module disabled "
                "(camoBonusType pinning will be inactive)\n");
            return false;
        }

        g_Installed = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkExecSuitCorrect),
            reinterpret_cast<void**>(&g_OrigExecSuitCorrect));

        Log("[OutfitCamoBonus] installed: %s (target=%p)\n",
            g_Installed ? "OK" : "FAIL", target);
        return g_Installed;
    }

    void Uninstall_OutfitCamoBonus_Hook()
    {
        if (!g_Installed) return;
        if (void* t = ResolveGameAddress(gAddr.CamouflageController_ExecSuitCorrect))
            DisableAndRemoveHook(t);
        g_OrigExecSuitCorrect = nullptr;
        g_Installed           = false;
        Log("[OutfitCamoBonus] removed\n");
    }
}
