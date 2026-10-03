#include "pch.h"
#include "CombatDeployCallbackImpl_RefreshMissionIconInGlobe.h"

#include "DeployMissionRegistry.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

#include <cstring>

namespace
{
    using RefreshMissionIconInGlobe_t = void(__fastcall*)(std::uint8_t* callback);
    using GlobeClear_t  = void(__fastcall*)(void* globe);
    using GlobeMarker_t = void(__fastcall*)(void* globe, float latitude, float longitude, std::uint32_t type);
    using GlobeFocus_t  = void(__fastcall*)(void* globe, float latitude, float longitude);

    constexpr std::size_t kCallbackDeploy    = 0xC8;
    constexpr std::size_t kCallbackSelected  = 0x5F38;
    constexpr std::size_t kCallbackGlobe     = 0x82F8;
    constexpr std::size_t kCallbackHighlight = 0x8300;

    constexpr std::size_t kGlobeClear  = 0x58;
    constexpr std::size_t kGlobeMarker = 0x48;
    constexpr std::size_t kGlobeFocus  = 0x28;

    constexpr std::size_t kInfoOnline    = 0x00;
    constexpr std::size_t kInfoLatitude  = 0x20;
    constexpr std::size_t kInfoLongitude = 0x24;
    constexpr std::size_t kInfoSeed      = 0x54;
    constexpr std::size_t kInfoAccepted  = 0x56;
    constexpr std::size_t kInfoMissionId = 0xA4;

    constexpr std::size_t kRecordLatitude  = 0x20;
    constexpr std::size_t kRecordLongitude = 0x24;

    constexpr std::uint8_t  kGlobeSlotCount    = 0x48;
    constexpr std::uint8_t  kTeamBound         = 0x0B;
    constexpr std::uint32_t kPlainMarker       = 6;
    constexpr std::uint32_t kOnlineTeamMarker  = 0x107;
    constexpr std::uint32_t kTeamMarkerFlag    = 0x100;
    constexpr std::uint32_t kHighlightFlag     = 0x200;

    RefreshMissionIconInGlobe_t g_OrigRefreshMissionIconInGlobe = nullptr;

    struct Globe
    {
        void*         object = nullptr;
        GlobeClear_t  clear = nullptr;
        GlobeMarker_t marker = nullptr;
        GlobeFocus_t  focus = nullptr;
    };

    bool ResolveGlobe(std::uint8_t* callback, Globe& globe)
    {
        globe.object = deployMission::EnginePointerAt<void>(callback, kCallbackGlobe);
        auto* vtable = deployMission::EnginePointerAt<std::uint8_t>(static_cast<std::uint8_t*>(globe.object), 0);
        void* clear = nullptr;
        void* marker = nullptr;
        void* focus = nullptr;
        if (!vtable
            || !deployMission::ReadEngine(vtable + kGlobeClear, clear)
            || !deployMission::ReadEngine(vtable + kGlobeMarker, marker)
            || !deployMission::ReadEngine(vtable + kGlobeFocus, focus))
            return false;
        globe.clear = reinterpret_cast<GlobeClear_t>(clear);
        globe.marker = reinterpret_cast<GlobeMarker_t>(marker);
        globe.focus = reinterpret_cast<GlobeFocus_t>(focus);
        return globe.clear && globe.marker && globe.focus;
    }

    template <typename T>
    T Field(const std::uint8_t* base, std::size_t offset)
    {
        T value{};
        deployMission::ReadEngine(base + offset, value);
        return value;
    }

    struct SlotMarker
    {
        std::uint16_t id = 0;
        float         latitude = 0.0f;
        float         longitude = 0.0f;
    };

    bool EffectiveSlotMarker(std::uint8_t* deploy, std::uint8_t slot, std::uint16_t placeholderId,
                             SlotMarker& out)
    {
        deployMission::MissionView mission;
        if (slot < deployMission::kSlotCount && deployMission::FindLiveMissionInSlot(deploy, slot, mission))
        {
            out.id = mission.id;
            std::memcpy(&out.latitude, mission.record + kRecordLatitude, sizeof(float));
            std::memcpy(&out.longitude, mission.record + kRecordLongitude, sizeof(float));
            return true;
        }
        const std::uint8_t* record = deployMission::VanillaRecord(deploy, placeholderId);
        if (!record)
            return false;
        out.id = placeholderId;
        out.latitude = Field<float>(record, kRecordLatitude);
        out.longitude = Field<float>(record, kRecordLongitude);
        return true;
    }

    void DrawOnlineSelection(std::uint8_t* callback, const Globe& globe, const std::uint8_t* selected)
    {
        const float latitude = Field<float>(selected, kInfoLatitude);
        const float longitude = Field<float>(selected, kInfoLongitude);
        if (Field<std::uint8_t>(selected, kInfoAccepted) != 0)
        {
            std::uint32_t type = kOnlineTeamMarker;
            if (Field<std::uint16_t>(callback, kCallbackHighlight) == Field<std::uint16_t>(selected, kInfoMissionId))
                type |= kHighlightFlag;
            globe.marker(globe.object, latitude, longitude, type);
        }
        globe.marker(globe.object, latitude, longitude, kPlainMarker);
        globe.focus(globe.object, latitude, longitude);
        deployMission::WriteEngine(callback + kCallbackHighlight, std::uint16_t{0});
    }

    void RefreshWithCustomMissions(std::uint8_t* callback, const Globe& globe, std::uint8_t* deploy)
    {
        globe.clear(globe.object);

        const auto* selected = deployMission::EnginePointerAt<std::uint8_t>(callback, kCallbackSelected);
        std::uint16_t selectedId = 0;
        if (selected && Field<std::uint16_t>(selected, kInfoSeed) != 0)
        {
            if (Field<std::uint8_t>(selected, kInfoOnline) != 0)
            {
                DrawOnlineSelection(callback, globe, selected);
                return;
            }
            selectedId = Field<std::uint16_t>(selected, kInfoMissionId);
        }
        else
        {
            selected = nullptr;
        }

        const std::uint16_t highlight = Field<std::uint16_t>(callback, kCallbackHighlight);
        std::uint32_t* words = deployMission::SlotWords(deploy);
        bool selectedDrawn = false;
        bool selectedDrawnLatest = false;
        for (std::uint8_t slot = 0; slot < kGlobeSlotCount; ++slot)
        {
            std::uint32_t word = 0;
            if (!words || !deployMission::ReadEngine(words + slot, word))
                continue;
            const std::uint16_t placeholderId = deployMission::DecodeSlotId(word);
            if (placeholderId == 0 || !deployMission::IsSlotAccepted(word))
                continue;
            const std::uint8_t team = (word & 0xF) < kTeamBound ? static_cast<std::uint8_t>(word & 0xF) : 0;
            if (team == 0)
                continue;

            SlotMarker marker;
            if (!EffectiveSlotMarker(deploy, slot, placeholderId, marker))
                continue;
            std::uint32_t type = (team + 6u) | kTeamMarkerFlag;
            if (marker.id == highlight)
                type = (team + 6u) | kTeamMarkerFlag | kHighlightFlag;
            globe.marker(globe.object, marker.latitude, marker.longitude, type);

            selectedDrawn = selectedDrawnLatest;
            if (marker.id == selectedId)
            {
                selectedDrawnLatest = true;
                selectedDrawn = true;
            }
        }

        if (selectedId == 0)
            return;
        if (selected)
        {
            const float latitude = Field<float>(selected, kInfoLatitude);
            const float longitude = Field<float>(selected, kInfoLongitude);
            if (!selectedDrawn)
                globe.marker(globe.object, latitude, longitude, kPlainMarker);
            globe.focus(globe.object, latitude, longitude);
        }
        deployMission::WriteEngine(callback + kCallbackHighlight, std::uint16_t{0});
    }

    void __fastcall hkRefreshMissionIconInGlobe(std::uint8_t* callback)
    {
        std::uint8_t* deploy = deployMission::EnginePointerAt<std::uint8_t>(callback, kCallbackDeploy);
        Globe globe;
        if (!deploy || !deployMission::AnyLiveFact(deploy) || !ResolveGlobe(callback, globe))
        {
            g_OrigRefreshMissionIconInGlobe(callback);
            return;
        }
        RefreshWithCustomMissions(callback, globe, deploy);
    }
}

bool Install_CombatDeployCallbackImpl_RefreshMissionIconInGlobe_Hook()
{
    void* target = ResolveGameAddress(gAddr.CombatDeployCallbackImpl_RefreshMissionIconInGlobe);
    const bool installed = CreateAndEnableHook(target,
        reinterpret_cast<void*>(&hkRefreshMissionIconInGlobe),
        reinterpret_cast<void**>(&g_OrigRefreshMissionIconInGlobe));
    if (!installed)
        LogError("Dispatch: RefreshMissionIconInGlobe hook failed, custom dispatch missions off");
    return installed;
}

bool Uninstall_CombatDeployCallbackImpl_RefreshMissionIconInGlobe_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.CombatDeployCallbackImpl_RefreshMissionIconInGlobe));
    g_OrigRefreshMissionIconInGlobe = nullptr;
    return true;
}
