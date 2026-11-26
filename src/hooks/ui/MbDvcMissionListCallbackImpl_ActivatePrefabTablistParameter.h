#pragma once

bool Add_MissionListLocationTab(int locationId, const char* nameLangId);
void Set_MissionListLocationTabVisible(int locationId, bool visible);

bool Install_MbDvcMissionListCallbackImpl_ActivatePrefabTablistParameter_Hook();
bool Uninstall_MbDvcMissionListCallbackImpl_ActivatePrefabTablistParameter_Hook();
