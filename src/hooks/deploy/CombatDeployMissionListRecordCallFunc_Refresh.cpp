#include "pch.h"
#include "CombatDeployMissionListRecordCallFunc_Refresh.h"

#include "DeployMissionRegistry.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using Refresh_t        = void(__fastcall*)(std::uint8_t* record, std::uint64_t index);
    using SetVisible_t     = void(__fastcall*)(void* ui, void* node, bool visible);
    using SetColor_t       = void(__fastcall*)(void* ui, void* node, std::uint32_t color);
    using SetRevengeIcon_t = void(__fastcall*)(void* node, std::uint16_t revengeId, void* ui);

    constexpr std::size_t   kRecordCallback      = 0x30;
    constexpr std::size_t   kRecordMarker        = 0x50;
    constexpr std::size_t   kRecordRevengeIcon   = 0x58;
    constexpr std::size_t   kCallbackUi          = 0x20;
    constexpr std::size_t   kCallbackListMode    = 0x6090;
    constexpr std::size_t   kCallbackInfos       = 0x718;
    constexpr std::size_t   kInfoSize            = 0xB0;
    constexpr std::size_t   kInfoAccepted        = 0x56;
    constexpr std::size_t   kInfoLogged          = 0x57;
    constexpr std::size_t   kInfoMissionId       = 0xA4;
    constexpr std::size_t   kUiSetIconVisible    = 0x2A8;
    constexpr std::size_t   kUiSetMarkerVisible  = 0x2B0;
    constexpr std::size_t   kUiSetColor          = 0x320;
    constexpr std::uint8_t  kListModeNoMarker    = 2;
    constexpr std::uint32_t kStoryMarkerColor    = 0xDFC45151;

    Refresh_t g_OrigRefresh = nullptr;

    template <typename Fn>
    Fn UiMethod(void* ui, std::size_t offset)
    {
        std::uint8_t* vtable = nullptr;
        void* function = nullptr;
        if (!deployMission::ReadEngine(ui, vtable) || !deployMission::ReadEngine(vtable + offset, function))
            return nullptr;
        return reinterpret_cast<Fn>(function);
    }

    void ShowStoryMarker(std::uint8_t* record, std::uint8_t* callback, std::uint8_t* info, void* ui)
    {
        std::uint8_t mode = 0;
        std::uint8_t accepted = 0;
        std::uint8_t logged = 0;
        if (!deployMission::ReadEngine(callback + kCallbackListMode, mode)
            || !deployMission::ReadEngine(info + kInfoAccepted, accepted)
            || !deployMission::ReadEngine(info + kInfoLogged, logged)
            || mode == kListModeNoMarker || accepted != 0 || logged != 0)
            return;

        void* marker = deployMission::EnginePointerAt<void>(record, kRecordMarker);
        const auto setVisible = UiMethod<SetVisible_t>(ui, kUiSetMarkerVisible);
        const auto setColor = UiMethod<SetColor_t>(ui, kUiSetColor);
        if (!marker || !setVisible || !setColor)
            return;
        setVisible(ui, marker, true);
        setColor(ui, marker, kStoryMarkerColor);
    }

    void ShowRevengeIcon(std::uint8_t* record, void* ui, std::uint16_t revengeId)
    {
        void* icon = deployMission::EnginePointerAt<void>(record, kRecordRevengeIcon);
        const auto setVisible = UiMethod<SetVisible_t>(ui, kUiSetIconVisible);
        const auto setRevengeIcon = reinterpret_cast<SetRevengeIcon_t>(ResolveGameAddress(gAddr.SetRevengeIcon));
        if (!icon || !setVisible || !setRevengeIcon)
            return;
        setVisible(ui, icon, true);
        setRevengeIcon(icon, revengeId, ui);
    }

    void ApplyCustomMarks(std::uint8_t* record, std::uint64_t index)
    {
        std::uint8_t* callback = deployMission::EnginePointerAt<std::uint8_t>(record, kRecordCallback);
        if (!callback)
            return;
        std::uint8_t* info = callback + kCallbackInfos + (index & 0xFFFFFFFF) * kInfoSize;
        std::uint16_t id = 0;
        if (!deployMission::ReadEngine(info + kInfoMissionId, id) || !deployMission::IsCustomId(id))
            return;

        deployMission::MissionView mission;
        if (!deployMission::FindMission(id, mission) || (!mission.important && mission.revengeMissionId == 0))
            return;
        void* ui = deployMission::EnginePointerAt<void>(callback, kCallbackUi);
        if (!ui)
            return;

        if (mission.important)
            ShowStoryMarker(record, callback, info, ui);
        if (mission.revengeMissionId != 0)
            ShowRevengeIcon(record, ui, mission.revengeMissionId);
    }

    void __fastcall hkRefresh(std::uint8_t* record, std::uint64_t index)
    {
        g_OrigRefresh(record, index);
        if (record)
            ApplyCustomMarks(record, index);
    }
}

bool Install_CombatDeployMissionListRecordCallFunc_Refresh_Hook()
{
    void* target = ResolveGameAddress(gAddr.CombatDeployMissionListRecordCallFunc_Refresh);
    const bool installed = CreateAndEnableHook(target,
        reinterpret_cast<void*>(&hkRefresh),
        reinterpret_cast<void**>(&g_OrigRefresh));
    if (!installed)
        LogError("Dispatch: mission list row hook failed, custom dispatch missions off");
    return installed;
}

bool Uninstall_CombatDeployMissionListRecordCallFunc_Refresh_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.CombatDeployMissionListRecordCallFunc_Refresh));
    g_OrigRefresh = nullptr;
    return true;
}
