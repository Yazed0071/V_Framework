#pragma once

#include <string>
#include <vector>

namespace V_FrameWorkModLoader
{
    // Scans mod/V_FrameWork/*.lua and returns file paths.
    std::vector<std::string> FindModFiles();
}
