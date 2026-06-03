#include "pch.h"
#include "TimeCigaretteUiHook.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using ShowTimeCigaretteUi_t = void(__fastcall*)(void* this_, std::uint32_t index);

    static ShowTimeCigaretteUi_t g_Orig         = nullptr;
    static bool                  g_Installed     = false;
    static std::atomic<void*>    g_UiController{ nullptr };

    static constexpr std::size_t kOwnerField        = 0x8;
    static constexpr std::size_t kManagerField      = 0x138;
    static constexpr std::size_t kUiControllerField = 0xD8;
    static constexpr std::size_t kActionInfoField   = 0x38;
    static constexpr std::size_t kSlotOriginField   = 0x24;
    static constexpr std::size_t kSlotArrayField    = 0x78;
    static constexpr std::size_t kSlotStride        = 0x1A0;
    static constexpr std::size_t kSlotFlagsField    = 0x17C;
    static constexpr std::uint32_t kShownBit        = 0x400;

    static constexpr std::size_t kShowUiVtblByteOffset = 0x210;
    static constexpr std::size_t kHideUiVtblByteOffset = 0x218;

    static bool TryReadSlot(void* this_, std::uint32_t index, void** outUiController, void** outElement)
    {
        *outUiController = nullptr;
        *outElement      = nullptr;

        if (!this_)
            return false;

        __try
        {
            const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(this_);

            void* owner = *reinterpret_cast<void**>(base + kOwnerField);
            if (owner)
            {
                void* manager = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(owner) + kManagerField);
                if (manager)
                    *outUiController = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(manager) + kUiControllerField);
            }

            void* actionInfo = *reinterpret_cast<void**>(base + kActionInfoField);
            const std::uintptr_t slotArray = *reinterpret_cast<std::uintptr_t*>(base + kSlotArrayField);
            if (!actionInfo || !slotArray)
                return *outUiController != nullptr;

            const std::uint32_t origin =
                *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(actionInfo) + kSlotOriginField);
            if (index < origin)
                return *outUiController != nullptr;

            *outElement = reinterpret_cast<void*>(
                slotArray + static_cast<std::uintptr_t>(index - origin) * kSlotStride);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            *outUiController = nullptr;
            *outElement      = nullptr;
            return false;
        }
    }

    static bool TryIsShown(void* element)
    {
        if (!element)
            return false;

        __try
        {
            const std::uint32_t flags =
                *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(element) + kSlotFlagsField);
            return (flags & kShownBit) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool TryCallUiVtblSlot(void* uiController, std::size_t vtblByteOffset)
    {
        if (!uiController)
            return false;

        __try
        {
            void** vtbl = *reinterpret_cast<void***>(uiController);
            if (!vtbl)
                return false;

            auto fn = reinterpret_cast<void(__fastcall*)(void*)>(vtbl[vtblByteOffset / sizeof(void*)]);
            if (!fn)
                return false;

            fn(uiController);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void __fastcall hkShowTimeCigaretteUi(void* this_, std::uint32_t index)
    {
        void* uiController = nullptr;
        void* element      = nullptr;
        const bool read    = TryReadSlot(this_, index, &uiController, &element);

        if (uiController)
            g_UiController.store(uiController, std::memory_order_relaxed);

        const bool wasShown = read && TryIsShown(element);

        if (g_Orig)
            g_Orig(this_, index);

        if (MissionCodeGuard::ShouldBypassHooks())
            return;

        if (read && !wasShown && TryIsShown(element))
            V_FrameWork::EmitMessage("Player", "TimeCigaretteUi", index);
    }
}

void TimeCigaretteUi_SetUiController(void* uiController)
{
    if (uiController)
        g_UiController.store(uiController, std::memory_order_relaxed);
}

bool Show_TimeCigaretteUi()
{
    return TryCallUiVtblSlot(g_UiController.load(std::memory_order_relaxed), kShowUiVtblByteOffset);
}

bool Hide_TimeCigaretteUi()
{
    return TryCallUiVtblSlot(g_UiController.load(std::memory_order_relaxed), kHideUiVtblByteOffset);
}

bool Install_TimeCigaretteUi_Hook()
{
    if (g_Installed)
        return true;

    if (!gAddr.TimeCigaretteActionPluginImpl_ShowTimeCigaretteUi)
    {
        Log("[TimeCigaretteUi] address not set for this build\n");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.TimeCigaretteActionPluginImpl_ShowTimeCigaretteUi);
    if (!target)
    {
        Log("[TimeCigaretteUi] resolve failed\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkShowTimeCigaretteUi),
        reinterpret_cast<void**>(&g_Orig));

    if (ok)
        g_Installed = true;

    Log("[TimeCigaretteUi] hook: %s (target=%p)\n", ok ? "OK" : "FAIL", target);
    return ok;
}

bool Uninstall_TimeCigaretteUi_Hook()
{
    if (!g_Installed)
        return true;

    if (gAddr.TimeCigaretteActionPluginImpl_ShowTimeCigaretteUi)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.TimeCigaretteActionPluginImpl_ShowTimeCigaretteUi));

    g_Orig      = nullptr;
    g_Installed = false;
    g_UiController.store(nullptr, std::memory_order_relaxed);
    return true;
}
