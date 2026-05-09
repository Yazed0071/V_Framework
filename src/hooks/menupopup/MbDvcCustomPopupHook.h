#pragma once

#include <cstdint>


bool Install_MbDvcCustomPopup_Hook();


bool Uninstall_MbDvcCustomPopup_Hook();


// Queue a popup with literal title/body.
bool Show_MbDvcAnnouncePopup(const char* title, const char* body);


// Queue popup using LangId label names.
bool Show_MbDvcAnnouncePopupByLangId(const char* titleLabel,
                                     const char* bodyLabel);
