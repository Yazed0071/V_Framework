#include "pch.h"

#include "Buddy2BlockControllerImpl_GetPathAtBuddyType.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>

#include "AddressSet.h"
#include "BuddyOutfitRegistry.h"
#include "FoxPathInternal.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using GetPathAtBuddyType_t = std::uint64_t*(__fastcall*)(void* self, std::uint64_t* outPath,
                                                             std::int32_t buddyType, void* a4);
    using FoxPath_Path_t = void*(__fastcall*)(void* path, std::uint64_t code64ext);

    GetPathAtBuddyType_t g_Orig = nullptr;
    void*                g_Target = nullptr;
    FoxPath_Path_t       g_FoxPath_Path = nullptr;

    constexpr std::uint16_t kQuietExitMission = 10260;

    std::mutex                      g_MissingMutex;
    std::unordered_set<std::string> g_MissingLogged;

    void WarnMissingOnce(const buddyoutfit::Entry& e)
    {
        std::lock_guard<std::mutex> lock(g_MissingMutex);
        if (g_MissingLogged.insert(e.key).second)
            LogWarn("BuddyOutfit '%s': fpkPath not installed, the vanilla %s is worn",
                e.key.c_str(), buddyoutfit::KindName(e.kind));
    }

    bool MainPackKind(std::int32_t buddyType, buddyoutfit::Kind& kind, std::size_t& varOff)
    {
        switch (buddyType)
        {
        case buddyoutfit::kBuddyTypeQuiet:
            kind = buddyoutfit::Kind::QuietCostume;
            varOff = buddyoutfit::kVarQuietCostume;
            return true;
        case buddyoutfit::kBuddyTypeHorse:
            kind = buddyoutfit::Kind::HorseTack;
            varOff = buddyoutfit::kVarHorseTack;
            return true;
        case buddyoutfit::kBuddyTypeDog:
            kind = buddyoutfit::Kind::DogGear;
            varOff = buddyoutfit::kVarDogGear;
            return true;
        default:
            return false;
        }
    }

    std::uint64_t* __fastcall hkGetPathAtBuddyType(void* self, std::uint64_t* outPath,
                                                   std::int32_t buddyType, void* a4)
    {
        buddyoutfit::Kind kind = buddyoutfit::Kind::QuietCostume;
        std::size_t varOff = 0;
        if (outPath && g_FoxPath_Path && MainPackKind(buddyType, kind, varOff)
            && buddyoutfit::HasEntries() && !MissionCodeGuard::ShouldBypassHooks())
        {
            std::uint8_t* vars = buddyoutfit::ReadScriptVars();
            if (vars
                && !(buddyType == buddyoutfit::kBuddyTypeQuiet
                     && *reinterpret_cast<std::uint16_t*>(vars + buddyoutfit::kVarMissionCode) == kQuietExitMission))
            {
                const std::uint8_t value = vars[varOff];
                buddyoutfit::Entry e;
                if (value >= buddyoutfit::FirstCustomValue(kind) && buddyoutfit::TryGetByValue(kind, value, &e))
                {
                    if (fox::detail::PathExistsByCode(e.fpkPath))
                    {
                        g_FoxPath_Path(outPath, e.fpkPath);
                        return outPath;
                    }
                    WarnMissingOnce(e);
                    vars[varOff] = 0;
                }
            }
        }
        return g_Orig(self, outPath, buddyType, a4);
    }
}

bool Install_Buddy2BlockControllerImpl_GetPathAtBuddyType_Hook()
{
    if (g_Target)
        return true;
    g_FoxPath_Path = reinterpret_cast<FoxPath_Path_t>(ResolveGameAddress(gAddr.FoxPath_Path));
    void* target = ResolveGameAddress(gAddr.Buddy2BlockControllerImpl_GetPathAtBuddyType);
    if (!target || !g_FoxPath_Path)
    {
        LogError("BuddyOutfit: buddy pack lookup address missing on this build, custom buddy outfits off");
        return false;
    }
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkGetPathAtBuddyType),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BuddyOutfit: buddy pack lookup hook failed, custom buddy outfits off");
        return false;
    }
    g_Target = target;
    return true;
}

bool Uninstall_Buddy2BlockControllerImpl_GetPathAtBuddyType_Hook()
{
    if (!g_Target)
        return true;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
    return true;
}
