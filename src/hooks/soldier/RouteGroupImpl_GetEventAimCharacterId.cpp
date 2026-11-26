#include <pch.h>
#include "RouteGroupImpl_GetEventAimCharacterId.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using GetEventAimCharacterId_t = std::uint64_t*(__fastcall*)(void* routeGroup, std::uint64_t* characterId, std::uint16_t route, std::uint16_t node, std::uint16_t event);

    // RouteSetTool writes a <character> as 32 bits; the player is the only game object that checks all 48
    constexpr std::uint64_t kPlayer  = 0x79D0B8071B05ull; // Player
    constexpr std::uint64_t kPlayer1 = 0x85FF34576F39ull; // Player1
    constexpr std::uint64_t kPlayer2 = 0xED4DBB7CA1BEull; // Player2
    constexpr std::uint64_t kLow32   = 0xFFFFFFFFull;

    const std::uint8_t kGetEventAimCharacterIdPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x8B, 0x41, 0x50, 0x33, 0xC9, 0x48, 0x8B, 0xDA, 0x44, 0x0F, 0xB6, 0x50, 0x40
    };

    GetEventAimCharacterId_t g_OrigGetEventAimCharacterId = nullptr;
    void*                    g_HookTarget                 = nullptr;

    std::uintptr_t GetEventAimCharacterIdAddr()
    {
        return gAddr.RouteGroupImpl_GetEventAimCharacterId;
    }

    std::uint64_t* __fastcall hk_GetEventAimCharacterId(void* routeGroup, std::uint64_t* characterId, std::uint16_t route, std::uint16_t node, std::uint16_t event)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigGetEventAimCharacterId, routeGroup, characterId, route, node, event);
        std::uint64_t* result = g_OrigGetEventAimCharacterId(routeGroup, characterId, route, node, event);
        if (!result || (*result & ~kLow32) != 0)
            return result;
        if (*result == (kPlayer & kLow32))
            *result = kPlayer;
        else if (*result == (kPlayer1 & kLow32))
            *result = kPlayer1;
        else if (*result == (kPlayer2 & kLow32))
            *result = kPlayer2;
        return result;
    }
}

bool Install_RouteGroupImpl_GetEventAimCharacterId_Hook()
{
    if (g_HookTarget)
        return true;
    const std::uintptr_t addr = GetEventAimCharacterIdAddr();
    if (!addr)
        return true;

    auto* target = static_cast<std::uint8_t*>(ResolveGameAddress(addr));
    const HookEntry entry = CheckHookEntry(target, kGetEventAimCharacterIdPrologue, sizeof(kGetEventAimCharacterIdPrologue));
    if (entry == HookEntry::Unknown)
    {
        LogError("RouteCharacterTarget: route code does not match %s, route <character>Player</character> targets made with RouteSetTool stay broken",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("RouteCharacterTarget: RouteGroupImpl::GetEventAimCharacterId", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_GetEventAimCharacterId), reinterpret_cast<void**>(&g_OrigGetEventAimCharacterId)))
    {
        LogError("RouteCharacterTarget: RouteGroupImpl::GetEventAimCharacterId hook failed, route <character>Player</character> targets made with RouteSetTool stay broken");
        return false;
    }
    g_HookTarget = target;
    return true;
}

bool Uninstall_RouteGroupImpl_GetEventAimCharacterId_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_OrigGetEventAimCharacterId = nullptr;
    }
    return true;
}
