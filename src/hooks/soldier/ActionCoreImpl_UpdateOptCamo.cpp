#include <pch.h>

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "GameClock.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "NativeCallPatch.h"
#include "ActionCoreImpl_UpdateOptCamo.h"
#include "AddressSet.h"
#include "../buddy/BuddyCodeCave.h"

namespace
{


    using UpdateOptCamo_t = void(__fastcall*)(void* self, std::uint32_t actorIndex);


    enum class OptCamoForceMode : std::uint32_t
    {
        None = 0,
        ForceOn = 1,
        ForceOff = 2
    };


    enum class ResolveOptCamoStatus : std::uint32_t
    {
        Ok = 0,
        NullSelf,
        NullRoot,
        NullMapOwner,
        NullMapTable,
        InvalidMappedIndex,
        NullConfigList,
        NullConfigBase,
        ExceptionThrown
    };

    static constexpr std::uint32_t kDesiredOptCamoBit = 0x20000000u;

    static constexpr std::uint32_t kSoldierIndexMask = 0x01FFu;

    static UpdateOptCamo_t g_OrigUpdateOptCamo = nullptr;

    struct OptCamoOverride
    {
        OptCamoForceMode mode = OptCamoForceMode::None;
        std::uint64_t    offAtMs = 0;   // 0 = stays on
    };

    static std::mutex g_UpdateOptCamoMutex;
    static std::unordered_map<std::uint32_t, OptCamoOverride> g_MappedIndexModes;
    static OptCamoOverride g_EverySoldierMode;
    static std::atomic<std::uint32_t> g_TimedOverrideCount{ 0 };

    constexpr std::uint32_t kPrepareBusyCheck    = 0x2C;
    constexpr std::uint32_t kPrepareSlotBase     = 0x6D;
    constexpr std::uint32_t kPrepareSlotBound    = 0x165;
    constexpr std::uint32_t kPrepareNextFovaSlot = 0x186;
    constexpr std::uint32_t kPrepareBusySet      = 0x1B0;
    constexpr std::uint32_t kShowFobBit          = 0x24;
    constexpr std::uint32_t kReleaseFobBit       = 0x1A;

    const std::uint8_t kSlotCountSetup[] = {
        0x41, 0xC7, 0x87, 0x84, 0x02, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,   // mov dword [r15+0x284],4: the camo slot count
        0x49, 0x8B, 0x87, 0xF8, 0x00, 0x00, 0x00,                           // mov rax,[r15+0xf8]
        0x8B, 0x88, 0xA4, 0x02, 0x00, 0x00                                  // mov ecx,[rax+0x2a4]
    };
    const std::uint8_t kPreparePrologue[] = {
        0x89, 0x54, 0x24, 0x10, 0x53, 0x55, 0x41, 0x55, 0x48, 0x83, 0xEC, 0x40,
        0x48, 0x8B, 0x81, 0xF8, 0x00, 0x00, 0x00, 0x8B, 0xDA, 0x48, 0x8B, 0xE9,
        0x44, 0x8B, 0xA8, 0xA4, 0x02, 0x00, 0x00
    };
    const std::uint8_t kShowPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20,
        0x48, 0x8B, 0x81, 0xF8, 0x00, 0x00, 0x00, 0x33, 0xDB, 0x48, 0x8B, 0xF1,
        0x44, 0x8B, 0x80, 0xA4, 0x02, 0x00, 0x00
    };
    const std::uint8_t kReleasePrologue[] = {
        0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20,
        0x48, 0x8B, 0x81, 0xF8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF9,
        0x8B, 0xB0, 0xA4, 0x02, 0x00, 0x00
    };

    constexpr std::size_t kOptCamoSlotPatchCount = 7;
    static NativeCodePatch g_OptCamoSlotPatches[kOptCamoSlotPatchCount]{};

    static std::uint64_t g_SoldierActorMap   = 0;
    static std::uint64_t g_SoldierStatusBase  = 0;
    static std::uint32_t g_SoldierStatusCount = 0;
}


static bool TryReadUInt32SEH(const std::uint32_t* ptr, std::uint32_t& outValue)
{
    outValue = 0;

    if (!ptr)
        return false;

    __try
    {
        outValue = *ptr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        outValue = 0;
        return false;
    }
}


static bool TryWriteUInt32SEH(std::uint32_t* ptr, std::uint32_t value)
{
    if (!ptr)
        return false;

    __try
    {
        *ptr = value;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}


static bool ResolveOptCamoFlagsField(
    void* self,
    std::uint32_t actorIndex,
    std::uint32_t*& outFlagsPtr,
    std::uint16_t& outMappedIndex,
    ResolveOptCamoStatus& outStatus)
{
    outFlagsPtr = nullptr;
    outMappedIndex = 0xFFFFu;
    outStatus = ResolveOptCamoStatus::NullSelf;

    if (!self)
        return false;

    __try
    {
        const std::uintptr_t selfAddr = reinterpret_cast<std::uintptr_t>(self);

        const std::uint64_t root =
            *reinterpret_cast<const std::uint64_t*>(selfAddr + 0x1E8ull);
        if (!root)
        {
            outStatus = ResolveOptCamoStatus::NullRoot;
            return false;
        }

        const std::uint64_t mapOwner =
            *reinterpret_cast<const std::uint64_t*>(root + 0x18ull);
        if (!mapOwner)
        {
            outStatus = ResolveOptCamoStatus::NullMapOwner;
            return false;
        }

        const std::uint64_t mapTable =
            *reinterpret_cast<const std::uint64_t*>(mapOwner + 0x28ull);
        if (!mapTable)
        {
            outStatus = ResolveOptCamoStatus::NullMapTable;
            return false;
        }

        const std::uint16_t mappedIndex =
            *reinterpret_cast<const std::uint16_t*>(
                mapTable + (static_cast<std::uint64_t>(actorIndex) * 2ull));

        outMappedIndex = mappedIndex;

        if (mappedIndex == 0x01FFu)
        {
            outStatus = ResolveOptCamoStatus::InvalidMappedIndex;
            return false;
        }

        const std::uint64_t configList =
            *reinterpret_cast<const std::uint64_t*>(root + 0x110ull);
        if (!configList)
        {
            outStatus = ResolveOptCamoStatus::NullConfigList;
            return false;
        }

        const std::uint32_t configCount =
            *reinterpret_cast<const std::uint32_t*>(configList + 0x10ull);

        const std::uint64_t configBase =
            *reinterpret_cast<const std::uint64_t*>(configList + 0x8ull);
        if (!configBase)
        {
            outStatus = ResolveOptCamoStatus::NullConfigBase;
            return false;
        }

        std::uint64_t configEntry = configBase;
        if (static_cast<std::uint32_t>(mappedIndex) < configCount)
        {
            configEntry = configBase + (static_cast<std::uint64_t>(mappedIndex) * 0x28ull);
        }

        outFlagsPtr = reinterpret_cast<std::uint32_t*>(configEntry + 0x10ull);
        g_SoldierActorMap    = mapTable;
        g_SoldierStatusBase  = configBase;
        g_SoldierStatusCount = configCount;
        outStatus = ResolveOptCamoStatus::Ok;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        outFlagsPtr = nullptr;
        outMappedIndex = 0xFFFFu;
        outStatus = ResolveOptCamoStatus::ExceptionThrown;
        return false;
    }
}


static OptCamoForceMode GetModeForMappedIndex(std::uint32_t mappedIndex, std::uint64_t nowMs)
{
    std::lock_guard<std::mutex> lock(g_UpdateOptCamoMutex);

    const auto it = g_MappedIndexModes.find(mappedIndex);
    OptCamoOverride& entry = it != g_MappedIndexModes.end() ? it->second : g_EverySoldierMode;
    if (entry.offAtMs != 0 && nowMs >= entry.offAtMs)
    {
        entry.mode = OptCamoForceMode::ForceOff;
        entry.offAtMs = 0;
        g_TimedOverrideCount.fetch_sub(1, std::memory_order_relaxed);
    }
    return entry.mode;
}


static void SetMappedIndexMode_NoLock(std::uint32_t mappedIndex, OptCamoForceMode mode, std::uint64_t offAtMs)
{
    const auto it = g_MappedIndexModes.find(mappedIndex);
    if (it != g_MappedIndexModes.end() && it->second.offAtMs != 0)
        g_TimedOverrideCount.fetch_sub(1, std::memory_order_relaxed);

    if (mode == OptCamoForceMode::None)
    {
        g_MappedIndexModes.erase(mappedIndex);
        return;
    }

    g_MappedIndexModes[mappedIndex] = OptCamoOverride{ mode, offAtMs };
    if (offAtMs != 0)
        g_TimedOverrideCount.fetch_add(1, std::memory_order_relaxed);
}


static void __fastcall hkUpdateOptCamo(void* self, std::uint32_t actorIndex)
{
    if (!g_OrigUpdateOptCamo)
        return;

    if (MissionCodeGuard::ShouldBypassHooks())
    {
        g_OrigUpdateOptCamo(self, actorIndex);
        return;
    }

    const std::uint64_t nowMs =
        g_TimedOverrideCount.load(std::memory_order_relaxed) != 0 ? GameClock::NowMs() : 0;

    std::uint32_t* flagsPtr = nullptr;
    std::uint16_t mappedIndex = 0xFFFFu;
    ResolveOptCamoStatus resolveStatus = ResolveOptCamoStatus::NullSelf;

    const bool resolved = ResolveOptCamoFlagsField(
        self,
        actorIndex,
        flagsPtr,
        mappedIndex,
        resolveStatus);

    if (!resolved || !flagsPtr)
    {
        g_OrigUpdateOptCamo(self, actorIndex);
        return;
    }

    const OptCamoForceMode mode = GetModeForMappedIndex(static_cast<std::uint32_t>(mappedIndex), nowMs);
    if (mode == OptCamoForceMode::None)
    {
        g_OrigUpdateOptCamo(self, actorIndex);
        return;
    }

    std::uint32_t savedFlags = 0;
    if (!TryReadUInt32SEH(flagsPtr, savedFlags))
    {
        g_OrigUpdateOptCamo(self, actorIndex);
        return;
    }

    std::uint32_t forcedFlags = savedFlags;
    if (mode == OptCamoForceMode::ForceOn)
    {
        forcedFlags = savedFlags | kDesiredOptCamoBit;
    }
    else if (mode == OptCamoForceMode::ForceOff)
    {
        forcedFlags = savedFlags & ~kDesiredOptCamoBit;
    }

    bool patchedFlags = false;
    if (forcedFlags != savedFlags)
    {
        patchedFlags = TryWriteUInt32SEH(flagsPtr, forcedFlags);
    }

    g_OrigUpdateOptCamo(self, actorIndex);

    if (patchedFlags)
    {
        TryWriteUInt32SEH(flagsPtr, savedFlags);
    }
}


static bool InstallOptCamoSlotPatches()
{
    if (g_OptCamoSlotPatches[0].installed)
        return true;

    auto* slotCount = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.RealizedSoldier2Impl_Construct_OptCamoSlotCount));
    auto* prepare   = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.RealizedSoldier2Impl_PrepareOptCamo));
    auto* show      = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.RealizedSoldier2Impl_ShowOptCamo));
    auto* release   = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.RealizedSoldier2Impl_ReleaseOptCamo));
    if (!slotCount || !prepare || !show || !release
        || CheckHookEntry(slotCount - sizeof(kSlotCountSetup), kSlotCountSetup, sizeof(kSlotCountSetup)) != HookEntry::Expected
        || CheckHookEntry(prepare, kPreparePrologue, sizeof(kPreparePrologue)) == HookEntry::Unknown
        || CheckHookEntry(show, kShowPrologue, sizeof(kShowPrologue)) == HookEntry::Unknown
        || CheckHookEntry(release, kReleasePrologue, sizeof(kReleasePrologue)) == HookEntry::Unknown)
    {
        LogError("OptCamoSlots: RealizedSoldier2Impl does not match %s, optical camo stays at one soldier at a time",
                 GetGameBuildName(gGameBuild));
        return false;
    }

    const std::uintptr_t boundSite = reinterpret_cast<std::uintptr_t>(prepare) + kPrepareSlotBound;
    CaveCode bound;
    bound.Emit({ 0x3B, 0xB5, 0x84, 0x02, 0x00, 0x00 });   // cmp esi,[rbp+0x284]: the game's own bound, every slot of the controller
    const std::size_t pastController = bound.Jcc8(0x73);
    bound.Emit({ 0xF6, 0x44, 0x24, 0x60, 0x01 });         // test byte [rsp+0x60],1: the FOB bit as PrepareOptCamo saved it
    const std::size_t fobMission = bound.Jcc8(0x75);
    bound.Emit({ 0x40, 0xF6, 0xC6, 0x03 });               // test sil,3: a slot index that is a multiple of 4 belongs to the next soldier
    const std::size_t pastOwnSlots = bound.Jcc8(0x74);
    bound.Bind8(fobMission);
    bound.JmpAbs(boundSite + 8);                          // on to the CreateOptCamoSlot call
    bound.Bind8(pastController);
    bound.Bind8(pastOwnSlots);
    bound.JmpAbs(reinterpret_cast<std::uintptr_t>(prepare) + kPrepareNextFovaSlot);

    auto* thunk = static_cast<std::uint8_t*>(HookArena::AllocateNear(boundSite, bound.bytes.size()));
    const std::int64_t rel = thunk
        ? static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(thunk)) - static_cast<std::int64_t>(boundSite + 5)
        : 0;
    if (!thunk || rel < INT32_MIN || rel > INT32_MAX)
    {
        LogError("OptCamoSlots: no code page near RealizedSoldier2Impl, optical camo stays at one soldier at a time");
        return false;
    }
    std::memcpy(thunk, bound.bytes.data(), bound.bytes.size());
    FlushInstructionCache(GetCurrentProcess(), thunk, bound.bytes.size());
    const std::int32_t rel32 = static_cast<std::int32_t>(rel);

    struct SlotPatch
    {
        std::uint8_t* at;
        std::uint8_t  vanilla[6];
        std::uint8_t  patched[6];
    };
    SlotPatch edits[kOptCamoSlotPatchCount] =
    {
        { slotCount,                    { 0xC1, 0xE9, 0x16, 0xF6, 0xC1, 0x01 }, { 0x83, 0xC9, 0x01, 0xF6, 0xC1, 0x01 } },   // shr ecx,0x16 -> or ecx,1: slot count = realized soldiers * 4
        { show + kShowFobBit,           { 0x41, 0xC1, 0xE8, 0x16, 0x41, 0xF6 }, { 0x41, 0x83, 0xC8, 0x01, 0x41, 0xF6 } },   // shr r8d,0x16 -> or r8d,1: shows the soldier's own 4 slots
        { release + kReleaseFobBit,     { 0xC1, 0xEE, 0x16, 0x40, 0x80, 0xE6 }, { 0x83, 0xCE, 0x01, 0x40, 0x80, 0xE6 } },   // shr esi,0x16 -> or esi,1: frees the soldier's own 4 slots
        { prepare + kPrepareBusyCheck,  { 0x75, 0x0D, 0xF6, 0x81, 0x8A, 0x02 }, { 0xEB, 0x0D, 0xF6, 0x81, 0x8A, 0x02 } },   // jnz -> jmp: no early out while another soldier is cloaked
        { prepare + kPrepareSlotBase,   { 0x74, 0x0B, 0x8B, 0xF3, 0x2B, 0xB5 }, { 0x90, 0x90, 0x8B, 0xF3, 0x2B, 0xB5 } },   // jz -> nop: first slot = (actor - first actor) * 4
        { prepare + kPrepareSlotBound,  { 0x3B, 0xB5, 0x84, 0x02, 0x00, 0x00 }, { 0xE9, 0x00, 0x00, 0x00, 0x00, 0x90 } },   // cmp esi,[rbp+0x284] -> jmp to the bound above
        { prepare + kPrepareBusySet,    { 0x75, 0x1D, 0x0F, 0xB6, 0x85, 0x8A }, { 0xEB, 0x1D, 0x0F, 0xB6, 0x85, 0x8A } },   // jnz -> jmp: the busy bit stays clear
    };
    std::memcpy(edits[5].patched + 1, &rel32, sizeof(rel32));

    for (std::size_t i = 0; i < kOptCamoSlotPatchCount; ++i)
    {
        g_OptCamoSlotPatches[i] = NativeCodePatch{};
        g_OptCamoSlotPatches[i].target = edits[i].at;
        g_OptCamoSlotPatches[i].size = 6;
        std::memcpy(g_OptCamoSlotPatches[i].expected, edits[i].vanilla, 6);
        std::memcpy(g_OptCamoSlotPatches[i].replacement, edits[i].patched, 6);
    }

    for (int attempt = 0; attempt < 50; ++attempt)
    {
        if (InstallNativeCodePatches(g_OptCamoSlotPatches, kOptCamoSlotPatchCount))
            return true;
        if (!NativePatchErrorIsTransient(GetLastError()))
            break;
        Sleep(4);
    }
    LogError("OptCamoSlots: patch refused (%s), optical camo stays at one soldier at a time",
             NativePatchErrorName(GetLastError()));
    return false;
}


bool Install_UpdateOptCamo_Hook()
{
    InstallOptCamoSlotPatches();

    void* target = ResolveGameAddress(gAddr.UpdateOptCamo);
    if (!target)
    {
        LogError("Hook: UpdateOptCamo: address missing, optical camo overrides off");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkUpdateOptCamo),
        reinterpret_cast<void**>(&g_OrigUpdateOptCamo));

    if (!ok)
        LogError("Hook: UpdateOptCamo: hook failed, optical camo overrides off");
    return ok;
}


bool Uninstall_UpdateOptCamo_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.UpdateOptCamo));
    g_OrigUpdateOptCamo = nullptr;
    RemoveNativeCodePatches(g_OptCamoSlotPatches, kOptCamoSlotPatchCount);

    Clear_UpdateOptCamoMappedIndexOverrides();
    return true;
}


void Set_UpdateOptCamoEnableMappedIndex(std::uint32_t mappedIndex, bool enabled, double seconds)
{
    const std::uint32_t soldierIndex = mappedIndex & kSoldierIndexMask;

    std::uint64_t offAtMs = 0;
    if (enabled && seconds > 0.0)
        offAtMs = GameClock::NowMs() + static_cast<std::uint64_t>(seconds * 1000.0);

    std::lock_guard<std::mutex> lock(g_UpdateOptCamoMutex);
    SetMappedIndexMode_NoLock(
        soldierIndex,
        enabled ? OptCamoForceMode::ForceOn : OptCamoForceMode::ForceOff,
        offAtMs);
}


void Set_UpdateOptCamoEnableEverySoldier(bool enabled, double seconds)
{
    std::uint64_t offAtMs = 0;
    if (enabled && seconds > 0.0)
        offAtMs = GameClock::NowMs() + static_cast<std::uint64_t>(seconds * 1000.0);

    std::lock_guard<std::mutex> lock(g_UpdateOptCamoMutex);
    g_MappedIndexModes.clear();
    g_EverySoldierMode = OptCamoOverride{ enabled ? OptCamoForceMode::ForceOn : OptCamoForceMode::ForceOff, offAtMs };
    g_TimedOverrideCount.store(offAtMs != 0 ? 1u : 0u, std::memory_order_relaxed);
}


void Clear_UpdateOptCamoMappedIndexOverrides()
{
    std::lock_guard<std::mutex> lock(g_UpdateOptCamoMutex);
    g_MappedIndexModes.clear();
    g_EverySoldierMode = OptCamoOverride{};
    g_TimedOverrideCount.store(0, std::memory_order_relaxed);
}


bool GetSoldierStatusArray(std::uint64_t& outBase, std::uint32_t& outCount)
{
    outBase  = g_SoldierStatusBase;
    outCount = g_SoldierStatusCount;
    return outBase != 0 && outCount != 0;
}


bool GetSoldierActorMap(std::uint64_t& outMapTable)
{
    outMapTable = g_SoldierActorMap;
    return outMapTable != 0;
}
