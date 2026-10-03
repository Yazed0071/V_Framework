#pragma once

bool Install_UiControllerImpl_UpdateEquipHud();
void Uninstall_UiControllerImpl_UpdateEquipHud();

void* EquipHud_ResolveInfo(void* uiController);
bool EquipHud_IsItemUsable(int equipId);
