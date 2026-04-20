#include "pch.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "ConvertParameterIdLogger.h"

namespace
{
    // fox::sd::ConvertParameterID signature: takes a C-string name, returns a
    // 32-bit parameter id (FNV-1 over the string). See mgsvtpp.exe.c line
    // 8381463 for an in-context use: `sd::_fnv132HashString(*puVar2)` feeding
    // the SetRTPC call inside fox::sdx::SourceBody::SetRTPC.
    using ConvertParameterID_t = std::uint32_t(__cdecl*)(const char* name);

    static ConvertParameterID_t g_OrigConvertParameterID = nullptr;
    static void* g_HookedAddr = nullptr;

    // De-dupe set: each unique (name) is logged exactly once to keep the log
    // readable. After warm-up the hook is effectively free — a single
    // hash-set .contains() check per call.
    static std::mutex g_Mutex;
    static std::unordered_set<std::string> g_SeenNames;

    static std::uint32_t __cdecl hkConvertParameterID(const char* name)
    {
        const std::uint32_t id = g_OrigConvertParameterID
            ? g_OrigConvertParameterID(name)
            : 0u;

        if (name && *name)
        {
            bool firstSeen = false;
            {
                std::lock_guard<std::mutex> lock(g_Mutex);
                firstSeen = g_SeenNames.emplace(name).second;
            }
            if (firstSeen)
            {
                Log("[ParamID] \"%s\" -> 0x%08X\n", name, id);
            }
        }
        return id;
    }
}

bool Install_ConvertParameterIdLogger_Hook()
{
    if (g_HookedAddr)
        return true;

    if (gAddr.Fox_Sd_ConvertParameterID == 0)
    {
        Log("[ConvertParameterIdLogger] address not available — skipping\n");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.Fox_Sd_ConvertParameterID);
    if (!target)
    {
        Log("[ConvertParameterIdLogger] ResolveGameAddress failed\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkConvertParameterID),
        reinterpret_cast<void**>(&g_OrigConvertParameterID));

    if (!ok)
    {
        Log("[ConvertParameterIdLogger] MinHook failed at %p\n", target);
        return false;
    }

    g_HookedAddr = target;
    Log("[Hook] ConvertParameterIdLogger: OK target=%p\n", target);
    return true;
}

bool Uninstall_ConvertParameterIdLogger_Hook()
{
    if (!g_HookedAddr)
        return true;

    DisableAndRemoveHook(g_HookedAddr);
    g_HookedAddr = nullptr;
    g_OrigConvertParameterID = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_SeenNames.clear();
    }

    Log("[ConvertParameterIdLogger] removed\n");
    return true;
}
