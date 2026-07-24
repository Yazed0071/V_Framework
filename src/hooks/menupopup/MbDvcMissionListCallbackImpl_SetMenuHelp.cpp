#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "MbDvcMissionListCallbackImpl_SetMenuHelp.h"
#include "log.h"

namespace
{
    constexpr std::size_t   kProvIdx_SetHelpText = 0x98 / sizeof(void*);
    constexpr std::uint32_t kYellowColorCode     = 0xDFC45151u;

    using SetMenuHelp_t = void  (__fastcall*)(void* self, std::uint64_t param1);
    using SetHelpText_t = void  (__fastcall*)(void* provider, std::uint64_t stringId,
                                              std::uint32_t zero, std::uint32_t colorCode);
    using LangIdToKey_t = void* (__fastcall*)(std::uint64_t* out, const char* langId);

    struct HelpOverride { std::uint64_t stringId; std::uint32_t colorCode; };

    std::mutex g_mtx;
    std::unordered_map<std::uint16_t, HelpOverride> g_overrides;

    SetMenuHelp_t g_orig = nullptr;

    bool TryReadCurrentMissionCode(void* self, std::uint16_t& outCode)
    {
        __try
        {
            const auto base = reinterpret_cast<const std::uint8_t*>(self);
            const std::uint32_t ac = *reinterpret_cast<const std::uint32_t*>(base + 0xac);
            if (ac == 0)
                return false;
            const std::uint32_t idx =
                (*reinterpret_cast<const std::uint32_t*>(base + 0xb8) +
                 *reinterpret_cast<const std::uint32_t*>(base + 0xb4)) % ac;
            outCode = *reinterpret_cast<const std::uint16_t*>(
                base + static_cast<std::uintptr_t>(idx) * 0x28 + 0x7e4);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool SafeSetHelpText(void* self, std::uint64_t stringId, std::uint32_t colorCode)
    {
        __try
        {
            const auto base = reinterpret_cast<const std::uint8_t*>(self);
            void* sub = *reinterpret_cast<void* const*>(base + 0x38);
            if (!sub)
                return false;
            void* provider = *reinterpret_cast<void* const*>(
                reinterpret_cast<const std::uint8_t*>(sub) + 0x60);
            if (!provider)
                return false;
            void** vtbl = *reinterpret_cast<void***>(provider);
            if (!vtbl)
                return false;
            auto setFn = reinterpret_cast<SetHelpText_t>(vtbl[kProvIdx_SetHelpText]);
            if (!setFn)
                return false;
            setFn(provider, stringId, 0, colorCode);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void __fastcall hk_SetMenuHelp(void* self, std::uint64_t param1)
    {
        if (!g_orig)
            return;

        if ((param1 & 0xFF) == 1)
        {
            std::uint16_t code = 0;
            if (TryReadCurrentMissionCode(self, code))
            {
                bool found = false;
                std::uint64_t customId = 0;
                std::uint32_t colorCode = 0;
                {
                    std::lock_guard<std::mutex> lk(g_mtx);
                    const auto it = g_overrides.find(code);
                    if (it != g_overrides.end())
                    {
                        found = true;
                        customId = it->second.stringId;
                        colorCode = it->second.colorCode;
                    }
                }

                if (found && customId != 0)
                {
                    if (SafeSetHelpText(self, customId, colorCode ? colorCode : kYellowColorCode))
                        return;
                }
            }
        }

        g_orig(self, param1);
    }

    std::uint64_t LangKeyFromString(const char* langId)
    {
        auto toKey = reinterpret_cast<LangIdToKey_t>(
            ResolveGameAddress(gAddr.Ui_LangIdToKey));
        if (!toKey || !langId)
            return 0;
        std::uint64_t key = 0;
        __try { toKey(&key, langId); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
        return key;
    }
}

bool Set_MissionMenuHelp(std::uint16_t missionCode, const char* langId, const char* colorName)
{
    std::uint64_t key = 0;
    if (langId && *langId)
    {
        key = LangKeyFromString(langId);
        if (!key)
        {
            Log("[MissionMenuHelp] WARN: langId '%s' resolved to 0 - override for mission "
                "%u not set.\n", langId, static_cast<unsigned>(missionCode));
            return false;
        }
    }

    const std::uint32_t colorCode = (colorName && *colorName)
        ? static_cast<std::uint32_t>(FoxHashes::StrCode32(colorName))
        : 0u;

    std::lock_guard<std::mutex> lk(g_mtx);
    HelpOverride& ov = g_overrides[missionCode];
    ov.stringId = key;
    ov.colorCode = colorCode;
    return true;
}

void Clear_MissionMenuHelp(std::uint16_t missionCode)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    g_overrides.erase(missionCode);
}

bool Install_MissionMenuHelp_Hook()
{
    void* target = ResolveGameAddress(gAddr.MbDvcMissionListCallbackImpl_SetMenuHelp);
    if (!target)
        return true;

    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_SetMenuHelp),
                             reinterpret_cast<void**>(&g_orig)))
    {
        Log("[MissionMenuHelp] ERROR: SetMenuHelp hook install failed - custom mission "
            "help text will not appear.\n");
        return false;
    }
    return true;
}

bool Uninstall_MissionMenuHelp_Hook()
{
    if (gAddr.MbDvcMissionListCallbackImpl_SetMenuHelp)
        DisableAndRemoveHook(ResolveGameAddress(
            gAddr.MbDvcMissionListCallbackImpl_SetMenuHelp));

    g_orig = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_overrides.clear();
    }
    return true;
}
