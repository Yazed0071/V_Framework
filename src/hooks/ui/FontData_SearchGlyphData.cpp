#include <pch.h>
#include "FontData_SearchGlyphData.h"

#include <Windows.h>
#include <intrin.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using SearchGlyphData_t = void*(__fastcall*)(std::uint8_t* fontData, std::uint32_t code);

    constexpr std::size_t kFontGlyphCount = 0x0E;
    constexpr std::size_t kFontGlyphs     = 0x18;   // sorted by code, 0x14 each

    struct Addresses
    {
        std::uintptr_t searchGlyphData;
        std::uintptr_t defaultCharLookups[3];   // the second lookup after a miss, for the font's default char
    };

    constexpr std::uint8_t kSearchGlyphDataEntry[] =
    {
        0x48, 0x83, 0xEC, 0x08, 0x0F, 0xB7, 0x41, 0x0E, 0x44, 0x8B, 0xDA, 0x66, 0x85, 0xC0, 0x75, 0x07,
    };

    SearchGlyphData_t g_Orig       = nullptr;
    void*             g_HookTarget = nullptr;
    void*             g_DefaultCharLookups[3] = {};
    std::atomic<bool> g_Warned{ false };

    bool IsDefaultCharLookup(void* returnAddress)
    {
        for (void* site : g_DefaultCharLookups)
            if (site == returnAddress)
                return true;
        return false;
    }

    bool CallsSearchGlyphData(void* returnAddress, void* searchGlyphData)
    {
        std::uint8_t call[5] = {};
        if (!returnAddress || !ReadEntryBytesSEH(static_cast<std::uint8_t*>(returnAddress) - 5, call, sizeof(call))
            || call[0] != 0xE8)
            return false;
        std::int32_t rel = 0;
        std::memcpy(&rel, call + 1, sizeof(rel));
        return static_cast<std::uint8_t*>(returnAddress) + rel == searchGlyphData;
    }

    void* __fastcall hk_SearchGlyphData(std::uint8_t* fontData, std::uint32_t code)
    {
        void* glyph = g_Orig(fontData, code);
        if (glyph || !fontData || !IsDefaultCharLookup(_ReturnAddress()))
            return glyph;

        if (*reinterpret_cast<std::uint16_t*>(fontData + kFontGlyphCount) == 0)
            return nullptr;
        if (!g_Warned.exchange(true, std::memory_order_relaxed))
            LogWarn("FontGlyphGuard: font %p has neither the character nor its default 0x%X, drawing its first glyph instead",
                    fontData, code & 0x0FFFFFFFu);
        return *reinterpret_cast<std::uint8_t**>(fontData + kFontGlyphs);
    }
}

bool Install_FontData_SearchGlyphData_Hook()
{
    if (g_HookTarget)
        return true;

    const Addresses addresses =
    {
        gAddr.FontData_SearchGlyphData,
        {
            gAddr.FontTextureRender_GetStringWidth_DefaultCharLookup,
            gAddr.FontTextureRender_RegisterString_DefaultCharLookup,
            gAddr.FontTextureRender_RegisterStringW_DefaultCharLookup,
        },
    };

    void* target = ResolveGameAddress(addresses.searchGlyphData);
    const HookEntry entry = CheckHookEntry(target, kSearchGlyphDataEntry, sizeof(kSearchGlyphDataEntry));
    if (entry == HookEntry::Unknown)
    {
        LogError("FontGlyphGuard: FontData::SearchGlyphData is not the known code on %s, a font missing its default character can freeze the game",
                 GetGameBuildName(gGameBuild));
        return false;
    }
    for (std::size_t i = 0; i < 3; ++i)
    {
        g_DefaultCharLookups[i] = ResolveGameAddress(addresses.defaultCharLookups[i]);
        if (!CallsSearchGlyphData(g_DefaultCharLookups[i], target))
        {
            LogError("FontGlyphGuard: default character lookup %zu is not the known code on %s, a font missing its default character can freeze the game",
                     i, GetGameBuildName(gGameBuild));
            return false;
        }
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("FontGlyphGuard: FontData::SearchGlyphData", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_SearchGlyphData), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("FontGlyphGuard: FontData::SearchGlyphData hook failed at %p, a font missing its default character can freeze the game", target);
        return false;
    }
    g_HookTarget = target;
    return true;
}

void Uninstall_FontData_SearchGlyphData_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
}
