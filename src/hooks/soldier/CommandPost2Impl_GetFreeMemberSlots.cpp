#include <pch.h>

#include "CommandPost2Impl_GetFreeMemberSlots.h"

#include <cstdint>

#include "AddressSet.h"
#include "CommandPost2Impl_Initialize.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using GetFreeMemberSlots_t = std::uint32_t(__fastcall*)(const std::uint8_t* controllers);

    constexpr std::size_t kControllerInterface = 0x20;    // soldiers ask through CommandPost2Impl + 0x20
    constexpr std::size_t kMembersUsed         = 0x174;

    // mov eax,360 / sub eax,[rcx+0x174] / ret
    const std::uint8_t kGetFreeMemberSlotsCode[] = { 0xB8, 0x68, 0x01, 0x00, 0x00, 0x2B, 0x81, 0x74, 0x01, 0x00, 0x00, 0xC3 };

    GetFreeMemberSlots_t g_Orig = nullptr;
    void*                g_Target = nullptr;

    std::uint32_t __fastcall hkGetFreeMemberSlots(const std::uint8_t* controllers)
    {
        const std::uint32_t used = *reinterpret_cast<const std::uint32_t*>(controllers + kMembersUsed);
        return GetCommandPostMemberCapacity(controllers - kControllerInterface) - used;
    }
}

bool Install_CommandPost2Impl_GetFreeMemberSlots_Hook()
{
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.CommandPost2Impl_GetFreeMemberSlots);
    if (!target)
    {
        LogWarn("CommandPost: free member slots address missing on %s, soldiers keep the 360 limit", GetGameBuildName(gGameBuild));
        return false;
    }
    if (CheckHookEntry(target, kGetFreeMemberSlotsCode, sizeof(kGetFreeMemberSlotsCode)) != HookEntry::Expected)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogWarn("CommandPost: free member slots code is not vanilla (%s), soldiers keep the 360 limit", desc);
        return false;
    }
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkGetFreeMemberSlots), reinterpret_cast<void**>(&g_Orig)))
    {
        LogWarn("CommandPost: free member slots hook failed, soldiers keep the 360 limit");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_CommandPost2Impl_GetFreeMemberSlots_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
}
