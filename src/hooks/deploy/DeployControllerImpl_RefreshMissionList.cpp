#include "pch.h"
#include "DeployControllerImpl_RefreshMissionList.h"

#include "DeployMissionRegistry.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using RefreshMissionList_t = void(__fastcall*)(std::uint8_t* impl);

    constexpr std::size_t kImplToIface          = 0x20;
    constexpr std::size_t kSeqProgressOffset    = 0x1E28;
    constexpr std::size_t kSeqLimitOffset       = 0x1E30;
    constexpr std::size_t kOpenedRareOffset     = 0x1E70;
    constexpr std::size_t kRareBandCountOffset  = 0x1E95;
    constexpr std::size_t kDirtyOffset          = 0x79D8;

    constexpr std::uint8_t kFirstSeqSlot    = 1;
    constexpr std::uint8_t kSeqSlotBound    = 0x15;
    constexpr std::uint8_t kFirstRareSlot   = 0x1F;
    constexpr std::uint8_t kLastRareSlot    = 0x23;
    constexpr std::uint8_t kFirstNormalSlot = 0x24;
    constexpr std::uint8_t kLastNormalSlot  = 0x28;

    RefreshMissionList_t g_OrigRefreshMissionList = nullptr;

    std::uint32_t SlotWord(std::uint32_t* words, std::uint8_t slot)
    {
        std::uint32_t word = 0;
        deployMission::ReadEngine(words + slot, word);
        return word;
    }

    void RefreshSequentialMission(std::uint8_t* iface, std::uint32_t* words)
    {
        std::uint16_t progress = 0;
        std::uint16_t limit = 0;
        deployMission::ReadEngine(deployMission::EnginePointerAt<std::uint16_t>(iface, kSeqProgressOffset), progress);
        deployMission::ReadEngine(deployMission::EnginePointerAt<std::uint16_t>(iface, kSeqLimitOffset), limit);
        if (progress >= kSeqSlotBound || progress > limit)
            return;

        for (std::uint8_t slot = kFirstSeqSlot; slot < kSeqSlotBound; ++slot)
        {
            if (slot != progress)
            {
                deployMission::CallClear(iface, slot);
                continue;
            }
            const std::uint32_t word = SlotWord(words, slot);
            if (deployMission::DecodeSlotId(word) != 0 && deployMission::IsSlotAccepted(word))
                continue;
            const std::uint16_t seed = deployMission::CallCreateSeed(iface);
            deployMission::CallGenerate(iface, slot, slot, seed, deployMission::RankOf(iface, slot));
        }
    }

    void FillPoolSlot(std::uint8_t* iface, std::uint8_t slot, bool rareBand)
    {
        const std::uint16_t from = deployMission::EffectiveRingPosition(iface, rareBand);
        const std::uint16_t pick = deployMission::NextInRing(iface, rareBand, from);
        deployMission::CommitRingPick(iface, rareBand, pick);

        const std::uint16_t rankSource = rareBand
            ? deployMission::EffectiveRingPosition(iface, false)
            : pick;
        const std::uint16_t seed = deployMission::CallCreateSeed(iface);
        deployMission::CallGenerate(iface, slot, pick, seed, deployMission::RankOf(iface, rankSource));
    }

    void RefreshPoolMissions(std::uint8_t* iface, std::uint32_t* words)
    {
        std::uint8_t openedRare = 0;
        std::uint8_t rareBandCount = 0;
        deployMission::ReadEngine(deployMission::EnginePointerAt<std::uint8_t>(iface, kOpenedRareOffset), openedRare);
        deployMission::ReadEngine(iface + kRareBandCountOffset, rareBandCount);

        if (openedRare == 1 || rareBandCount == 0)
        {
            for (std::uint8_t slot = kFirstNormalSlot; slot <= kLastNormalSlot; ++slot)
                if (deployMission::DecodeSlotId(SlotWord(words, slot)) == 0)
                    FillPoolSlot(iface, slot, false);
        }

        if (openedRare == 1)
        {
            for (std::uint8_t slot = kFirstRareSlot; slot <= kLastRareSlot; ++slot)
                if (deployMission::DecodeSlotId(SlotWord(words, slot)) == 0)
                    FillPoolSlot(iface, slot, true);
        }
        else
        {
            for (std::uint8_t slot = kFirstRareSlot; slot <= kLastRareSlot; ++slot)
                deployMission::CallClear(iface, slot);
        }
    }

    void __fastcall hkRefreshMissionList(std::uint8_t* impl)
    {
        std::uint8_t* iface = impl + kImplToIface;
        std::uint32_t* words = deployMission::SlotWords(iface);
        if (!words || !deployMission::IsArmed() || MissionCodeGuard::ShouldBypassHooks()
            || !deployMission::HasRotationMissions())
        {
            g_OrigRefreshMissionList(impl);
            return;
        }

        RefreshSequentialMission(iface, words);
        RefreshPoolMissions(iface, words);

        const std::uint8_t dirty = 1;
        deployMission::WriteEngine(iface + kDirtyOffset, dirty);
    }
}

bool Install_DeployControllerImpl_RefreshMissionList_Hook()
{
    void* target = ResolveGameAddress(gAddr.DeployControllerImpl_RefreshMissionList);
    const bool installed = CreateAndEnableHook(target,
        reinterpret_cast<void*>(&hkRefreshMissionList),
        reinterpret_cast<void**>(&g_OrigRefreshMissionList));
    if (!installed)
        LogError("Dispatch: RefreshMissionList hook failed, custom dispatch missions off");
    return installed;
}

bool Uninstall_DeployControllerImpl_RefreshMissionList_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.DeployControllerImpl_RefreshMissionList));
    g_OrigRefreshMissionList = nullptr;
    return true;
}
