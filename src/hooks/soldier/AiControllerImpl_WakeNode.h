#pragma once

bool Install_AiControllerImpl_WakeNode_Hooks();
void Uninstall_AiControllerImpl_WakeNode_Hooks();
bool AiControllerImpl_WakeNode_IsWatching();   // false on builds without the AI node addresses
