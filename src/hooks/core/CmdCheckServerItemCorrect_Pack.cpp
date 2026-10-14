#include "pch.h"
#include "CmdCheckServerItemCorrect_Pack.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "AddressSet.h"
#include "DevelopArrayGrow.h"
#include "HookUtils.h"
#include "../outfit/CustomHeadRegistry.h"
#include "log.h"

namespace
{
    using Pack_t = std::uint64_t(__fastcall*)(void* self);
    using QuarkTable_t = std::uint8_t* (__fastcall*)();
    using ReadLoadout_t = void(__fastcall*)(void* holder, std::uint8_t* out, std::uint64_t flag);

    constexpr std::size_t   kItemListNum = 0x50;
    constexpr std::size_t   kItemList    = 0x54;    // develop ids of everything equipped
    constexpr std::size_t   kItemCount   = 0x554;
    constexpr std::uint32_t kItemMax     = 0x140;
    constexpr std::uint32_t kHeadEntry   = 1;       // builder lists the head right after the suit when loadout byte 3 is set
    constexpr std::uint32_t kNoEntry     = 0xFFFFFFFF;

    constexpr std::size_t kApp_LoadoutHolder = 0x130;
    constexpr std::size_t kSlot_ReadLoadout  = 0xC8;
    constexpr std::size_t kLoadoutSize       = 0xE8;
    constexpr std::size_t kLoadoutHead       = 3;

    const std::uint8_t kPrologue154[] = {
        0x40, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF,
        0x48, 0x89, 0x5C, 0x24, 0x50, 0x48, 0x89, 0x74, 0x24, 0x58, 0x48, 0x8B, 0xD9,
        0x48, 0x8D, 0x71, 0x08
    };
    const std::uint8_t kPrologue153[] = {
        0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF,
        0x48, 0x89, 0x5C, 0x24, 0x50, 0x48, 0x89, 0x74, 0x24, 0x58, 0x48, 0x89, 0xCB,
        0x48, 0x8D, 0x71, 0x08
    };

    struct PackTarget
    {
        std::uintptr_t      addr;
        const std::uint8_t* prologue;
        std::size_t         size;
    };

    Pack_t g_Orig = nullptr;
    void*  g_Addr = nullptr;

    std::mutex        g_ScanLock;
    std::uint64_t     g_CustomIds[equip::kDevelopIdBitWords];
    std::atomic<bool> g_ScanFailLogged{ false };

    PackTarget Target()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:  return { 0x145B4DE40ull, kPrologue153, sizeof(kPrologue153) };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:  return { 0x14752AD10ull, kPrologue153, sizeof(kPrologue153) };
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:  return { 0x140835160ull, kPrologue154, sizeof(kPrologue154) };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:  return { 0x1408351C0ull, kPrologue154, sizeof(kPrologue154) };
        default:                                           return {};
        }
    }

    bool PrologueMatchesSEH(const void* fn, const PackTarget& target)
    {
        __try
        {
            return std::memcmp(fn, target.prologue, target.size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadLoadoutHeadSEH(std::uint8_t* quark, std::uint8_t& head)
    {
        __try
        {
            const std::uint8_t* app = *reinterpret_cast<std::uint8_t* const*>(quark + 0x98);
            void* holder = app ? *reinterpret_cast<void* const*>(app + kApp_LoadoutHolder) : nullptr;
            if (!holder)
                return false;
            std::uint8_t loadout[kLoadoutSize] = {};
            const auto vtbl = *reinterpret_cast<const std::uintptr_t*>(holder);
            const auto read = *reinterpret_cast<const ReadLoadout_t*>(vtbl + kSlot_ReadLoadout);
            read(holder, loadout, 0);   // same object, slot and arguments as the item check builder
            head = loadout[kLoadoutHead];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool WearsCustomHead()
    {
        const auto quarkTable = reinterpret_cast<QuarkTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
        std::uint8_t* quark = quarkTable ? quarkTable() : nullptr;
        std::uint8_t head = 0;
        return quark && ReadLoadoutHeadSEH(quark, head) && head >= outfit::kCustomHeadSlotBase;
    }

    void DropCustomIdsSEH(std::uint8_t* self, const std::uint64_t* custom, std::uint32_t dropEntry)
    {
        __try
        {
            std::uint32_t count = *reinterpret_cast<const std::uint32_t*>(self + kItemCount);
            if (count > kItemMax)
                count = kItemMax;
            auto* ids = reinterpret_cast<std::uint32_t*>(self + kItemList);
            std::uint32_t kept = 0;
            for (std::uint32_t i = 0; i < count; ++i)
            {
                const std::uint32_t id = ids[i];
                if (i == dropEntry)
                    continue;
                if (custom && id <= 0xFFFF && (custom[id >> 6] >> (id & 63) & 1))
                    continue;
                ids[kept++] = id;
            }
            if (kept == count)
                return;
            std::memset(ids + kept, 0, (count - kept) * sizeof(std::uint32_t));
            *reinterpret_cast<std::uint32_t*>(self + kItemCount) = kept;
            *reinterpret_cast<std::uint32_t*>(self + kItemListNum) = kept;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    std::uint64_t __fastcall hkPack(void* self)
    {
        if (self)
        {
            const std::uint32_t dropEntry = WearsCustomHead() ? kHeadEntry : kNoEntry;
            std::lock_guard<std::mutex> lock(g_ScanLock);
            const bool scanned = equip::ScanCustomOnlyDevelopIds(g_CustomIds, equip::kDevelopIdBitWords);
            if (!scanned && !g_ScanFailLogged.exchange(true))
                LogWarn("ItemCheckGuard: R&D table unreadable, custom R&D ids in CMD_CHECK_SERVER_ITEM_CORRECT sent as is");
            DropCustomIdsSEH(static_cast<std::uint8_t*>(self), scanned ? g_CustomIds : nullptr, dropEntry);
        }
        return g_Orig(self);
    }
}

bool Install_CmdCheckServerItemCorrect_Pack()
{
    const PackTarget target = Target();
    if (target.addr == 0)
    {
        LogError("ItemCheckGuard: CMD_CHECK_SERVER_ITEM_CORRECT pack address missing for %s, equipped custom items reach the server",
            GetGameBuildName(gGameBuild));
        return false;
    }

    void* fn = ResolveGameAddress(target.addr);
    if (!fn || !PrologueMatchesSEH(fn, target))
    {
        LogError("ItemCheckGuard: CMD_CHECK_SERVER_ITEM_CORRECT pack prologue mismatch on %s, equipped custom items reach the server",
            GetGameBuildName(gGameBuild));
        return false;
    }

    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkPack),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ItemCheckGuard: CMD_CHECK_SERVER_ITEM_CORRECT pack hook failed on %s, equipped custom items reach the server",
            GetGameBuildName(gGameBuild));
        return false;
    }

    g_Addr = fn;
    return true;
}

void Uninstall_CmdCheckServerItemCorrect_Pack()
{
    if (g_Addr)
    {
        DisableAndRemoveHook(g_Addr);
        g_Addr = nullptr;
    }
    g_Orig = nullptr;
}
