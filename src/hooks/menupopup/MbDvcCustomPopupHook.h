#pragma once

#include <cstdint>


bool Install_MbDvcCustomPopup_Hook();


bool Uninstall_MbDvcCustomPopup_Hook();


// Queue a popup with literal title/body.
bool Show_MbDvcAnnouncePopupReport(const char* title, const char* body);


// Queue popup using LangId label names.
bool Show_MbDvcAnnouncePopupByLangId(const char* titleLabel,
                                     const char* bodyLabel);


// Queue Server popup (slot picked internally).
bool Show_MbDvcAnnouncePopupReward(const char* title, const char* body);


// Queue Server popup using LangId label names.
bool Show_MbDvcAnnouncePopupRewardLangId(const char* titleLabel,
                                         const char* bodyLabel);
