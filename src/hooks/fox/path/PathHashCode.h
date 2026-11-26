#pragma once

#include <cstdint>
#include <string>

bool Install_PathHashCode_Hook();
void Uninstall_PathHashCode_Hook();
std::string PackPathFor(std::uint64_t pathCode);   // empty when the game never hashed that pack's path while we watched
