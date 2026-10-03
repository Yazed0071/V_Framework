#include "pch.h"

#include "PlayerInfoInterfaceImpl_GetPartsType.h"
#include "OutfitAbilities.h"
#include "OutfitRegistry.h"
#include "BlockControllerImpl_LoadPartsNew.h"
#include "MissionCodeGuard.h"

#include <intrin.h>
#include <cstdint>
#include <cstring>
#include <iterator>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    constexpr std::size_t kMaxReturnSites = 8;

    constexpr std::uint8_t kPrologue_GetPartsType[] = { 0x48, 0x83, 0xEC, 0x28, 0xE8 };
    constexpr std::uint8_t kEpilogue_GetPartsType[] =
        { 0x0F, 0xB7, 0x80, 0x8A, 0x00, 0x00, 0x00 };

    struct GetterSites
    {
        std::uintptr_t        getter;
        const std::uintptr_t* returns;
        std::size_t           returnCount;
    };

    constexpr std::uintptr_t kLocalReturns_En154[] =
    {
        0x14093027Dull, 0x14098D41Aull,
    };
    constexpr std::uintptr_t kIndexReturns_En154[] =
    {
        0x140743509ull, 0x140743519ull, 0x140743558ull, 0x140743616ull,
        0x140743626ull, 0x1407448C5ull, 0x1407448D5ull,
    };

    constexpr GetterSites kLocal_En154 =
        { 0x141E02D50ull, kLocalReturns_En154, std::size(kLocalReturns_En154) };
    constexpr GetterSites kIndex_En154 =
        { 0x141E02B80ull, kIndexReturns_En154, std::size(kIndexReturns_En154) };

    using GetPartsTypeLocal_t = std::uint32_t (__fastcall*)(void* self);
    using GetPartsType_t      = std::uint32_t (__fastcall*)(std::uint32_t playerIndex);

    struct HookState
    {
        std::uintptr_t target = 0;
        std::uintptr_t returns[kMaxReturnSites] = {};
        std::size_t    returnCount = 0;
        bool           installed = false;
    };

    static HookState           g_Local;
    static HookState           g_Index;
    static GetPartsTypeLocal_t g_OrigLocal = nullptr;
    static GetPartsType_t      g_OrigIndex = nullptr;

    static bool IsListedReturn(const HookState& h, const void* ret)
    {
        const auto r = reinterpret_cast<std::uintptr_t>(ret);
        for (std::size_t i = 0; i < h.returnCount; ++i)
            if (h.returns[i] == r) return true;
        return false;
    }

    static std::uint8_t SprintPartsType()
    {
        const std::uint8_t sprint = outfit::SprintLive();
        if (sprint == outfit::kSprintNone || MissionCodeGuard::ShouldBypassHooks())
            return outfit::kSprintNone;
        return sprint;
    }

    static std::uint32_t __fastcall hkGetPartsTypeLocal(void* self)
    {
        const std::uint8_t sprint = SprintPartsType();
        if (sprint != outfit::kSprintNone && IsListedReturn(g_Local, _ReturnAddress()))
            return sprint;
        return g_OrigLocal(self);
    }

    static std::uint32_t __fastcall hkGetPartsType(std::uint32_t playerIndex)
    {
        if (playerIndex == outfit::GetLocalPartsSlot())
        {
            const std::uint8_t sprint = SprintPartsType();
            if (sprint != outfit::kSprintNone
                && IsListedReturn(g_Index, _ReturnAddress()))
                return sprint;
        }
        return g_OrigIndex(playerIndex);
    }

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

    static bool GetterMatches(std::uintptr_t fn)
    {
        std::uint8_t b[16] = {};
        if (!ReadCode_SEH(fn, b, sizeof(b))) return false;
        return std::memcmp(b, kPrologue_GetPartsType, sizeof(kPrologue_GetPartsType)) == 0
            && std::memcmp(b + 9, kEpilogue_GetPartsType, sizeof(kEpilogue_GetPartsType)) == 0;
    }

    static bool CallsGetter(std::uintptr_t ret, std::uintptr_t fn)
    {
        std::uint8_t b[5] = {};
        if (!ReadCode_SEH(ret - 5, b, sizeof(b)) || b[0] != 0xE8) return false;
        std::int32_t rel = 0;
        std::memcpy(&rel, b + 1, sizeof(rel));
        return ret + static_cast<std::intptr_t>(rel) == fn;
    }

    static bool Prepare(const char* what, const GetterSites& sites, HookState& h)
    {
        const auto fn = reinterpret_cast<std::uintptr_t>(ResolveGameAddress(sites.getter));
        if (!fn || !GetterMatches(fn))
        {
            LogError("RaidenSprint: %s signature mismatch, sparks and dash sound off", what);
            return false;
        }
        h.returnCount = 0;
        for (std::size_t i = 0; i < sites.returnCount && i < kMaxReturnSites; ++i)
        {
            const auto ret = reinterpret_cast<std::uintptr_t>(
                ResolveGameAddress(sites.returns[i]));
            if (!ret || !CallsGetter(ret, fn))
            {
                LogError("RaidenSprint: %s call site 0x%llX mismatch, sparks and dash sound off",
                    what, static_cast<unsigned long long>(sites.returns[i]));
                return false;
            }
            h.returns[h.returnCount++] = ret;
        }
        h.target = fn;
        return true;
    }

    static void Remove(HookState& h)
    {
        if (!h.installed) return;
        DisableAndRemoveHook(reinterpret_cast<void*>(h.target));
        h.installed = false;
        h.target    = 0;
    }
}

namespace outfit
{
    bool Install_PlayerInfoGetPartsType_Hooks()
    {
        if (g_Local.installed || g_Index.installed) return true;

        if (!IsEn154Family(gGameBuild))
        {
            LogError("RaidenSprint: no addresses for build %s, sparks and dash sound off",
                GetGameBuildName(gGameBuild));
            return false;
        }

        if (!Prepare("GetPartsTypeLocal", kLocal_En154, g_Local)
            || !Prepare("GetPartsType", kIndex_En154, g_Index))
            return false;

        g_Local.installed = CreateAndEnableHook(
            reinterpret_cast<void*>(g_Local.target),
            reinterpret_cast<void*>(&hkGetPartsTypeLocal),
            reinterpret_cast<void**>(&g_OrigLocal));
        g_Index.installed = CreateAndEnableHook(
            reinterpret_cast<void*>(g_Index.target),
            reinterpret_cast<void*>(&hkGetPartsType),
            reinterpret_cast<void**>(&g_OrigIndex));

        if (!g_Local.installed || !g_Index.installed)
        {
            LogError("RaidenSprint: GetPartsType hook failed, sparks and dash sound off");
            Uninstall_PlayerInfoGetPartsType_Hooks();
            return false;
        }
        return true;
    }

    void Uninstall_PlayerInfoGetPartsType_Hooks()
    {
        Remove(g_Index);
        Remove(g_Local);
        g_OrigIndex = nullptr;
        g_OrigLocal = nullptr;
    }
}
