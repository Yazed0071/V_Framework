#pragma once

#include <cstdint>

bool Install_ThrowingImpl_UpdateStatus_Hook();
void Uninstall_ThrowingImpl_UpdateStatus_Hook();

void ThrowingImpl_ForgetStop(std::uint32_t handle);
