#include <pch.h>

#include "MissionPreparationSystemImpl_GetParamBuddyPartsFromVars.h"

#include <atomic>
#include <cstdint>
#include <intrin.h>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "../buddy/BattleGearBuddy.h"
#include "../buddy/BattleGearLoadoutSlots.h"

#ifdef _DEBUG
namespace
{
    using GetParamBuddyPartsFromVars_t = void(__fastcall*)(void* system, std::uint8_t buddyType, float* param, std::uint8_t walkerFlag);

    constexpr int kParamFloats = 5;   // position x y z w, then rotation
    constexpr int kLoggedCalls = 20;

    const std::uint8_t kGetParamEntry[] = { 0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40 };

    GetParamBuddyPartsFromVars_t g_Orig = nullptr;
    void*                        g_Target = nullptr;
    std::atomic<int>             g_Logged{ 0 };

    bool ReadFloatsSEH(const float* param, float* out)
    {
        __try
        {
            for (int i = 0; i < kParamFloats; ++i)
                out[i] = param[i];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hkGetParamBuddyPartsFromVars(void* system, std::uint8_t buddyType, float* param, std::uint8_t walkerFlag)
    {
        g_Orig(system, buddyType, param, walkerFlag);
        battleGearLoadoutSlots::AfterGetParam(buddyType, reinterpret_cast<std::uint8_t*>(param), walkerFlag, _ReturnAddress());
        if (buddyType != battleGear::kBuddyType || !param)
            return;
        float v[kParamFloats] = {};
        if (ReadFloatsSEH(param, v) && g_Logged.fetch_add(1, std::memory_order_relaxed) < kLoggedCalls)
            LogDebug("BattleGear: preview spot for Battle Gear: position %.2f %.2f %.2f, rotation %.2f, walker flag %u",
                v[0], v[1], v[2], v[4], walkerFlag);
    }
}
#endif

bool Install_MissionPreparationSystemImpl_GetParamBuddyPartsFromVars_Hook()
{
#ifndef _DEBUG
    return true;
#else
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.MissionPreparationSystemImpl_GetParamBuddyPartsFromVars);
    if (!target)
        return false;
    const HookEntry state = CheckHookEntry(target, kGetParamEntry, sizeof(kGetParamEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogWarn("BattleGear: preview param code differs on this build (%s), preview probe off", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: preview param", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkGetParamBuddyPartsFromVars), reinterpret_cast<void**>(&g_Orig)))
    {
        LogWarn("BattleGear: preview param hook failed, preview probe off");
        return false;
    }
    g_Target = target;
    return true;
#endif
}

void Uninstall_MissionPreparationSystemImpl_GetParamBuddyPartsFromVars_Hook()
{
#ifdef _DEBUG
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
#endif
}
