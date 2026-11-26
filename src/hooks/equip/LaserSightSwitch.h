#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct lua_State;

namespace LaserSightSwitch
{
    constexpr std::uint32_t kMenuAction  = 0x465FFD41;   // UI_EQUIP_OP_3
    constexpr std::uint32_t kAimAction   = 0x4EEF444D;   // UI_LASER_SWITCH
    constexpr std::uint32_t kHoldAction  = 0x81E6E224;   // UI_HOLD
    constexpr std::uint16_t kHudHasLaser = 0x80;
    constexpr std::uint16_t kHudLaserOn  = 0x1000;

    void LoadSaved();
    bool AnyOff();
    bool IsOff(std::int32_t equipId);
    void SetOn(std::int32_t equipId, bool on);
    void KeepSaved(std::int32_t equipId);
    std::int32_t EquipIdOfEngineId(std::uint32_t engineEquipId);

    void SetOptionIcon(int optionId, const std::string& ftexPath);
    std::uint64_t BoxPictureForWeapon(std::int32_t equipId);

    void NoteAimKeyToggle();
    bool AimKeyDebounced();
    bool AimKeyToggledJustNow();

    void RefreshHudWords(void* hudInfo, std::int32_t equipId, bool on);

    void EnsureKeysRegistered(lua_State* L);

    void* CheckedEntry(std::uintptr_t address, const std::uint8_t* prologue, std::size_t size, const char* function);
}
