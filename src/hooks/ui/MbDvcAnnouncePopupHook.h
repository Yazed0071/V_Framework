#pragma once

#include <cstdint>


bool Install_MbDvcAnnouncePopupHook();


bool Uninstall_MbDvcAnnouncePopupHook();


namespace MbDvcAnnouncePopup
{
    bool ShowPopup(const char* text, std::uint8_t reserveId, const char* langKey);

    std::uint64_t GetActiveTargetHash();
}
