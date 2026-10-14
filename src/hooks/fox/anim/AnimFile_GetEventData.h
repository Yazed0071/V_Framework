#pragma once

#include <cstdint>
#include <string>

struct ClipEventLabel
{
    std::string clip;
    std::string event;
    std::uint32_t eventHash = 0;
};

bool Install_AnimFile_GetEventData_Hook();
bool Uninstall_AnimFile_GetEventData_Hook();

bool AddClipEvent(std::uint64_t clip, int frame, const ClipEventLabel& label);
bool FindAddedClipEvent(const void* group, int index, ClipEventLabel& label);
