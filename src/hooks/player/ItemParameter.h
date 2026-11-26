#pragma once

#include <cstdint>

namespace ItemParameter
{
    enum class Field { Count, Seconds };

    int  GradeCount(std::uint32_t equipId);                                         // 0 = not one of the tunable items
    bool HasField(std::uint32_t equipId, Field field);
    void Set(std::uint32_t equipId, int grade, Field field, float value);           // grade 1 is the first
}

bool Install_SkillAndItemParameterSystemImpl_GetItemParamWithEqpId_Hook();
void Uninstall_SkillAndItemParameterSystemImpl_GetItemParamWithEqpId_Hook();
