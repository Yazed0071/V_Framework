#pragma once
#include <cstdint>

namespace SoldierNotice
{
    constexpr int           kCalledOver     = 0x33;   // a comrade passed the word: turns and walks over calmly
    constexpr int           kCalledForHelp  = 0x34;   // what vanilla's call for help gives: comes over weapon ready
    constexpr std::uint16_t kNoTarget       = 0xFFFF;

    const char* Problem(int kind, std::uint16_t targetId, bool hasPosition);   // nullptr when the soldier can act on it
    bool Add(std::uint32_t soldierIndex, int kind, std::uint16_t targetId, const float* position);
    bool AddToAll(int kind, std::uint16_t targetId, const float* position, float range);   // range 0: the soldiers' own hearing or sight
    void ClearPending();
}

namespace CallComrade
{
    void DeliverComradeCallRelaxed(std::uint32_t comradeIndex, std::uint16_t callerId);
    void DeliverComradeCallOnGuard(std::uint32_t comradeIndex, std::uint16_t callerId);
}

bool Install_NoticeControllerImpl_CheckForceNotice_Hook();
void Uninstall_NoticeControllerImpl_CheckForceNotice_Hook();
