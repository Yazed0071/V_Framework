#include "pch.h"
#include "TornadoDualPatch.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    static constexpr std::uint8_t kOriginalBytes[2] = { 0x74, 0x10 };
    static constexpr std::uint8_t kEnabledBytes[2]  = { 0x90, 0x90 };
    static bool g_Applied = false;

    static bool WriteBytes(void* target, const std::uint8_t* src)
    {
        DWORD oldProtect = 0;
        if (!VirtualProtect(target, sizeof(kOriginalBytes), PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            LogError("TornadoDual: VirtualProtect failed (err=%lu), patch bytes not written", GetLastError());
            return false;
        }
        std::memcpy(target, src, sizeof(kOriginalBytes));
        DWORD restored = 0;
        VirtualProtect(target, sizeof(kOriginalBytes), oldProtect, &restored);
        FlushInstructionCache(GetCurrentProcess(), target, sizeof(kOriginalBytes));
        return true;
    }
}

bool Install_TornadoDualPatch()
{
    if (g_Applied)
        return true;

    if (!gAddr.TornadoDualPatch)
    {
        LogError("TornadoDual: patch address missing for this build, Tornado dual patch off");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.TornadoDualPatch);
    if (!target)
    {
        LogError("TornadoDual: patch address unresolved, Tornado dual patch off");
        return false;
    }

    const auto* cur = static_cast<const std::uint8_t*>(target);
    if (cur[0] != kOriginalBytes[0] || cur[1] != kOriginalBytes[1])
    {
        LogError("TornadoDual: unexpected bytes %02X %02X at patch site, Tornado dual patch off",
                 static_cast<unsigned>(cur[0]), static_cast<unsigned>(cur[1]));
        return false;
    }

    if (!WriteBytes(target, kEnabledBytes))
        return false;

    g_Applied = true;
    return true;
}

void Uninstall_TornadoDualPatch()
{
    if (!g_Applied)
        return;

    if (gAddr.TornadoDualPatch)
    {
        void* target = ResolveGameAddress(gAddr.TornadoDualPatch);
        if (target)
            WriteBytes(target, kOriginalBytes);
    }
    g_Applied = false;
}
