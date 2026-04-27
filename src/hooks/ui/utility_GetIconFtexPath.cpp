#include "pch.h"
#include "utility_GetIconFtexPath.h"

#include <Windows.h>
#include <cstdint>
#include <unordered_map>
#include <mutex>

#include "HookUtils.h"
#include "log.h"
#include "AddressSet.h"

namespace
{
    using GetIconFtexPath_t = std::int64_t* (__fastcall*)(std::int64_t* outPathId, std::uint32_t equipId, int mode);

    static GetIconFtexPath_t g_OrigGetIconFtexPath = nullptr;
    static std::unordered_map<int, std::uint64_t> g_PerEquipIconPaths;
    static std::mutex g_PerEquipIconPathsMutex;
    static bool g_EquipIconFtexPathHookInstalled = false;
}

// Sets a custom FTEX icon path for one equipId.
// Params: equipId (int), texturePathHash (uint64_t)
void EquipIcon_SetEquipIdIconFtexPath(int equipId, uint64_t texturePathHash)
{
    std::lock_guard<std::mutex> lock(g_PerEquipIconPathsMutex);
    g_PerEquipIconPaths[equipId] = texturePathHash;

    Log("[EquipIcon] EquipId %d -> 0x%llX\n",
        equipId,
        static_cast<unsigned long long>(texturePathHash));
}

// Clears a custom FTEX icon path for one equipId.
// Params: equipId (int)
void EquipIcon_ClearIconFtexPath(int equipId)
{
    std::lock_guard<std::mutex> lock(g_PerEquipIconPathsMutex);
    g_PerEquipIconPaths.erase(equipId);
    Log("[EquipIcon] EquipId %d cleared\n", equipId);
}

// Clears all custom FTEX icon path overrides.
// Params: none
void EquipIcon_ClearAllIconFtexPaths()
{
    std::lock_guard<std::mutex> lock(g_PerEquipIconPathsMutex);
    g_PerEquipIconPaths.clear();
    Log("[EquipIcon] All per-equip icon paths cleared\n");
}

// Hooked version of tpp::ui::utility::GetIconFtexPath.
// Params: outPathId (int64_t*), equipId (uint32_t), mode (int)
static std::int64_t* __fastcall hkGetIconFtexPath(std::int64_t* outPathId, std::uint32_t equipId, int mode)
{
    {
        std::lock_guard<std::mutex> lock(g_PerEquipIconPathsMutex);
        const auto it = g_PerEquipIconPaths.find(static_cast<int>(equipId));
        if (it != g_PerEquipIconPaths.end())
        {
            *outPathId = static_cast<std::int64_t>(it->second);
            Log("[EquipIcon] equipId=%u mode=%d -> 0x%llX\n",
                equipId,
                mode,
                static_cast<unsigned long long>(it->second));
            return outPathId;
        }
    }

    // Diagnostic fall-through logging (added 2026-04-27 to debug the
    // "vanilla weapon icon doesn't show when custom suit equipped"
    // regression). UI polls this every frame; dedup by (equipId, mode,
    // resultIsZero) tuple so we only log when the outcome changes.
    //
    // What we want to see in the log:
    //   - When wearing a vanilla suit: equipId=N mode=M -> non-zero path
    //   - When wearing a custom suit: equipId=N mode=M -> zero path  (THE BUG)
    // OR
    //   - When wearing a custom suit: equipId=0 mode=M  (the slot is asking
    //                                                    for "no weapon")
    //
    // Either signal points us at the upstream cause — whether it's the
    // orig returning null or the caller passing equipId=0 because the
    // loadout reset.
    std::int64_t saved = outPathId ? *outPathId : 0;
    auto* result = g_OrigGetIconFtexPath(outPathId, equipId, mode);
    const std::int64_t got = (outPathId && result) ? *outPathId : 0;

    static std::uint32_t s_lastEquipId = 0xFFFFFFFFu;
    static int           s_lastMode    = -1;
    static bool          s_lastWasZero = false;
    const bool isZero = (got == 0);
    if (s_lastEquipId != equipId
        || s_lastMode != mode
        || s_lastWasZero != isZero)
    {
        Log("[EquipIcon] orig fall-through: equipId=%u mode=%d -> %s "
            "(path=0x%llX)\n",
            equipId,
            mode,
            isZero ? "ZERO (no icon)" : "OK",
            static_cast<unsigned long long>(got));
        s_lastEquipId = equipId;
        s_lastMode    = mode;
        s_lastWasZero = isZero;
    }
    (void)saved;
    return result;
}

// Installs the Equip icon FTEX path hook.
// Params: none
bool Install_EquipIconFtexPath_Hook()
{
    if (g_EquipIconFtexPathHookInstalled)
    {
        Log("[EquipIcon] hook already installed\n");
        return true;
    }

    void* target = ResolveGameAddress(gAddr.GetIconFtexPath);
    if (!target)
    {
        Log("[EquipIcon] target resolve failed\n");
        return false;
    }

    if (!CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkGetIconFtexPath),
        reinterpret_cast<void**>(&g_OrigGetIconFtexPath)))
    {
        Log("[EquipIcon] hook install failed\n");
        return false;
    }

    g_EquipIconFtexPathHookInstalled = true;
    Log("[EquipIcon] hook installed\n");
    return true;
}

// Uninstalls the Equip icon FTEX path hook.
// Params: none
bool Uninstall_EquipIconFtexPath_Hook()
{
    if (!g_EquipIconFtexPathHookInstalled)
        return true;

    void* target = ResolveGameAddress(gAddr.GetIconFtexPath);
    if (target)
        DisableAndRemoveHook(target);

    g_OrigGetIconFtexPath = nullptr;
    g_EquipIconFtexPathHookInstalled = false;

    Log("[EquipIcon] hook removed\n");
    return true;
}
