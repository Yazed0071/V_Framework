#include <pch.h>

#include "RealizedWalkerGear2Impl_Execute1.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "BattleGearBuddy.h"
#include "BattleGearEffects.h"
#include "BattleGearLegs.h"
#include "BattleGearSound.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "WalkerGear2Impl_SetUpMainWeapon.h"
#include "log.h"

namespace
{
    using Execute1_t = void(__fastcall*)(void* self, const void* context);

    constexpr std::size_t   kController     = 0x10;
    constexpr std::size_t   kFirstInstance  = 0x54;
    constexpr std::size_t   kInstanceCount  = 0x58;
    constexpr std::size_t   kGameObjectImpl = 0xA0;
    constexpr std::size_t   kImplType       = 0x08;
    constexpr std::size_t   kCtrlFirst      = 0x0C;
    constexpr std::size_t   kCtrlRecords    = 0x10;
    constexpr std::size_t   kRecordSize     = 0xC0;
    constexpr std::uint16_t kBuddyWalker    = 0x2C;

    const std::uint8_t kExecute1Entry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
                                            0x74, 0x24, 0x18, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20 };

    Execute1_t g_Orig = nullptr;
    void*      g_Target = nullptr;

    std::uint8_t* BuddyWalkerRecordSEH(void* self, void*& walkerInterface)
    {
        __try
        {
            auto* base = static_cast<std::uint8_t*>(self);
            auto* impl = *reinterpret_cast<std::uint8_t**>(base + kGameObjectImpl);
            auto* ctrl = *reinterpret_cast<std::uint8_t**>(base + kController);
            if (!impl || !ctrl || *reinterpret_cast<std::uint16_t*>(impl + kImplType) != kBuddyWalker)
                return nullptr;
            walkerInterface = impl;
            if (*reinterpret_cast<std::uint32_t*>(base + kInstanceCount) == 0)
                return nullptr;
            const std::uint32_t instance = *reinterpret_cast<std::uint32_t*>(base + kFirstInstance);
            auto* records = *reinterpret_cast<std::uint8_t**>(ctrl + kCtrlRecords);
            const std::uint32_t first = *reinterpret_cast<std::uint32_t*>(ctrl + kCtrlFirst);
            if (!records || instance < first)
                return nullptr;
            return records + (instance - first) * kRecordSize;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void __fastcall hkExecute1(void* self, const void* context)
    {
        g_Orig(self, context);
        void* walkerInterface = nullptr;
        if (!battleGear::IsBattleGearOut() || battleGear::HoldsAnotherBuddy())
        {
            battleGearLegs::SetFieldRecord(nullptr);
            battleGearWeapons::OnNotOut();
            battleGearEffects::Drop();
            battleGearSound::Drop();
            if (battleGear::IsBattleGearLoaded() && BuddyWalkerRecordSEH(self, walkerInterface))   // the ACC shows it while another buddy is the sortie pick
                KeepBattleGearWalkerPartsHidden(walkerInterface);
            return;
        }
        if (std::uint8_t* record = BuddyWalkerRecordSEH(self, walkerInterface))
        {
            KeepBattleGearWalkerPartsHidden(walkerInterface);
            battleGearLegs::SetFieldRecord(record);
            battleGearWeapons::OnFieldFrame();
            battleGearEffects::OnFieldFrame();
        }
    }
}

bool Install_RealizedWalkerGear2Impl_Execute1_Hook()
{
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.RealizedWalkerGear2Impl_Execute1);
    if (!target)
        return false;
    const HookEntry state = CheckHookEntry(target, kExecute1Entry, sizeof(kExecute1Entry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogWarn("BattleGear: walker update code differs on this build (%s), ground fit off", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: walker Execute1", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkExecute1), reinterpret_cast<void**>(&g_Orig)))
    {
        LogWarn("BattleGear: walker update hook failed, ground fit off");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_RealizedWalkerGear2Impl_Execute1_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    battleGearLegs::SetFieldRecord(nullptr);
}
