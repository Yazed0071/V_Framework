#pragma once

#include <cstdint>

#include "ThrowingSystemImpl_ExecuteSerially.h"

bool Install_ThrowingImpl_UpdateAction_Hooks();
void Uninstall_ThrowingImpl_UpdateAction_Hooks();

void ThrowingImpl_ForgetActivation(std::uint32_t handle);
void ThrowingImpl_RunUpdateAction(ThrowingRequestKind kind, void* impl, std::uint32_t localIndex,
                                  std::uint16_t equipId, int option);
