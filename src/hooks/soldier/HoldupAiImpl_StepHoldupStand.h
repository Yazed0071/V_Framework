#pragma once

#include <cstdint>

bool Install_HoldupAiImpl_StepHoldupStand_Hooks();
bool Uninstall_HoldupAiImpl_StepHoldupStand_Hooks();
bool HoldupSelfRelease_TakeFreed(std::uint32_t soldier); // true once for a soldier whose hold-up countdown ran out
void HoldupSelfRelease_Clear();
void HoldupSelfRelease_ClearForReload();   // the countdowns, keeps SetHoldupSelfRelease
void HoldupSelfRelease_SetEnabled(bool enabled);   // every soldier; on by default
void HoldupSelfRelease_SetEnabledFor(std::uint32_t soldier, bool enabled);   // beats the global one for him until the mission ends
void HoldupSelfRelease_OnNoticeTick(std::uint8_t* noticeController, std::uint32_t soldier);   // fallback when his weapon node never runs
void HoldupSelfRelease_OnNodeSleep(std::uint32_t agent, void* node);   // posts his notice as his weapon node lets go of him
bool HoldupSelfRelease_TakeRadioCall(std::uint32_t soldier);   // true once: his next radio call is the one about being held up
