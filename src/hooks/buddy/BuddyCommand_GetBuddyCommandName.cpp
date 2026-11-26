#include <pch.h>

#include "BuddyCommand_GetBuddyCommandName.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using GetName_t = std::uint64_t*(__fastcall*)(std::uint64_t* langId, std::uint32_t command);

    struct LangSwap
    {
        std::uint64_t dWalkerLangId;
        std::uint64_t battleGearLangId;
    };

    constexpr LangSwap kLines[] = {
        { 0x0000E9E4E15FFB33ull, 0x00000FECBDF0EB80ull },   // voice_cmd_wg_whistle_call, V_voice_cmd_bg_whistle_call
        { 0x0000BCABBD5C99A5ull, 0x000082C9A3B78F00ull },   // voice_cmd_wg_whistle_call_help, V_voice_cmd_bg_whistle_call_help
        { 0x0000814CD0298FACull, 0x00007AEA0B51E867ull },   // voice_cmd_wg_normal_mode_help, V_voice_cmd_bg_normal_mode_help
        { 0x0000FDF5C8257546ull, 0x000086B7CCC0AD38ull },   // voice_cmd_wg_stealth_mode_help, V_voice_cmd_bg_stealth_mode_help
        { 0x00004575069071B0ull, 0x00003C176324698Full },   // voice_cmd_wg_search_mode_help, V_voice_cmd_bg_search_mode_help
        { 0x0000A61A19B544E8ull, 0x0000BB7034CA3783ull },   // voice_cmd_wg_intercept_mode_help, V_voice_cmd_bg_intercept_mode_help
    };

    // dec edx ; mov qword [rcx], 0 ; cmp edx, 0x19 ; ja +0x1CD (both switches share the shape)
    const std::uint8_t kSwitchEntry[] = { 0xFF, 0xCA, 0x48, 0xC7, 0x01, 0x00, 0x00, 0x00, 0x00, 0x83, 0xFA, 0x19, 0x0F, 0x87, 0xCD, 0x01, 0x00, 0x00 };

    struct Hook
    {
        const char* what;
        GetName_t   orig;
        void*       target;
    };

    Hook g_Help = { "voice-command help", nullptr, nullptr };
    Hook g_Name = { "voice-command label", nullptr, nullptr };

    void SwapSEH(std::uint64_t* langId)
    {
        __try
        {
            for (const LangSwap& s : kLines)
            {
                if (*langId == s.dWalkerLangId)
                {
                    *langId = s.battleGearLangId;
                    return;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    std::uint64_t* __fastcall hkGetHelp(std::uint64_t* langId, std::uint32_t command)
    {
        std::uint64_t* out = g_Help.orig(langId, command);
        if (langId && battleGearWeapons::OnField())
            SwapSEH(langId);
        return out;
    }

    std::uint64_t* __fastcall hkGetName(std::uint64_t* langId, std::uint32_t command)
    {
        std::uint64_t* out = g_Name.orig(langId, command);
        if (langId && battleGearWeapons::OnField())
            SwapSEH(langId);
        return out;
    }

    bool HookOne(Hook& h, std::uintptr_t address, void* detour)
    {
        if (h.target)
            return true;
        void* target = address ? ResolveGameAddress(address) : nullptr;
        if (!target)
        {
            LogError("BattleGear: %s lookup address missing on this build, the Battle Gear orders keep the D-Walker text", h.what);
            return false;
        }
        const HookEntry state = CheckHookEntry(target, kSwitchEntry, sizeof(kSwitchEntry));
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("BattleGear: %s lookup differs on this build (%s), the Battle Gear orders keep the D-Walker text", h.what, desc);
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop(h.what, target);
        if (!CreateAndEnableHook(target, detour, reinterpret_cast<void**>(&h.orig)))
        {
            LogError("BattleGear: %s lookup hook failed, the Battle Gear orders keep the D-Walker text", h.what);
            return false;
        }
        h.target = target;
        return true;
    }

    void UnhookOne(Hook& h)
    {
        if (!h.target)
            return;
        DisableAndRemoveHook(h.target);
        h.target = nullptr;
        h.orig = nullptr;
    }
}

bool Install_BuddyCommand_GetBuddyCommandName_Hooks()
{
    const bool help = HookOne(g_Help, gAddr.BuddyCommand_GetBuddyCommandHelpName, reinterpret_cast<void*>(&hkGetHelp));
    const bool name = HookOne(g_Name, gAddr.BuddyCommand_GetBuddyCommandName, reinterpret_cast<void*>(&hkGetName));
    return help && name;
}

void Uninstall_BuddyCommand_GetBuddyCommandName_Hooks()
{
    UnhookOne(g_Name);
    UnhookOne(g_Help);
}
