#include <pch.h>
#include "Reticle_ResetLayout.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

// Vanilla use-after-free in the HUD reticle. Each reticle type gets its layouts from its own window
// (ReticleCommonFactory::CreateWindow -> Reticle::SetLayout(type, layout, assetLayout)). The Init*Asset of the
// current type stores a model of that type's asset layout at Reticle+0x140, and the next Init*Asset hides it
// through a virtual call before building its own. When the window is deleted, ReticleCommonFactory::DeleteWindow
// calls Reticle::ResetLayout(type, layout) and then destroys the window, but ResetLayout clears +0x130, +0x138,
// +0x148 and +0x150 only: +0x140 keeps pointing at a model of the destroyed window, and the next reticle init calls
// into freed memory (crash READ 0xA8 when the memory was reused). The hook forgets +0x140 when it belongs to the
// window being deleted, as ResetLayout does for the other handles.
namespace
{
    using ResetLayout_t = void (__fastcall*)(std::uint8_t* self, int type, void* layout);

    static ResetLayout_t g_OrigResetLayout = nullptr;
    static void* g_HookAddr = nullptr;
    static bool g_Logged = false;

    constexpr std::uintptr_t RETICLE_ASSET      = 0x140;
    constexpr std::uintptr_t LAYOUT_MODEL_COUNT = 0xC0;
    constexpr std::uintptr_t LAYOUT_MODELS      = 0xC8;

    // ResetLayout's own clears of +0x130/+0x138/+0x148/+0x150 (EN 1.0.15.4 0x14094338B), to confirm the address
    constexpr std::uintptr_t RESET_CLEARS_AT = 0x25B;
    const std::uint8_t kResetClears[] = {
        0x48, 0x89, 0xAB, 0x30, 0x01, 0x00, 0x00,
        0x48, 0x89, 0xAB, 0x38, 0x01, 0x00, 0x00,
        0x48, 0x89, 0xAB, 0x48, 0x01, 0x00, 0x00,
        0x48, 0x89, 0xAB, 0x50, 0x01, 0x00, 0x00,
    };

    struct TypeLayouts { int type; std::uintptr_t layout; std::uintptr_t assetLayout; };

    // Reticle::SetLayout: where each reticle type keeps its layout and its asset layout (type 10 has no asset layout)
    constexpr TypeLayouts kTypeLayouts[] = {
        { 1, 0xC8, 0x120 }, { 2, 0xA8, 0x100 }, { 3, 0xB0, 0x108 }, { 4, 0x88, 0xE0 },  { 5, 0x98, 0xF0 },
        { 6, 0xA0, 0xF8 },  { 7, 0xB8, 0x110 }, { 8, 0xC0, 0x118 }, { 9, 0x90, 0xE8 },  { 11, 0xD0, 0x128 },
    };

    static int AvFilter(unsigned int code)
    {
        return code == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
    }

    static bool LayoutHoldsModel(std::uint8_t* layout, void* model)
    {
        __try
        {
            const std::uint32_t count = *reinterpret_cast<std::uint32_t*>(layout + LAYOUT_MODEL_COUNT);
            void** models = *reinterpret_cast<void***>(layout + LAYOUT_MODELS);
            for (std::uint32_t i = 0; models && count <= 4096 && i < count; ++i)
                if (models[i] == model)
                    return true;
            return false;
        }
        __except (AvFilter(GetExceptionCode())) { return false; }
    }

    // The window being deleted is the type's current one when the type's layout is the one passed in; its asset
    // layout is then the type's asset layout, and the reticle asset dies with it if it is one of its models.
    static void ForgetAssetOfDeletedWindow(std::uint8_t* self, int type, void* layout)
    {
        if (!self || !layout)
            return;
        for (const TypeLayouts& t : kTypeLayouts)
        {
            if (t.type != type)
                continue;
            void** asset = reinterpret_cast<void**>(self + RETICLE_ASSET);
            std::uint8_t* assetLayout = *reinterpret_cast<std::uint8_t**>(self + t.assetLayout);
            if (*reinterpret_cast<void**>(self + t.layout) != layout || !*asset || !assetLayout
                || !LayoutHoldsModel(assetLayout, *asset))
                return;
            *asset = nullptr;
            if (!g_Logged)
            {
                g_Logged = true;
            }
            return;
        }
    }

    static void __fastcall hk_ResetLayout(std::uint8_t* self, int type, void* layout)
    {
        ForgetAssetOfDeletedWindow(self, type, layout);
        if (g_OrigResetLayout)
            g_OrigResetLayout(self, type, layout);
    }

    static bool HasResetClears(const std::uint8_t* fn)
    {
        __try
        {
            return std::memcmp(fn + RESET_CLEARS_AT, kResetClears, sizeof(kResetClears)) == 0;
        }
        __except (AvFilter(GetExceptionCode())) { return false; }
    }
}

bool Install_Reticle_ResetLayout_Hook()
{
    void* target = ResolveGameAddress(gAddr.Reticle_ResetLayout);
    if (!target)
    {
        LogError("ReticleLayoutReset: no Reticle::ResetLayout address on %s, reticle fix off",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (!HasResetClears(static_cast<const std::uint8_t*>(target)))
    {
        LogError("ReticleLayoutReset: Reticle::ResetLayout does not match on %s, reticle fix off",
            GetGameBuildName(gGameBuild));
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target, reinterpret_cast<void*>(&hk_ResetLayout),
        reinterpret_cast<void**>(&g_OrigResetLayout));
    if (!ok)
    {
        LogError("ReticleLayoutReset: Reticle::ResetLayout hook failed, reticle fix off");
        return false;
    }

    g_HookAddr = target;
    return true;
}

void Uninstall_Reticle_ResetLayout_Hook()
{
    if (g_HookAddr)
        DisableAndRemoveHook(g_HookAddr);
    g_HookAddr = nullptr;
    g_OrigResetLayout = nullptr;
}
