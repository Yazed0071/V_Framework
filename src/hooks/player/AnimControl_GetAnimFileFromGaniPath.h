#pragma once

#include <cstdint>

bool Install_AnimControl_GetAnimFileFromGaniPath_Hook();
bool Uninstall_AnimControl_GetAnimFileFromGaniPath_Hook();

void* FrameWorkMotion_GetAnimFile(std::uint64_t clip); // the clip from V_FrameWork_motion.mtar, null when it isn't there or the archive is off

using AnimControlWatch_t = void (*)(void* animControl);
bool AnimControl_GetAnimFileFromGaniPath_Watch(AnimControlWatch_t watch);
