#pragma once
#include <cstdint>

namespace CallComrade
{
    constexpr std::uint64_t kInterrogationSignal = 0xAF8A7DB1B076ull; // the request the player sends a held soldier (real name unknown)
    constexpr std::uint32_t kCallComradeRequest  = 3;

    bool IsCallPending();
    void UpdatePendingCall();
}

bool Install_Soldier2Impl_ProcessSignal_Hook();
void Uninstall_Soldier2Impl_ProcessSignal_Hook();
