#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>

namespace GameClock
{
    constexpr std::int64_t kLongestStepMs = 100;   // a pause or a hitch counts as this much

    // Milliseconds of play: it only advances between calls, so poll it every frame while a timer runs.
    inline std::uint64_t NowMs()
    {
        static std::atomic<std::int64_t>  s_LastReal{ 0 };
        static std::atomic<std::uint64_t> s_Played{ 0 };

        const std::int64_t real = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const std::int64_t last = s_LastReal.exchange(real, std::memory_order_acq_rel);
        std::int64_t step = last ? real - last : 0;
        if (step < 0)
            step = 0;
        if (step > kLongestStepMs)
            step = kLongestStepMs;
        return s_Played.fetch_add(static_cast<std::uint64_t>(step), std::memory_order_acq_rel) + static_cast<std::uint64_t>(step);
    }
}
