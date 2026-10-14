#include "pch.h"

#include "Buddy2BlockControllerImpl_GetBuddyQuietFovaFile.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>

#include "AddressSet.h"
#include "BuddyCodeCave.h"
#include "BuddyOutfitRegistry.h"
#include "FoxPathInternal.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using GetFovaFile_t  = std::uint64_t*(__fastcall*)(void* self, std::uint64_t* outPath);
    using FoxPath_Path_t = void*(__fastcall*)(void* path, std::uint64_t code64ext);

    GetFovaFile_t  g_Orig = nullptr;
    void*          g_Target = nullptr;
    FoxPath_Path_t g_FoxPath_Path = nullptr;

    BuddyCodeCave g_Fv2Cave{ "Quiet bruise fv2 lookup" };
    alignas(8) volatile std::uint64_t g_Fv2Id = 0;
    std::uint64_t g_VanillaFv2Id = 0;

    constexpr std::uint8_t kFv2LoadBytes[] =
    {
        0x48, 0x8B, 0x15, 0x38, 0xD9, 0x02, 0x02 // mov rdx, [rip+X] (X = the qui0_dam_on.fv2 id)
    };

    std::mutex                      g_MissingMutex;
    std::unordered_set<std::string> g_MissingLogged;

    std::uint64_t* __fastcall hkGetBuddyQuietFovaFile(void* self, std::uint64_t* outPath)
    {
        g_Fv2Id = g_VanillaFv2Id;
        buddyoutfit::Entry e;
        std::uint8_t* vars = buddyoutfit::ReadScriptVars();
        if (vars && outPath && g_FoxPath_Path && !MissionCodeGuard::ShouldBypassHooks()
            && vars[buddyoutfit::kVarQuietCostume] >= buddyoutfit::FirstCustomValue(buddyoutfit::Kind::QuietCostume)
            && buddyoutfit::TryGetByValue(buddyoutfit::Kind::QuietCostume, vars[buddyoutfit::kVarQuietCostume], &e)
            && e.bruiseFpk && e.bruiseFv2)
        {
            if (fox::detail::PathExistsByCode(e.bruiseFpk))
            {
                g_Fv2Id = e.bruiseFv2;
                g_FoxPath_Path(outPath, e.bruiseFpk);
                return outPath;
            }
            std::lock_guard<std::mutex> lock(g_MissingMutex);
            if (g_MissingLogged.insert(e.key).second)
                LogWarn("BuddyOutfit '%s': bruiseFpk not installed, NAKED's bruise is used", e.key.c_str());
        }
        return g_Orig(self, outPath);
    }

    bool InstallFv2Cave()
    {
        const std::uintptr_t site = gAddr.Buddy2BlockControllerImpl_QuietFv2IdLoad;
        auto* at = site ? static_cast<std::uint8_t*>(ResolveGameAddress(site)) : nullptr;
        if (!at || std::memcmp(at, kFv2LoadBytes, sizeof(kFv2LoadBytes)) != 0)
        {
            LogError("BuddyOutfit: Quiet bruise fv2 site missing or changed, own bruise files off");
            return false;
        }
        std::int32_t rel = 0;
        std::memcpy(&rel, at + 3, sizeof(rel));
        const std::uintptr_t next = reinterpret_cast<std::uintptr_t>(at) + sizeof(kFv2LoadBytes);
        g_VanillaFv2Id = *reinterpret_cast<const std::uint64_t*>(next + rel);
        g_Fv2Id = g_VanillaFv2Id;

        CaveCode c;
        c.Emit({ 0x48, 0xBA });                                         // mov rdx, &g_Fv2Id
        c.Imm64(reinterpret_cast<std::uint64_t>(&g_Fv2Id));
        c.Emit({ 0x48, 0x8B, 0x12 });                                   // mov rdx, [rdx]
        c.JmpAbs(next);
        return InstallBuddyCodeCave(g_Fv2Cave, site, kFv2LoadBytes, sizeof(kFv2LoadBytes), c);
    }
}

bool Install_Buddy2BlockControllerImpl_GetBuddyQuietFovaFile_Hooks()
{
    if (g_Target)
        return true;
    if (!InstallFv2Cave())
        return false;
    g_FoxPath_Path = reinterpret_cast<FoxPath_Path_t>(ResolveGameAddress(gAddr.FoxPath_Path));
    void* target = ResolveGameAddress(gAddr.Buddy2BlockControllerImpl_GetBuddyQuietFovaFile);
    if (!target || !g_FoxPath_Path
        || !CreateAndEnableHook(target, reinterpret_cast<void*>(&hkGetBuddyQuietFovaFile),
                                reinterpret_cast<void**>(&g_Orig)))
    {
        UninstallBuddyCodeCave(g_Fv2Cave);
        LogError("BuddyOutfit: Quiet bruise pack hook failed or address missing, own bruise files off");
        return false;
    }
    g_Target = target;
    return true;
}

bool Uninstall_Buddy2BlockControllerImpl_GetBuddyQuietFovaFile_Hooks()
{
    if (g_Target)
    {
        DisableAndRemoveHook(g_Target);
        g_Target = nullptr;
        g_Orig = nullptr;
    }
    UninstallBuddyCodeCave(g_Fv2Cave);
    return true;
}
