#include <pch.h>

#include "AnimControl_PoseToBoneMatrices.h"

#include <cstdint>

#include "AddressSet.h"
#include "BattleGearLegs.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using PoseToBoneMatrices_t = void*(__fastcall*)(std::uint8_t* animControl, std::uint8_t* skeleton);

    const std::uint8_t kPoseToBoneMatricesEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x57, 0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00 };

    PoseToBoneMatrices_t g_Orig = nullptr;
    void*                g_Target = nullptr;

    void* __fastcall hkPoseToBoneMatrices(std::uint8_t* animControl, std::uint8_t* skeleton)
    {
        void* result = g_Orig(animControl, skeleton);
        battleGearLegs::OnBonesWritten(skeleton);
        return result;
    }
}

bool Install_AnimControl_PoseToBoneMatrices_Hook()
{
    if (g_Target)
        return true;
    void* target = gAddr.AnimControl_PoseToBoneMatrices ? ResolveGameAddress(gAddr.AnimControl_PoseToBoneMatrices) : nullptr;
    if (!target)
    {
        LogWarn("BattleGear: bone matrix address missing on %s, the legs stay as the D-Walker carries them", GetGameBuildName(gGameBuild));
        return false;
    }
    const HookEntry state = CheckHookEntry(target, kPoseToBoneMatricesEntry, sizeof(kPoseToBoneMatricesEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogWarn("BattleGear: AnimControl bone matrix code differs on this build (%s), legs off", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: AnimControl bone matrices", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkPoseToBoneMatrices), reinterpret_cast<void**>(&g_Orig)))
    {
        LogWarn("BattleGear: bone matrix hook failed, legs off");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_AnimControl_PoseToBoneMatrices_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
}
