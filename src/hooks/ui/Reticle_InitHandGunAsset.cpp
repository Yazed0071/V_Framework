#include "pch.h"
#include "Reticle_InitHandGunAsset.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using InitHandGunAsset_t = bool (__fastcall*)(void* self, std::uint8_t withSecondary);

    static InitHandGunAsset_t g_OrigInitHandGunAsset = nullptr;
    static void* g_HookAddr = nullptr;

    constexpr std::uintptr_t RETICLE_ASSET        = 0x140;
    constexpr std::uintptr_t RETICLE_LAYOUTS_FROM = 0x80;   // the reticle's layouts; each weapon type's asset init takes the asset from one of them
    constexpr std::uintptr_t RETICLE_LAYOUTS_TO   = 0x128;
    constexpr std::uintptr_t LAYOUT_MODEL_COUNT   = 0xC0;
    constexpr std::uintptr_t LAYOUT_MODELS        = 0xC8;

    static int AvFilter(unsigned int code)
    {
        return code == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
    }

    // the asset is a model the previous init took from one of the reticle's layouts (Layout::GetModel searches only
    // the layout's own models); once those layouts are rebuilt the asset is freed, and its memory may already hold
    // something else, so only "still one of their models" proves it alive
    static bool LayoutHoldsModel(std::uint8_t* layout, void* asset)
    {
        __try
        {
            const std::uint32_t count = *reinterpret_cast<std::uint32_t*>(layout + LAYOUT_MODEL_COUNT);
            void** models = *reinterpret_cast<void***>(layout + LAYOUT_MODELS);
            for (std::uint32_t i = 0; models && count <= 4096 && i < count; ++i)
                if (models[i] == asset)
                    return true;
            return false;
        }
        __except (AvFilter(GetExceptionCode())) { return false; }   // not every field in the range is a layout
    }

    static bool IsModelOfReticleLayouts(std::uint8_t* self, void* asset)
    {
        for (std::uintptr_t offset = RETICLE_LAYOUTS_FROM; offset <= RETICLE_LAYOUTS_TO; offset += sizeof(void*))
        {
            std::uint8_t* layout = *reinterpret_cast<std::uint8_t**>(self + offset);
            if (layout && LayoutHoldsModel(layout, asset))
                return true;
        }
        return false;
    }

    static void DropDanglingReticleAsset(std::uint8_t* self)
    {
        if (!self)
            return;
        void** slot = reinterpret_cast<void**>(self + RETICLE_ASSET);
        bool dropped = false;
        __try
        {
            void* asset = *slot;
            if (asset && !IsModelOfReticleLayouts(self, asset))
            {
                *slot = nullptr;
                dropped = true;
            }
        }
        __except (AvFilter(GetExceptionCode()))
        {
            *slot = nullptr;
            dropped = true;
        }
        if (dropped)
            LogInfo("ReticleAssetGuard: dropped a freed handgun reticle asset");
    }

    static bool __fastcall hk_InitHandGunAsset(std::uint8_t* self,
                                               std::uint8_t withSecondary)
    {
        DropDanglingReticleAsset(self);
        return g_OrigInitHandGunAsset
            ? g_OrigInitHandGunAsset(self, withSecondary) : false;
    }
}

bool Install_Reticle_InitHandGunAsset_Hook()
{
    void* target = ResolveGameAddress(gAddr.Reticle_InitHandGunAsset);
    if (!target)
    {
        LogError("ReticleAssetGuard: no InitHandGunAsset address on %s, handgun reticle guard off",
            GetGameBuildName(gGameBuild));
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target, reinterpret_cast<void*>(&hk_InitHandGunAsset),
        reinterpret_cast<void**>(&g_OrigInitHandGunAsset));
    if (!ok)
    {
        LogError("ReticleAssetGuard: InitHandGunAsset hook failed, handgun reticle guard off");
        return false;
    }

    g_HookAddr = target;
    return true;
}

void Uninstall_Reticle_InitHandGunAsset_Hook()
{
    if (g_HookAddr)
        DisableAndRemoveHook(g_HookAddr);
    g_HookAddr = nullptr;
    g_OrigInitHandGunAsset = nullptr;
}
