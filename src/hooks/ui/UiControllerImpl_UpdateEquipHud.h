#pragma once

bool Install_UiControllerImpl_UpdateEquipHud();
void Uninstall_UiControllerImpl_UpdateEquipHud();

void* EquipHud_ResolveInfo(void* uiController);
void* EquipHud_CachedInfo();
bool EquipHud_IsItemUsable(int equipId);
