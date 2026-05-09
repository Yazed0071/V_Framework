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


    using UpdateAnnounceNormal_t = std::int32_t (__fastcall*)(void* self);
    using ReserveAnnouncePopup_t = void (__fastcall*)(void* ctrl, const void* param);
    using GetLangText_t          = const char* (__fastcall*)(void* langMgr, std::uint64_t hash);


    static UpdateAnnounceNormal_t g_OrigUpdateAnnounceNormal = nullptr;
    static ReserveAnnouncePopup_t g_OrigReserveAnnouncePopup = nullptr;
    static void*                  g_ReserveHookTarget = nullptr;
    static std::once_flag         g_ReserveHookInstallFlag;


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
        bool            reserved = false;
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
    static bool SafeVerifyOurReservationLanded(void* ctrl)
    {
        if (!ctrl)
            return false;

        __try
        {
            for (std::size_t i = 0; i < kCtrl_NumSlots; ++i)
            {
                const std::uint8_t* slot = SlotPtr(ctrl, i);
                if (*(slot + kSlot_ReserveIdOffset) == kReserveId_NormalSlot0)
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


    // Reserve a slot 0 with magic.
    static bool SafeReserveOurSlot(void* ctrl)
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
            p.commonValue2 = 0;
            p.commonValue3 = 0;
            p.commonValue4 = 0;
            p.reserveId    = kReserveId_NormalSlot0;

            reserveFn(ctrl, &p);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
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


    // Reserve any queued unreserved entries.
    static void DrainUnreservedReservations(void* ctrl)
    {
        if (!ctrl)
            return;

        for (;;)
        {
            // Find unreserved candidate.
            bool haveCandidate = false;
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                for (auto& entry : g_PendingQueue)
                {
                    if (!entry.reserved)
                    {
                        haveCandidate = true;
                        break;
                    }
                }
            }
            if (!haveCandidate)
                return;

            const bool ok = SafeReserveOurSlot(ctrl);

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


    static std::int32_t __fastcall hk_UpdateAnnounceNormal(void* self)
    {
        if (!self || !g_OrigUpdateAnnounceNormal)
        {
            return g_OrigUpdateAnnounceNormal ? g_OrigUpdateAnnounceNormal(self) : 0;
        }

        const std::uint32_t prevState = SafeReadInnerState(self);

        // Pre-call: capture pointers, drain queue.
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

        // Peek slot only when state is not 1.
        bool nextSlotIsOurs = false;
        if (ctrl && prevState != 1)
        {
            (void)SafeFindFirstSlotIsOurs(
                ctrl, kReserveId_NormalSlot0, &nextSlotIsOurs);
        }

        const std::int32_t result = g_OrigUpdateAnnounceNormal(self);

        const std::uint32_t currState = SafeReadInnerState(self);

        // Override on transitions into state 1.
        if (nextSlotIsOurs && prevState != 1 && currState == 1)
        {
            // Pop first reserved; write outside lock.
            bool            havePopped = false;
            PopupTextSource title;
            PopupTextSource body;
            {
                std::lock_guard<std::mutex> lock(g_PendingMutex);
                for (auto it = g_PendingQueue.begin(); it != g_PendingQueue.end(); ++it)
                {
                    if (it->reserved)
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
                // Resolve hashes; null fallbacks to "".
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
}  // namespace


bool Install_MbDvcCustomPopup_Hook()
{
    void* target = ResolveGameAddress(
        gAddr.MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceNormal);
    if (!target)
    {
        Log("[Hook] MbDvcCustomPopup: target resolve failed (addr=%llX)\n",
            static_cast<unsigned long long>(
                gAddr.MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceNormal));
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hk_UpdateAnnounceNormal),
        reinterpret_cast<void**>(&g_OrigUpdateAnnounceNormal));

    Log("[Hook] MbDvcCustomPopup: %s (target=%p, orig=%p) "
        "[reserve hook installs lazily on first popup pipeline tick]\n",
        ok ? "OK" : "FAIL", target, g_OrigUpdateAnnounceNormal);
    return ok;
}


bool Uninstall_MbDvcCustomPopup_Hook()
{
    void* target = ResolveGameAddress(
        gAddr.MbDvcAnnouncePopupCallbackImpl_UpdateAnnounceNormal);
    DisableAndRemoveHook(target);
    g_OrigUpdateAnnounceNormal = nullptr;

    if (g_ReserveHookTarget)
    {
        DisableAndRemoveHook(g_ReserveHookTarget);
        g_ReserveHookTarget = nullptr;
        g_OrigReserveAnnouncePopup = nullptr;
    }

    g_PopupController.store(nullptr, std::memory_order_relaxed);
    g_LangManager.store(nullptr, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        g_PendingQueue.clear();
    }

    Log("[Hook] MbDvcCustomPopup: removed (target=%p)\n", target);
    return true;
}


// Internal: literal-or-hash for both fields.
static bool Show_MbDvcAnnouncePopup_Impl(const char*  titleLiteral,
                                         std::uint64_t titleHash,
                                         const char*  bodyLiteral,
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
        p.title    = std::move(titleSrc);
        p.body     = std::move(bodySrc);
        p.reserved = false;
        g_PendingQueue.push_back(std::move(p));
    }

    // No controller yet — defer reservation.
    if (!ctrl)
    {
        return true;
    }

    // Reserve immediately.
    const bool reserveOk = SafeReserveOurSlot(ctrl);
    if (!reserveOk)
    {
        std::lock_guard<std::mutex> lock(g_PendingMutex);
        if (!g_PendingQueue.empty())
            g_PendingQueue.pop_back();
        Log("[MbDvcCustomPopup] Show: SafeReserveOurSlot raised; entry rolled back\n");
        return false;
    }

    // Verify reservation landed in ring.
    const bool landed = SafeVerifyOurReservationLanded(ctrl);
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


bool Show_MbDvcAnnouncePopup(const char* title, const char* body)
{
    return Show_MbDvcAnnouncePopup_Impl(
        title ? title : "",
        0,
        body ? body : "",
        0);
}


bool Show_MbDvcAnnouncePopupByLangId(const char* titleLabel, const char* bodyLabel)
{
    // Empty labels skip lookup.
    const char*  titleLit  = nullptr;
    std::uint64_t titleHash = 0;
    if (titleLabel && *titleLabel)
    {
        titleHash = FoxHashes::StrCode64(titleLabel);
    }
    else
    {
        titleLit = "";
    }

    const char*  bodyLit  = nullptr;
    std::uint64_t bodyHash = 0;
    if (bodyLabel && *bodyLabel)
    {
        bodyHash = FoxHashes::StrCode64(bodyLabel);
    }
    else
    {
        bodyLit = "";
    }

    return Show_MbDvcAnnouncePopup_Impl(titleLit, titleHash, bodyLit, bodyHash);
}


