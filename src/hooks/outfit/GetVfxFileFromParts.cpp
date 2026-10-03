#include "pch.h"

#include "GetVfxFileFromParts.h"
#include "OutfitAbilities.h"
#include "OutfitRegistry.h"
#include "BlockControllerImpl_LoadPartsNew.h"
#include "PlayerAppearance_LoadPlayerFv2s.h"
#include "MissionCodeGuard.h"

#include <atomic>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    constexpr std::uint64_t kSprintSparkName = 0x1815C5433172ull;
    constexpr std::size_t   kVfxFile_Resource = 0x80;

    constexpr std::uintptr_t kGetVfxFileFromParts_En154 = 0x1407440E0ull;
    constexpr std::uintptr_t kSparkCallReturn_En154     = 0x140742E02ull;

    constexpr std::uint8_t kPrologue[] =
    {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x48, 0xC7, 0x44, 0x24, 0x20,
        0xFE, 0xFF, 0xFF, 0xFF,
    };

    using GetVfxFileFromParts_t = void* (__fastcall*)(
        void* animationController, std::uint32_t playerIndex, std::uint64_t name);

    static GetVfxFileFromParts_t g_Orig      = nullptr;
    static std::uintptr_t        g_Target    = 0;
    static bool                  g_Installed = false;
    static std::atomic<std::uint64_t> g_WarnedSpark{ 0 };

    static bool ReadCode_SEH(std::uintptr_t addr, void* out, std::size_t n)
    {
        __try
        {
            std::memcpy(out, reinterpret_cast<const void*>(addr), n);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool VfxLoaded_SEH(void* file)
    {
        __try
        {
            return *reinterpret_cast<void* const*>(
                static_cast<const std::uint8_t*>(file) + kVfxFile_Resource) != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void* OutfitSpark(std::uint32_t playerIndex)
    {
        if (playerIndex != outfit::GetLocalPartsSlot()
            || outfit::SprintLive() == outfit::kSprintNone)
            return nullptr;
        const std::uint64_t spark = outfit::SprintSparkLive();
        if (spark == 0 || MissionCodeGuard::ShouldBypassHooks())
            return nullptr;
        void* file = outfit::ddheadlayers::FindLocalHeadFile(spark);
        if (file && VfxLoaded_SEH(file))
            return file;
        if (g_WarnedSpark.exchange(spark, std::memory_order_relaxed) != spark)
            LogWarn("OutfitAbilities: sprintSpark .vfx not loaded, is it in the outfit's pack?");
        return nullptr;
    }

    static void* __fastcall hkGetVfxFileFromParts(
        void* animationController, std::uint32_t playerIndex, std::uint64_t name)
    {
        if (name == kSprintSparkName)
            if (void* file = OutfitSpark(playerIndex))
                return file;
        return g_Orig(animationController, playerIndex, name);
    }

    static bool SiteMatches(std::uintptr_t fn, std::uintptr_t ret)
    {
        std::uint8_t body[sizeof(kPrologue)] = {};
        if (!ReadCode_SEH(fn, body, sizeof(body))
            || std::memcmp(body, kPrologue, sizeof(kPrologue)) != 0)
            return false;
        std::uint8_t call[5] = {};
        if (!ReadCode_SEH(ret - 5, call, sizeof(call)) || call[0] != 0xE8)
            return false;
        std::int32_t rel = 0;
        std::memcpy(&rel, call + 1, sizeof(rel));
        return ret + static_cast<std::intptr_t>(rel) == fn;
    }
}

namespace outfit
{
    bool Install_GetVfxFileFromParts_Hook()
    {
        if (g_Installed) return true;

        if (!IsEn154Family(gGameBuild))
        {
            LogError("OutfitAbilities: no GetVfxFileFromParts address for build %s, sprintSpark off",
                GetGameBuildName(gGameBuild));
            return false;
        }

        const auto fn = reinterpret_cast<std::uintptr_t>(
            ResolveGameAddress(kGetVfxFileFromParts_En154));
        const auto ret = reinterpret_cast<std::uintptr_t>(
            ResolveGameAddress(kSparkCallReturn_En154));
        if (!fn || !ret || !SiteMatches(fn, ret))
        {
            LogError("OutfitAbilities: GetVfxFileFromParts signature mismatch, sprintSpark off");
            return false;
        }

        g_Installed = CreateAndEnableHook(
            reinterpret_cast<void*>(fn),
            reinterpret_cast<void*>(&hkGetVfxFileFromParts),
            reinterpret_cast<void**>(&g_Orig));
        if (!g_Installed)
        {
            LogError("OutfitAbilities: GetVfxFileFromParts hook failed, sprintSpark off");
            return false;
        }
        g_Target = fn;
        return true;
    }

    void Uninstall_GetVfxFileFromParts_Hook()
    {
        if (!g_Installed) return;
        DisableAndRemoveHook(reinterpret_cast<void*>(g_Target));
        g_Installed = false;
        g_Target    = 0;
        g_Orig      = nullptr;
    }
}
