#include <pch.h>

#include "CommandPost2Impl_CreateController.h"

#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "CommandPost2Impl_Initialize.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    struct GroupDesc
    {
        std::uint32_t count;
        std::uint32_t hash;
        std::uint16_t idBase;   // game object type << 9
    };

    struct GroupState
    {
        std::uint32_t membersUsed;
        std::uint32_t groups;
        bool          joinsGroup;
    };

    using CreateController_t = void*(__fastcall*)(std::uint8_t* controllers, const GroupDesc* desc);

    constexpr std::size_t   kControllerInterface = 0x20;    // the game registers through CommandPost2Impl + 0x20
    constexpr std::size_t   kGroupArray          = 0x1F0;   // 0x48 bytes per group
    constexpr std::size_t   kGroupSize           = 0x48;
    constexpr std::size_t   kGroupHash           = 0x38;
    constexpr std::size_t   kMembersUsed         = 0x174;
    constexpr std::size_t   kGroupCount          = 0x324;
    constexpr std::uint32_t kGroupSlots          = 4;
    constexpr std::uint32_t kMostGroupsRead      = 16;
    constexpr std::uint32_t kNoHash              = 0xBF169F98;

    const std::uint8_t kCreateControllerEntry[] = { 0x40, 0x57, 0x44, 0x8B, 0x52, 0x04, 0x45, 0x33, 0xC9, 0x48, 0x8B, 0xFA,
                                                    0x4C, 0x8B, 0xD9, 0x41, 0x81, 0xFA, 0x98, 0x9F, 0x16, 0xBF, 0x74, 0x2E };

    CreateController_t         g_Orig = nullptr;
    void*                      g_Target = nullptr;
    std::atomic<std::uint32_t> g_ReportedBuild{ 0 };   // command post build count + 1 of the last overflow reported

    bool ReadGroupStateSEH(const std::uint8_t* controllers, const GroupDesc* desc, GroupState& state)
    {
        __try
        {
            state.membersUsed = *reinterpret_cast<const std::uint32_t*>(controllers + kMembersUsed);
            state.groups = *reinterpret_cast<const std::uint32_t*>(controllers + kGroupCount);
            state.joinsGroup = false;
            if (desc->hash == kNoHash)
                return true;
            const std::uint8_t* groups = *reinterpret_cast<const std::uint8_t* const*>(controllers + kGroupArray);
            for (std::uint32_t i = 0; i < state.groups && i < kMostGroupsRead; ++i)
                if (*reinterpret_cast<const std::uint64_t*>(groups + i * kGroupSize + kGroupHash) == desc->hash)
                    state.joinsGroup = true;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    const char* GroupName(std::uint16_t idBase)
    {
        switch (idBase >> 9)
        {
        case 2:  return "soldiers";
        case 15: return "enemy helis";
        case 26: return "UAVs";
        case 27: return "security cameras";
        default: return "game objects";
        }
    }

    void* __fastcall hkCreateController(std::uint8_t* controllers, const GroupDesc* desc)
    {
        GroupState state = {};
        if (controllers && desc && ReadGroupStateSEH(controllers, desc, state) && !state.joinsGroup)
        {
            const std::uint32_t capacity = GetCommandPostMemberCapacity(controllers - kControllerInterface);
            const std::uint32_t freeSlots = state.membersUsed < capacity ? capacity - state.membersUsed : 0;
            const std::uint32_t build = GetCommandPostBuildCount() + 1;
            const unsigned mission = MissionCodeGuard::GetCurrentMissionCode();
            if (desc->count > freeSlots && g_ReportedBuild.exchange(build) != build)
                LogError("CommandPost: mission %u: %u %s need command post member slots, only %u of %u are free "
                         "(soldiers, cameras, UAVs and enemy helis share them): expect a crash",
                         mission, desc->count, GroupName(desc->idBase), freeSlots, capacity);
            else if (state.groups >= kGroupSlots && g_ReportedBuild.exchange(build) != build)
                LogError("CommandPost: mission %u: %s would be a 5th group of command post members, the game holds %u: expect a crash",
                         mission, GroupName(desc->idBase), kGroupSlots);
        }
        return g_Orig(controllers, desc);
    }
}

bool Install_CommandPost2Impl_CreateController_Hook()
{
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.CommandPost2Impl_CreateController);
    if (!target)
        return false;
    const HookEntry state = CheckHookEntry(target, kCreateControllerEntry, sizeof(kCreateControllerEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogWarn("CommandPost: CreateController code differs on this build (%s), member overflow check off", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("CommandPost: CreateController", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkCreateController), reinterpret_cast<void**>(&g_Orig)))
    {
        LogWarn("CommandPost: CreateController hook failed, member overflow check off");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_CommandPost2Impl_CreateController_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
}
