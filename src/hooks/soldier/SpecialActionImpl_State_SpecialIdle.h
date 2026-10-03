#pragma once

#include <cstdint>

namespace HostageStruggleTags
{
    constexpr std::uint64_t kStart = 0xC49C77894758ull; // hash of the state that plays prs0non_hlp_f_st (real name unknown)
    constexpr std::uint64_t kLoop  = 0xCC1EFC1B1808ull; // hash of the state that plays prs0non_hlp_f (real name unknown)
    constexpr std::uint64_t kEnd   = 0xCF5520A61624ull; // hash of the state that plays prs0non_hlp_f_ed (real name unknown)
    constexpr std::uint64_t kMask  = 0xFFFFFFFFFFFFull;
    constexpr std::uint32_t kPlayState = 0x6F1EF16Du; // "PlayState"
}

bool Install_SpecialActionImpl_State_SpecialIdle_Hook();
bool Uninstall_SpecialActionImpl_State_SpecialIdle_Hook();
bool Is_SpecialActionImpl_State_SpecialIdle_Hooked();
