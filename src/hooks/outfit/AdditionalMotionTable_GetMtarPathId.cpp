#include "pch.h"

#include "AdditionalMotionTable_GetMtarPathId.h"
#include "OutfitRegistry.h"

#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"


namespace
{
    using GetMtarPathId_t     = std::uint64_t* (__fastcall*)(std::uint64_t*, std::uint32_t);
    using UpdateMotionBlock_t = void (__fastcall*)(void*);
    using GetFileForPathId_t  = void* (__fastcall*)(void*, std::uint64_t);
    using GetBlockAtIndex_t   = void* (__fastcall*)(void*, std::uint32_t);
    using AdditionalMtarAll_t = void (__fastcall*)(void*, std::uint32_t);

    static GetMtarPathId_t     g_OrigGetMtarPathId        = nullptr;
    static UpdateMotionBlock_t g_OrigUpdateMotionBlock    = nullptr;
    static GetFileForPathId_t  g_OrigBlockGetFileForPathId = nullptr;
    static GetBlockAtIndex_t   g_GetBlockAtIndex          = nullptr;
    static AdditionalMtarAll_t g_OrigAddMtarAll           = nullptr;
    static AdditionalMtarAll_t g_RemoveMtarAll            = nullptr;
    static bool                g_Installed                = false;
    static bool                g_FallbackReady            = false;

    constexpr std::size_t      kMaxPlayerImpls   = 4;
    constexpr std::size_t      kImplAddedMaskOff = 0x680;
    static std::atomic<void*>  g_PlayerImpls[kMaxPlayerImpls] = {};

    constexpr std::size_t      kMaxMotionCtls = 4;
    static std::atomic<void*>  g_MotionBlockCtls[kMaxMotionCtls] = {};
    static thread_local bool   g_InMotionFallback = false;

    constexpr std::size_t     kMaxSeenBlocks = 96;
    static std::atomic<void*> g_SeenBlocks[kMaxSeenBlocks] = {};

    static void RememberBlock(void* block)
    {
        if (!block)
            return;
        for (std::size_t i = 0; i < kMaxSeenBlocks; ++i)
        {
            void* cur = g_SeenBlocks[i].load(std::memory_order_acquire);
            if (cur == block)
                return;
            if (cur == nullptr)
            {
                void* expected = nullptr;
                if (g_SeenBlocks[i].compare_exchange_strong(
                        expected, block, std::memory_order_acq_rel))
                    return;
                if (expected == block)
                    return;
            }
        }
    }

    constexpr std::size_t kCtlLoaderOff     = 0xB8;
    constexpr std::size_t kCtlMainLoaderOff = 0x160;
    constexpr std::size_t kLoaderGroupOff   = 0x50;
    constexpr std::size_t kGroupCountOff    = 0x1C;
    constexpr std::size_t kMtarPathOff      = 0x30;
    constexpr std::size_t kMtarDataOff      = 0x98;
    constexpr std::size_t kCtlWalkStateOff  = 0x124;
    constexpr std::size_t kCtlWalkIndexOff  = 0x128;
    constexpr std::uint32_t kMotionTableCount = 32;

    struct MotionEntrySwap
    {
        void* original;
        void* written;
        bool  valid;
    };
    static MotionEntrySwap            g_EntrySwaps[kMotionTableCount] = {};
    static std::atomic<int>           g_SwapsLive{ 0 };
    static std::atomic<std::uint32_t> g_SwapEpochMission{ 0xFFFFFFFFu };

    struct DirectSlotBinding
    {
        std::atomic<void*>         bound{ nullptr };
        std::atomic<std::uint64_t> boundHash{ 0 };
        std::atomic<void*>         desired{ nullptr };
    };
    static DirectSlotBinding          g_DirectSlots[kMotionTableCount];
    static std::atomic<std::uint32_t> g_DirectSlotMask{ 0 };

    using MtarAdd_t    = int  (__fastcall*)(void*, void*);
    using MtarRemove_t = void (__fastcall*)(void*, void*);
    static MtarAdd_t    g_AddMtar    = nullptr;
    static MtarRemove_t g_RemoveMtar = nullptr;

    constexpr std::size_t kAnimCtlCountOff  = 0x1C;
    constexpr std::size_t kAnimCtlArrayOff  = 0x98;
    constexpr std::size_t kAnimEntryStride  = 0x30;
    constexpr std::size_t kAnimEntryFileOff = 0x18;

    constexpr std::size_t kMaxAnimCtls = 8;
    static std::atomic<void*> g_AnimCtls[kMaxAnimCtls]{};

    constexpr std::size_t kMaxMtarHolders = 16;
    static std::atomic<void*> g_MtarHolders[kMaxMtarHolders]{};

    static void NoteDirectSlotBinding(int slot, void* file, std::uint64_t hash)
    {
        if (slot < 0 || static_cast<std::uint32_t>(slot) >= kMotionTableCount || !file)
            return;
        g_DirectSlots[slot].boundHash.store(hash, std::memory_order_release);
        g_DirectSlots[slot].bound.store(file, std::memory_order_release);
        g_DirectSlotMask.fetch_or(1u << static_cast<unsigned>(slot),
                                  std::memory_order_acq_rel);
    }

    constexpr int kReresolveMaxDeferTicks = 120;
    static std::atomic<bool> g_PartsPipelineBusy{ false };
    static std::atomic<bool> g_ReresolvePending{ false };
    static std::atomic<int>  g_ReresolveDeferTicks{ 0 };


    static std::uint64_t ResolveDesiredHash_SEH(
        std::uint64_t currentHash, int* outSlot)
    {
        if (outSlot) *outSlot = -1;
        if (!g_FallbackReady)
            return 0;
        if (currentHash == 0)
            return 0;

        int slot = outfit::MotionMtarSlotFromVanillaHash(currentHash);
        if (slot < 0 && !outfit::IsMotionMtarOverrideHash(currentHash, &slot))
            return 0;
        if (slot < 0)
            return 0;
        if (outSlot) *outSlot = slot;

        std::uint64_t desired = 0;
        __try
        {
            std::uint8_t parts = outfit::ReadLivePartsType();
            std::uint8_t pt    = outfit::ReadLivePlayerType();
            if (parts < outfit::kCustomPartsTypeStart
                || parts > outfit::kCustomPartsTypeEnd)
            {
                parts = outfit::GetMotionOutfitHintPartsType();
                pt    = outfit::GetMotionOutfitHintPlayerType();
            }
            if (parts >= outfit::kCustomPartsTypeStart
                && parts <= outfit::kCustomPartsTypeEnd)
            {
                const outfit::OutfitEntry* entry = nullptr;
                if (outfit::TryGetOutfitByPartsType(parts, &entry) && entry)
                {
                    const std::uint8_t varIdx = entry->HasVariants()
                        ? outfit::GetActiveVariantLockFree(parts)
                        : std::uint8_t{ 0 };
                    desired = entry->GetMotionMtarOverride(
                        pt, static_cast<std::size_t>(slot), varIdx);
                    if (desired == 0)
                    {
                        const std::uint8_t firstPt =
                            entry->FirstSupportedPlayerType();
                        if (firstPt != pt)
                            desired = entry->GetMotionMtarOverride(
                                firstPt, static_cast<std::size_t>(slot), varIdx);
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            desired = 0;
        }

        if (desired == 0)
            desired = outfit::MotionMtarVanillaHash(static_cast<std::size_t>(slot));
        if (desired == 0 || desired == currentHash)
            return 0;
        return desired;
    }

    static std::uint64_t* __fastcall hkGetMtarPathId(
        std::uint64_t* outPath, std::uint32_t index)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigGetMtarPathId, outPath, index);

        std::uint64_t* r = g_OrigGetMtarPathId(outPath, index);
        if (!r)
            return r;

        const std::uint64_t original = *r;
        const std::uint64_t desired = ResolveDesiredHash_SEH(original, nullptr);
        if (desired != 0)
        {
            *r = desired;
        }

        return r;
    }

    static void __fastcall hkUpdateAdditionalMotionBlock(void* self)
    {
        for (std::size_t i = 0; i < kMaxMotionCtls; ++i)
        {
            void* cur = g_MotionBlockCtls[i].load(std::memory_order_acquire);
            if (cur == self)
                break;
            if (cur == nullptr)
            {
                void* expected = nullptr;
                if (g_MotionBlockCtls[i].compare_exchange_strong(
                        expected, self, std::memory_order_acq_rel))
                    break;
                if (expected == self)
                    break;
            }
        }
        g_OrigUpdateMotionBlock(self);
    }

    static void __fastcall hkAddAdditionalMtarAll(void* self, std::uint32_t group)
    {
        if (self)
        {
            for (std::size_t i = 0; i < kMaxPlayerImpls; ++i)
            {
                void* cur = g_PlayerImpls[i].load(std::memory_order_acquire);
                if (cur == self)
                    break;
                if (cur == nullptr)
                {
                    void* expected = nullptr;
                    if (g_PlayerImpls[i].compare_exchange_strong(
                            expected, self, std::memory_order_acq_rel))
                        break;
                    if (expected == self)
                        break;
                }
            }
        }
        g_OrigAddMtarAll(self, group);
    }

    static void DetachLiveMtarGroups_SEH(std::uint32_t* savedMasks)
    {
        if (!g_RemoveMtarAll || !g_OrigAddMtarAll)
            return;

        for (std::size_t i = 0; i < kMaxPlayerImpls; ++i)
        {
            void* impl = g_PlayerImpls[i].load(std::memory_order_acquire);
            if (!impl)
                continue;
            __try
            {
                const std::uint32_t mask = *reinterpret_cast<std::uint32_t*>(
                    static_cast<std::uint8_t*>(impl) + kImplAddedMaskOff);
                savedMasks[i] = mask;
                for (std::uint32_t g = 0; g < 32; ++g)
                {
                    if ((mask & (1u << g)) == 0)
                        continue;
                    g_RemoveMtarAll(impl, g);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                savedMasks[i] = 0;
            }
        }
    }

    static void ReattachLiveMtarGroups_SEH(const std::uint32_t* savedMasks)
    {
        if (!g_RemoveMtarAll || !g_OrigAddMtarAll)
            return;

        for (std::size_t i = 0; i < kMaxPlayerImpls; ++i)
        {
            void* impl = g_PlayerImpls[i].load(std::memory_order_acquire);
            if (!impl || savedMasks[i] == 0)
                continue;
            __try
            {
                for (std::uint32_t g = 0; g < 32; ++g)
                {
                    if ((savedMasks[i] & (1u << g)) == 0)
                        continue;
                    g_OrigAddMtarAll(impl, g);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
    }

    struct SearchHit
    {
        void* weak = nullptr;
    };

    static bool MtarArchiveResident(void* file)
    {
        if (!file)
            return false;
        bool ok = false;
        __try
        {
            ok = *reinterpret_cast<void**>(
                     static_cast<std::uint8_t*>(file) + kMtarDataOff) != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ok = false;
        }
        return ok;
    }

    static void* TakeArchiveFromBlock(void* blk, std::uint64_t pathId,
                                      bool requireResident, void** weak)
    {
        void* f = g_OrigBlockGetFileForPathId(blk, pathId);
        if (!f)
            return nullptr;
        if (requireResident && !MtarArchiveResident(f))
        {
            if (weak && !*weak)
                *weak = f;
            return nullptr;
        }
        return f;
    }

    static void* SearchPlayerBlocksForPathId_SEH(
        void* skipBlock, std::uint64_t pathId, SearchHit* hit = nullptr,
        bool requireResident = false)
    {
        void* weakHit = nullptr;
        static const std::size_t kLoaderOffsets[] = {
            kCtlMainLoaderOff, kCtlLoaderOff };

        void* found = nullptr;
        __try
        {
            for (std::size_t ci = 0; ci < kMaxMotionCtls && !found; ++ci)
            {
                void* ctl = g_MotionBlockCtls[ci].load(std::memory_order_acquire);
                if (!ctl)
                    continue;

                for (std::size_t li = 0;
                     li < sizeof(kLoaderOffsets) / sizeof(kLoaderOffsets[0])
                         && !found;
                     ++li)
                {
                    auto* loader = *reinterpret_cast<std::uint8_t**>(
                        static_cast<std::uint8_t*>(ctl) + kLoaderOffsets[li]);
                    if (!loader)
                        continue;

                    auto* group = *reinterpret_cast<std::uint8_t**>(
                        loader + kLoaderGroupOff);
                    if (!group)
                        continue;

                    std::uint32_t count = *reinterpret_cast<std::uint32_t*>(
                        group + kGroupCountOff);
                    if (count > 64)
                        count = 64;

                    for (std::uint32_t i = 0; i < count && !found; ++i)
                    {
                        void* blk = g_GetBlockAtIndex(group, i);
                        if (!blk || blk == skipBlock)
                            continue;
                        found = TakeArchiveFromBlock(
                            blk, pathId, requireResident, &weakHit);
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            found = nullptr;
        }

        for (std::size_t i = 0; i < kMaxSeenBlocks && !found; ++i)
        {
            void* blk = g_SeenBlocks[i].load(std::memory_order_acquire);
            if (!blk || blk == skipBlock)
                continue;
            __try
            {
                found = TakeArchiveFromBlock(
                    blk, pathId, requireResident, &weakHit);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_SeenBlocks[i].store(nullptr, std::memory_order_release);
                found = nullptr;
            }
        }

        if (hit)
            hit->weak = weakHit;
        return found;
    }

    static void* __fastcall hkBlockGetFileForPathId(
        void* block, std::uint64_t pathId)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigBlockGetFileForPathId, block, pathId);

        RememberBlock(block);

        if (g_InMotionFallback)
            return g_OrigBlockGetFileForPathId(block, pathId);

        const int askedSlot = g_FallbackReady
            ? outfit::MotionMtarSlotFromVanillaHash(pathId)
            : -1;

        if (askedSlot >= 0)
        {
            const std::uint64_t custom =
                ResolveDesiredHash_SEH(pathId, nullptr);
            if (custom != 0 && outfit::IsMotionMtarOverrideHash(custom, nullptr))
            {
                g_InMotionFallback = true;
                void* sub = g_OrigBlockGetFileForPathId(block, custom);
                if (!sub)
                    sub = SearchPlayerBlocksForPathId_SEH(block, custom);
                g_InMotionFallback = false;
                if (sub)
                {
                    NoteDirectSlotBinding(askedSlot, sub, custom);
                    return sub;
                }
            }
        }

        g_InMotionFallback = true;
        void* file = g_OrigBlockGetFileForPathId(block, pathId);
        if (file)
        {
            g_InMotionFallback = false;
            NoteDirectSlotBinding(askedSlot, file, pathId);
            return file;
        }

        int slot = -1;
        if (!outfit::IsMotionMtarOverrideHash(pathId, &slot))
        {
            g_InMotionFallback = false;
            return nullptr;
        }

        void* found = SearchPlayerBlocksForPathId_SEH(block, pathId);
        if (!found)
        {
            std::uint64_t vanillaHash = 0;
            if (slot >= 0)
                vanillaHash = outfit::MotionMtarVanillaHash(
                    static_cast<std::size_t>(slot));
            if (vanillaHash != 0)
                found = g_OrigBlockGetFileForPathId(block, vanillaHash);

            static std::atomic<int> s_missWarn{ 0 };
            if (s_missWarn.fetch_add(1) < 64)
                LogWarn("OutfitMotionMtar: mtar %016llX (slot %d) not in a player2 motion fpk, %s",
                    pathId, slot,
                    found ? "vanilla motion used" : "slot left empty");
        }
        g_InMotionFallback = false;
        return found;
    }

    static void DirectSwapEntryFile_SEH(
        std::uint32_t idx, std::uint64_t* pathPtr)
    {
        __try
        {
            const std::uint64_t hash = *pathPtr;
            if (hash == 0)
                return;
            void** filePtr = reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(pathPtr) + 0x10);
            void* file = SearchPlayerBlocksForPathId_SEH(nullptr, hash);
            if (file)
            {
                if (*filePtr != file)
                {
                    if (idx < kMotionTableCount)
                    {
                        if (!g_EntrySwaps[idx].valid)
                            g_SwapsLive.fetch_add(1, std::memory_order_relaxed);
                        if (!g_EntrySwaps[idx].valid
                            || *filePtr != g_EntrySwaps[idx].written)
                            g_EntrySwaps[idx].original = *filePtr;
                        g_EntrySwaps[idx].valid = true;
                    }
                    *filePtr = file;
                    if (idx < kMotionTableCount)
                        g_EntrySwaps[idx].written = file;
                }
            }
            else
            {
                if (idx < kMotionTableCount
                    && g_EntrySwaps[idx].valid
                    && g_EntrySwaps[idx].original != nullptr
                    && *filePtr == g_EntrySwaps[idx].written)
                {
                    *filePtr = g_EntrySwaps[idx].original;
                    g_EntrySwaps[idx].original = nullptr;
                    g_EntrySwaps[idx].written  = nullptr;
                    g_EntrySwaps[idx].valid    = false;
                    g_SwapsLive.fetch_sub(1, std::memory_order_relaxed);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void RefreshDirectSlotTargets()
    {
        const std::uint32_t mask = g_DirectSlotMask.load(std::memory_order_acquire);
        if (mask == 0)
            return;
        for (std::uint32_t slot = 0; slot < kMotionTableCount; ++slot)
        {
            if ((mask & (1u << slot)) == 0)
                continue;
            const std::uint64_t vanillaHash =
                outfit::MotionMtarVanillaHash(static_cast<std::size_t>(slot));
            if (vanillaHash == 0)
                continue;
            int resolved = -1;
            std::uint64_t want = ResolveDesiredHash_SEH(vanillaHash, &resolved);
            if (want == 0)
                want = vanillaHash;
            const std::uint64_t haveHash =
                g_DirectSlots[slot].boundHash.load(std::memory_order_acquire);
            if (haveHash == 0 || want == haveHash)
            {
                g_DirectSlots[slot].desired.store(nullptr,
                                                 std::memory_order_release);
                continue;
            }
            SearchHit hit{};
            void* file =
                SearchPlayerBlocksForPathId_SEH(nullptr, want, &hit, true);
            if (!file)
            {
                g_DirectSlots[slot].desired.store(nullptr,
                                                 std::memory_order_release);
                static std::atomic<int> s_dead{ 0 };
                if (s_dead.fetch_add(1, std::memory_order_relaxed) < 8)
                    LogWarn("OutfitMotionMtar: slot %u cannot switch to mtar %016llX (%s), old motion until next load",
                        slot, want,
                        hit.weak ? "not resident" : "not loaded");
                continue;
            }
            g_DirectSlots[slot].desired.exchange(
                file, std::memory_order_acq_rel);
        }
    }

    static void ClearDirectSlotBindings()
    {
        for (std::uint32_t slot = 0; slot < kMotionTableCount; ++slot)
        {
            g_DirectSlots[slot].bound.store(nullptr, std::memory_order_release);
            g_DirectSlots[slot].boundHash.store(0, std::memory_order_release);
            g_DirectSlots[slot].desired.store(nullptr, std::memory_order_release);
        }
        g_DirectSlotMask.store(0, std::memory_order_release);
    }

    static void RevertSwappedEntries_SEH()
    {
        if (!g_OrigGetMtarPathId)
            return;

        for (std::uint32_t idx = 0; idx < kMotionTableCount; ++idx)
        {
            if (!g_EntrySwaps[idx].valid)
                continue;
            __try
            {
                std::uint64_t out = 0;
                std::uint64_t* r = g_OrigGetMtarPathId(&out, idx);
                if (r)
                {
                    void** filePtr = reinterpret_cast<void**>(
                        reinterpret_cast<std::uint8_t*>(r) + 0x10);
                    if (*filePtr == g_EntrySwaps[idx].written)
                        *filePtr = g_EntrySwaps[idx].original;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
            g_EntrySwaps[idx].original = nullptr;
            g_EntrySwaps[idx].written  = nullptr;
            g_EntrySwaps[idx].valid    = false;
        }
        g_SwapsLive.store(0, std::memory_order_relaxed);
    }
}

namespace outfit
{
    static std::uint64_t ReadMtarPath_SEH(void* mtar)
    {
        std::uint64_t path = 0;
        __try
        {
            path = *reinterpret_cast<std::uint64_t*>(
                static_cast<std::uint8_t*>(mtar) + kMtarPathOff);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            path = 0;
        }
        return path;
    }

    static uintptr_t MtarAddAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:  return 0x141a6b8f0ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:  return 0x141a6b860ull;
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:  return 0x141a6bb80ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:  return 0x141a6bca0ull;
        default:                                           return 0;
        }
    }

    static uintptr_t MtarRemoveAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:  return 0x141a6cc90ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:  return 0x141a6cc00ull;
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:  return 0x141a6cf20ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:  return 0x141a6d040ull;
        default:                                           return 0;
        }
    }

    static void NoteCustomMtarHolder(void* holder)
    {
        if (!holder)
            return;
        for (std::size_t i = 0; i < kMaxMtarHolders; ++i)
        {
            void* cur = g_MtarHolders[i].load(std::memory_order_acquire);
            if (cur == holder)
                return;
            if (cur == nullptr)
            {
                void* expected = nullptr;
                if (g_MtarHolders[i].compare_exchange_strong(
                        expected, holder, std::memory_order_acq_rel))
                    return;
                if (expected == holder)
                    return;
            }
        }
    }

    static int __fastcall hkMtarAdd(void* holder, void* mtarFile)
    {
        const int r = g_AddMtar(holder, mtarFile);
        if (r != 0 && holder && mtarFile && AnyMotionMtarOverridesRegistered())
        {
            const std::uint64_t held = ReadMtarPath_SEH(mtarFile);
            if (held != 0 && IsMotionMtarOverrideHash(held, nullptr))
                NoteCustomMtarHolder(holder);
        }
        return r;
    }

    void NoteAnimControl(void* animControl)
    {
        if (!animControl)
            return;
        for (std::size_t i = 0; i < kMaxAnimCtls; ++i)
        {
            void* cur = g_AnimCtls[i].load(std::memory_order_acquire);
            if (cur == animControl)
                return;
            if (cur == nullptr)
            {
                void* expected = nullptr;
                if (g_AnimCtls[i].compare_exchange_strong(
                        expected, animControl, std::memory_order_acq_rel))
                    return;
                if (expected == animControl)
                    return;
            }
        }
    }

    struct CustomBinding
    {
        void*         file = nullptr;
        std::uint64_t hash = 0;
        int           slot = -1;
    };

    static int CollectCustomBindings_SEH(
        void* ac, CustomBinding* out, int maxOut)
    {
        int found = 0;
        __try
        {
            auto* base = static_cast<std::uint8_t*>(ac);
            const std::uint32_t count =
                *reinterpret_cast<std::uint32_t*>(base + kAnimCtlCountOff);
            auto* arr = *reinterpret_cast<std::uint8_t**>(
                base + kAnimCtlArrayOff);
            if (!arr || count == 0 || count > 64)
                return 0;
            for (std::uint32_t i = 0; i < count && found < maxOut; ++i)
            {
                auto* e = arr + i * kAnimEntryStride;
                void* file = *reinterpret_cast<void**>(e + kAnimEntryFileOff);
                if (!file)
                    continue;
                const std::uint64_t held = *reinterpret_cast<std::uint64_t*>(
                    static_cast<std::uint8_t*>(file) + kMtarPathOff);
                int slot = -1;
                if (held == 0 || !outfit::IsMotionMtarOverrideHash(held, &slot))
                    continue;
                out[found].file = file;
                out[found].hash = held;
                out[found].slot = slot;
                ++found;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return found;
    }

    static bool IsFileBound_SEH(void* ac, void* file)
    {
        bool bound = false;
        __try
        {
            auto* base = static_cast<std::uint8_t*>(ac);
            const std::uint32_t count =
                *reinterpret_cast<std::uint32_t*>(base + kAnimCtlCountOff);
            auto* arr = *reinterpret_cast<std::uint8_t**>(
                base + kAnimCtlArrayOff);
            if (!arr || count > 64)
                return false;
            for (std::uint32_t i = 0; i < count && !bound; ++i)
                bound = *reinterpret_cast<void**>(
                    arr + i * kAnimEntryStride + kAnimEntryFileOff) == file;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            bound = false;
        }
        return bound;
    }

    static bool SwapBinding_SEH(void* ac, void* oldFile, void* newFile)
    {
        bool ok = false;
        __try
        {
            const bool alreadyBound = IsFileBound_SEH(ac, newFile);
            g_RemoveMtar(ac, oldFile);
            if (alreadyBound)
                ok = true;
            else if (g_AddMtar(ac, newFile) != 0)
                ok = true;
            else
            {
                g_AddMtar(ac, oldFile);
                ok = false;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ok = false;
        }
        return ok;
    }

    static int RebindHolderToVanilla(void* ac)
    {
        CustomBinding held[32]{};
        const int n = CollectCustomBindings_SEH(ac, held, 32);
        if (n == 0)
            return 0;

        for (int i = 0; i < n; ++i)
        {
            const std::uint64_t want = outfit::MotionMtarVanillaHash(
                static_cast<std::size_t>(held[i].slot));
            if (want == 0 || want == held[i].hash)
                continue;
            SearchHit hit{};
            void* vanilla =
                SearchPlayerBlocksForPathId_SEH(nullptr, want, &hit, true);
            if (!vanilla || vanilla == held[i].file)
            {
                static std::atomic<int> s_miss{ 0 };
                if (s_miss.fetch_add(1, std::memory_order_relaxed) < 8)
                    LogWarn("OutfitMotionMtar: slot %d keeps custom mtar %016llX, vanilla %016llX not resident",
                        held[i].slot, held[i].hash, want);
                continue;
            }
            if (!SwapBinding_SEH(ac, held[i].file, vanilla))
            {
                static std::atomic<int> s_full{ 0 };
                if (s_full.fetch_add(1, std::memory_order_relaxed) < 4)
                    LogWarn("OutfitMotionMtar: slot %d anim control full, keeps custom motion instead of %016llX",
                        held[i].slot, want);
            }
        }
        return n;
    }

    static void RebindAnimControlsToVanilla()
    {
        if (!g_AddMtar || !g_RemoveMtar)
            return;

        void*       visited[kMaxMtarHolders + kMaxAnimCtls] = {};
        std::size_t visitedCount = 0;
        int         totalFound   = 0;

        for (std::size_t pass = 0; pass < 2; ++pass)
        {
            const std::size_t count = pass == 0 ? kMaxMtarHolders : kMaxAnimCtls;
            for (std::size_t i = 0; i < count; ++i)
            {
                void* ac = pass == 0
                    ? g_MtarHolders[i].load(std::memory_order_acquire)
                    : g_AnimCtls[i].load(std::memory_order_acquire);
                if (!ac)
                    continue;
                bool seen = false;
                for (std::size_t v = 0; v < visitedCount && !seen; ++v)
                    seen = visited[v] == ac;
                if (seen)
                    continue;
                visited[visitedCount++] = ac;
                totalFound += RebindHolderToVanilla(ac);
            }
        }

        if (totalFound == 0 && g_SwapsLive.load(std::memory_order_relaxed) != 0)
        {
            static std::atomic<int> s_none{ 0 };
            if (s_none.fetch_add(1, std::memory_order_relaxed) < 8)
                LogWarn("OutfitMotionMtar: no watched mtar holder has the custom motion, it stays until next load");
        }
    }

    void* RedirectMotionMtarForClip(void* mtar)
    {
        if (!mtar)
            return nullptr;
        const std::uint32_t mask = g_DirectSlotMask.load(std::memory_order_acquire);
        if (mask == 0)
            return nullptr;

        const std::uint64_t livePath = ReadMtarPath_SEH(mtar);

        for (std::uint32_t slot = 0; slot < kMotionTableCount; ++slot)
        {
            if ((mask & (1u << slot)) == 0)
                continue;
            void* want = g_DirectSlots[slot].desired.load(std::memory_order_acquire);
            if (!want || want == mtar)
                continue;

            const bool byPath =
                livePath != 0
                && livePath == g_DirectSlots[slot].boundHash.load(
                                   std::memory_order_acquire);
            const bool byPtr =
                g_DirectSlots[slot].bound.load(std::memory_order_acquire) == mtar;
            if (!byPath && !byPtr)
                continue;

            return want;
        }

        return nullptr;
    }

    void RevertAdditionalMotionSwaps()
    {
        g_PartsPipelineBusy.store(false, std::memory_order_relaxed);
        ClearDirectSlotBindings();
        for (std::size_t i = 0; i < kMaxMtarHolders; ++i)
            g_MtarHolders[i].store(nullptr, std::memory_order_release);

        const std::uint32_t mission =
            static_cast<std::uint32_t>(MissionCodeGuard::GetCurrentMissionCode());

        const int live = g_SwapsLive.load(std::memory_order_relaxed);
        if (!g_Installed || live == 0)
        {
            g_SwapEpochMission.store(mission, std::memory_order_relaxed);
            return;
        }

        RevertSwappedEntries_SEH();
        g_SwapEpochMission.store(mission, std::memory_order_relaxed);
    }

    static void RunAdditionalMotionReresolve()
    {
        for (std::size_t ci = 0; ci < kMaxMotionCtls; ++ci)
        {
            void* ctl = g_MotionBlockCtls[ci].load(std::memory_order_acquire);
            if (!ctl)
                continue;
            __try
            {
                auto* base = static_cast<std::uint8_t*>(ctl);
                if (*reinterpret_cast<void**>(base + kCtlLoaderOff) == nullptr)
                    continue;
                *reinterpret_cast<std::uint32_t*>(base + kCtlWalkIndexOff) = 0;
                *reinterpret_cast<std::uint32_t*>(base + kCtlWalkStateOff) = 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        std::uint32_t savedMasks[kMaxPlayerImpls] = {};
        DetachLiveMtarGroups_SEH(savedMasks);

        const bool wasInFallback = g_InMotionFallback;
        g_InMotionFallback = true;
        for (std::uint32_t idx = 0; idx < kMotionTableCount; ++idx)
        {
            std::uint64_t out = 0;
            std::uint64_t* r = hkGetMtarPathId(&out, idx);
            if (!r)
                continue;
            DirectSwapEntryFile_SEH(idx, r);
        }
        RefreshDirectSlotTargets();
        g_InMotionFallback = wasInFallback;

        ReattachLiveMtarGroups_SEH(savedMasks);
    }

    void RequestAdditionalMotionReresolve()
    {
        const std::uint32_t mission =
            static_cast<std::uint32_t>(MissionCodeGuard::GetCurrentMissionCode());
        if (g_SwapEpochMission.load(std::memory_order_relaxed) != mission)
            RevertAdditionalMotionSwaps();

        MISSION_GUARD_RETURN_VOID();
        if (!g_FallbackReady)
            return;
        if (!outfit::AnyMotionMtarOverridesRegistered())
            return;

        if (g_PartsPipelineBusy.load(std::memory_order_relaxed))
        {
            g_ReresolveDeferTicks.store(kReresolveMaxDeferTicks,
                                        std::memory_order_relaxed);
            g_ReresolvePending.store(true, std::memory_order_relaxed);
            return;
        }

        RunAdditionalMotionReresolve();
    }

    void NotePartsPipelineBusy(bool busy)
    {
        g_PartsPipelineBusy.store(busy, std::memory_order_relaxed);

        if (!g_ReresolvePending.load(std::memory_order_relaxed))
            return;
        if (MissionCodeGuard::ShouldBypassHooks())
            return;

        if (busy)
        {
            const int left =
                g_ReresolveDeferTicks.load(std::memory_order_relaxed);
            if (left > 1)
            {
                g_ReresolveDeferTicks.store(left - 1,
                                            std::memory_order_relaxed);
                return;
            }
            g_ReresolveDeferTicks.store(0, std::memory_order_relaxed);

            static std::atomic<int> s_lateLogged{ 0 };
            if (s_lateLogged.fetch_add(1, std::memory_order_relaxed) < 8)
                LogWarn("OutfitMotionMtar: parts still loading after %d ticks, motion re-bind forced, a slot may keep the old suit",
                    kReresolveMaxDeferTicks);
        }

        g_ReresolvePending.store(false, std::memory_order_relaxed);

        const std::uint32_t mission =
            static_cast<std::uint32_t>(MissionCodeGuard::GetCurrentMissionCode());
        if (g_SwapEpochMission.load(std::memory_order_relaxed) != mission)
        {
            RevertAdditionalMotionSwaps();
            return;
        }
        if (!g_FallbackReady) return;
        if (!outfit::AnyMotionMtarOverridesRegistered()) return;

        RunAdditionalMotionReresolve();
    }

    void NoteLiveOutfitIdentity(unsigned char partsType, bool settled)
    {
        if (!g_Installed || !g_FallbackReady)
            return;
        if (!settled)
            return;

        const std::uint8_t parts = static_cast<std::uint8_t>(partsType);
        if (parts >= outfit::kCustomPartsTypeStart
            && parts <= outfit::kCustomPartsTypeEnd)
            return;

        const std::uint8_t hint = outfit::GetMotionOutfitHintPartsType();
        const bool hintIsCustom = hint >= outfit::kCustomPartsTypeStart
                               && hint <= outfit::kCustomPartsTypeEnd;
        if (!hintIsCustom && g_SwapsLive.load(std::memory_order_relaxed) == 0)
            return;

        outfit::ClearMotionOutfitHint();

        RequestAdditionalMotionReresolve();
        RebindAnimControlsToVanilla();
    }

    bool Install_OutfitMotionMtar_Hook()
    {
        if (g_Installed) return true;

        void* target = ResolveGameAddress(gAddr.AdditionalMotionTable_GetMtarPathId);
        if (!target)
        {
            LogError("OutfitMotionMtar: GetMtarPathId address missing, motionMtars off");
            return false;
        }

        g_Installed = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkGetMtarPathId),
            reinterpret_cast<void**>(&g_OrigGetMtarPathId));

        if (!g_Installed)
        {
            LogError("OutfitMotionMtar: GetMtarPathId hook failed, motionMtars off");
            return false;
        }

        void* updateTarget = ResolveGameAddress(
            gAddr.BlockControllerImpl_UpdateAdditionalMotionBlock);
        void* getFileTarget = ResolveGameAddress(gAddr.Fox_Block_GetFileForPathId);
        void* getBlockAt    = ResolveGameAddress(gAddr.Fox_BlockGroup_GetBlockAtIndex);

        if (!updateTarget || !getFileTarget || !getBlockAt)
        {
            LogError("OutfitMotionMtar: block lookup address missing, motionMtars off");
            return g_Installed;
        }

        g_GetBlockAtIndex = reinterpret_cast<GetBlockAtIndex_t>(getBlockAt);

        const bool updateOk = CreateAndEnableHook(
            updateTarget,
            reinterpret_cast<void*>(&hkUpdateAdditionalMotionBlock),
            reinterpret_cast<void**>(&g_OrigUpdateMotionBlock));
        const bool getFileOk = updateOk && CreateAndEnableHook(
            getFileTarget,
            reinterpret_cast<void*>(&hkBlockGetFileForPathId),
            reinterpret_cast<void**>(&g_OrigBlockGetFileForPathId));

        if (!updateOk || !getFileOk)
        {
            LogError("OutfitMotionMtar: block lookup hook failed, motionMtars off");
            if (updateOk)
                DisableAndRemoveHook(updateTarget);
            g_OrigUpdateMotionBlock = nullptr;
            return g_Installed;
        }

        g_FallbackReady = true;

        void* addAllTarget = ResolveGameAddress(
            gAddr.Player2Impl_AddAdditionalMtarAll);
        void* removeAll = ResolveGameAddress(
            gAddr.Player2Impl_RemoveAdditionalMtarAll);

        if (!addAllTarget || !removeAll)
        {
            LogError("OutfitMotionMtar: Add/RemoveAdditionalMtarAll address missing, motionMtars change only on player rebuild");
            return g_Installed;
        }

        if (!CreateAndEnableHook(
                addAllTarget,
                reinterpret_cast<void*>(&hkAddAdditionalMtarAll),
                reinterpret_cast<void**>(&g_OrigAddMtarAll)))
        {
            LogError("OutfitMotionMtar: AddAdditionalMtarAll hook failed, motionMtars change only on player rebuild");
            g_OrigAddMtarAll = nullptr;
            return g_Installed;
        }

        g_RemoveMtarAll = reinterpret_cast<AdditionalMtarAll_t>(removeAll);

        void* addMtar    = ResolveGameAddress(MtarAddAddr());
        void* removeMtar = ResolveGameAddress(MtarRemoveAddr());
        if (addMtar && removeMtar)
        {
            g_RemoveMtar = reinterpret_cast<MtarRemove_t>(removeMtar);
            if (!CreateAndEnableHook(
                    addMtar,
                    reinterpret_cast<void*>(&hkMtarAdd),
                    reinterpret_cast<void**>(&g_AddMtar)))
            {
                g_AddMtar = reinterpret_cast<MtarAdd_t>(addMtar);
                LogError("OutfitMotionMtar: AddMtar hook failed, vanilla outfit may keep custom motion until next load");
            }
        }
        else
        {
            LogError("OutfitMotionMtar: AddMtar/RemoveMtar address missing, vanilla outfit keeps custom motion until next load");
        }

        return g_Installed;
    }

    void Uninstall_OutfitMotionMtar_Hook()
    {
        if (!g_Installed) return;
        g_FallbackReady = false;
        ClearDirectSlotBindings();
        if (g_OrigAddMtarAll)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.Player2Impl_AddAdditionalMtarAll))
                DisableAndRemoveHook(t);
            g_OrigAddMtarAll = nullptr;
        }
        g_RemoveMtarAll = nullptr;
        if (void* t = ResolveGameAddress(MtarAddAddr()))
            DisableAndRemoveHook(t);
        g_AddMtar       = nullptr;
        g_RemoveMtar    = nullptr;
        for (std::size_t i = 0; i < kMaxAnimCtls; ++i)
            g_AnimCtls[i].store(nullptr, std::memory_order_release);
        for (std::size_t i = 0; i < kMaxMtarHolders; ++i)
            g_MtarHolders[i].store(nullptr, std::memory_order_release);
        for (std::size_t i = 0; i < kMaxPlayerImpls; ++i)
            g_PlayerImpls[i].store(nullptr, std::memory_order_release);
        if (void* t = ResolveGameAddress(gAddr.Fox_Block_GetFileForPathId))
            DisableAndRemoveHook(t);
        if (void* t = ResolveGameAddress(
                gAddr.BlockControllerImpl_UpdateAdditionalMotionBlock))
            DisableAndRemoveHook(t);
        if (void* t = ResolveGameAddress(gAddr.AdditionalMotionTable_GetMtarPathId))
            DisableAndRemoveHook(t);
        g_OrigBlockGetFileForPathId = nullptr;
        g_OrigUpdateMotionBlock     = nullptr;
        g_OrigGetMtarPathId         = nullptr;
        for (std::size_t i = 0; i < kMaxMotionCtls; ++i)
            g_MotionBlockCtls[i].store(nullptr, std::memory_order_release);
        g_Installed = false;
    }
}
