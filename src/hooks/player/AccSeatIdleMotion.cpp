#include "pch.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <new>
#include <vector>

#include <string>

#include "AccSeatIdleMotion.h"
#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "NativeCallPatch.h"
#include "log.h"

namespace
{
    using GetAnimFileFromGaniPath_t = void*(__fastcall*)(void* animControl, std::uint64_t clipId);

    constexpr std::uint64_t kSeatIdleClip = 0xFC53E304E5E9DBF4ull; // /Assets/tpp/motion/SI_game/fani/bodies/snap/snaputh/snaputh_q_seat_idl_lp.gani
    constexpr std::uint64_t kDropSeatClip = 0xFC51900164E7C27Eull; // /Assets/tpp/motion/SI_game/fani/bodies/snap/snaputh/snaputh_q_seat_idl_b_lp.gani
    constexpr std::size_t kMaxDropSeatPoses = 32;
    constexpr std::uint32_t kAnyMission = 0xFFFFFFFFu;

    struct DropSeatPose
    {
        std::atomic<std::uint32_t> mission;
        std::atomic<std::uint64_t> clip;
    };

    constexpr std::size_t kMaxGaniSwaps = 256;

    struct GaniSwap
    {
        std::atomic<std::uint64_t> from;
        std::atomic<std::uint64_t> to;
    };
    constexpr std::size_t kSiteLength = 7;
    constexpr std::size_t kPatchLength = 6; // 5-byte jmp plus a nop
    constexpr std::uint8_t kSiteBytes[kSiteLength] = { 0x48, 0x63, 0x19, 0x48, 0x8B, 0x1C, 0x0B }; // movsxd rbx,[rcx] / mov rbx,[rbx+rcx], where the game reads a clip id

    std::atomic<std::uint64_t> g_ReplacementClip{0};
    std::atomic<std::uint64_t> g_WarnedClip{0};
    std::atomic<std::uint64_t> g_WarnedPose{0};
    std::atomic<std::uint64_t> g_WarnedSwap{0};
    GaniSwap g_GaniSwaps[kMaxGaniSwaps] = {};
    std::atomic<std::size_t> g_GaniSwapCount{0};
    std::mutex g_GaniSwapLock;
    DropSeatPose g_DropSeatPoses[kMaxDropSeatPoses] = {};
    std::atomic<std::size_t> g_DropSeatPoseCount{0};
    std::mutex g_DropSeatPoseLock;
    GetAnimFileFromGaniPath_t g_GetAnimFileFromGaniPath = nullptr;
    NativeCodePatch g_Patch{};
    bool g_Installed = false;

    std::uintptr_t ReadPtr(std::uintptr_t p)
    {
        __try
        {
            return p ? *reinterpret_cast<std::uintptr_t*>(p) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void* FindClip(void* animControl, std::uint64_t clipId)
    {
        __try
        {
            return g_GetAnimFileFromGaniPath(animControl, clipId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    std::uint32_t ReadU32(std::uintptr_t p)
    {
        __try
        {
            return p ? *reinterpret_cast<std::uint32_t*>(p) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void LogPick(std::uintptr_t animControl, std::uint64_t clip, bool found)
    {
        static const std::uint64_t heliPath = FoxHashes::PathCode64Ext("/Assets/tpp/motion/mtar/player2/player2_heli.mtar");
        const std::uint32_t count = ReadU32(animControl + 0x1C); // how many motion archives this control has
        const std::uintptr_t base = ReadPtr(animControl + 0x98); // the archive list
        std::string entries;
        int heliIndex = -1;
        bool heliResident = false;
        for (std::uint32_t i = 0; i < count && i < 64; ++i)
        {
            const std::uintptr_t entry = base + static_cast<std::uintptr_t>(i) * 0x30; // each list entry is 0x30 bytes
            const std::uintptr_t file = ReadPtr(entry + 0x18); // the archive file
            const std::uint64_t path = ReadPtr(entry + 0x20); // its path hash
            const bool resident = file && ReadPtr(file + 0x98); // archive data, null while it isn't loaded
            if (path == heliPath || (file && ReadPtr(file + 0x30) == heliPath))
            {
                heliIndex = static_cast<int>(i);
                heliResident = resident;
            }
            char item[40];
            sprintf_s(item, " %u:%012llX%s", i, path & 0xFFFFFFFFFFFFull, !file ? "-" : (resident ? "" : "*"));
            entries += item;
        }
        LogInfo("SetMotion: pick clip 0x%016llX %s mission %u ac %p entries %u heli %d%s heliPath %016llX |%s",
                clip, found ? "HIT" : "MISS", MissionCodeGuard::GetCurrentMissionCode(),
                reinterpret_cast<void*>(animControl), count, heliIndex, heliResident ? " resident" : (heliIndex >= 0 ? " NOT resident" : ""),
                heliPath, entries.c_str());
    }

    std::uint64_t __fastcall PickSeatIdleClip(std::uint64_t vanillaClip, std::uintptr_t builder)
    {
        const std::uint64_t clip = g_ReplacementClip.load(std::memory_order_acquire);
        if (!clip)
            return vanillaClip;
        void* animControl = reinterpret_cast<void*>(ReadPtr(builder + 0x10)); // the AnimControl this tree is being built for
        const bool found = animControl && FindClip(animControl, clip);
        LogPick(reinterpret_cast<std::uintptr_t>(animControl), clip, found);
        if (found)
            return clip;
        if (g_WarnedClip.exchange(clip, std::memory_order_relaxed) != clip)
            LogWarn("SetMotion: clip 0x%016llX is not in the player's motion archives, ACC idle stays vanilla", clip);
        return vanillaClip;
    }

    std::uint64_t DropSeatPoseFor(std::uint32_t mission)
    {
        std::uint64_t anyMission = 0;
        const std::size_t count = g_DropSeatPoseCount.load(std::memory_order_acquire);
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::uint32_t poseMission = g_DropSeatPoses[i].mission.load(std::memory_order_relaxed);
            const std::uint64_t clip = g_DropSeatPoses[i].clip.load(std::memory_order_relaxed);
            if (poseMission == mission && clip)
                return clip;
            if (poseMission == kAnyMission)
                anyMission = clip;
        }
        return anyMission;
    }

    std::uint64_t PickDropSeatPose(std::uint64_t clip, void* animControl)
    {
        const std::uint64_t pose = DropSeatPoseFor(MissionCodeGuard::GetCurrentMissionCode());
        if (!pose)
            return clip;
        if (animControl && FindClip(animControl, pose))
            return pose;
        LogPick(reinterpret_cast<std::uintptr_t>(animControl), pose, false);
        if (g_WarnedPose.exchange(pose, std::memory_order_relaxed) != pose)
            LogWarn("SetPlayerDropSeatPose: clip 0x%016llX is not in the player's motion archives, the drop seat pose stays vanilla", pose);
        return clip;
    }

    std::uint64_t SwappedGani(std::uint64_t clip)
    {
        const std::size_t count = g_GaniSwapCount.load(std::memory_order_acquire);
        for (std::size_t i = 0; i < count; ++i)
        {
            if (g_GaniSwaps[i].from.load(std::memory_order_relaxed) == clip)
                return g_GaniSwaps[i].to.load(std::memory_order_relaxed);
        }
        return 0;
    }

    std::uint64_t __fastcall PickLeafClip(std::uint64_t clip, std::uintptr_t builder)
    {
        void* animControl = reinterpret_cast<void*>(ReadPtr(builder + 0x10)); // the AnimControl this tree is being built for
        std::uint64_t picked = clip;
        if (clip == kSeatIdleClip)
            picked = PickSeatIdleClip(clip, builder);
        else if (clip == kDropSeatClip)
            picked = PickDropSeatPose(clip, animControl);
        if (picked != clip)
            return picked;
        const std::uint64_t swapped = SwappedGani(clip);
        if (!swapped)
            return clip;
        if (animControl && FindClip(animControl, swapped))
            return swapped;
        if (g_WarnedSwap.exchange(swapped, std::memory_order_relaxed) != swapped)
            LogWarn("SwapGani: clip 0x%016llX is not in that character's motion archives, 0x%016llX plays unchanged", swapped, clip);
        return clip;
    }

    struct Code
    {
        std::vector<std::uint8_t> bytes;

        void Emit(std::initializer_list<std::uint8_t> list)
        {
            bytes.insert(bytes.end(), list.begin(), list.end());
        }

        void Emit64(std::uint64_t v)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
            bytes.insert(bytes.end(), p, p + 8);
        }
    };

    bool UnwindMatches(std::uint8_t* site, PRUNTIME_FUNCTION parent, DWORD64 imageBase,
                       std::uint8_t* storage, PRUNTIME_FUNCTION stub, std::size_t codeStart)
    {
        DWORD64 stackA[96]{};
        DWORD64 stackB[96]{};
        for (std::size_t i = 0; i < 96; ++i)
        {
            stackA[i] = 0x1000 + i; // same fake stack on both sides so the two unwinds can be compared
            stackB[i] = 0x1000 + i;
        }
        CONTEXT a{};
        a.ContextFlags = CONTEXT_FULL;
        a.Rip = reinterpret_cast<DWORD64>(site);
        a.Rsp = reinterpret_cast<DWORD64>(&stackA[0]);
        CONTEXT b{};
        b.ContextFlags = CONTEXT_FULL;
        b.Rip = reinterpret_cast<DWORD64>(storage + codeStart);
        b.Rsp = reinterpret_cast<DWORD64>(&stackB[0]);
        void* handler = nullptr;
        DWORD64 frame = 0;
        __try
        {
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, a.Rip, parent, &a, &handler, &frame, nullptr);
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, reinterpret_cast<DWORD64>(storage), b.Rip, stub, &b, &handler, &frame, nullptr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        const DWORD64 deltaA = a.Rsp - reinterpret_cast<DWORD64>(&stackA[0]);
        const DWORD64 deltaB = b.Rsp - reinterpret_cast<DWORD64>(&stackB[0]);
        return a.Rip == b.Rip && deltaA == deltaB && a.Rbx == b.Rbx && a.Rbp == b.Rbp && a.Rsi == b.Rsi
            && a.Rdi == b.Rdi && a.R12 == b.R12 && a.R13 == b.R13 && a.R14 == b.R14 && a.R15 == b.R15;
    }

    std::uint8_t* BuildStub(std::uint8_t* site)
    {
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION parent = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(site), &imageBase, nullptr);
        if (!parent)
            return nullptr;
        const std::uint8_t* info = reinterpret_cast<const std::uint8_t*>(imageBase + parent->UnwindData);
        if ((info[0] >> 3) != 0) // unwind flags set, can't copy chained or handler info
            return nullptr;
        const std::size_t prolog = info[1]; // prolog size in bytes
        const std::size_t infoSize = 4 + 2 * ((static_cast<std::size_t>(info[2]) + 1) & ~std::size_t(1)); // header plus unwind codes, padded to an even count
        const std::size_t codeStart = (prolog + 0x10) & ~std::size_t(0xF); // start past the copied prolog so the unwinder sees us as the function body

        Code code;
        code.bytes.assign(codeStart, 0xCC);
        code.Emit({ 0x48, 0x63, 0x19 }); // movsxd rbx, [rcx]   (the two instructions we replaced)
        code.Emit({ 0x48, 0x8B, 0x1C, 0x0B }); // mov rbx, [rbx+rcx]
        code.Emit({ 0x48, 0x89, 0xD9 }); // mov rcx, rbx   (clip id)
        code.Emit({ 0x48, 0x89, 0xF2 }); // mov rdx, rsi   (the tree builder)
        code.Emit({ 0x48, 0xB8 }); // mov rax, PickLeafClip
        code.Emit64(reinterpret_cast<std::uint64_t>(&PickLeafClip));
        code.Emit({ 0xFF, 0xD0 }); // call rax
        code.Emit({ 0x48, 0x89, 0xC3 }); // mov rbx, rax   (use whichever clip we picked)
        code.Emit({ 0x48, 0xB8 }); // mov rax, back into the game right after what we replaced
        code.Emit64(reinterpret_cast<std::uint64_t>(site + kSiteLength));
        code.Emit({ 0xFF, 0xE0 }); // jmp rax
        const std::size_t codeEnd = code.bytes.size();
        while (code.bytes.size() % 4) // unwind info has to start 4-byte aligned
            code.Emit({ 0xCC });
        const std::size_t unwindOffset = code.bytes.size();
        code.bytes.insert(code.bytes.end(), info, info + infoSize);

        auto* storage = static_cast<std::uint8_t*>(HookArena::AllocateNear(reinterpret_cast<std::uintptr_t>(site), code.bytes.size()));
        auto* functions = new (std::nothrow) RUNTIME_FUNCTION[1]{};
        if (!storage || !functions)
            return nullptr;
        std::memcpy(storage, code.bytes.data(), code.bytes.size());
        functions[0].BeginAddress = 0;
        functions[0].EndAddress = static_cast<DWORD>(codeEnd);
        functions[0].UnwindData = static_cast<DWORD>(unwindOffset);
        if (!RtlAddFunctionTable(functions, 1, reinterpret_cast<DWORD64>(storage)))
            return nullptr;
        if (!UnwindMatches(site, parent, imageBase, storage, functions, codeStart))
        {
            RtlDeleteFunctionTable(functions);
            LogError("SetMotion: stub unwind check failed, V_title_sequence.SetMotion off");
            return nullptr;
        }
        FlushInstructionCache(GetCurrentProcess(), storage, code.bytes.size());
        return storage + codeStart;
    }
}

bool AccSeatIdleMotion_Install()
{
    if (g_Installed)
        return true;
    void* resolved = ResolveGameAddress(gAddr.MotionGraphBlendNodeTraverser_LeafClipRead);
    g_GetAnimFileFromGaniPath = reinterpret_cast<GetAnimFileFromGaniPath_t>(ResolveGameAddress(gAddr.AnimControl_GetAnimFileFromGaniPath));
    if (!resolved || !g_GetAnimFileFromGaniPath)
        return true;
    std::uint8_t* site = static_cast<std::uint8_t*>(resolved);
    if (std::memcmp(site, kSiteBytes, kSiteLength) != 0)
    {
        LogError("SetMotion: motion graph leaf read mismatch at %p, V_title_sequence.SetMotion off", site);
        return true;
    }
    std::uint8_t* entry = BuildStub(site);
    if (!entry)
    {
        LogError("SetMotion: could not build the seat idle stub, V_title_sequence.SetMotion off");
        return true;
    }
    const std::int64_t rel = reinterpret_cast<std::int64_t>(entry) - (reinterpret_cast<std::int64_t>(site) + 5);
    if (rel < INT32_MIN || rel > INT32_MAX)
    {
        LogError("SetMotion: stub out of jump range, V_title_sequence.SetMotion off");
        return true;
    }
    const std::int32_t rel32 = static_cast<std::int32_t>(rel);
    g_Patch = {};
    g_Patch.target = site;
    g_Patch.size = static_cast<unsigned char>(kPatchLength);
    std::memcpy(g_Patch.expected, kSiteBytes, kPatchLength);
    g_Patch.replacement[0] = 0xE9; // jmp rel32 to our stub
    std::memcpy(g_Patch.replacement + 1, &rel32, 4);
    g_Patch.replacement[5] = 0x90; // nop over the leftover byte
    DWORD error = ERROR_SUCCESS;
    int hardFailures = 0;
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        if (InstallNativeCodePatches(&g_Patch, 1))
        {
            g_Installed = true;
            return true;
        }
        error = GetLastError();
        if (!NativePatchErrorIsTransient(error) && ++hardFailures >= 5)
            break;
        Sleep(NativePatchErrorIsTransient(error) ? 1 : 20);
    }
    LogError("SetMotion: patching the motion graph leaf read failed (%s, error %lu), V_title_sequence.SetMotion off",
             NativePatchErrorName(error), error);
    return true;
}

void AccSeatIdleMotion_Uninstall()
{
    g_ReplacementClip.store(0, std::memory_order_release);
    g_DropSeatPoseCount.store(0, std::memory_order_release);
    g_GaniSwapCount.store(0, std::memory_order_release);
    if (!g_Installed)
        return;
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        if (RemoveNativeCodePatches(&g_Patch, 1))
            break;
        if (!NativePatchErrorIsTransient(GetLastError()))
            break;
        Sleep(1);
    }
    g_Installed = false;
}

bool AccSeatIdleMotion_Set(std::uint64_t clipId)
{
    if (clipId == kSeatIdleClip)
        clipId = 0;
    g_ReplacementClip.store(clipId, std::memory_order_release);
    if (clipId && !g_Installed)
    {
        LogWarn("SetMotion: the ACC idle swap is not available on %s", GetGameBuildName(gGameBuild));
        return false;
    }
    return true;
}

bool AccSeatIdleMotion_SetDropSeatPose(std::uint64_t clipId, std::uint32_t missionCode, bool anyMission)
{
    if (clipId == kDropSeatClip)
        clipId = 0;
    const std::uint32_t mission = anyMission ? kAnyMission : missionCode;
    std::lock_guard<std::mutex> lock(g_DropSeatPoseLock);
    const std::size_t count = g_DropSeatPoseCount.load(std::memory_order_acquire);
    if (!clipId && anyMission)
    {
        g_DropSeatPoseCount.store(0, std::memory_order_release);
        return true;
    }
    std::size_t slot = count;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (g_DropSeatPoses[i].mission.load(std::memory_order_relaxed) == mission)
            slot = i;
    }
    if (slot == count)
    {
        if (!clipId)
            return true;
        if (count == kMaxDropSeatPoses)
        {
            LogWarn("SetPlayerDropSeatPose: only %zu missions can have their own pose, mission %u ignored", kMaxDropSeatPoses, mission);
            return false;
        }
        g_DropSeatPoses[slot].mission.store(mission, std::memory_order_relaxed);
    }
    g_DropSeatPoses[slot].clip.store(clipId, std::memory_order_relaxed);
    if (slot == count)
        g_DropSeatPoseCount.store(count + 1, std::memory_order_release);
    if (clipId && !g_Installed)
    {
        LogWarn("SetPlayerDropSeatPose: the pose swap is not available on %s", GetGameBuildName(gGameBuild));
        return false;
    }
    return true;
}

bool SwapGani(std::uint64_t from, std::uint64_t to)
{
    if (!from || !to || from == to)
        return RestoreGani(from, 0);
    std::lock_guard<std::mutex> lock(g_GaniSwapLock);
    const std::size_t count = g_GaniSwapCount.load(std::memory_order_acquire);
    std::size_t slot = count;
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::uint64_t slotFrom = g_GaniSwaps[i].from.load(std::memory_order_relaxed);
        if (slotFrom == from)
        {
            slot = i;
            break;
        }
        if (slot == count && !g_GaniSwaps[i].to.load(std::memory_order_relaxed))
            slot = i;
    }
    if (slot == count && count == kMaxGaniSwaps)
    {
        LogWarn("SwapGani: only %zu swaps can be active, 0x%016llX ignored", kMaxGaniSwaps, from);
        return false;
    }
    g_GaniSwaps[slot].from.store(from, std::memory_order_relaxed);
    g_GaniSwaps[slot].to.store(to, std::memory_order_release);
    if (slot == count)
        g_GaniSwapCount.store(count + 1, std::memory_order_release);
    if (!g_Installed)
    {
        LogWarn("SwapGani: the gani swap is not available on %s", GetGameBuildName(gGameBuild));
        return false;
    }
    return true;
}

bool RestoreGani(std::uint64_t from, std::uint64_t to)
{
    std::lock_guard<std::mutex> lock(g_GaniSwapLock);
    const std::size_t count = g_GaniSwapCount.load(std::memory_order_acquire);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (g_GaniSwaps[i].from.load(std::memory_order_relaxed) != from)
            continue;
        const std::uint64_t current = g_GaniSwaps[i].to.load(std::memory_order_relaxed);
        if (!current || (to && current != to))
            return false;
        g_GaniSwaps[i].to.store(0, std::memory_order_release);
        return true;
    }
    return false;
}

void RestoreAllGani()
{
    std::lock_guard<std::mutex> lock(g_GaniSwapLock);
    g_GaniSwapCount.store(0, std::memory_order_release);
}

std::uint64_t AccSeatIdleMotion_Replacement()
{
    return g_ReplacementClip.load(std::memory_order_acquire);
}
