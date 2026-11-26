#include <pch.h>

#include "TppModelViewerBuddyCommand_Execute.h"

#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "../buddy/BattleGearBuddy.h"
#include "../buddy/BattleGearCustomizePage.h"

namespace
{
    using Execute_t = void(__fastcall*)(void* command, std::int32_t* result, std::int32_t mode, std::uint8_t* param);

    constexpr std::size_t  kParamBuddyType = 0x14;
    constexpr std::uint8_t kWalkerGearType = 4;     // BuddyType.WALKER_GEAR
#ifdef _DEBUG
    constexpr std::size_t kParamPlaceFlag = 0x64;   // nonzero: the buddy is placed at the param pose
    constexpr int         kLoggedCalls    = 40;
#endif

    const std::uint8_t kExecuteEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x40 };

    Execute_t g_Orig = nullptr;
    void*     g_Target = nullptr;
#ifdef _DEBUG
    std::atomic<int> g_Logged{ 0 };
#endif

    std::uint8_t ReadByteSEH(const std::uint8_t* base, std::size_t offset)
    {
        __try
        {
            return base ? base[offset] : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void WriteByteSEH(std::uint8_t* base, std::size_t offset, std::uint8_t value)
    {
        __try
        {
            base[offset] = value;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    // Customize > BATTLE GEAR is the D-Walker page: its viewer loads the Battle Gear the way the sortie preview does
    void __fastcall hkExecute(void* command, std::int32_t* result, std::int32_t mode, std::uint8_t* param)
    {
        if (param && battleGearCustomizePage::Active() && ReadByteSEH(param, kParamBuddyType) == kWalkerGearType)
            WriteByteSEH(param, kParamBuddyType, battleGear::kBuddyType);
        g_Orig(command, result, mode, param);
#ifdef _DEBUG
        if (ReadByteSEH(param, kParamBuddyType) != battleGear::kBuddyType)
            return;
        if (g_Logged.fetch_add(1, std::memory_order_relaxed) < kLoggedCalls)
            LogDebug("BattleGear: preview viewer mode %d for Battle Gear (place flag %u) -> result %d",   // modes 0 init, 1 end, 2 change, 3 confirm, 4 view, 5 hide
                mode, ReadByteSEH(param, kParamPlaceFlag), result ? *result : 0);
#endif
    }
}

bool Install_TppModelViewerBuddyCommand_Execute_Hook()
{
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.TppModelViewerBuddyCommand_Execute);
    if (!target)
        return false;
    const HookEntry state = CheckHookEntry(target, kExecuteEntry, sizeof(kExecuteEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogWarn("BattleGear: preview viewer code differs on this build (%s), Customize > BATTLE GEAR shows the D-Walker", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: preview viewer", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkExecute), reinterpret_cast<void**>(&g_Orig)))
    {
        LogWarn("BattleGear: preview viewer hook failed, Customize > BATTLE GEAR shows the D-Walker");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_TppModelViewerBuddyCommand_Execute_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
}
