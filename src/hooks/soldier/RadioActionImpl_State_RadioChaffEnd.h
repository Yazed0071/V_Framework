#pragma once

bool RadioChaffEnd_IsShoutingInstead();   // true inside the voice call vanilla makes when chaff jams a soldier's radio
void RadioChaffEnd_NoteShout();

bool Install_RadioActionImpl_State_RadioChaffEnd_Hook();
bool Uninstall_RadioActionImpl_State_RadioChaffEnd_Hook();
