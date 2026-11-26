#pragma once

#include <cstdint>

namespace GameObjectPosition
{
    bool Read(std::uint32_t gameObjectId, float position[3]);
    bool ReadTransform(std::uint32_t gameObjectId, float position[3], float rotation[4]); // rotation is a quaternion x, y, z, w
}
