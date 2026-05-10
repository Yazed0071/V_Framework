#include "pch.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "log.h"
#include "MbDvcCustomPopupHook.h"


namespace
{
    // 'VFPC' — tags our reservations.
    constexpr std::uint32_t kVPopupMagic = 0x56465043u;


    // MbDvcAnnouncePopupCallbackImpl layout.
    constexpr std::uintptr_t kImpl_TitleBufOffset       = 0x60;
    constexpr std::size_t    kImpl_TitleBufSize         = 0x80;
    constexpr std::uintptr_t kImpl_BodyBufOffset        = 0xe0;
    constexpr std::size_t    kImpl_BodyBufSize          = 0x400;
    constexpr std::uintptr_t kImpl_InnerStateOffset     = 0x558;
    constexpr std::uintptr_t kImpl_QuarkRootOffset      = 0x38;
    constexpr std::uintptr_t kQuarkRoot_PopupCtrlOffset = 0x80;
    constexpr std::uintptr_t kQuarkRoot_LangMgrOffset   = 0x20;
    constexpr std::uintptr_t kQuarkRoot_GateSvcOffset   = 0x48;


    // Slot ring layout (verified JP build).
    constexpr std::uintptr_t kCtrl_SlotsBaseOffset    = 0x08;
    constexpr std::size_t    kCtrl_SlotSize           = 0x14;
    constexpr std::size_t    kCtrl_NumSlots           = 0x10;
    constexpr std::uintptr_t kSlot_CommonValue1Offset = 0x00;
    constexpr std::uintptr_t kSlot_ReserveIdOffset    = 0x10;
    constexpr std::uint8_t   kReserveId_Empty         = 0x0E;
    constexpr std::uint8_t   kReserveId_NormalSlot0   = 0x00;


    // ReserveAnnouncePopup — controller vtable[+8].
    constexpr std::size_t kCtrlVtableIndex_ReserveAnnouncePopup = 0x08 / sizeof(void*);


    // GetLangText — lang manager vtable[+0x750].
    constexpr std::size_t kLangVtableIndex_GetLangText = 0x750 / sizeof(void*);


    // Gate svc method — vtable[+0x610].
    constexpr std::size_t kGateVtableIndex = 0x610 / sizeof(void*);


    using UpdateAnnounceNormal_t = std::int32_t (__fastcall*)(void* self);
    using ReserveAnnouncePopup_t = void (__fastcall*)(void* ctrl, const void* param);
    using GetLangText_t          = const char* (__fastcall*)(void* langMgr, std::uint64_t hash);
    using GateFn_t               = char (__fastcall*)(void* svc);


    static UpdateAnnounceNormal_t g_OrigUpdateAnnounceNormal = nullptr;
    static UpdateAnnounceNormal_t g_OrigUpdateAnnounceServer = nullptr;
    static ReserveAnnouncePopup_t g_OrigReserveAnnouncePopup = nullptr;
    static GateFn_t               g_OrigGateFn               = nullptr;
    static void*                  g_ReserveHookTarget        = nullptr;
    static void*                  g_GateHookTarget           = nullptr;
    static std::once_flag         g_ReserveHookInstallFlag;
    static std::once_flag         g_GateHookInstallFlag;


    // Set true around our hook's call to original.
    // Gate hook sees flag → returns 1 unconditionally.
    static thread_local bool      g_BypassPopupGate          = false;


    // Captured at first hook fire.
    static std::atomic<void*> g_PopupController{ nullptr };
    static std::atomic<void*> g_LangManager{ nullptr };


    // One-shot diagnostic dump.
    static std::atomic<bool> g_DiagDumpDone{ false };


    // Literal string or LangId hash.
    struct PopupTextSource
    {
        bool          isHash = false;
        std::string   literal;
        std::uint64_t hash = 0;
    };

    struct PendingPopup
    {
        PopupTextSource title;
        PopupTextSource body;
        std::uint8_t    reserveId = kReserveId_NormalSlot0;
        bool            reserved  = false;
    };
    static std::deque<PendingPopup> g_PendingQueue;
    static std::mutex               g_PendingMutex;


    // 20-byte ReserveParam.
#pragma pack(push, 1)
    struct ReserveParam
    {
        std::uint32_t commonValue1;
        std::uint32_t commonValue2;
        std::uint32_t commonValue3;
        std::uint32_t commonValue4;
        std::uint8_t  reserveId;
        std::uint8_t  pad[3];
    };
#pragma pack(pop)
    static_assert(sizeof(ReserveParam) == kCtrl_SlotSize, "ReserveParam matches slot stride");


    inline std::uint8_t* SlotPtr(void* ctrl, std::size_t i)
    {
        return reinterpret_cast<std::uint8_t*>(ctrl)
             + kCtrl_SlotsBaseOffset
             + i * kCtrl_SlotSize;
    }


    // SEH leaves — POD-only for C2712.

    // Read incoming ReserveParam fields.
    static bool SafeReadIncomingParam(const void* param,
                                      std::uint32_t* outCv1,
                                      std::uint8_t*  outReserveId)
    {
        *outCv1 = 0;
        *outReserveId = 0xFF;
        if (!param)
            return false;

        __try
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(param);
            *outCv1       = *reinterpret_cast<const std::uint32_t*>(bytes + kSlot_CommonValue1Offset);
            *outReserveId = *(bytes + kSlot_ReserveIdOffset);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }


    // self -> quarkRoot -> popup controller.
    static void* SafeReadControllerFromSelf(void* self)
    {
        if (!self)
            return nullptr;

        __try
        {
            const auto selfBytes = reinterpret_cast<std::uintptr_t>(self);
            void* quarkRoot = *reinterpret_cast<void**>(selfBytes + kImpl_QuarkRootOffset);
            if (!quarkRoot)
                return nullptr;

            return *reinterpret_cast<void**>(
                reinterpret_cast<std::uintptr_t>(quarkRoot) + kQuarkRoot_PopupCtrlOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }


    // Read inner state byte.
    static std::uint32_t SafeReadInnerState(void* self)
    {
        if (!self)
            return 0;

        __try
        {
            return *reinterpret_cast<std::uint32_t*>(
                reinterpret_cast<std::uintptr_t>(self) + kImpl_InnerStateOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }


    // Any magic-tagged slot of given types?
    static bool SafeAnyOurSlotInRing(void* ctrl,
                                     const std::uint8_t* eligibleIds,
                                     std::size_t numEligibleIds)
    {
        if (!ctrl || !eligibleIds || numEligibleIds == 0)
            return false;

        __try
        {
            for (std::size_t i = 0; i < kCtrl_NumSlots; ++i)
            {
                const std::uint8_t* slot = SlotPtr(ctrl, i);
                const std::uint8_t  rid  = *(slot + kSlot_ReserveIdOffset);
                bool eligible = false;
                for (std::size_t k = 0; k < numEligibleIds; ++k)
                {
                    if (rid == eligibleIds[k]) { eligible = true; break; }
                }
                if (!eligible) continue;
                const std::uint32_t cv1 = *reinterpret_cast<const std::uint32_t*>(
                    slot + kSlot_CommonValue1Offset);
                if (cv1 == kVPopupMagic)
                    return true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return false;
    }


    // First slot match; flag if magic-tagged.
    static bool SafeFindFirstSlotIsOurs(void* ctrl,
                                        std::uint8_t wantedReserveId,
                                        bool* outIsOurs)
    {
        *outIsOurs = false;
        if (!ctrl)
            return false;

        __try
        {
            for (std::size_t i = 0; i < kCtrl_NumSlots; ++i)
            {
                const std::uint8_t* slot = SlotPtr(ctrl, i);
                if (*(slot + kSlot_ReserveIdOffset) == wantedReserveId)
                {
                    const std::uint32_t cv1 = *reinterpret_cast<const std::uint32_t*>(
                        slot + kSlot_CommonValue1Offset);
                    *outIsOurs = (cv1 == kVPopupMagic);
                    return true;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return false;
    }


    // Count empty slots in ring.
    static std::size_t SafeCountEmptySlots(void* ctrl)
    {
        if (!ctrl)
            return 0;

        __try
        {
            std::size_t count = 0;
            for (std::size_t i = 0; i < kCtrl_NumSlots; ++i)
            {
                const std::uint8_t* slot = SlotPtr(ctrl, i);
                if (*(slot + kSlot_ReserveIdOffset) == kReserveId_Empty)
                    ++count;
            }
            return count;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }


    // Free oldest tagged slot for game.
    static bool SafeEvictOldestOurSlot(void* ctrl)
    {
        if (!ctrl)
            return false;

        __try
        {
            for (std::size_t i = 0; i < kCtrl_NumSlots; ++i)
            {
                std::uint8_t* slot = SlotPtr(ctrl, i);
                const std::uint8_t reserveId = *(slot + kSlot_ReserveIdOffset);
                if (reserveId == kReserveId_Empty)
                    continue;

                const std::uint32_t cv1 = *reinterpret_cast<std::uint32_t*>(
                    slot + kSlot_CommonValue1Offset);
                if (cv1 == kVPopupMagic)
                {
                    std::memset(slot, 0, kCtrl_SlotSize);
                    *(slot + kSlot_ReserveIdOffset) = kReserveId_Empty;
                    return true;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return false;
    }


    // Write a single byte at self+offset.
    static void SafeWriteImplByte(void* self, std::uintptr_t off, std::uint8_t val)
    {
        if (!self) return;
        __try
        {
            *(std::uint8_t*)((std::uintptr_t)self + off) = val;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }


    // Write a uint32 at self+offset.
    static void SafeWriteImplDword(void* self, std::uintptr_t off, std::uint32_t val)
    {
        if (!self) return;
        __try
        {
            *(std::uint32_t*)((std::uintptr_t)self + off) = val;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }


    // Write title+body into impl buffers.
    static bool SafeWriteTitleBody(void* self,
                                   const char* title,
                                   const char* body)
    {
        if (!self || !title || !body)
            return false;

        __try
        {
            char* titleBuf = reinterpret_cast<char*>(
                reinterpret_cast<std::uintptr_t>(self) + kImpl_TitleBufOffset);
            char* bodyBuf = reinterpret_cast<char*>(
                reinterpret_cast<std::uintptr_t>(self) + kImpl_BodyBufOffset);

            strncpy_s(titleBuf, kImpl_TitleBufSize, title, _TRUNCATE);
            strncpy_s(bodyBuf,  kImpl_BodyBufSize,  body,  _TRUNCATE);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }


    // Invoke ReserveAnnouncePopup via vtable.
    static bool SafeCallReserveAnnouncePopup(void* ctrl, const ReserveParam* p)
    {
        if (!ctrl || !p)
            return false;

        __try
        {
            void** vtbl = *reinterpret_cast<void***>(ctrl);
            if (!vtbl)
                return false;

            auto reserveFn = reinterpret_cast<ReserveAnnouncePopup_t>(
                vtbl[kCtrlVtableIndex_ReserveAnnouncePopup]);
            if (!reserveFn)
                return false;

            reserveFn(ctrl, p);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }


    // Verify our slot landed in ring.
    static bool SafeVerifyOurReservationLanded(void* ctrl, std::uint8_t reserveId)
    {
        if (!ctrl)
            return false;

        __try
        {
            for (std::size_t i = 0; i < kCtrl_NumSlots; ++i)
            {
                const std::uint8_t* slot = SlotPtr(ctrl, i);
                if (*(slot + kSlot_ReserveIdOffset) == reserveId)
                {
                    const std::uint32_t cv1 = *reinterpret_cast<const std::uint32_t*>(
                        slot + kSlot_CommonValue1Offset);
                    if (cv1 == kVPopupMagic)
                        return true;
                }
            }
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            // Fault → assume success.
            return true;
        }
    }


    // self -> quarkRoot -> gate service object.
    static void* SafeReadGateSvcFromSelf(void* self)
    {
        if (!self)
            return nullptr;

        __try
        {
            const auto selfBytes = reinterpret_cast<std::uintptr_t>(self);
            void* quarkRoot = *reinterpret_cast<void**>(selfBytes + kImpl_QuarkRootOffset);
            if (!quarkRoot)
                return nullptr;

            return *reinterpret_cast<void**>(
                reinterpret_cast<std::uintptr_t>(quarkRoot) + kQuarkRoot_GateSvcOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }


    // self -> quarkRoot -> lang manager.
    static void* SafeReadLangManagerFromSelf(void* self)
    {
        if (!self)
            return nullptr;

        __try
        {
            const auto selfBytes = reinterpret_cast<std::uintptr_t>(self);
            void* quarkRoot = *reinterpret_cast<void**>(selfBytes + kImpl_QuarkRootOffset);
            if (!quarkRoot)
                return nullptr;

            return *reinterpret_cast<void**>(
                reinterpret_cast<std::uintptr_t>(quarkRoot) + kQuarkRoot_LangMgrOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }


    // Resolve StringId hash to text.
    static const char* SafeResolveLangText(void* langMgr, std::uint64_t hash)
    {
        if (!langMgr)
            return nullptr;

        __try
        {
            void** vtbl = *reinterpret_cast<void***>(langMgr);
            if (!vtbl)
                return nullptr;

            auto fn = reinterpret_cast<GetLangText_t>(vtbl[kLangVtableIndex_GetLangText]);
            if (!fn)
                return nullptr;

            return fn(langMgr, hash);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }


    // Read first matching slot's cv1.
    static std::uint32_t SafeReadFirstSlotCv1(void* ctrl, std::uint8_t wantedReserveId)
    {
        if (!ctrl)
            return 0;

        __try
        {
            for (std::size_t i = 0; i < kCtrl_NumSlots; ++i)
            {
                const std::uint8_t* slot = SlotPtr(ctrl, i);
                if (*(slot + kSlot_ReserveIdOffset) == wantedReserveId)
                {
                    return *reinterpret_cast<const std::uint32_t*>(
                        slot + kSlot_CommonValue1Offset);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
        return 0;
    }


    // Reserve magic-tagged slot of given type.
    static bool SafeReserveOurSlot(void* ctrl, std::uint8_t reserveId)
    {
        if (!ctrl)
            return false;

        __try
        {
            void** vtbl = *reinterpret_cast<void***>(ctrl);
            if (!vtbl)
                return false;

            auto reserveFn = reinterpret_cast<ReserveAnnouncePopup_t>(
                vtbl[kCtrlVtableIndex_ReserveAnnouncePopup]);
            if (!reserveFn)
                return false;

            ReserveParam p{};
            p.commonValue1 = kVPopupMagic;
            p.reserveId    = reserveId;

            reserveFn(ctrl, &p);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }


    // Read gate function pointer (svc vtable[+0x610]).
    static void* SafeGetGateFnPtr(void* gateSvc)
    {
        if (!gateSvc)
            return nullptr;

        __try
        {
            void** vtbl = *reinterpret_cast<void***>(gateSvc);
            if (!vtbl)
                return nullptr;
            return vtbl[kGateVtableIndex];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }


    // Read ReserveAnnouncePopup function pointer.
    static void* SafeGetReserveFnPtr(void* ctrl)
    {
        if (!ctrl)
            return nullptr;

        __try
        {
            void** vtbl = *reinterpret_cast<void***>(ctrl);
            if (!vtbl)
                return nullptr;
            return vtbl[kCtrlVtableIndex_ReserveAnnouncePopup];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }


    // One-shot slot-ring layout dump.
    static void SafeDiagDumpSlots(void* ctrl, const void* incomingParam)
    {
        if (!ctrl)
            return;

        __try
        {
            const auto* base = reinterpret_cast<const std::uint8_t*>(ctrl);

            // Vtable pointer.
            const std::uintptr_t vtbl = *reinterpret_cast<const std::uintptr_t*>(base);
            Log("[MbDvcCustomPopup][DIAG] controller=%p vtable=0x%016llX\n",
                ctrl, static_cast<unsigned long long>(vtbl));

            // Incoming param (20 bytes).
            if (incomingParam)
            {
                const auto* pb = reinterpret_cast<const std::uint8_t*>(incomingParam);
                Log("[MbDvcCustomPopup][DIAG] incoming param 20 bytes: "
                    "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X "
                    "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
                    pb[0],  pb[1],  pb[2],  pb[3],  pb[4],  pb[5],  pb[6],  pb[7],
                    pb[8],  pb[9],  pb[10], pb[11], pb[12], pb[13], pb[14], pb[15],
                    pb[16], pb[17], pb[18], pb[19]);
            }

            // Dump both layouts side by side.
            Log("[MbDvcCustomPopup][DIAG] --- assuming OLD layout (stride=12, type@+0x08) ---\n");
            for (int i = 0; i < 8; ++i)
            {
                const auto* slot = base + 0x08 + (std::size_t)i * 0x0c;
                Log("[MbDvcCustomPopup][DIAG] slot[%d] cv1=0x%08X cv2=0x%08X type=0x%02X\n",
                    i,
                    *reinterpret_cast<const std::uint32_t*>(slot + 0x00),
                    *reinterpret_cast<const std::uint32_t*>(slot + 0x04),
                    *(slot + 0x08));
            }

            Log("[MbDvcCustomPopup][DIAG] --- assuming NEW layout (stride=20, type@+0x10) ---\n");
            for (int i = 0; i < 8; ++i)
            {
                const auto* slot = base + 0x08 + (std::size_t)i * 0x14;
                Log("[MbDvcCustomPopup][DIAG] slot[%d] cv1=0x%08X cv2=0x%08X cv3=0x%08X cv4=0x%08X type=0x%02X\n",
                    i,
                    *reinterpret_cast<const std::uint32_t*>(slot + 0x00),
                    *reinterpret_cast<const std::uint32_t*>(slot + 0x04),
                    *reinterpret_cast<const std::uint32_t*>(slot + 0x08),
                    *reinterpret_cast<const std::uint32_t*>(slot + 0x0c),
                    *(slot + 0x10));
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[MbDvcCustomPopup][DIAG] dump raised; aborting diag\n");
        }
    }


    // Reserve hook — game-priority eviction.
    static void __fastcall hk_ReserveAnnouncePopup(void* ctrl, const void* param)
    {
        if (!ctrl || !param || !g_OrigReserveAnnouncePopup)
        {
            if (g_OrigReserveAnnouncePopup)
                g_OrigReserveAnnouncePopup(ctrl, param);
            return;
        }

        // One-shot layout dump.
        if (!g_DiagDumpDone.exchange(true, std::memory_order_relaxed))
        {
            SafeDiagDumpSlots(ctrl, param);
        }

        std::uint32_t incomingCv1 = 0;
        std::uint8_t  incomingReserveId = 0xFF;
        if (!SafeReadIncomingParam(param, &incomingCv1, &incomingReserveId))
        {
            Log("[MbDvcCustomPopup] Reserve hook: param read raised; passing through\n");
            g_OrigReserveAnnouncePopup(ctrl, param);
            return;
        }

        const bool isOurs = (incomingCv1 == kVPopupMagic);

        // Only Normal popups are eligible.
        const bool isNormalSlot = (incomingReserveId == 0x00 || incomingReserveId == 0x01);

        const std::size_t empty = SafeCountEmptySlots(ctrl);

        if (empty == 0 && isNormalSlot)
        {
            if (isOurs)
            {
                // Refuse our overflow.
                Log("[MbDvcCustomPopup] Reserve hook: ring full + our reservation -> reject\n");
                return;
            }
            else
            {
                // Evict ours for game popup.
                const bool evicted = SafeEvictOldestOurSlot(ctrl);
                if (evicted)
                {
                    {
                        std::lock_guard<std::mutex> lock(g_PendingMutex);
                        if (!g_PendingQueue.empty())
                            g_PendingQueue.pop_front();
                    }
                    Log("[MbDvcCustomPopup] Reserve hook: ring full + game popup -> "
                        "evicted oldest V_FrameWork slot to make room\n");
                }
                else
                {
                    Log("[MbDvcCustomPopup] Reserve hook: ring full + game popup but "
                        "no V_FrameWork slot to evict; game reservation will silently fail\n");
                }
            }
        }

        g_OrigReserveAnnouncePopup(ctrl, param);
    }


    // Lazy install of Reserve hook.
    static void TryInstallReserveHook(void* ctrl)
    {
        if (!ctrl)
            return;

        std::call_once(g_ReserveHookInstallFlag, [ctrl]()
        {
            void* reserveFn = SafeGetReserveFnPtr(ctrl);
            if (!reserveFn)
            {
                Log("[MbDvcCustomPopup] ReserveHook install: vtable[+8] not resolvable\n");
                return;
            }

            const bool ok = CreateAndEnableHook(
                reserveFn,
                reinterpret_cast<void*>(&hk_ReserveAnnouncePopup),
                reinterpret_cast<void**>(&g_OrigReserveAnnouncePopup));
            if (ok)
            {
                g_ReserveHookTarget = reserveFn;
                Log("[MbDvcCustomPopup] ReserveHook install: OK (target=%p)\n", reserveFn);
            }
            else
            {
                Log("[MbDvcCustomPopup] ReserveHook install: MinHook FAILED (target=%p)\n", reserveFn);
            }
        });
    }


    // Gate hook — bypass only when our hook set the flag.
    static char __fastcall hk_GateFn(void* svc)
    {
        if (g_BypassPopupGate)
            return 1;
        return g_OrigGateFn ? g_OrigGateFn(svc) : 0;
    }


    // Lazy install of gate hook.
    static void TryInstallGateHook(void* self)
    {
        if (!self)
            return;

        std::call_once(g_GateHookInstallFlag, [self]()
        {
            void* gateSvc = SafeReadGateSvcFromSelf(self);
            if (!gateSvc)
            {
                Log("[MbDvcCustomPopup] GateHook install: gate svc not resolvable\n");
                return;
            }

            void* fn = SafeGetGateFnPtr(gateSvc);
            if (!fn)
            {
                Log("[MbDvcCustomPopup] GateHook install: vtable[+0x610] not resolvable\n");
                return;
            }

            const bool ok = CreateAndEnableHook(
                fn,
                reinterpret_cast<void*>(&hk_GateFn),
                reinterpret_cast<void**>(&g_OrigGateFn));
            if (ok)
            {
                g_GateHookTarget = fn;
                Log("[MbDvcCustomPopup] GateHook install: OK (target=%p)\n", fn);
            }
            else
            {
                Log("[MbDvcCustomPopup] GateHook install: MinHook FAILED (target=%p)\n", fn);
            }
        });
    }


    // Reserve any queued unreserved entries.
    static void DrainUnreservedReservations(void* ctrl)
    {
        if (!ctrl)
            return;

        for (;;)
        {
            // Find unreserved candidate; capture its reserveId.
            bool         haveCandidate = false;
            std::uint8_t candidateRid  = 0;
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                for (auto& entry : g_PendingQueue)
                {
                    if (!entry.reserved)
                    {
                        haveCandidate = true;
                        candidateRid  = entry.reserveId;
                        break;
                    }
                }
            }
            if (!haveCandidate)
                return;

            const bool ok = SafeReserveOurSlot(ctrl, candidateRid);

            // Mark reserved or drop on failure.
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                for (auto it = g_PendingQueue.begin(); it != g_PendingQueue.end(); ++it)
                {
                    if (!it->reserved)
                    {
                        if (ok)
                        {
                            it->reserved = true;
                        }
                        else
                        {
                            g_PendingQueue.erase(it);
                        }
                        break;
                    }
                }
            }

            if (!ok)
            {
                Log("[MbDvcCustomPopup] DrainUnreserved: Reserve failed (ring may be full); "
                    "dropped one queued entry\n");
                return;
            }
        }
    }


    // Shared body for Normal/Server hooks.
    static std::int32_t RunPopupOverrideHook(
        void*                      self,
        UpdateAnnounceNormal_t     origFn,
        const std::uint8_t*        eligibleIds,
        std::size_t                numEligibleIds)
    {
        if (!self || !origFn)
        {
            return origFn ? origFn(self) : 0;
        }

        const std::uint32_t prevState = SafeReadInnerState(self);

        // Capture pointers, drain queue.
        void* ctrl = SafeReadControllerFromSelf(self);
        if (ctrl)
        {
            g_PopupController.store(ctrl, std::memory_order_relaxed);
            TryInstallReserveHook(ctrl);
            DrainUnreservedReservations(ctrl);
        }

        void* lang = SafeReadLangManagerFromSelf(self);
        if (lang)
        {
            g_LangManager.store(lang, std::memory_order_relaxed);
        }

        // Install gate hook lazily.
        TryInstallGateHook(self);

        // First-slot check (decides if THIS call's consumed slot was ours).
        bool nextSlotIsOurs = false;
        if (ctrl && prevState != 1)
        {
            for (std::size_t i = 0; i < numEligibleIds && !nextSlotIsOurs; ++i)
            {
                bool isOurs = false;
                SafeFindFirstSlotIsOurs(ctrl, eligibleIds[i], &isOurs);
                if (isOurs) nextSlotIsOurs = true;
            }
        }

        // Anywhere-in-ring check (decides gate bypass).
        // Game popups in front of ours will also benefit, which is fine —
        // the override only fires when our slot is first.
        const bool anyOurSlotPending = SafeAnyOurSlotInRing(
            ctrl, eligibleIds, numEligibleIds);

        const bool prevBypass = g_BypassPopupGate;
        if (anyOurSlotPending)
            g_BypassPopupGate = true;

        const std::int32_t  result    = origFn(self);

        g_BypassPopupGate = prevBypass;

        const std::uint32_t currState = SafeReadInnerState(self);

        // Override on transitions into state 1.
        if (nextSlotIsOurs && prevState != 1 && currState == 1)
        {
            // Pop first reserved entry of an eligible type.
            bool            havePopped = false;
            PopupTextSource title;
            PopupTextSource body;
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                for (auto it = g_PendingQueue.begin(); it != g_PendingQueue.end(); ++it)
                {
                    if (!it->reserved) continue;
                    bool eligible = false;
                    for (std::size_t i = 0; i < numEligibleIds; ++i)
                    {
                        if (eligibleIds[i] == it->reserveId)
                        {
                            eligible = true;
                            break;
                        }
                    }
                    if (eligible)
                    {
                        title = std::move(it->title);
                        body  = std::move(it->body);
                        g_PendingQueue.erase(it);
                        havePopped = true;
                        break;
                    }
                }
            }

            if (havePopped)
            {
                // Resolve hashes; null falls back to "".
                const char* titleText = "";
                const char* bodyText  = "";

                if (title.isHash)
                    titleText = SafeResolveLangText(lang, title.hash);
                else
                    titleText = title.literal.c_str();
                if (!titleText) titleText = "";

                if (body.isHash)
                    bodyText = SafeResolveLangText(lang, body.hash);
                else
                    bodyText = body.literal.c_str();
                if (!bodyText) bodyText = "";

                if (SafeWriteTitleBody(self, titleText, bodyText))
                {
                    Log("[MbDvcCustomPopup] override APPLIED: title=\"%s\" body=\"%.40s%s\"\n",
                        titleText,
                        bodyText,
                        std::strlen(bodyText) > 40 ? "..." : "");
                }
                else
                {
                    Log("[MbDvcCustomPopup] override write failed (self=%p)\n", self);
                }
            }
        }

        return result;
    }


    // Normal — slot 0 / 1.
    static std::int32_t __fastcall hk_UpdateAnnounceNormal(void* self)
    {
        static const std::uint8_t kNormalIds[] = { 0, 1 };
        const std::int32_t result = RunPopupOverrideHook(
            self, g_OrigUpdateAnnounceNormal, kNormalIds, sizeof(kNormalIds));

        // Set Update's back-to-Server flag when ours pending.
        // Update reads state[0x30] at param_1+0x30 (its view) which lives at
        // self+0x50 in ours (multiple-inheritance adjustor: Update sees
        // param_1 = self + 0x20). Direct state[0x10]=7 won't work because
        // Update unconditionally writes state[0x10]=9 after case 8 breaks;
        // only the case-8 conditional path (state[0x30]!=0 && !IsInvalid)
        // transitions via goto, skipping that overwrite.
        if (result == 2)
        {
            void* ctrl = g_PopupController.load(std::memory_order_relaxed);
            static const std::uint8_t kServerIds[] = { 2, 3, 4, 7, 8 };
            if (SafeAnyOurSlotInRing(ctrl, kServerIds, sizeof(kServerIds)))
            {
                SafeWriteImplByte (self, 0x50, 1);   // state[0x30] = back-to-Server flag
                SafeWriteImplDword(self, 0x5c, 0);   // reset inner slot counter
            }
        }
        return result;
    }


    // Server — slot 2 / 3 / 4 / 7 / 8.
    static std::int32_t __fastcall hk_UpdateAnnounceServer(void* self)
    {
        static const std::uint8_t kServerIds[] = { 2, 3, 4, 7, 8 };
        return RunPopupOverrideHook(self, g_OrigUpdateAnnounceServer,
                                    kServerIds, sizeof(kServerIds));
    }
}  // namespace


bool Install_MbDvcCustomPopup_Hook()
{
    // Normal hook (slot 0/1).
    void* targetN = ResolveGameAddress(
        gAddr.MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceNormal);
    if (!targetN)
    {
        Log("[Hook] MbDvcCustomPopup: Normal target resolve failed (addr=%llX)\n",
            static_cast<unsigned long long>(
                gAddr.MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceNormal));
        return false;
    }

    const bool okN = CreateAndEnableHook(
        targetN,
        reinterpret_cast<void*>(&hk_UpdateAnnounceNormal),
        reinterpret_cast<void**>(&g_OrigUpdateAnnounceNormal));
    Log("[Hook] MbDvcCustomPopup Normal: %s (target=%p)\n",
        okN ? "OK" : "FAIL", targetN);

    // Server hook (slot 2/3/4/7/8). Optional — non-fatal if address is 0.
    if (gAddr.MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceServer)
    {
        void* targetS = ResolveGameAddress(
            gAddr.MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceServer);
        if (targetS)
        {
            const bool okS = CreateAndEnableHook(
                targetS,
                reinterpret_cast<void*>(&hk_UpdateAnnounceServer),
                reinterpret_cast<void**>(&g_OrigUpdateAnnounceServer));
            Log("[Hook] MbDvcCustomPopup Server: %s (target=%p)\n",
                okS ? "OK" : "FAIL", targetS);
        }
    }
    else
    {
        Log("[Hook] MbDvcCustomPopup Server: skipped (no address for current build)\n");
    }

    return okN;
}


bool Uninstall_MbDvcCustomPopup_Hook()
{
    void* targetN = ResolveGameAddress(
        gAddr.MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceNormal);
    DisableAndRemoveHook(targetN);
    g_OrigUpdateAnnounceNormal = nullptr;

    if (gAddr.MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceServer)
    {
        void* targetS = ResolveGameAddress(
            gAddr.MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceServer);
        DisableAndRemoveHook(targetS);
        g_OrigUpdateAnnounceServer = nullptr;
    }

    if (g_ReserveHookTarget)
    {
        DisableAndRemoveHook(g_ReserveHookTarget);
        g_ReserveHookTarget = nullptr;
        g_OrigReserveAnnouncePopup = nullptr;
    }

    if (g_GateHookTarget)
    {
        DisableAndRemoveHook(g_GateHookTarget);
        g_GateHookTarget = nullptr;
        g_OrigGateFn = nullptr;
    }

    g_PopupController.store(nullptr, std::memory_order_relaxed);
    g_LangManager.store(nullptr, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        g_PendingQueue.clear();
    }

    Log("[Hook] MbDvcCustomPopup: removed\n");
    return true;
}


// Internal: literal-or-hash for both fields, configurable reserveId.
static bool Show_MbDvcAnnouncePopup_Impl(std::uint8_t  reserveId,
                                         const char*   titleLiteral,
                                         std::uint64_t titleHash,
                                         const char*   bodyLiteral,
                                         std::uint64_t bodyHash)
{
    PopupTextSource titleSrc;
    if (titleLiteral)
    {
        titleSrc.isHash  = false;
        titleSrc.literal = titleLiteral;
    }
    else
    {
        titleSrc.isHash = true;
        titleSrc.hash   = titleHash;
    }

    PopupTextSource bodySrc;
    if (bodyLiteral)
    {
        bodySrc.isHash  = false;
        bodySrc.literal = bodyLiteral;
    }
    else
    {
        bodySrc.isHash = true;
        bodySrc.hash   = bodyHash;
    }

    void* ctrl = g_PopupController.load(std::memory_order_relaxed);

    // Cap at ring capacity (16).
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        if (g_PendingQueue.size() >= kCtrl_NumSlots)
        {
            Log("[MbDvcCustomPopup] Show: queue full (%zu); rejecting new entry\n",
                g_PendingQueue.size());
            return false;
        }
        PendingPopup p;
        p.title     = std::move(titleSrc);
        p.body      = std::move(bodySrc);
        p.reserveId = reserveId;
        p.reserved  = false;
        g_PendingQueue.push_back(std::move(p));
    }

    // No controller yet — defer reservation.
    if (!ctrl)
    {
        return true;
    }

    // Reserve immediately.
    const bool reserveOk = SafeReserveOurSlot(ctrl, reserveId);
    if (!reserveOk)
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        if (!g_PendingQueue.empty())
            g_PendingQueue.pop_back();
        Log("[MbDvcCustomPopup] Show: SafeReserveOurSlot raised; entry rolled back\n");
        return false;
    }

    // Verify reservation landed in ring.
    const bool landed = SafeVerifyOurReservationLanded(ctrl, reserveId);
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        if (!g_PendingQueue.empty())
        {
            if (landed)
                g_PendingQueue.back().reserved = true;
            else
                g_PendingQueue.pop_back();
        }
    }

    if (!landed)
    {
        Log("[MbDvcCustomPopup] Show: post-reserve verification found no V_FrameWork slot "
            "(ring may have been full and our reservation was rejected); entry rolled back\n");
        return false;
    }

    return true;
}


bool Show_MbDvcAnnouncePopupReport(const char* title, const char* body)
{
    return Show_MbDvcAnnouncePopup_Impl(
        kReserveId_NormalSlot0,
        title ? title : "", 0,
        body ? body : "",  0);
}


bool Show_MbDvcAnnouncePopupByLangId(const char* titleLabel, const char* bodyLabel)
{
    // Empty labels skip lookup.
    const char*  titleLit  = nullptr;
    std::uint64_t titleHash = 0;
    if (titleLabel && *titleLabel)
        titleHash = FoxHashes::StrCode64(titleLabel);
    else
        titleLit = "";

    const char*  bodyLit  = nullptr;
    std::uint64_t bodyHash = 0;
    if (bodyLabel && *bodyLabel)
        bodyHash = FoxHashes::StrCode64(bodyLabel);
    else
        bodyLit = "";

    return Show_MbDvcAnnouncePopup_Impl(
        kReserveId_NormalSlot0,
        titleLit, titleHash,
        bodyLit,  bodyHash);
}


bool Show_MbDvcAnnouncePopupReward(const char* title, const char* body)
{
    // Slot 2 — simplest Server template, no format args.
    constexpr std::uint8_t kServerSlot = 2;
    return Show_MbDvcAnnouncePopup_Impl(
        kServerSlot,
        title ? title : "", 0,
        body ? body : "",  0);
}


bool Show_MbDvcAnnouncePopupRewardLangId(const char* titleLabel,
                                         const char* bodyLabel)
{
    constexpr std::uint8_t kServerSlot = 2;

    // Empty labels skip lookup.
    const char*  titleLit  = nullptr;
    std::uint64_t titleHash = 0;
    if (titleLabel && *titleLabel)
        titleHash = FoxHashes::StrCode64(titleLabel);
    else
        titleLit = "";

    const char*  bodyLit  = nullptr;
    std::uint64_t bodyHash = 0;
    if (bodyLabel && *bodyLabel)
        bodyHash = FoxHashes::StrCode64(bodyLabel);
    else
        bodyLit = "";

    return Show_MbDvcAnnouncePopup_Impl(
        kServerSlot,
        titleLit, titleHash,
        bodyLit,  bodyHash);
}


// Shared resolver for other modules.
const char* MbDvcCustom_TryResolveLangText(std::uint64_t hash)
{
    void* lang = g_LangManager.load(std::memory_order_relaxed);
    if (!lang)
        return nullptr;
    return SafeResolveLangText(lang, hash);
}


