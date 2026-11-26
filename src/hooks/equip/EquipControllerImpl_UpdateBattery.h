#pragma once

#include <cstdint>

namespace ItemBattery
{
    constexpr float kRechargeStillSeconds  = 90.0f;
    constexpr float kRechargeMovingSeconds = 30.0f;

    void Set(std::uint16_t equipId, float seconds, float rechargeStillSeconds, float rechargeMovingSeconds, std::uint8_t gaugeLength);
    bool GetCharge(std::uint16_t equipId, float& fraction);
    bool SetCharge(std::uint16_t equipId, float fraction);
    void SetOn(std::uint16_t equipId, int state);   // 1 on, 0 off, -1 unset (drains while selected)
    int  IsOn(std::uint16_t equipId);
    void RefillAll();
    bool IsActiveHere();
    bool GaugeLevel(int equipId, std::uint8_t& level);
}

bool Install_EquipControllerImpl_UpdateBattery_Hook();
void Uninstall_EquipControllerImpl_UpdateBattery_Hook();
