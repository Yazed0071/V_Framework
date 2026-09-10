#pragma once

#include <cstdint>

bool SetMissionPreparationMusic(std::uint32_t playEventHash,
                                std::uint32_t stopEventHash,
                                std::uint32_t missionCode = 0);

bool Install_MissionPreparationCallbackImpl_Start_Hook();

bool Uninstall_MissionPreparationCallbackImpl_Start_Hook();
