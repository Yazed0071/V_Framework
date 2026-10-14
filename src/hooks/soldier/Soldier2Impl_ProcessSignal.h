#pragma once
#include <cstdint>

namespace CallComrade
{
    constexpr std::uint64_t kInterrogationSignal     = 0xAF8A7DB1B076ull; // the request the player sends a held soldier (real name unknown)
    constexpr std::uint32_t kCallComradeRequest      = 3;   // these requests are ours, vanilla ignores them
    constexpr std::uint32_t kWillComplyRequest       = 6;   // he only notes whether he would do what Snake says
    constexpr std::uint32_t kSayNotUnderstoodRequest = 7;
    constexpr std::uint32_t kSayRefusalRequest       = 8;

    enum class HeldAnswer : std::uint8_t
    {
        Complies,
        NotUnderstood,   // Snake has no interpreter for his language
        Refuses,         // his surrender is fake
    };

    bool       IsCallPending();
    void       UpdatePendingCall();
    HeldAnswer TakeAnswer();   // his answer to the last kWillComplyRequest; Complies when he never got it
    void       ClearForNewMission();
    bool       IsDeafToNoise(void* noticeController, std::uint32_t soldierIndex, const void* noise);   // shouts the answering comrade or held-up soldiers ignore
}

bool Install_Soldier2Impl_ProcessSignal_Hook();
void Uninstall_Soldier2Impl_ProcessSignal_Hook();
