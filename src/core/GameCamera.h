#pragma once

namespace GameCamera
{
    constexpr float kSoldierRadius = 1.2f;

    bool  IsOnScreen(const float* position, float radius);
    float DistanceSq(const float* position);
}
