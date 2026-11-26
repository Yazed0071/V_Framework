#pragma once

#include <cstdint>

void NoticeDownComradeAiImpl_StepPreRadio_OnNodeWake(void* node, std::uint32_t agent);   // the first downed-comrade node to wake gives us its step table
void NoticeDownComradeAiImpl_StepPreRadio_OnNodeSleep(void* node, std::uint32_t agent);
bool NoticeDownComradeAiImpl_FoundSpot(std::uint32_t soldierIndex, float out[3]);   // only inside his StepPreRadio: where the body lies
std::uint16_t NoticeDownComradeAiImpl_FoundComrade(std::uint32_t soldierIndex);   // only inside his StepPreRadio: the downed comrade, 0xFFFF if unknown
void Uninstall_NoticeDownComradeAiImpl_StepPreRadio();
