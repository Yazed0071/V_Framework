#pragma once

bool Install_AnimControl_GetAnimFileFromGaniPath_Hook();
bool Uninstall_AnimControl_GetAnimFileFromGaniPath_Hook();

using AnimControlWatch_t = void (*)(void* animControl);
bool AnimControl_GetAnimFileFromGaniPath_Watch(AnimControlWatch_t watch);   // every anim control asked for a clip; nullptr stops it
