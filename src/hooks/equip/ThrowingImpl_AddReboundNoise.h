#pragma once

#include <cstdint>

bool Install_ThrowingImpl_AddReboundNoise_Hook();
void Uninstall_ThrowingImpl_AddReboundNoise_Hook();

void ThrowingImpl_AddReboundNoiseQuiet(void* impl, std::uint32_t localIndex, const float* position);
