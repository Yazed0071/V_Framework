#include "pch.h"
#include "UiTexturePrefetch.h"

#include <Windows.h>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "AddressSet.h"
#include "HookUtils.h"

namespace
{
    using GetUixUtility_t       = void** (__fastcall*)();
    using GetPrefetchCtrl_t     = void* (__fastcall*)();
    using IsPrefetchTexture_t   = bool (__fastcall*)(void* controller, std::uint64_t textureHash);

    constexpr std::size_t UIX_CREATE_PREFETCH = 0x548;
    constexpr std::size_t UIX_DELETE_PREFETCH = 0x550;

    constexpr std::uint64_t VANILLA_BG = 0x15695ED8A56AE919ull; // hash of the default equip panel poster ftex (real name unknown)
    constexpr std::uint64_t FLAT_WHITE = 0x156B0FA3F5A52C33ull; // /Assets/fox/ui/prefab/ModelAsset/prefab/Pictures/ui_prefab_flat_white.ftex

    struct Ref
    {
        int  count = 0;
        bool foreign = false;
    };

    std::mutex g_Lock;
    std::unordered_map<std::uint64_t, Ref> g_Refs;

    struct LookupAddrs { std::uintptr_t getController; std::uintptr_t isPrefetch; };

    LookupAddrs LookupAddrsForBuild()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
            return { 0x141DA70B0ull, 0x141DBBEE0ull };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
            return { 0x141DA6F90ull, 0x141DBBF40ull };
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:
            return { 0x141DA6C50ull, 0x141DBB930ull };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:
            return { 0x141DA6B00ull, 0x141DBB970ull };
        default:
            return { 0, 0 };
        }
    }

    bool ReleaseVerified()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:
            return true;
        default:
            return false;
        }
    }

    void CallUixSEH(std::size_t slot, std::uint64_t textureHash)
    {
        if (gAddr.GetUixUtilityToFeedQuarkEnvironment == 0)
            return;
        auto getter = reinterpret_cast<GetUixUtility_t>(ResolveGameAddress(gAddr.GetUixUtilityToFeedQuarkEnvironment));
        if (!getter)
            return;
        __try
        {
            void** util = getter();
            if (!util) return;
            void** vtbl = *reinterpret_cast<void***>(util);
            if (!vtbl) return;
            if (slot == UIX_CREATE_PREFETCH)
            {
                auto fn = reinterpret_cast<void(__fastcall*)(void*, std::uint64_t, int)>(vtbl[slot / sizeof(void*)]);
                if (fn) fn(util, textureHash, 2);
            }
            else
            {
                auto fn = reinterpret_cast<void(__fastcall*)(void*, std::uint64_t)>(vtbl[slot / sizeof(void*)]);
                if (fn) fn(util, textureHash);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // 1 = already in the game's prefetch list, 0 = not, -1 = can't tell on this build
    int IsAlreadyPrefetchedSEH(std::uint64_t textureHash)
    {
        const LookupAddrs a = LookupAddrsForBuild();
        auto getController = reinterpret_cast<GetPrefetchCtrl_t>(ResolveGameAddress(a.getController));
        auto isPrefetch = reinterpret_cast<IsPrefetchTexture_t>(ResolveGameAddress(a.isPrefetch));
        if (!getController || !isPrefetch)
            return -1;
        __try
        {
            void* controller = getController();
            if (!controller) return -1;
            return isPrefetch(controller, textureHash) ? 1 : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }
}

namespace UiTexturePrefetch
{
    void Prefetch(std::uint64_t textureHash)
    {
        if (textureHash != 0)
            CallUixSEH(UIX_CREATE_PREFETCH, textureHash);
    }

    void Acquire(std::uint64_t textureHash)
    {
        if (textureHash == 0)
            return;
        bool first = false;
        {
            std::lock_guard<std::mutex> lock(g_Lock);
            Ref& ref = g_Refs[textureHash];
            first = ref.count++ == 0;
            if (first)
                ref.foreign = textureHash == VANILLA_BG || textureHash == FLAT_WHITE || IsAlreadyPrefetchedSEH(textureHash) == 1;
        }
        Prefetch(textureHash);
    }

    void Release(std::uint64_t textureHash)
    {
        if (textureHash == 0)
            return;
        {
            std::lock_guard<std::mutex> lock(g_Lock);
            const auto it = g_Refs.find(textureHash);
            if (it == g_Refs.end() || --it->second.count > 0)
                return;
            const bool foreign = it->second.foreign;
            g_Refs.erase(it);
            if (foreign || !ReleaseVerified())
                return;
        }
        CallUixSEH(UIX_DELETE_PREFETCH, textureHash);
    }

}
