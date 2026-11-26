#include <pch.h>
#include "EquipPluginImpl_NormalMtevCallback.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

// The game's item events (radio and the like) always put the item on MTP_LHAND_B. A clip that also carries the event
// V_FrameWork_ItemRightHand, placed after the item event, puts it on MTP_RHAND_B instead.

namespace
{
    using Callback_t  = void(__fastcall*)(std::uint8_t* equipPlugin, std::uint32_t slot, void* event);
    using EventName_t = std::uint32_t(__fastcall*)(void* event);

    constexpr std::uint32_t kItemRightHand      = 0x1C14A9A6; // V_FrameWork_ItemRightHand
    constexpr std::size_t   kSlotBase           = 0x40;
    constexpr std::size_t   kWorks              = 0x98;
    constexpr std::size_t   kWorkSize           = 0x30;
    constexpr std::size_t   kWorkFlags          = 0x24;
    constexpr std::size_t   kWorkItemPoint      = 0x28;       // index into the soldier's motion point table
    constexpr std::uint32_t kItemInHand         = 4;
    constexpr std::uint8_t  kRightHandItemPoint = 3;          // MTP_RHAND_B (the game's item events use 6, MTP_LHAND_B)

    const std::uint8_t kPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x70
    };

    Callback_t g_Orig       = nullptr;
    void*      g_HookTarget = nullptr;

    std::uintptr_t NormalMtevCallbackAddr()
    {
        return gAddr.SoldierEquipPluginImpl_NormalMtevCallback;
    }

    void MoveItemToRightHandSEH(std::uint8_t* equipPlugin, std::uint32_t slot, void* event)
    {
        __try
        {
            if ((*reinterpret_cast<EventName_t*>(*reinterpret_cast<void**>(event)))(event) != kItemRightHand)
                return;
            std::uint8_t* work = (slot - *reinterpret_cast<std::uint32_t*>(equipPlugin + kSlotBase)) * kWorkSize
                + *reinterpret_cast<std::uint8_t**>(equipPlugin + kWorks);
            if (*reinterpret_cast<std::uint32_t*>(work + kWorkFlags) & kItemInHand)
                work[kWorkItemPoint] = kRightHandItemPoint;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hk_NormalMtevCallback(std::uint8_t* equipPlugin, std::uint32_t slot, void* event)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_Orig, equipPlugin, slot, event);
        g_Orig(equipPlugin, slot, event);
        MoveItemToRightHandSEH(equipPlugin, slot, event);
    }
}

bool Install_EquipPluginImpl_NormalMtevCallback_Hook()
{
    if (g_HookTarget)
        return true;
    const std::uintptr_t addr = NormalMtevCallbackAddr();
    if (!addr)
        return true;

    auto* target = static_cast<std::uint8_t*>(ResolveGameAddress(addr));
    const HookEntry entry = CheckHookEntry(target, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown)
    {
        LogError("ClipEvents: soldier item event code does not match %s, V_FrameWork_ItemRightHand is ignored", GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("ClipEvents: EquipPluginImpl::NormalMtevCallback", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_NormalMtevCallback), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ClipEvents: EquipPluginImpl::NormalMtevCallback hook failed, V_FrameWork_ItemRightHand is ignored");
        return false;
    }
    g_HookTarget = target;
    return true;
}

bool Uninstall_EquipPluginImpl_NormalMtevCallback_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig = nullptr;
    }
    return true;
}
