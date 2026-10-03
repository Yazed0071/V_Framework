#include "pch.h"
#include "ServerManager_BufferRelease.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using BufferRelease_t = void(__fastcall*)();

    BufferRelease_t g_Orig      = nullptr;
    void**          g_Instance  = nullptr;
    bool            g_Installed = false;
    bool            g_Reported  = false;

    void* const* ResolveInstanceSlot(const std::uint8_t* target)
    {
        if (!target)
            return nullptr;
        __try
        {
            if (target[0] == 0xE9)
            {
                std::int32_t jmp = 0;
                std::memcpy(&jmp, target + 1, sizeof(jmp));
                target += 5 + jmp;
            }

            const std::uint8_t* mov = target + 4;
            if (mov[0] != 0x48 || mov[1] != 0x8B || mov[2] != 0x0D)
                return nullptr;
            std::int32_t rel = 0;
            std::memcpy(&rel, mov + 3, sizeof(rel));
            return reinterpret_cast<void* const*>(mov + 7 + rel);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool InstanceIsLive()
    {
        if (!g_Instance)
            return true;
        __try
        {
            return *g_Instance != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hkBufferRelease()
    {
        if (!InstanceIsLive())
        {
            if (!g_Reported)
            {
                g_Reported = true;
                LogWarn("ServerManager: BufferRelease with singleton gone, release skipped");
            }
            return;
        }
        if (g_Orig)
            g_Orig();
    }
}

bool Install_ServerManagerBufferRelease()
{
    if (g_Installed)
        return true;
    if (!gAddr.Net_ServerManagerBufferRelease)
        return false;

    void* target = ResolveGameAddress(gAddr.Net_ServerManagerBufferRelease);
    if (!target)
        return false;

    g_Instance = const_cast<void**>(
        ResolveInstanceSlot(reinterpret_cast<const std::uint8_t*>(target)));
    if (!g_Instance)
    {
        LogError("ServerManager: %p has no singleton load, teardown guard off", target);
        return false;
    }

    if (!CreateAndEnableHook(target, &hkBufferRelease,
                             reinterpret_cast<void**>(&g_Orig)))
    {
        g_Orig = nullptr;
        LogError("ServerManager: teardown guard hook failed @ %p", target);
        return false;
    }

    g_Installed = true;
    return true;
}

bool Uninstall_ServerManagerBufferRelease()
{
    if (!g_Installed)
        return true;
    if (gAddr.Net_ServerManagerBufferRelease)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.Net_ServerManagerBufferRelease));
    g_Orig      = nullptr;
    g_Instance  = nullptr;
    g_Installed = false;
    return true;
}
