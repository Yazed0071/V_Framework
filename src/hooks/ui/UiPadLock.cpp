#include "pch.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "UiPadLock.h"
#include "UixUtilityControl.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    constexpr std::size_t kDevIsButtonPress       = 0x028;
    constexpr std::size_t kDevIsButtonPressByName = 0x030;
    constexpr std::size_t kDevResetDisablePad     = 0x0A0;
    constexpr std::size_t kDevIsMouseButtonDownByName = 0x178;
    constexpr std::size_t kDevGetMouseWheel         = 0x168;
    constexpr std::size_t kDevIsMouseButtonDown     = 0x170;   // the by-name mouse reads call these three through the vtable
    constexpr std::size_t kDevIsMouseButtonPressing = 0x180;
    constexpr std::size_t kDevIsMouseButtonUp       = 0x190;

    constexpr std::size_t kPadRepeat      = 0x038;
    constexpr std::size_t kPadPress       = 0x058;
    constexpr std::size_t kPadRelease     = 0x080;
    constexpr std::size_t kPadLongPress   = 0x0A8;   // gives back 0 idle, 1 long, 2 held, 3 let go early
    constexpr std::size_t kPadStickAxis   = 0x0C8;
    constexpr std::size_t kPadIsDisableRequested = 0x120;
    constexpr std::size_t kDaemonPad      = 0xA0;

    constexpr std::int16_t kFuncterStickAxisCode[] = {   // UiPadFuncter::GetStickAxis, -1 is any byte
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
        0x8B, 0x01, 0x41, 0x8B, 0xD9, 0x41, 0x8B, 0xF8, 0x8B, 0xF2, 0xFF, 0x90, 0x20, 0x01, 0x00, 0x00,
        0x84, 0xC0, 0x74, 0x13, 0x0F, 0x57, 0xC0, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x8B, 0x74, 0x24,
        0x38, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3, 0x44, 0x8B, 0xC3, 0x8B, 0xD7, 0x8B, 0xCE, 0x48, 0x8B,
        0x5C, 0x24, 0x30, 0x48, 0x8B, 0x74, 0x24, 0x38, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xE9, -1, -1,
        -1, -1,
    };
    constexpr std::size_t kFuncterStickJump = 0x4D;   // the jump into fox::gk::Pad::GetStickAxis
    constexpr std::int16_t kGkStickAxisCode[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
        0x48, 0x83, 0xEC, 0x20, 0x41, 0x8B, 0xF8, 0x8B, 0xF2, 0x8B, 0xD9, 0xE8, -1, -1, -1, -1,
        0x8B, 0xCB, 0x48, 0x8B, 0xE8, 0xE8, -1, -1, -1, -1, 0x83, 0xF8, 0xFF, 0x75, 0x05, 0x0F,
        0x57, 0xC0, 0xEB, 0x16, 0x8B, 0xC8, 0x48, 0x6B, 0xC9, 0x64, 0x48, 0x03, 0xCE, 0x48, 0x8D, 0x0C,
        0x4F, 0xF3, 0x0F, 0x10, 0x84, 0x8D, 0x90, 0x02, 0x00, 0x00,
    };
    constexpr std::int16_t kBaseStickAxisCode[] = {   // plain UiPadBase::GetStickAxis
        0x40, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0x48,
        0x89, 0x5C, 0x24, 0x58, 0x48, 0x89, 0x74, 0x24, 0x60, 0x0F, 0x29, 0x74, 0x24, 0x30, 0x41, 0x8B,
        0xD9, 0x41, 0x8B, 0xF8, 0x8B, 0xF2, 0x48, 0x8B, 0x01, 0xFF, 0x90, 0x20, 0x01, 0x00, 0x00, 0x84,
        0xC0, 0x74, 0x05, 0x0F, 0x57, 0xC0, 0xEB, 0x2D, 0x48, 0x8D, 0x4C, 0x24, 0x50, 0xE8, -1, -1,
        -1, -1, 0x90, 0x44, 0x8B, 0xCB, 0x44, 0x8B, 0xC7, 0x8B, 0xD6, 0x48, 0x8B, 0x08, 0xE8, -1,
        -1, -1, -1,
    };
    constexpr std::size_t kBaseStickCall = 0x4E;   // the call into Daemon::GetAxis
    constexpr std::int16_t kDaemonGetAxisCode[] = {
        0x44, 0x8B, 0xD2, 0x41, 0x8B, 0xC0, 0x4D, 0x6B, 0xD2, 0x42, 0x4C, 0x03, 0xD0, 0x41, 0x8B, 0xC1,
        0x4A, 0x8D, 0x14, 0x50, 0xF3, 0x0F, 0x10, 0x84, 0x91, 0xD8, 0x02, 0x00, 0x00, 0xC3,
    };

    constexpr std::size_t kMaxOwnRequests = 4;   // the pad keeps 4 requests at most

    constexpr std::int16_t kIsButtonPressCode[] = {   // UixUtilityImpl::IsButtonPress
        0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF,
        0x48, 0x89, 0x5C, 0x24, 0x40, 0x48, 0x89, 0x74, 0x24, 0x48, 0x8B, 0xF2, 0x48, 0x8D, 0x4C, 0x24, 0x50,
        0xE8, -1, -1, -1, -1,                                         // +0x20 calls Daemon::GetInstance
        0x90, 0x48, 0x8B, 0x00, 0x48, 0x8B, 0xB8, 0xA0, 0x00, 0x00, 0x00, 0x33, 0xD2, 0x48, 0x8D, 0x4C, 0x24, 0x50,
        0xE8, -1, -1, -1, -1,                                         // +0x37 calls EntityPtrBase::Set
        0x48, 0x8B, 0x1F,
        0xE8, -1, -1, -1, -1,                                         // +0x3F calls the UI pad index getter
        0x8B, 0xD0, 0x45, 0x33, 0xC9, 0x44, 0x8B, 0xC6, 0x48, 0x8B, 0xCF, 0xFF, 0x53, 0x58,
    };
    constexpr std::size_t kCallGetInstance = 0x20;
    constexpr std::size_t kCallEntitySet   = 0x37;
    constexpr std::size_t kCallPadIndex    = 0x3F;

    constexpr std::uint8_t kPadIndexCode[] = { 0x8B, 0x05 };   // mov eax, [rip+x] ; ret
    constexpr std::uint8_t kIsDisableRequestedCode[] = { 0x33, 0xC0, 0x39, 0x41, 0x18, 0x0F, 0x97, 0xC0, 0xC3 };   // true when the request count at +0x18 is above 0

    using GetInstance_t = void** (__fastcall*)(void** out);
    using EntitySet_t   = void (__fastcall*)(void** ptr, void* value);
    using PadIndex_t    = std::uint32_t (__fastcall*)();
    using PressByName_t = bool (__fastcall*)(void** util, std::uint64_t name);
    using ResetDisable_t = void (__fastcall*)(void** util, std::uint32_t id);
    using PadButton_t   = bool (__fastcall*)(void* pad, std::uint32_t index, std::uint32_t name, bool force);
    using PadRepeat_t   = bool (__fastcall*)(void* pad, std::uint32_t index, std::uint32_t name, double first, double interval, bool force);
    using PadLong_t     = int (__fastcall*)(void* pad, std::uint32_t index, std::uint32_t name, double seconds, bool force);
    using MouseButton_t = bool (__fastcall*)(void** util, std::uint32_t code);
    using MouseWheel_t  = float (__fastcall*)(void** util);
    using GkStickAxis_t = float (__fastcall*)(std::uint32_t index, std::uint32_t stick, std::uint32_t axis);
    using DaemonAxis_t  = float (__fastcall*)(void* daemon, std::uint32_t index, std::uint32_t stick, std::uint32_t axis);

    std::mutex     g_ResolveLock;
    bool           g_Resolved = false;
    bool           g_Failed = false;
    GetInstance_t  g_GetInstance = nullptr;
    EntitySet_t    g_EntitySet = nullptr;
    PadIndex_t     g_PadIndex = nullptr;

    void*          g_StickSlotFn = nullptr;
    GkStickAxis_t  g_GkStickAxis = nullptr;
    DaemonAxis_t   g_DaemonAxis = nullptr;
    bool           g_StickWarned = false;

    void**         g_PressByNameSlot = nullptr;
    PressByName_t  g_OrigPressByName = nullptr;
    bool           g_PressByNameSwapped = false;
    bool           g_PressByNameFailed = false;
    std::atomic<uipad::GameButtonFilter> g_ButtonFilter{ nullptr };

    void**         g_MouseDownSlot = nullptr;
    PressByName_t  g_OrigMouseDown = nullptr;
    bool           g_MouseDownSwapped = false;
    bool           g_MouseDownFailed = false;
    std::atomic<uipad::GameButtonFilter> g_MouseFilter{ nullptr };

    // V_UixUtility.SetDisablePad holds the mouse too: the game's disable request only reaches the pad and keyboard reads,
    // its mouse reads go to fox::MouseListener directly
    void**         g_MouseButtonDownSlot = nullptr;
    void**         g_MouseButtonPressingSlot = nullptr;
    void**         g_MouseButtonUpSlot = nullptr;
    void**         g_MouseWheelSlot = nullptr;
    MouseButton_t  g_OrigMouseButtonDown = nullptr;
    MouseButton_t  g_OrigMouseButtonPressing = nullptr;
    MouseButton_t  g_OrigMouseButtonUp = nullptr;
    MouseWheel_t   g_OrigMouseWheel = nullptr;
    bool           g_HeldMouseSwapped = false;

    std::mutex     g_OwnLock;
    std::uint32_t  g_OwnRequests[kMaxOwnRequests] = {};
    std::size_t    g_OwnCount = 0;
    std::atomic<bool> g_HoldsOwn{ false };

    thread_local int t_OwnQuery = 0;

    void* CallTarget(const std::uint8_t* call)
    {
        std::int32_t rel = 0;
        std::memcpy(&rel, call + 1, sizeof(rel));
        return const_cast<std::uint8_t*>(call + 5 + rel);
    }

    bool MatchesSEH(const std::uint8_t* code, const std::int16_t* pattern, std::size_t count)
    {
        __try
        {
            for (std::size_t i = 0; i < count; ++i)
                if (pattern[i] >= 0 && code[i] != static_cast<std::uint8_t>(pattern[i]))
                    return false;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool BytesSEH(const void* code, const std::uint8_t* expect, std::size_t count)
    {
        __try
        {
            return std::memcmp(code, expect, count) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool UtilityIsLive()
    {
        void* press = UixUtilityControl::Method(kDevIsButtonPress);
        void* byName = UixUtilityControl::Method(kDevIsButtonPressByName);
        return press && byName && press != byName;
    }

    bool Resolve()
    {
        std::lock_guard<std::mutex> lock(g_ResolveLock);
        if (g_Resolved || g_Failed)
            return g_Resolved;
        if (!UixUtilityControl::IsSupportedOnThisBuild() || !UtilityIsLive())
            return false;

        const auto* code = static_cast<const std::uint8_t*>(UixUtilityControl::Method(kDevIsButtonPress));
        if (!MatchesSEH(code, kIsButtonPressCode, sizeof(kIsButtonPressCode) / sizeof(kIsButtonPressCode[0])))
        {
            g_Failed = true;
            LogWarn("V_UixUtility: IsButtonPress at %p is not the known code, reads past SetDisablePad off", code);
            return false;
        }
        void* padIndex = CallTarget(code + kCallPadIndex);
        if (!BytesSEH(padIndex, kPadIndexCode, sizeof(kPadIndexCode)))
        {
            g_Failed = true;
            LogWarn("V_UixUtility: UI pad index getter at %p is not the known code, reads past SetDisablePad off", padIndex);
            return false;
        }
        g_GetInstance = reinterpret_cast<GetInstance_t>(CallTarget(code + kCallGetInstance));
        g_EntitySet = reinterpret_cast<EntitySet_t>(CallTarget(code + kCallEntitySet));
        g_PadIndex = reinterpret_cast<PadIndex_t>(padIndex);
        g_Resolved = true;
        return true;
    }

    void* PadSEH()
    {
        void* tmp = nullptr;
        void* pad = nullptr;
        __try
        {
            void** held = g_GetInstance(&tmp);
            void* daemon = held ? *held : nullptr;
            if (daemon)
                pad = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(daemon) + kDaemonPad);
            g_EntitySet(&tmp, nullptr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            pad = nullptr;
        }
        return pad;
    }

    void* Slot(void* pad, std::size_t offset)
    {
        return (*reinterpret_cast<void***>(pad))[offset / sizeof(void*)];
    }

    bool PadLooksRight(void* pad)
    {
        __try
        {
            return BytesSEH(Slot(pad, kPadIsDisableRequested), kIsDisableRequestedCode, sizeof(kIsDisableRequestedCode));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool __fastcall hkIsButtonPressByName(void** util, std::uint64_t name)
    {
        const uipad::GameButtonFilter filter = g_ButtonFilter.load(std::memory_order_acquire);
        if (filter && t_OwnQuery == 0 && filter(static_cast<std::uint32_t>(name)))
            return false;
        return g_OrigPressByName(util, name);
    }

    bool EnsurePressHook()
    {
        std::lock_guard<std::mutex> lock(g_ResolveLock);
        if (g_PressByNameSwapped || g_PressByNameFailed)
            return g_PressByNameSwapped;
        if (!UtilityIsLive())
            return false;
        void** util = UixUtilityControl::Utility();
        void** vtbl = util ? *reinterpret_cast<void***>(util) : nullptr;
        g_PressByNameSlot = vtbl ? &vtbl[kDevIsButtonPressByName / sizeof(void*)] : nullptr;
        g_PressByNameSwapped = g_PressByNameSlot && SwapVtableSlot(g_PressByNameSlot,
            reinterpret_cast<void*>(&hkIsButtonPressByName), reinterpret_cast<void**>(&g_OrigPressByName));
        if (!g_PressByNameSwapped)
        {
            g_PressByNameFailed = true;
            LogWarn("V_UixUtility: IsButtonPressByName slot not swapped, game button filter off");
        }
        return g_PressByNameSwapped;
    }

    bool __fastcall hkIsMouseButtonDownByName(void** util, std::uint64_t name)
    {
        const uipad::GameButtonFilter filter = g_MouseFilter.load(std::memory_order_acquire);
        if (filter && t_OwnQuery == 0 && filter(static_cast<std::uint32_t>(name)))
            return false;
        return g_OrigMouseDown(util, name);
    }

    bool EnsureMouseHook()
    {
        std::lock_guard<std::mutex> lock(g_ResolveLock);
        if (g_MouseDownSwapped || g_MouseDownFailed)
            return g_MouseDownSwapped;
        if (!UtilityIsLive())
            return false;
        g_MouseDownSlot = UixUtilityControl::MethodSlot(kDevIsMouseButtonDownByName);
        g_MouseDownSwapped = g_MouseDownSlot && SwapVtableSlot(g_MouseDownSlot,
            reinterpret_cast<void*>(&hkIsMouseButtonDownByName), reinterpret_cast<void**>(&g_OrigMouseDown));
        if (!g_MouseDownSwapped)
        {
            g_MouseDownFailed = true;
            LogWarn("V_UixUtility: IsMouseButtonDownByName slot not swapped, game mouse filter off");
        }
        return g_MouseDownSwapped;
    }

    // while a Lua mod holds a disable request, the game's mouse reads see nothing; V_UixUtility's own reads pass
    bool GameMouseHeld()
    {
        return t_OwnQuery == 0 && g_HoldsOwn.load(std::memory_order_acquire);
    }

    bool __fastcall hkIsMouseButtonDown(void** util, std::uint32_t code)
    {
        return !GameMouseHeld() && g_OrigMouseButtonDown(util, code);
    }

    bool __fastcall hkIsMouseButtonPressing(void** util, std::uint32_t code)
    {
        return !GameMouseHeld() && g_OrigMouseButtonPressing(util, code);
    }

    bool __fastcall hkIsMouseButtonUp(void** util, std::uint32_t code)
    {
        return !GameMouseHeld() && g_OrigMouseButtonUp(util, code);
    }

    float __fastcall hkGetMouseWheel(void** util)
    {
        return GameMouseHeld() ? 0.0f : g_OrigMouseWheel(util);
    }

    void SwapHeldMouseSlot(std::size_t devOffset, void* hook, void** original, void**& slot)
    {
        slot = UixUtilityControl::MethodSlot(devOffset);
        if (!slot || !SwapVtableSlot(slot, hook, original))
        {
            slot = nullptr;
            LogWarn("V_UixUtility: mouse read at dev slot 0x%zX not swapped, SetDisablePad does not hold it", devOffset);
        }
    }

    void EnsureHeldMouseHooks()
    {
        std::lock_guard<std::mutex> lock(g_ResolveLock);
        if (g_HeldMouseSwapped || !UtilityIsLive())
            return;
        g_HeldMouseSwapped = true;
        SwapHeldMouseSlot(kDevIsMouseButtonDown, reinterpret_cast<void*>(&hkIsMouseButtonDown),
            reinterpret_cast<void**>(&g_OrigMouseButtonDown), g_MouseButtonDownSlot);
        SwapHeldMouseSlot(kDevIsMouseButtonPressing, reinterpret_cast<void*>(&hkIsMouseButtonPressing),
            reinterpret_cast<void**>(&g_OrigMouseButtonPressing), g_MouseButtonPressingSlot);
        SwapHeldMouseSlot(kDevIsMouseButtonUp, reinterpret_cast<void*>(&hkIsMouseButtonUp),
            reinterpret_cast<void**>(&g_OrigMouseButtonUp), g_MouseButtonUpSlot);
        SwapHeldMouseSlot(kDevGetMouseWheel, reinterpret_cast<void*>(&hkGetMouseWheel),
            reinterpret_cast<void**>(&g_OrigMouseWheel), g_MouseWheelSlot);
    }

    void* DaemonSEH()
    {
        void* tmp = nullptr;
        void* daemon = nullptr;
        __try
        {
            void** held = g_GetInstance(&tmp);
            daemon = held ? *held : nullptr;
            g_EntitySet(&tmp, nullptr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            daemon = nullptr;
        }
        return daemon;
    }

    void* StickSlotSEH(void* pad)
    {
        __try { return Slot(pad, kPadStickAxis); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    bool ResolveStick(void* pad)
    {
        void* fn = StickSlotSEH(pad);
        if (!fn)
            return false;
        std::lock_guard<std::mutex> lock(g_ResolveLock);
        if (fn == g_StickSlotFn)
            return g_GkStickAxis || g_DaemonAxis;
        g_StickSlotFn = fn;
        g_GkStickAxis = nullptr;
        g_DaemonAxis = nullptr;
        const auto* code = static_cast<const std::uint8_t*>(fn);
        if (MatchesSEH(code, kFuncterStickAxisCode, sizeof(kFuncterStickAxisCode) / sizeof(kFuncterStickAxisCode[0])))
        {
            void* target = CallTarget(code + kFuncterStickJump);
            if (MatchesSEH(static_cast<const std::uint8_t*>(target), kGkStickAxisCode, sizeof(kGkStickAxisCode) / sizeof(kGkStickAxisCode[0])))
                g_GkStickAxis = reinterpret_cast<GkStickAxis_t>(target);
        }
        else if (MatchesSEH(code, kBaseStickAxisCode, sizeof(kBaseStickAxisCode) / sizeof(kBaseStickAxisCode[0])))
        {
            void* target = CallTarget(code + kBaseStickCall);
            if (MatchesSEH(static_cast<const std::uint8_t*>(target), kDaemonGetAxisCode, sizeof(kDaemonGetAxisCode) / sizeof(kDaemonGetAxisCode[0])))
                g_DaemonAxis = reinterpret_cast<DaemonAxis_t>(target);
        }
        if (!g_GkStickAxis && !g_DaemonAxis && !g_StickWarned)
        {
            g_StickWarned = true;
            LogWarn("V_UixUtility: UI pad GetStickAxis at %p is not a known code, the stick is not read past SetDisablePad", fn);
        }
        return g_GkStickAxis || g_DaemonAxis;
    }

    template <typename F>
    bool PadQuery(F&& query)
    {
        if (!Resolve())
            return false;
        void* pad = PadSEH();
        if (!pad || !PadLooksRight(pad))
            return false;
        bool ok = false;
        auto run = [&]() { query(pad, g_PadIndex()); ok = true; };
        UixUtilityControl::Guarded(run);
        return ok;
    }

    void ResetDisableSEH(void* fn, void** util, std::uint32_t id)
    {
        __try
        {
            reinterpret_cast<ResetDisable_t>(fn)(util, id);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

namespace uipad
{
    OwnQuery::OwnQuery() { ++t_OwnQuery; }
    OwnQuery::~OwnQuery() { --t_OwnQuery; }

    void NoteOwnDisableRequest(std::uint32_t id, bool held)
    {
        {
            std::lock_guard<std::mutex> lock(g_OwnLock);
            std::size_t at = g_OwnCount;
            for (std::size_t i = 0; i < g_OwnCount; ++i)
                if (g_OwnRequests[i] == id)
                    at = i;
            if (held && at == g_OwnCount && g_OwnCount < kMaxOwnRequests)
                g_OwnRequests[g_OwnCount++] = id;
            else if (!held && at < g_OwnCount)
                g_OwnRequests[at] = g_OwnRequests[--g_OwnCount];
            g_HoldsOwn.store(g_OwnCount > 0, std::memory_order_release);
        }
        if (held)
            EnsureHeldMouseHooks();
    }

    bool HoldsOwnDisableRequest()
    {
        return g_HoldsOwn.load(std::memory_order_acquire);
    }

    void ReleaseOwnDisableRequests()
    {
        std::uint32_t ids[kMaxOwnRequests] = {};
        std::size_t count = 0;
        {
            std::lock_guard<std::mutex> lock(g_OwnLock);
            count = g_OwnCount;
            std::memcpy(ids, g_OwnRequests, sizeof(ids));
            g_OwnCount = 0;
            g_HoldsOwn.store(false, std::memory_order_release);
        }
        if (count == 0)
            return;
        void** util = UixUtilityControl::Utility();
        void* reset = UixUtilityControl::Method(kDevResetDisablePad);
        if (!util || !reset)
            return;
        for (std::size_t i = 0; i < count; ++i)
            ResetDisableSEH(reset, util, ids[i]);
        LogInfo("V_UixUtility: %zu disable pad request(s) a Lua mod left set were reset", count);
    }

    bool SetGameButtonFilter(GameButtonFilter filter)
    {
        if (filter && !EnsurePressHook())
            return false;
        g_ButtonFilter.store(filter, std::memory_order_release);
        return true;
    }

    bool SetGameMouseFilter(GameButtonFilter filter)
    {
        if (filter && !EnsureMouseHook())
            return false;
        g_MouseFilter.store(filter, std::memory_order_release);
        return true;
    }

    bool Press(std::uint32_t name, bool& out)
    {
        return PadQuery([&](void* pad, std::uint32_t index) {
            out = reinterpret_cast<PadButton_t>(Slot(pad, kPadPress))(pad, index, name, true);
        });
    }

    bool Pressing(std::uint32_t name, bool& out)
    {
        return PadQuery([&](void* pad, std::uint32_t index) {
            out = reinterpret_cast<PadLong_t>(Slot(pad, kPadLongPress))(pad, index, name, 0.0, true) != 0;
        });
    }

    bool StickAxis(std::uint32_t stick, float& x, float& y)
    {
        x = y = 0.0f;
        bool read = false;
        const bool ok = PadQuery([&](void* pad, std::uint32_t index) {
            if (!ResolveStick(pad))
                return;
            if (g_GkStickAxis)
            {
                x = g_GkStickAxis(index, stick, 0);
                y = g_GkStickAxis(index, stick, 1);
                read = true;
            }
            else if (void* daemon = DaemonSEH())
            {
                x = g_DaemonAxis(daemon, index, stick, 0);
                y = g_DaemonAxis(daemon, index, stick, 1);
                read = true;
            }
        });
        return ok && read;
    }

    bool Release(std::uint32_t name, bool& out)
    {
        return PadQuery([&](void* pad, std::uint32_t index) {
            out = reinterpret_cast<PadButton_t>(Slot(pad, kPadRelease))(pad, index, name, true);
        });
    }

    bool Repeat(std::uint32_t name, float first, float interval, bool& out)
    {
        return PadQuery([&](void* pad, std::uint32_t index) {
            out = reinterpret_cast<PadRepeat_t>(Slot(pad, kPadRepeat))(pad, index, name, first, interval, true);
        });
    }

    bool LongPressResult(std::uint32_t name, float seconds, int& out)
    {
        return PadQuery([&](void* pad, std::uint32_t index) {
            out = reinterpret_cast<PadLong_t>(Slot(pad, kPadLongPress))(pad, index, name, seconds, true);
        });
    }

    void Uninstall()
    {
        ReleaseOwnDisableRequests();
        g_ButtonFilter.store(nullptr);
        g_MouseFilter.store(nullptr);
        std::lock_guard<std::mutex> lock(g_ResolveLock);
        if (g_PressByNameSwapped)
        {
            RestoreVtableSlot(g_PressByNameSlot, reinterpret_cast<void*>(g_OrigPressByName));
            g_PressByNameSwapped = false;
            g_OrigPressByName = nullptr;
        }
        if (g_MouseDownSwapped)
        {
            RestoreVtableSlot(g_MouseDownSlot, reinterpret_cast<void*>(g_OrigMouseDown));
            g_MouseDownSwapped = false;
            g_OrigMouseDown = nullptr;
        }
        if (g_HeldMouseSwapped)
        {
            RestoreVtableSlot(g_MouseButtonDownSlot, reinterpret_cast<void*>(g_OrigMouseButtonDown));
            RestoreVtableSlot(g_MouseButtonPressingSlot, reinterpret_cast<void*>(g_OrigMouseButtonPressing));
            RestoreVtableSlot(g_MouseButtonUpSlot, reinterpret_cast<void*>(g_OrigMouseButtonUp));
            RestoreVtableSlot(g_MouseWheelSlot, reinterpret_cast<void*>(g_OrigMouseWheel));
            g_MouseButtonDownSlot = g_MouseButtonPressingSlot = g_MouseButtonUpSlot = g_MouseWheelSlot = nullptr;
            g_HeldMouseSwapped = false;
        }
    }
}
