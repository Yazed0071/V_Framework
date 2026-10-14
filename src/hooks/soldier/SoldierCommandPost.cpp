#include "pch.h"
#include "SoldierCommandPost.h"

#include <cstddef>
#include <cstdint>

namespace
{
    constexpr std::size_t   kSoldierCpController     = 0xD0;
    constexpr std::size_t   kCpControllerCommandPost = 0x08;
    constexpr std::size_t   kCpControllerClient      = 0x30;   // which of the CP system's clients these soldiers are, not a CP
    constexpr std::size_t   kCommandPostClients      = 0x1F0;
    constexpr std::size_t   kCpClientSize            = 0x48;
    constexpr std::size_t   kCpClientFirstMember     = 0x40;
    constexpr std::size_t   kCommandPostMembers      = 0x58;
    constexpr std::size_t   kCpMemberSize            = 0x18;
    constexpr std::size_t   kCpMemberGameObjectId    = 0x10;
    constexpr std::size_t   kCpMemberCp              = 0x12;
    constexpr std::uint16_t kSoldierIdBase           = 0x400;

    template <typename T>
    T At(const std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<const T*>(base + offset);
    }
}

std::uint8_t SoldierCommandPost::IndexOf(const std::uint8_t* soldiers, std::uint32_t soldierIndex)   // as the CP system's GetCommandPostIndex reads it
{
    const std::uint8_t* controller = soldiers ? At<const std::uint8_t*>(soldiers, kSoldierCpController) : nullptr;
    const std::uint8_t* commandPost = controller ? At<const std::uint8_t*>(controller, kCpControllerCommandPost) : nullptr;
    if (!commandPost)
        return kNone;
    const std::uint8_t* clients = At<const std::uint8_t*>(commandPost, kCommandPostClients);
    const std::uint8_t* members = At<const std::uint8_t*>(commandPost, kCommandPostMembers);
    if (!clients || !members)
        return kNone;

    const std::uint32_t client = At<std::uint32_t>(controller, kCpControllerClient);
    const std::uint32_t first = At<std::uint16_t>(clients + client * kCpClientSize, kCpClientFirstMember);
    const std::uint8_t* member = members + (first + soldierIndex) * kCpMemberSize;
    if (At<std::uint16_t>(member, kCpMemberGameObjectId) != (kSoldierIdBase | soldierIndex))
        return kNone;
    return At<std::uint8_t>(member, kCpMemberCp);
}
