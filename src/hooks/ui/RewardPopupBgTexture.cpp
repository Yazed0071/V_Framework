#include "pch.h"
#include "RewardPopupBgTexture.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"

namespace
{
    using PopupSetup_t     = std::uint64_t(__fastcall*)(void* self, void* param);
    using SetNodeTexture_t = void(__fastcall*)(void* manager, void* node,
                                               std::uint64_t texturePathHash,
                                               std::uint64_t slotNameHash);
    using GetUixUtility_t  = void** (__fastcall*)();

    constexpr std::uint64_t  ICON_TEXTURE_SLOT_HASH = 0xCAFB3BBF9889ull;
    constexpr std::uintptr_t POPUP_OWNER_OFFSET   = 0x10;
    constexpr std::uintptr_t POPUP_PANEL_OFFSET   = 0x20;
    constexpr std::uintptr_t PICTURE_NODE_OFFSET = 0x50;
    constexpr std::size_t    SET_NODE_TEXTURE_SLOT = 0x518 / sizeof(void*);
    constexpr std::size_t    SETUP_HASH_SITE_OFFSET = 0xC7;

    constexpr std::uint8_t kHashSitePattern[] =
    {
        0x48, 0xBB, 0x15, 0x9A, 0x25, 0x89, 0xFA, 0xB6, 0x68, 0x15
    };

    constexpr std::uint8_t kSetupPrologue[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x08,
        0x48, 0x89, 0x74, 0x24, 0x10,
        0x57,
        0x48, 0x83, 0xEC, 0x40,
        0x48, 0x83, 0x79, 0x08, 0x00
    };

    PopupSetup_t     g_OrigSetup      = nullptr;
    GetUixUtility_t  g_GetUixUtility  = nullptr;
    void*            g_Target         = nullptr;
    bool             g_BindFailLogged = false;

    uint64_t g_CurrentTexture = 0;


    void Prefetch(std::uint64_t textureHash)
    {
        if (textureHash == 0 || gAddr.GetUixUtilityToFeedQuarkEnvironment == 0)
            return;
        if (!g_GetUixUtility)
            g_GetUixUtility = reinterpret_cast<GetUixUtility_t>(ResolveGameAddress(gAddr.GetUixUtilityToFeedQuarkEnvironment));
        if (!g_GetUixUtility)
            return;
        __try
        {
            void** util = g_GetUixUtility();
            if (!util) return;
            void** vtbl = *reinterpret_cast<void***>(util);
            if (!vtbl) return;
            auto fn = reinterpret_cast<void(__fastcall*)(void*, std::uint64_t, int)>(vtbl[0x548 / sizeof(void*)]);
            if (fn) fn(util, textureHash, 2);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }


    void Apply(void* self)
    {
        const std::uint64_t custom = g_CurrentTexture;
        if (!self || custom == 0)
            return;

        Prefetch(custom);

        void*            panel      = nullptr;
        void*            node       = nullptr;
        SetNodeTexture_t setTexture = nullptr;

        __try
        {
            auto* const base = reinterpret_cast<std::uint8_t*>(self);
            auto* const owner = *reinterpret_cast<std::uint8_t**>(base + POPUP_OWNER_OFFSET);
            if (owner)
                panel = *reinterpret_cast<void**>(owner + POPUP_PANEL_OFFSET);
            node = *reinterpret_cast<void**>(base + PICTURE_NODE_OFFSET);
            if (panel)
            {
                void** const vtbl = *reinterpret_cast<void***>(panel);
                if (vtbl)
                    setTexture = reinterpret_cast<SetNodeTexture_t>(vtbl[SET_NODE_TEXTURE_SLOT]);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        if (!panel || !node || !setTexture)
        {
            if (!g_BindFailLogged)
            {
                g_BindFailLogged = true;
                Log("[RewardPopupBg] popup drew with panel=%p node=%p setter=%p - the background "
                    "picture could not be reached, so the popup keeps its vanilla texture\n",
                    panel, node, reinterpret_cast<void*>(setTexture));
            }
            return;
        }

        __try
        {
            setTexture(panel, node, custom, ICON_TEXTURE_SLOT_HASH);
            LogDebug("[RewardPopupBg] popup background -> %016llX on picture node %p\n",
                     static_cast<unsigned long long>(custom),
                     node);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }


    std::uint64_t __fastcall hk_PopupSetup(void* self, void* param)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return g_OrigSetup(self, param);

        const std::uint64_t result = g_OrigSetup(self, param);
        Apply(self);
        return result;
    }


    std::size_t ScanRegion(std::uint8_t* begin, std::size_t size, std::uint8_t** outSite, std::size_t found)
    {
        if (size < sizeof(kHashSitePattern))
            return found;
        __try
        {
            const std::uint8_t first = kHashSitePattern[0];
            const std::size_t  last  = size - sizeof(kHashSitePattern);
            for (std::size_t i = 0; i <= last; ++i)
            {
                if (begin[i] != first)
                    continue;
                if (std::memcmp(begin + i, kHashSitePattern, sizeof(kHashSitePattern)) == 0)
                {
                    if (found == 0)
                        *outSite = begin + i;
                    ++found;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return found;
    }


    std::size_t FindHashSite(std::uint8_t** outSite)
    {
        auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
        if (!base)
            return 0;
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
        std::size_t found = 0;
        for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; ++s)
        {
            if (!(sec[s].Characteristics & IMAGE_SCN_MEM_EXECUTE))
                continue;
            std::uint8_t* cur = base + sec[s].VirtualAddress;
            std::uint8_t* end = cur + sec[s].Misc.VirtualSize;
            while (cur < end)
            {
                MEMORY_BASIC_INFORMATION mbi{};
                if (!VirtualQuery(cur, &mbi, sizeof(mbi)))
                    break;
                auto* regionEnd = static_cast<std::uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
                if (regionEnd > end)
                    regionEnd = end;
                const bool executable = (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ
                                                        | PAGE_EXECUTE_READWRITE
                                                        | PAGE_EXECUTE_WRITECOPY)) != 0;
                if (mbi.State == MEM_COMMIT && executable && !(mbi.Protect & PAGE_GUARD))
                    found = ScanRegion(cur, static_cast<std::size_t>(regionEnd - cur), outSite, found);
                cur = regionEnd;
            }
        }
        return found;
    }


    bool PrologueMatches(const std::uint8_t* at)
    {
        __try
        {
            return std::memcmp(at, kSetupPrologue, sizeof(kSetupPrologue)) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }


    void* LocateSetup()
    {
        std::uint8_t* site = nullptr;
        const std::size_t hits = FindHashSite(&site);
        if (hits != 1 || !site)
        {
            Log("[RewardPopupBg] popup setup not located (%zu pattern hit(s)) - override disabled\n", hits);
            return nullptr;
        }

        std::uint8_t* const entry = site - SETUP_HASH_SITE_OFFSET;
        if (!PrologueMatches(entry))
        {
            Log("[RewardPopupBg] prologue mismatch at %p - override disabled\n", entry);
            return nullptr;
        }
        return entry;
    }
}


void RewardPopupBg_SetCurrentPopupTexture(uint64_t textureHash)
{
    g_CurrentTexture = textureHash;
    Prefetch(textureHash);
}


bool Install_RewardPopupBgTexture_Hook()
{
    g_Target = LocateSetup();
    if (!g_Target)
        return false;

    const bool ok = CreateAndEnableHook(
        g_Target,
        reinterpret_cast<void*>(&hk_PopupSetup),
        reinterpret_cast<void**>(&g_OrigSetup));

#ifdef _DEBUG
    Log("[Hook] RewardPopupBg: %s (target=%p)\n", ok ? "OK" : "FAIL", g_Target);
#else
    if (!ok)
        Log("[Hook] RewardPopupBg: FAIL (target=%p)\n", g_Target);
#endif
    if (!ok)
        g_Target = nullptr;
    return ok;
}


bool Uninstall_RewardPopupBgTexture_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);

    g_Target         = nullptr;
    g_OrigSetup      = nullptr;
    g_CurrentTexture = 0;
    g_BindFailLogged = false;
    return true;
}
