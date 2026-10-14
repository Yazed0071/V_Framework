#pragma once

#include <cstdint>

#include "StaffServerSanitize.h"

namespace mbSecurity
{
    enum class Outcome
    {
        Unchanged,
        Corrected,
        Mismatch,
        Unavailable
    };

    Outcome RemoveCustomStaffFromSecurity(std::uint8_t* packSelf, const staffServer::SectionPoints& points);   // CMD_SYNC_MOTHER_BASE pack this, right after the builder
}
