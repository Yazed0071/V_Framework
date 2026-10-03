#include "pch.h"
#include "CombatDeployCallbackImpl_RefreshMissionList.h"

#include "DeployMissionRegistry.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using RefreshMissionList_t = void(__fastcall*)(std::uint8_t* callback, bool keepCursor,
                                                   std::uint32_t top, std::uint32_t cursor);

    constexpr std::size_t  kListMode      = 0x60B0;
    constexpr std::size_t  kRows          = 0x738;
    constexpr std::size_t  kRowSize       = 0xB0;
    constexpr std::size_t  kRowLimit      = 0x80;
    constexpr std::uint8_t kAvailableList = 0;

    RefreshMissionList_t g_OrigRefreshMissionList = nullptr;
    thread_local std::uint8_t* t_BuildingRows = nullptr;

    void __fastcall hkRefreshMissionList(std::uint8_t* callback, bool keepCursor,
                                         std::uint32_t top, std::uint32_t cursor)
    {
        std::uint8_t mode = 0xFF;
        std::uint8_t* const previous = t_BuildingRows;
        t_BuildingRows = (callback && deployMission::ReadEngine(callback + kListMode, mode) && mode == kAvailableList)
            ? callback + kRows : nullptr;
        g_OrigRefreshMissionList(callback, keepCursor, top, cursor);
        t_BuildingRows = previous;
    }
}

void ArrangeMissionListRow(std::uint8_t* missionInfo)
{
    std::uint8_t* const rows = t_BuildingRows;
    if (!rows || missionInfo < rows || missionInfo >= rows + kRowLimit * kRowSize)
        return;
    const std::size_t offset = static_cast<std::size_t>(missionInfo - rows);
    if (offset % kRowSize == 0)
        deployMission::ArrangeListRows(rows, offset / kRowSize + 1);
}

bool Install_CombatDeployCallbackImpl_RefreshMissionList_Hook()
{
    void* target = ResolveGameAddress(gAddr.CombatDeployCallbackImpl_RefreshMissionList);
    const bool installed = CreateAndEnableHook(target,
        reinterpret_cast<void*>(&hkRefreshMissionList),
        reinterpret_cast<void**>(&g_OrigRefreshMissionList));
    if (!installed)
        LogError("Dispatch: mission list build hook failed, custom dispatch missions off");
    return installed;
}

bool Uninstall_CombatDeployCallbackImpl_RefreshMissionList_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.CombatDeployCallbackImpl_RefreshMissionList));
    g_OrigRefreshMissionList = nullptr;
    return true;
}
