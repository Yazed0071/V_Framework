#pragma once

#include <cstdint>

namespace outfit
{
    bool TryResolveStaffVisualFace(std::uint64_t staffId, std::uint16_t nativeFaceId,
                                   std::uint16_t& visualFaceId);

    bool TryResolveLocalPlayerStaffFace(std::uint16_t nativeFaceId, std::uint16_t& visualFaceId);

    bool IsLocalPlayerFaceIdCustom(std::uint16_t& customFaceId);

    void PollStaffFaceHeal(std::uint32_t missionCode);

    bool EnsureFobFaceGuard();

    void NoteStaffHooksApplied(bool applied);

    bool Install_StaffFaceContainment();

    void Uninstall_StaffFaceContainment();
}
