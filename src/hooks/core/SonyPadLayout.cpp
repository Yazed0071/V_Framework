#include <pch.h>
#include "SonyPadLayout.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using ReadDirectInput_t = void(__fastcall*)(void** buttonTable, std::uint8_t* record);

    constexpr std::uint8_t kReadDirectInputEntry[] =
    {
        0x40, 0x55, 0x53, 0x56, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xC1, 0x48, 0x81, 0xEC, 0xC8, 0x00, 0x00,
        0x00, 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x45, 0x07, 0x48,
        0x8B, 0x7A, 0x18, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF1, 0xC6, 0x42, 0x15, 0x00, 0x48, 0x85, 0xFF,
    };
    constexpr std::uint8_t kReadDirectInputMask[] =
    {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };

    constexpr std::size_t   kRecordVendor      = 0x10;
    constexpr std::size_t   kRecordProduct     = 0x12;
    constexpr std::uint16_t kVendorSony        = 0x054C;
    constexpr std::uint16_t kProductDualShock4 = 0x05C4;   // the only Sony pad the reader treats as a DualShock 4; every other one gets the PS3 button order and upside-down triggers
    constexpr std::uint16_t kProductDualShock3 = 0x0268;   // the pad that PS3 layout is right for

    ReadDirectInput_t          g_Orig       = nullptr;
    void*                      g_HookTarget = nullptr;
    std::atomic<std::uint16_t> g_LastLogged{ 0 };

    void __fastcall hk_ReadDirectInput(void** buttonTable, std::uint8_t* record)
    {
        if (!buttonTable || !record)
        {
            g_Orig(buttonTable, record);
            return;
        }
        auto& vendor  = *reinterpret_cast<std::uint16_t*>(record + kRecordVendor);
        auto& product = *reinterpret_cast<std::uint16_t*>(record + kRecordProduct);
        if (vendor != kVendorSony || product == kProductDualShock4 || product == kProductDualShock3)
        {
            g_Orig(buttonTable, record);
            return;
        }

        const std::uint16_t realProduct = product;
        if (g_LastLogged.exchange(realProduct, std::memory_order_relaxed) != realProduct)
            LogInfo("SonyPadLayout: reading Sony pad product 0x%04X with the DualShock 4 layout", realProduct);

        *buttonTable = nullptr;   // the reader picks its button table again, this time as a DualShock 4
        product = kProductDualShock4;
        g_Orig(buttonTable, record);
        product = realProduct;
    }
}

bool Install_SonyPadLayout_Hook()
{
    if (g_HookTarget)
        return true;

    void* target = ResolveGameAddress(gAddr.DevPad_ReadDirectInput);
    if (!target)
    {
        LogError("SonyPadLayout: no address on %s, Sony pads other than the first DualShock 4 keep the PS3 layout",
                 GetGameBuildName(gGameBuild));
        return false;
    }

    const HookEntry entry = CheckHookEntryMasked(target, kReadDirectInputEntry, kReadDirectInputMask,
                                                 sizeof(kReadDirectInputEntry));
    if (entry == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(target, found, sizeof(found));
        LogError("SonyPadLayout: the pad reader is not the known code on %s (%s), Sony pads other than the first "
                 "DualShock 4 keep the PS3 layout", GetGameBuildName(gGameBuild), found);
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("SonyPadLayout: pad reader", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_ReadDirectInput), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("SonyPadLayout: pad reader hook failed at %p, Sony pads other than the first DualShock 4 keep "
                 "the PS3 layout", target);
        return false;
    }
    g_HookTarget = target;
    return true;
}

void Uninstall_SonyPadLayout_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
}
