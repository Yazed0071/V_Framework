#pragma once

#include <atomic>
#include <cstdint>

bool Install_PhaseSneakAiImpl_PreUpdate_Hook();
bool Uninstall_PhaseSneakAiImpl_PreUpdate_Hook();

void Set_SahelanForcePhase(std::int32_t phase);
std::int32_t Get_SahelanCurrentPhase();

namespace SahelanPhaseForce
{
    inline std::atomic<std::int32_t> g_ForcedPhase  { -1 };
    inline std::atomic<std::int32_t> g_CurrentPhase { -1 };
}
