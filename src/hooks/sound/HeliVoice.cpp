#include <pch.h>
#include "HeliVoice.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    struct PatchSite
    {
        std::uint8_t written[sizeof(unsigned int)] = {};
        bool         ours   = false;
        bool         warned = false;
    };

    std::unordered_map<uintptr_t, PatchSite> g_Sites;

    using FNVHash32_t = unsigned int(__fastcall*)(const char* strToHash);
    static FNVHash32_t g_FNVHash32 = nullptr;

    unsigned int GetFNVHash32(const char* strToHash)
    {
        if (!g_FNVHash32 && gAddr.FNVHash32)
        {
            g_FNVHash32 = reinterpret_cast<FNVHash32_t>(
                ResolveGameAddress(gAddr.FNVHash32));
        }

        if (!g_FNVHash32 || !strToHash)
            return 0;

        const unsigned int ret = g_FNVHash32(strToHash);
        return ret;
    }

    bool TogglePatch(bool isEnable,
                     uintptr_t pointer,
                     SIZE_T dwSize,
                     const std::uint8_t* originalBytes,
                     const std::uint8_t* enabledBytes)
    {
        if (!pointer)
        {
            LogError("HeliVoice: address missing for this build, heli voice not swapped");
            return false;
        }

        void* target = ResolveGameAddress(pointer);
        if (!target)
        {
            LogError("HeliVoice: address 0x%llx not resolved, heli voice not swapped",
                static_cast<unsigned long long>(pointer));
            return false;
        }

        PatchSite& site = g_Sites[pointer];
        std::uint8_t live[sizeof(site.written)] = {};
        if (dwSize > sizeof(live) || !ReadEntryBytesSEH(target, live, dwSize))
        {
            LogWarn("HeliVoice: site 0x%llx unreadable, heli voice not swapped",
                static_cast<unsigned long long>(pointer));
            return false;
        }

        const bool holdsVanilla = std::memcmp(live, originalBytes, dwSize) == 0;
        const bool holdsNew     = std::memcmp(live, enabledBytes, dwSize) == 0;
        const bool holdsOurs    = site.ours && std::memcmp(live, site.written, dwSize) == 0;

        if (isEnable ? holdsNew : holdsVanilla)
        {
            site.ours = isEnable;
            if (isEnable)
                std::memcpy(site.written, enabledBytes, dwSize);
            return true;
        }

        if (!holdsVanilla && !holdsOurs)
        {
            if (!site.warned)
            {
                site.warned = true;
                LogWarn("HeliVoice: another module changed the site 0x%llx, this part of the heli voice is off",
                    static_cast<unsigned long long>(pointer));
            }
            return false;
        }

        DWORD oldProtect = 0;
        if (!VirtualProtect(target, dwSize, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            LogError("HeliVoice: VirtualProtect failed at 0x%llx (err=%lu), heli voice not swapped",
                static_cast<unsigned long long>(pointer),
                GetLastError());
            return false;
        }

        const std::uint8_t* src = isEnable ? enabledBytes : originalBytes;
        std::memcpy(target, src, dwSize);
        site.ours = isEnable;
        if (isEnable)
            std::memcpy(site.written, enabledBytes, dwSize);

        DWORD restored = 0;
        VirtualProtect(target, dwSize, oldProtect, &restored);
        FlushInstructionCache(GetCurrentProcess(), target, dwSize);

        return true;
    }

    static const char* kOriginalVoiceEvent = "DD_vox_SH_voice";
    static const char* kOriginalRadioEvent = "DD_vox_SH_radio";
}

bool SetEnableHeliVoice(bool isEnable,
                        const char* DD_vox_SH_voice_new,
                        const char* DD_vox_SH_radio_new)
{
    unsigned int origVoiceHash = GetFNVHash32(kOriginalVoiceEvent);
    unsigned int origRadioHash = GetFNVHash32(kOriginalRadioEvent);
    unsigned int newVoiceHash  = GetFNVHash32(DD_vox_SH_voice_new);
    unsigned int newRadioHash  = GetFNVHash32(DD_vox_SH_radio_new);

    if (!origVoiceHash || !origRadioHash)
    {
        LogError("HeliVoice: FNV hash function missing for this build, heli voice not swapped");
        return false;
    }

    auto* origVoiceBytes = reinterpret_cast<std::uint8_t*>(&origVoiceHash);
    auto* origRadioBytes = reinterpret_cast<std::uint8_t*>(&origRadioHash);
    auto* newVoiceBytes  = reinterpret_cast<std::uint8_t*>(&newVoiceHash);
    auto* newRadioBytes  = reinterpret_cast<std::uint8_t*>(&newRadioHash);

    const SIZE_T dwSize = sizeof(unsigned int);

    const bool ok0 = TogglePatch(isEnable, gAddr.DD_vox_SH_voice,  dwSize, origVoiceBytes, newVoiceBytes);
    const bool ok1 = TogglePatch(isEnable, gAddr.DD_vox_SH_radio,  dwSize, origRadioBytes, newRadioBytes);
    const bool ok2 = TogglePatch(isEnable, gAddr.DD_vox_SH_radio2, dwSize, origRadioBytes, newRadioBytes);
    const bool ok3 = TogglePatch(isEnable, gAddr.DD_vox_SH_radio3, dwSize, origRadioBytes, newRadioBytes);

    return ok0 && ok1 && ok2 && ok3;
}
