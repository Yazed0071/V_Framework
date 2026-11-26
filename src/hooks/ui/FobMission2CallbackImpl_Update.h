#pragma once

#include <cstdint>

bool Install_FobMission2CallbackImpl_Update_Hook();
void Uninstall_FobMission2CallbackImpl_Update_Hook();
std::uint8_t SelectedFobMenuTab();   // 0xFF outside the FOB menu's update
