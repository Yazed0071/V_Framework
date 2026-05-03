#include "pch.h"

#include "OutfitSupplyDropPickup.h"
#include "OutfitRegistry.h"
#include "OutfitRuntimeParts.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{


    using StateHandler1_t = void (__fastcall*)(
        void* work, std::uint32_t row, std::uint32_t subState, void* arg4);

    static StateHandler1_t g_OrigStateHandler1      = nullptr;
    static bool            g_InstalledStateHandler1 = false;


    using Reset_t = void (__fastcall*)(void* self);

    static Reset_t g_OrigReset      = nullptr;
    static bool    g_InstalledReset = false;


    using SettledHandler_t = void (__fastcall*)(void* self);

    static SettledHandler_t g_OrigSettledHandler      = nullptr;
    static bool             g_InstalledSettledHandler = false;


    using OnDropTimerTick_t = void (__fastcall*)(
        void* self, std::uint16_t decrementBy, void* posVec);

    static OnDropTimerTick_t g_OrigOnDropTimerTick      = nullptr;
    static bool              g_InstalledOnDropTimerTick = false;


    using RequestToDropImpl_t = void (__fastcall*)(void* self, void* request);

    static RequestToDropImpl_t g_OrigRequestToDropImpl      = nullptr;
    static bool                g_InstalledRequestToDropImpl = false;


    using RestoreRequestFromSVars_t = void (__fastcall*)(void* self);

    static RestoreRequestFromSVars_t g_OrigRestore       = nullptr;
    static bool                      g_InstalledRestore  = false;


    static bool ConsumeStashAndEquip(const char* tag)
    {
        const std::uint16_t pendingDevId =
            outfit::ConsumePendingSupplyDropDevelopId();


        const std::uint8_t pendingVariantIdx =
            outfit::ConsumePendingSupplyDropVariantIdx();

        if (pendingDevId == 0) return false;

        const outfit::OutfitEntry* entry = nullptr;
        if (!outfit::TryGetOutfitByDevelopId(pendingDevId, &entry) || !entry)
        {
            Log("[OutfitSupplyDropPickup:%s] consumed stash developId=%u "
                "but no matching registered entry; skipping\n",
                tag, static_cast<unsigned>(pendingDevId));
            return true;
        }


        std::uint8_t variantIdx = pendingVariantIdx;
        if (entry->variantCount == 0)
            variantIdx = 0;
        else if (variantIdx >= entry->variantCount)
            variantIdx = 0;


        std::uint8_t variantSelector = entry->selectorCode;
        if (variantIdx > 0 && variantIdx < outfit::kMaxVariantsPerOutfit)
        {
            const std::uint8_t code = entry->variantSelectorCodes[variantIdx];
            if (code != 0xFF) variantSelector = code;
        }


        outfit::SetActiveVariant(entry->partsType, variantIdx);

        // Snake↔Avatar bridging: drive the reload for the LIVE player when
        // known, so a Snake-registered outfit applies to the visible Avatar
        // (and vice versa). Fall back to the registered playerType when the
        // live state isn't reachable.
        const std::uint8_t livePT = outfit::ReadLivePlayerType();
        const std::uint8_t reloadPT =
            (livePT != 0xFF) ? livePT : entry->playerType;

        Log("[OutfitSupplyDropPickup:%s] forcing equip of stashed "
            "developId=%u partsType=0x%02X selector=0x%02X variantIdx=%u "
            "(baseSelector=0x%02X) reloadPT=%u (registered=%u)\n",
            tag,
            static_cast<unsigned>(entry->developId),
            static_cast<unsigned>(entry->partsType),
            static_cast<unsigned>(variantSelector),
            static_cast<unsigned>(variantIdx),
            static_cast<unsigned>(entry->selectorCode),
            static_cast<unsigned>(reloadPT),
            static_cast<unsigned>(entry->playerType));

        __try
        {
            outfit::ForcePartsReload(reloadPT,
                                      entry->partsType,
                                      variantSelector);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitSupplyDropPickup:%s] SEH while driving "
                "ForcePartsReload\n", tag);
        }
        return true;
    }


    static void __fastcall hkStateHandler1(
        void* work, std::uint32_t row, std::uint32_t subState, void* arg4)
    {
        if (g_OrigStateHandler1)
            g_OrigStateHandler1(work, row, subState, arg4);


        if (subState == 10u)
            ConsumeStashAndEquip("PickupAnim");
    }


    static void __fastcall hkReset(void* self)
    {
        std::uint8_t  flags10c = 0;
        std::uint32_t flags124 = 0;
        if (self)
        {
            __try
            {
                const auto* base = reinterpret_cast<const std::uint8_t*>(self);
                flags10c = base[0x10c];
                flags124 = *reinterpret_cast<const std::uint32_t*>(base + 0x124);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                flags10c = 0;
                flags124 = 0;
            }
        }

        const bool dropFlags  = (flags10c & 0x07u) != 0;
        const bool activeColl = (flags124 & 0x20000u) != 0;
        const bool realPickup = dropFlags || activeColl;

        if (g_OrigReset) g_OrigReset(self);

        if (!realPickup) return;

        ConsumeStashAndEquip("ResetBackstop");
    }


    static void __fastcall hkRequestToDropImpl(void* self, void* request);


    static void __fastcall hkSettledHandler(void* self)
    {
        if (!self)
        {
            if (g_OrigSettledHandler) g_OrigSettledHandler(self);
            return;
        }


        const std::uint16_t pendingDevId =
            outfit::PeekPendingSupplyDropDevelopId();
        const outfit::OutfitEntry* entry = nullptr;
        const bool isOurRequest =
            pendingDevId != 0
         && outfit::TryGetOutfitByDevelopId(pendingDevId, &entry)
         && entry;

        if (!isOurRequest)
        {


            if (g_OrigSettledHandler) g_OrigSettledHandler(self);
            return;
        }


        std::uint32_t flags124Pre = 0;
        std::uint32_t modePre     = 0;
        __try
        {
            const auto* base = reinterpret_cast<const std::uint8_t*>(self);
            flags124Pre = *reinterpret_cast<const std::uint32_t*>(base + 0x124);
            modePre     = *reinterpret_cast<const std::uint32_t*>(base + 0xF0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {


            if (g_OrigSettledHandler) g_OrigSettledHandler(self);
            return;
        }

        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(self);
            auto* flags124 =
                reinterpret_cast<std::uint32_t*>(base + 0x124);
            auto* mode =
                reinterpret_cast<std::uint32_t*>(base + 0xF0);


            *flags124 = (*flags124 & ~(0x80u | 0x10u | 0x200u)) | 0x40u;


            *flags124 |= 0x28u;
            *mode      = 8u;

            Log("[OutfitSupplyDropPickup:SettledHandler] override for "
                "custom outfit developId=%u: skipped orig and "
                "manually advanced state. mode 0x%X→0x8, flags124 "
                "0x%08X→0x%08X (set 0x40/0x20/0x08; cleared "
                "0x80/0x10/0x200 to suppress downstream auto-burst "
                "pipeline in cases 0xe/0xf/0x10) — phase 2 should "
                "engage for player E-press pickup\n",
                static_cast<unsigned>(entry->developId),
                static_cast<unsigned>(modePre),
                static_cast<unsigned>(flags124Pre),
                static_cast<unsigned>(*flags124));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitSupplyDropPickup:SettledHandler] SEH while "
                "writing override state for developId=%u — falling "
                "through to orig (auto-burst may follow)\n",
                static_cast<unsigned>(entry->developId));
            if (g_OrigSettledHandler) g_OrigSettledHandler(self);
        }
    }


    static void __fastcall hkOnDropTimerTick(
        void* self, std::uint16_t decrementBy, void* posVec)
    {

        std::uint32_t modePre = 0;
        std::uint32_t flagsPre = 0;
        bool snapshotOk = false;
        if (self)
        {
            __try
            {
                const auto* base = reinterpret_cast<const std::uint8_t*>(self);
                modePre  = *reinterpret_cast<const std::uint32_t*>(base + 0xF0);
                flagsPre = *reinterpret_cast<const std::uint32_t*>(base + 0x124);
                snapshotOk = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        if (g_OrigOnDropTimerTick) g_OrigOnDropTimerTick(self, decrementBy, posVec);

        if (!snapshotOk || !self) return;


        const std::uint16_t pendingDevId =
            outfit::PeekPendingSupplyDropDevelopId();
        const outfit::OutfitEntry* entry = nullptr;
        const bool isOurRequest =
            pendingDevId != 0
         && outfit::TryGetOutfitByDevelopId(pendingDevId, &entry)
         && entry;
        if (!isOurRequest) return;


        std::uint32_t modePost  = 0;
        std::uint32_t flagsPost = 0;
        __try
        {
            const auto* base = reinterpret_cast<const std::uint8_t*>(self);
            modePost  = *reinterpret_cast<const std::uint32_t*>(base + 0xF0);
            flagsPost = *reinterpret_cast<const std::uint32_t*>(base + 0x124);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return; }


        const bool wasMode10  = (modePre == 10u);
        const bool nowMode0xB = (modePost == 0x0Bu);
        const bool bit0x10NewlySet =
            ((flagsPre & 0x10u) == 0u) && ((flagsPost & 0x10u) != 0u);

        if (wasMode10 && nowMode0xB && bit0x10NewlySet)
        {
            __try
            {
                auto* base = reinterpret_cast<std::uint8_t*>(self);


                *reinterpret_cast<std::uint32_t*>(base + 0xF0) = 10u;


                *reinterpret_cast<std::uint32_t*>(base + 0x124) =
                    flagsPost & ~0x10u;

                Log("[OutfitSupplyDropPickup:OnDropTimerTick] reverted "
                    "auto-burst transition for custom outfit developId=%u "
                    "(mode 10→0xB → 10, flags 0x%08X → 0x%08X with bit "
                    "0x10 cleared) — box stays in chopper-drop-active "
                    "state for phase-2 interactive pickup\n",
                    static_cast<unsigned>(entry->developId),
                    static_cast<unsigned>(flagsPost),
                    static_cast<unsigned>(flagsPost & ~0x10u));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                Log("[OutfitSupplyDropPickup:OnDropTimerTick] SEH "
                    "reverting mode/flags for developId=%u\n",
                    static_cast<unsigned>(entry->developId));
            }
        }
    }

    static void __fastcall hkRequestToDropImpl(void* self, void* request)
    {


        if (self)
        {
            const std::uint16_t pendingDevId =
                outfit::PeekPendingSupplyDropDevelopId();
            const outfit::OutfitEntry* entry = nullptr;
            const bool isOurRequest =
                pendingDevId != 0
             && outfit::TryGetOutfitByDevelopId(pendingDevId, &entry)
             && entry;

            if (isOurRequest)
            {
                __try
                {
                    const std::uint32_t flags124 =
                        *reinterpret_cast<const std::uint32_t*>(
                            reinterpret_cast<const std::uint8_t*>(self) + 0x124);
                    Log("[OutfitSupplyDropPickup:RequestToDropImpl] "
                        "observed flags124=0x%08X (bit 0x20 %s) for "
                        "custom outfit developId=%u partsType=0x%02X — "
                        "leaving state untouched (orig decides interactive "
                        "vs auto-burst per cVar3/bit-0x20 gate)\n",
                        static_cast<unsigned>(flags124),
                        (flags124 & 0x20u) ? "set (iDroid pattern)"
                                           : "clear (dev-menu pattern)",
                        static_cast<unsigned>(entry->developId),
                        static_cast<unsigned>(entry->partsType));
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    Log("[OutfitSupplyDropPickup:RequestToDropImpl] SEH "
                        "reading self+0x124 (self=%p)\n", self);
                }
            }
        }

        if (g_OrigRequestToDropImpl) g_OrigRequestToDropImpl(self, request);
    }


    static void __fastcall hkRestore(void* self)
    {


        const std::uint16_t pendingDevId =
            outfit::ConsumePendingSupplyDropDevelopId();
        if (pendingDevId != 0)
        {
            Log("[OutfitSupplyDropPickup:Restore] cleared stale stash "
                "developId=%u (save/load boundary)\n",
                static_cast<unsigned>(pendingDevId));
        }
        if (g_OrigRestore) g_OrigRestore(self);
    }
}

namespace outfit
{
    bool Install_OutfitSupplyDropPickup_Hook()
    {

        if (!g_InstalledStateHandler1)
        {
            void* target = ResolveGameAddress(
                gAddr.SupplyCboxActionPluginImpl_StateHandler1);
            if (target)
            {
                g_InstalledStateHandler1 = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkStateHandler1),
                    reinterpret_cast<void**>(&g_OrigStateHandler1));
                Log("[OutfitSupplyDropPickup] StateHandler1 installed: %s "
                    "(target=%p)\n",
                    g_InstalledStateHandler1 ? "OK" : "FAIL", target);
            }
            else
            {
                Log("[OutfitSupplyDropPickup] StateHandler1 target unresolved\n");
            }
        }


        if (!g_InstalledReset)
        {
            void* target = ResolveGameAddress(
                gAddr.SupplyCboxSystemImpl_Reset);
            if (target)
            {
                g_InstalledReset = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkReset),
                    reinterpret_cast<void**>(&g_OrigReset));
                Log("[OutfitSupplyDropPickup] Reset installed: %s "
                    "(target=%p)\n",
                    g_InstalledReset ? "OK" : "FAIL", target);
            }
            else
            {
                Log("[OutfitSupplyDropPickup] Reset target unresolved\n");
            }
        }


        if (false && !g_InstalledSettledHandler)
        {
            void* target = ResolveGameAddress(
                gAddr.SupplyCboxSystemImpl_SettledHandler);
            if (target)
            {
                g_InstalledSettledHandler = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkSettledHandler),
                    reinterpret_cast<void**>(&g_OrigSettledHandler));
                Log("[OutfitSupplyDropPickup] SettledHandler installed: "
                    "%s (target=%p)\n",
                    g_InstalledSettledHandler ? "OK" : "FAIL", target);
            }
        }
        Log("[OutfitSupplyDropPickup] SettledHandler hook DISABLED "
            "2026-04-28 — auto-burst path required for body asset-load "
            "dispatch; ResetBackstop drives equip on landing\n");


        if (!g_InstalledOnDropTimerTick)
        {
            void* target = ResolveGameAddress(
                gAddr.SupplyCboxSystemImpl_OnDropTimerTick);
            if (target)
            {
                g_InstalledOnDropTimerTick = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkOnDropTimerTick),
                    reinterpret_cast<void**>(&g_OrigOnDropTimerTick));
                Log("[OutfitSupplyDropPickup] OnDropTimerTick installed: %s "
                    "(target=%p)\n",
                    g_InstalledOnDropTimerTick ? "OK" : "FAIL", target);
            }
            else
            {
                Log("[OutfitSupplyDropPickup] OnDropTimerTick target unresolved "
                    "(JP build?) — auto-burst-on-landing reversion will not "
                    "run; Reset backstop still equips the outfit\n");
            }
        }


        if (!g_InstalledRequestToDropImpl)
        {
            void* target = ResolveGameAddress(
                gAddr.SupplyCboxSystemImpl_RequestToDropImpl);
            if (target)
            {
                g_InstalledRequestToDropImpl = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkRequestToDropImpl),
                    reinterpret_cast<void**>(&g_OrigRequestToDropImpl));
                Log("[OutfitSupplyDropPickup] RequestToDropImpl installed: %s "
                    "(target=%p)\n",
                    g_InstalledRequestToDropImpl ? "OK" : "FAIL", target);
            }
            else
            {
                Log("[OutfitSupplyDropPickup] RequestToDropImpl target unresolved "
                    "(JP build?) — dev-menu R&D Request will fall back to "
                    "auto-burst behavior\n");
            }
        }


        if (!g_InstalledRestore)
        {
            void* target = ResolveGameAddress(
                gAddr.SupplyCboxGameObjectImpl_RestoreRequestFromSVars);
            if (target)
            {
                g_InstalledRestore = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkRestore),
                    reinterpret_cast<void**>(&g_OrigRestore));
                Log("[OutfitSupplyDropPickup] Restore installed: %s "
                    "(target=%p)\n",
                    g_InstalledRestore ? "OK" : "FAIL", target);
            }
            else
            {
                Log("[OutfitSupplyDropPickup] Restore target unresolved\n");
            }
        }

        return g_InstalledStateHandler1
            || g_InstalledReset
            || g_InstalledSettledHandler
            || g_InstalledOnDropTimerTick
            || g_InstalledRequestToDropImpl
            || g_InstalledRestore;
    }

    void Uninstall_OutfitSupplyDropPickup_Hook()
    {
        if (g_InstalledStateHandler1)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.SupplyCboxActionPluginImpl_StateHandler1))
                DisableAndRemoveHook(t);
            g_OrigStateHandler1       = nullptr;
            g_InstalledStateHandler1  = false;
        }

        if (g_InstalledReset)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.SupplyCboxSystemImpl_Reset))
                DisableAndRemoveHook(t);
            g_OrigReset       = nullptr;
            g_InstalledReset  = false;
        }

        if (g_InstalledSettledHandler)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.SupplyCboxSystemImpl_SettledHandler))
                DisableAndRemoveHook(t);
            g_OrigSettledHandler       = nullptr;
            g_InstalledSettledHandler  = false;
        }

        if (g_InstalledOnDropTimerTick)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.SupplyCboxSystemImpl_OnDropTimerTick))
                DisableAndRemoveHook(t);
            g_OrigOnDropTimerTick       = nullptr;
            g_InstalledOnDropTimerTick  = false;
        }

        if (g_InstalledRequestToDropImpl)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.SupplyCboxSystemImpl_RequestToDropImpl))
                DisableAndRemoveHook(t);
            g_OrigRequestToDropImpl       = nullptr;
            g_InstalledRequestToDropImpl  = false;
        }

        if (g_InstalledRestore)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.SupplyCboxGameObjectImpl_RestoreRequestFromSVars))
                DisableAndRemoveHook(t);
            g_OrigRestore       = nullptr;
            g_InstalledRestore  = false;
        }

        Log("[OutfitSupplyDropPickup] removed\n");
    }
}
