#include "pch.h"
#include "DeployControllerImpl_GenerateDeployMission.h"

#include "DeployMissionRegistry.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

#include <string>

namespace
{
    using GenerateDeployMission_t = void(__fastcall*)(std::uint8_t* iface, std::uint8_t slot,
                                                       std::uint16_t id, std::uint16_t seed,
                                                       std::uint8_t rank);

    constexpr int kSeedRerollLimit = 16;

    GenerateDeployMission_t g_OrigGenerateDeployMission = nullptr;

    std::uint16_t UniqueSeed(std::uint8_t* iface, std::uint8_t slot, std::uint16_t id, std::uint16_t seed)
    {
        for (int attempt = 0; attempt < kSeedRerollLimit
             && deployMission::SeedCollides(iface, slot, id, seed); ++attempt)
            seed = deployMission::CallCreateSeed(iface);
        return seed;
    }

    void GenerateCustomMission(std::uint8_t* iface, std::uint8_t slot, std::uint16_t id,
                               std::uint16_t seed, std::uint8_t rank)
    {
        deployMission::MissionView mission;
        if (!deployMission::FindMission(id, mission))
        {
            LogWarn("Dispatch: slot %u asked for unknown deployMissionId %u, slot cleared",
                static_cast<unsigned>(slot), static_cast<unsigned>(id));
            deployMission::CallClear(iface, slot);
            return;
        }

        if (!deployMission::IsArmed() || MissionCodeGuard::ShouldBypassHooks())
        {
            deployMission::CallClear(iface, slot);
            return;
        }

        const std::uint16_t placeholder = deployMission::ChoosePlaceholder(iface, slot, mission);
        if (placeholder == 0)
        {
            LogWarn("Dispatch: '%s' found no free vanilla mission to stand in for it, slot %u cleared",
                mission.key.c_str(), static_cast<unsigned>(slot));
            deployMission::CallClear(iface, slot);
            return;
        }

        const std::uint16_t uniqueSeed = UniqueSeed(iface, slot, placeholder, seed);
        g_OrigGenerateDeployMission(iface, slot, placeholder, uniqueSeed, rank);
        deployMission::ForgetForeignFacts(slot, placeholder, uniqueSeed, mission.key);
        deployMission::BindSlot(slot, placeholder, uniqueSeed, mission.key);
    }

    void __fastcall hkGenerateDeployMission(std::uint8_t* iface, std::uint8_t slot, std::uint16_t id,
                                            std::uint16_t seed, std::uint8_t rank)
    {
        if (id >= deployMission::kVanillaIdBound)
        {
            GenerateCustomMission(iface, slot, id, seed, rank);
            return;
        }

        const std::uint16_t uniqueSeed = UniqueSeed(iface, slot, id, seed);
        g_OrigGenerateDeployMission(iface, slot, id, uniqueSeed, rank);
        deployMission::ForgetForeignFacts(slot, id, uniqueSeed, std::string());
    }
}

bool Install_DeployControllerImpl_GenerateDeployMission_Hook()
{
    void* target = ResolveGameAddress(gAddr.DeployControllerImpl_GenerateDeployMission);
    const bool installed = CreateAndEnableHook(target,
        reinterpret_cast<void*>(&hkGenerateDeployMission),
        reinterpret_cast<void**>(&g_OrigGenerateDeployMission));
    if (!installed)
        LogError("Dispatch: GenerateDeployMission hook failed, custom dispatch missions off");
    return installed;
}

bool Uninstall_DeployControllerImpl_GenerateDeployMission_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.DeployControllerImpl_GenerateDeployMission));
    g_OrigGenerateDeployMission = nullptr;
    return true;
}
