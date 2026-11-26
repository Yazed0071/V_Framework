#pragma once

#include <cstdint>

bool ThrowingImpl_AimedThrowVelocity(void* impl, const std::uint8_t* desc, float gravity, float fallbackSeconds,
                                     float* velocity);   // false = the desc gives its own velocity
