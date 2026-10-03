#pragma once
#include <cstdint>

namespace SoldierNotice
{
    constexpr int           kLastKind       = 0x3F;   // notice types the soldier's notice controller knows go up to 0x3C
    constexpr int           kCalledOver     = 0x33;   // a comrade passed the word: turns and walks over calmly
    constexpr std::uint16_t kNoTarget       = 0xFFFF;

    bool Add(std::uint32_t soldierIndex, int kind, std::uint16_t targetId, const float* position);
    void ClearPending();
}

namespace CallComrade
{
    void DeliverComradeCallRelaxed(std::uint32_t comradeIndex, std::uint16_t callerId);
}

bool Install_NoticeControllerImpl_CheckForceNotice_Hook();
void Uninstall_NoticeControllerImpl_CheckForceNotice_Hook();
