#include "pch.h"
#include "FontData_SearchGlyphData.h"

#include <Windows.h>
#include <intrin.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

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

    constexpr Addresses kEn154 =
    {
        0x140225B40,
        {
            0x140224726,   // FontTextureRender::GetStringWidth
            0x1402251B5,   // FontTextureRender::RegisterString
            0x1402252FC,   // FontTextureRender::RegisterStringW
        },
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
        return *reinterpret_cast<std::uint8_t**>(fontData + kFontGlyphs);   // vanilla reads a null glyph here with the font renderer locked: a freeze if anything catches the fault
    }
}

bool Install_FontData_SearchGlyphData_Hook()
{
    if (g_HookTarget)
        return true;

    if (!AddressSetRuntime::IsEn154Family(gGameBuild))
    {
        LogWarn("FontGlyphGuard: no addresses for %s, a font missing its default character can still freeze the game",
                GetGameBuildName(gGameBuild));
        return true;
    }

    for (std::size_t i = 0; i < 3; ++i)
        g_DefaultCharLookups[i] = ResolveGameAddress(kEn154.defaultCharLookups[i]);

    void* target = ResolveGameAddress(kEn154.searchGlyphData);
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
