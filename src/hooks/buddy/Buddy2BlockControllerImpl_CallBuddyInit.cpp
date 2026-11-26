#include <pch.h>

#include "Buddy2BlockControllerImpl_CallBuddyInit.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "BattleGearBuddy.h"
#include "HookUtils.h"
#include "log.h"

#ifdef _DEBUG
namespace
{
    using CallBuddyInit_t   = bool(__fastcall*)(void* controller, std::uint8_t buddyType, std::uint8_t mode, bool force);
    using GetBuddyService_t = std::uint8_t*(__fastcall*)();

    constexpr std::size_t kControllerPaths      = 0x38;    // fox::Path per slot: main, then sub slots 1-5
    constexpr std::size_t kControllerGroups     = 0x68;    // block group per slot, same order
    constexpr std::size_t kControllerFlags      = 0x128;
    constexpr std::size_t kControllerType       = 0x174;
    constexpr std::size_t kControllerLoadedType = 0x175;
    constexpr std::size_t kControllerState      = 0x17F;
    constexpr std::size_t kControllerSlotStates = 0x182;   // sub slots 1-5, any nonzero refuses a call
    constexpr std::size_t kControllerSlotFlags  = 0x188;
    constexpr std::size_t kControllerMode       = 0x18D;
    constexpr std::size_t kServiceDisabledTypes = 0x16E;   // one bit per buddy type
    constexpr std::size_t kGroupBlockCount      = 0x1C;
    constexpr std::size_t kGroupBlocks          = 0x48;
    constexpr std::size_t kBlockStatus          = 0x80;    // 0 empty, 3 loaded
    constexpr int         kSubSlots             = 5;
    constexpr int         kSlots                = 6;
    constexpr int         kLoggedCalls          = 40;
    constexpr int         kLoggedStates         = 80;
    constexpr ULONGLONG   kWatchMs              = 30000;
    constexpr ULONGLONG   kWatchStepMs          = 50;

    const std::uint8_t kCallBuddyInitEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48,
        0x89, 0x7C, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83
    };

    struct BlockState
    {
        std::uint8_t  state;
        std::uint8_t  type;
        std::uint8_t  loadedType;
        std::uint8_t  mode;
        std::uint32_t flags;
        std::uint8_t  slotState[kSubSlots];
        std::uint8_t  slotFlag[kSubSlots];
        std::uint64_t path[kSlots];
        int           status[kSlots];
        int           disabledTypes;
    };

    CallBuddyInit_t        g_Orig = nullptr;
    GetBuddyService_t      g_GetBuddyService = nullptr;
    void*                  g_Target = nullptr;
    std::atomic<int>       g_Logged{ 0 };
    std::atomic<int>       g_StatesLogged{ 0 };
    std::atomic<void*>     g_Watched{ nullptr };
    std::atomic<ULONGLONG> g_WatchUntil{ 0 };
    ULONGLONG              g_NextLook = 0;
    BlockState             g_Last = {};

    int BlockStatus(const std::uint8_t* group)
    {
        if (!group)
            return -1;
        if (*reinterpret_cast<const std::uint32_t*>(group + kGroupBlockCount) == 0)
            return -2;
        const auto* block = *reinterpret_cast<const std::uint8_t* const*>(group + kGroupBlocks);
        return block ? *reinterpret_cast<const int*>(block + kBlockStatus) : -3;
    }

    bool ReadBlockStateSEH(const std::uint8_t* controller, BlockState& s)
    {
        __try
        {
            s.state = controller[kControllerState];
            s.type = controller[kControllerType];
            s.loadedType = controller[kControllerLoadedType];
            s.mode = controller[kControllerMode];
            s.flags = *reinterpret_cast<const std::uint32_t*>(controller + kControllerFlags);
            for (int i = 0; i < kSubSlots; ++i)
            {
                s.slotState[i] = controller[kControllerSlotStates + i];
                s.slotFlag[i] = controller[kControllerSlotFlags + i];
            }
            for (int i = 0; i < kSlots; ++i)
            {
                s.path[i] = *reinterpret_cast<const std::uint64_t*>(controller + kControllerPaths + i * 8);
                s.status[i] = BlockStatus(*reinterpret_cast<const std::uint8_t* const*>(controller + kControllerGroups + i * 8));
            }
            s.disabledTypes = -1;
            if (g_GetBuddyService)
                if (const std::uint8_t* service = g_GetBuddyService())
                    s.disabledTypes = service[kServiceDisabledTypes];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool __fastcall hkCallBuddyInit(void* controller, std::uint8_t buddyType, std::uint8_t mode, bool force)
    {
        if (!controller)
            return g_Orig(controller, buddyType, mode, force);
        BlockState before;
        std::memset(&before, 0, sizeof(before));
        ReadBlockStateSEH(static_cast<const std::uint8_t*>(controller), before);
        const bool started = g_Orig(controller, buddyType, mode, force);
        g_Watched.store(controller, std::memory_order_release);
        g_WatchUntil.store(GetTickCount64() + kWatchMs, std::memory_order_relaxed);
        if (g_Logged.fetch_add(1, std::memory_order_relaxed) < kLoggedCalls)
            LogDebug("BattleGear: CallBuddyInit for buddy type %u (mode %u, force %d) -> %d; state %u, slots %u %u %u %u %u, "
                     "loaded type %u, disabled types 0x%02X",
                buddyType, mode, force ? 1 : 0, started ? 1 : 0, before.state, before.slotState[0], before.slotState[1],
                before.slotState[2], before.slotState[3], before.slotState[4], before.loadedType, before.disabledTypes);
        return started;
    }
}
#endif

void LogBuddyBlockStateWhileWatched()
{
#ifdef _DEBUG
    void* controller = g_Watched.load(std::memory_order_acquire);
    const ULONGLONG now = GetTickCount64();
    if (!controller || now > g_WatchUntil.load(std::memory_order_relaxed) || now < g_NextLook)
        return;
    g_NextLook = now + kWatchStepMs;
    BlockState s;
    std::memset(&s, 0, sizeof(s));
    if (!ReadBlockStateSEH(static_cast<const std::uint8_t*>(controller), s) || std::memcmp(&s, &g_Last, sizeof(s)) == 0)
        return;
    g_Last = s;
    if (g_StatesLogged.fetch_add(1, std::memory_order_relaxed) >= kLoggedStates)
        return;
    LogDebug("BattleGear: buddy block state %u (type %u, loaded %u, mode %u, flags 0x%08X) main %016llX/%d | "
             "slot1 %u/%u %016llX/%d | slot2 %u/%u %016llX/%d | slot3 %u/%u %016llX/%d | slot4 %u/%u %016llX/%d | "
             "slot5 %u/%u %016llX/%d",   // per slot: state/flag path/block status (-1 no group, 0 empty, 3 loaded)
        s.state, s.type, s.loadedType, s.mode, s.flags, s.path[0], s.status[0],
        s.slotState[0], s.slotFlag[0], s.path[1], s.status[1], s.slotState[1], s.slotFlag[1], s.path[2], s.status[2],
        s.slotState[2], s.slotFlag[2], s.path[3], s.status[3], s.slotState[3], s.slotFlag[3], s.path[4], s.status[4],
        s.slotState[4], s.slotFlag[4], s.path[5], s.status[5]);
#endif
}

bool Install_Buddy2BlockControllerImpl_CallBuddyInit_Hook()
{
#ifndef _DEBUG
    return true;
#else
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.Buddy2BlockControllerImpl_CallBuddyInit);
    if (!target)
        return false;
    g_GetBuddyService = reinterpret_cast<GetBuddyService_t>(ResolveGameAddress(gAddr.BuddyServiceImpl_GetBuddyService));
    const HookEntry state = CheckHookEntry(target, kCallBuddyInitEntry, sizeof(kCallBuddyInitEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogWarn("BattleGear: CallBuddyInit code differs on this build (%s), buddy call probe off", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: CallBuddyInit", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkCallBuddyInit), reinterpret_cast<void**>(&g_Orig)))
    {
        LogWarn("BattleGear: CallBuddyInit hook failed, buddy call probe off");
        return false;
    }
    g_Target = target;
    return true;
#endif
}

void Uninstall_Buddy2BlockControllerImpl_CallBuddyInit_Hook()
{
#ifdef _DEBUG
    if (!g_Target)
        return;
    g_Watched.store(nullptr, std::memory_order_release);
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
#endif
}
