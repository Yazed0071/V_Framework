#pragma once

#include <cstdint>

#include "MotionLoaderImpl_GetMtarIds.h"

constexpr int kPartMotionRowReceiver    = 234;
constexpr int kPartMotionRowUnderBarrel = 235;

bool PartMotionRows_Install();
void PartMotionRows_Uninstall();
bool PartMotionRows_Active();

bool PartMotionRows_SetEquipRow(unsigned int equipId, int control,
                                const std::uint16_t motion[4],
                                const std::uint16_t pose[4],
                                const std::uint8_t flags[2]);
void PartMotionRows_ClearEquipRow(unsigned int equipId, int control);
bool PartMotionRows_AppendNativeRows(const ReceiverPartMotion& motion,
                                     ReceiverPartRowIndices& out);
