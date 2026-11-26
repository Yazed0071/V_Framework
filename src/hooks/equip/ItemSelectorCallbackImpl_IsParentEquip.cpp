#include <pch.h>
#include "ItemSelectorCallbackImpl_IsParentEquip.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "DevelopArrayGrow.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using IsParentEquip_t = bool(__fastcall*)(void* selector, std::uint16_t row, std::uint16_t ancestor);
    using GetBaseId_t     = std::uint16_t(__fastcall*)(void* develop, std::uint16_t row);

    constexpr std::size_t kSelectorDevelop = 0x58;
    constexpr std::size_t kGetBaseIdSlot   = 0x1A0 / sizeof(void*);
    constexpr int         kMostGrades      = 256;   // a custom parent chain that loops back on itself ends here

    const std::uint8_t kEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48,
        0x89, 0x7C, 0x24, 0x20, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20,
    };

    IsParentEquip_t g_Orig   = nullptr;
    void*           g_Target = nullptr;

    enum class Walk { Found, Ended, ReachedVanilla };

    Walk WalkCustomGradesSEH(void* selector, std::uint16_t& row, std::uint16_t ancestor, std::uint32_t firstCustom)
    {
        __try
        {
            void* develop = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(selector) + kSelectorDevelop);
            const GetBaseId_t getBaseId = (*reinterpret_cast<GetBaseId_t* const*>(develop))[kGetBaseIdSlot];
            for (int step = 0; step < kMostGrades; ++step)
            {
                if (row == equip::kFlowSentinel)
                    return Walk::Ended;
                if (row == ancestor)
                    return Walk::Found;
                if (row < firstCustom)
                    return Walk::ReachedVanilla;
                row = getBaseId(develop, row);
            }
            return Walk::Ended;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return Walk::Ended;
        }
    }

    bool __fastcall hkIsParentEquip(void* selector, std::uint16_t row, std::uint16_t ancestor)
    {
        const std::uint32_t firstCustom = equip::FirstCustomFlowIndex();
        if ((row < firstCustom && ancestor < firstCustom) || ancestor == equip::kFlowSentinel || !selector
            || MissionCodeGuard::ShouldBypassHooks())
            return g_Orig(selector, row, ancestor);
        switch (WalkCustomGradesSEH(selector, row, ancestor, firstCustom))
        {
        case Walk::Found:          return true;
        case Walk::ReachedVanilla: return g_Orig(selector, row, ancestor);   // vanilla grades keep the game's own rule
        default:                   return false;
        }
    }
}

bool Install_ItemSelectorCallbackImpl_IsParentEquip_Hook()
{
    g_Target = ResolveGameAddress(gAddr.ItemSelectorCallbackImpl_IsParentEquip);
    if (!g_Target)
    {
        LogError("GradeFamily: IsParentEquip address missing for this build, two grades of a custom item can be carried");
        return true;
    }
    const HookEntry entry = CheckHookEntry(g_Target, kEntry, sizeof(kEntry));
    if (entry == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(g_Target, desc, sizeof(desc));
        LogError("GradeFamily: ItemSelectorCallbackImpl::IsParentEquip differs on this build (%s), "
                 "two grades of a custom item can be carried", desc);
        g_Target = nullptr;
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("GradeFamily: ItemSelectorCallbackImpl::IsParentEquip", g_Target);
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkIsParentEquip), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("GradeFamily: IsParentEquip hook failed, two grades of a custom item can be carried");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_ItemSelectorCallbackImpl_IsParentEquip_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
