#include "pch.h"
#include "LoadoutManagerImpl_StoreLoadoutInfoToPlayerVars.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using StoreLoadoutInfoFn  = void(__fastcall*)(void* self);
    using GetQuarkSystemTable = std::uint8_t* (__fastcall*)();

    constexpr std::size_t  kLoadoutArray     = 0x10;
    constexpr std::size_t  kLoadoutSlotByte  = 0x3B4;
    constexpr std::size_t  kLoadoutStride    = 0xE8;
    constexpr std::size_t  kItemCellFirst    = 0x74;
    constexpr std::size_t  kItemCellStride   = 4;
    constexpr int          kItemCellCount    = 8;
    constexpr std::size_t  kQuarkApp         = 0x98;
    constexpr std::size_t  kAppScriptVars    = 0x10;
    constexpr std::size_t  kItemLevelBase    = 0x9A4;
    constexpr std::int32_t kItemWindowFirst  = 0x1C2;
    constexpr std::int32_t kItemWindowLast   = 0x209;

    const std::uint8_t kPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
        0x48, 0x89, 0x7C, 0x24, 0x20, 0x55, 0x41, 0x54, 0x41, 0x55,
        0x41, 0x56, 0x41, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x70
    };

    const std::uint8_t kPrologueClone[sizeof(kPrologue)] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
        0x48, 0x89, 0x7C, 0x24, 0x20, 0x55, 0x41, 0x54, 0x41, 0x55,
        0x41, 0x56, 0x41, 0x57, 0x48, 0x89, 0xE5, 0x48, 0x83, 0xEC, 0x70
    };

    struct GuardedByte
    {
        std::uint8_t* at    = nullptr;
        std::uint8_t  value = 0;
    };

    StoreLoadoutInfoFn g_Orig = nullptr;
    void*              g_Addr = nullptr;
    bool               g_Reported = false;

    std::uintptr_t StoreLoadoutInfoAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:  return 0x140EEF130ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:  return 0x140EEF160ull;
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:  return 0x1492FD310ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:  return 0x149CC0030ull;
        default:                                           return 0;
        }
    }

    const std::uint8_t* ExpectedPrologue()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:  return kPrologueClone;
        default:                                           return kPrologue;
        }
    }

    bool PrologueMatchesSEH(const void* fn)
    {
        __try
        {
            return std::memcmp(fn, ExpectedPrologue(), sizeof(kPrologue)) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint8_t* ResolveScriptVarsSEH()
    {
        auto getQuark = reinterpret_cast<GetQuarkSystemTable>(
            ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (!getQuark)
            return nullptr;
        __try
        {
            std::uint8_t* quark = getQuark();
            if (!quark)
                return nullptr;
            auto* app = *reinterpret_cast<std::uint8_t**>(quark + kQuarkApp);
            if (!app)
                return nullptr;
            return *reinterpret_cast<std::uint8_t**>(app + kAppScriptVars);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    int CollectStompedBytesSEH(void* self, std::uint8_t* vars,
                               GuardedByte* out, int capacity)
    {
        __try
        {
            auto* base = static_cast<std::uint8_t*>(self);
            const std::size_t slot = *(base + kLoadoutSlotByte);
            const std::uint8_t* info =
                base + kLoadoutArray + slot * kLoadoutStride;
            int n = 0;
            for (int i = 0; i < kItemCellCount && n < capacity; ++i)
            {
                const std::int32_t id = *reinterpret_cast<const std::uint16_t*>(
                    info + kItemCellFirst + i * kItemCellStride);
                if (id == 0 || (id >= kItemWindowFirst && id <= kItemWindowLast))
                    continue;
                std::uint8_t* at = vars + kItemLevelBase + id;
                out[n].at    = at;
                out[n].value = *at;
                ++n;
            }
            return n;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void RestoreStompedBytesSEH(const GuardedByte* saved, int count)
    {
        __try
        {
            for (int i = 0; i < count; ++i)
                *saved[i].at = saved[i].value;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkStoreLoadoutInfoToPlayerVars(void* self)
    {
        GuardedByte saved[kItemCellCount];
        int guarded = 0;
        std::uint8_t* vars = self ? ResolveScriptVarsSEH() : nullptr;
        if (vars)
            guarded = CollectStompedBytesSEH(self, vars, saved, kItemCellCount);

        g_Orig(self);

        if (guarded == 0)
            return;
        RestoreStompedBytesSEH(saved, guarded);
        if (g_Reported)
            return;
        g_Reported = true;
        LogWarn("LoadoutVars: %d loadout item(s) outside item window 0x%X-0x%X, stray level write undone, item keeps no level",
            guarded, static_cast<unsigned>(kItemWindowFirst), static_cast<unsigned>(kItemWindowLast));
    }
}

bool Install_LoadoutManagerImpl_StoreLoadoutInfoToPlayerVars()
{
    const std::uintptr_t addr = StoreLoadoutInfoAddr();
    if (addr == 0)
    {
        LogError("LoadoutVars: StoreLoadoutInfoToPlayerVars address missing on %s, custom loadout items corrupt script vars",
            GetGameBuildName(gGameBuild));
        return false;
    }

    void* fn = ResolveGameAddress(addr);
    if (!fn || !PrologueMatchesSEH(fn))
    {
        LogError("LoadoutVars: StoreLoadoutInfoToPlayerVars 0x%llX prologue differs on %s, custom loadout items corrupt script vars",
            static_cast<unsigned long long>(addr), GetGameBuildName(gGameBuild));
        return false;
    }

    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkStoreLoadoutInfoToPlayerVars),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("LoadoutVars: StoreLoadoutInfoToPlayerVars hook failed at 0x%llX on %s, custom loadout items corrupt script vars",
            static_cast<unsigned long long>(addr), GetGameBuildName(gGameBuild));
        return false;
    }

    g_Addr = fn;
    return true;
}

void Uninstall_LoadoutManagerImpl_StoreLoadoutInfoToPlayerVars()
{
    if (g_Addr)
    {
        DisableAndRemoveHook(g_Addr);
        g_Addr = nullptr;
    }
    g_Orig = nullptr;
}
