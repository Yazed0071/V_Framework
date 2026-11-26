#pragma once

bool Install_GetFobStatusCallback_AnnounceDefense_Hooks();
void Uninstall_GetFobStatusCallback_AnnounceDefense_Hooks();
void FobRadio_Pump();   // main thread, outside Lua
