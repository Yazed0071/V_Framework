#pragma once

#include <cstdint>

namespace SupportWeapon
{
    constexpr std::uint32_t kEquipIdCount = 0x10000;   // real ids, wide included
    constexpr int kNoBlastId = -1;
    constexpr std::uint32_t kNoticeNothing = 0xFFFF;   // noticeAs = EQP_None
    constexpr float kGameGravity = 9.8f;

    struct Physics
    {
        int   bounce          = -1;     // engine byte = bounce x 250, -1 keeps the item's own
        int   friction        = -1;     // engine byte = friction x 250
        int   rollingFriction = -1;     // engine u16 = rolling friction x 100
        float radius          = 0.0f;
        float throwSpeed      = 1.0f;
    };

    bool IsVanillaSupportWeaponEquipId(std::uint32_t equipId);
    bool IsPlaceAsDonor(std::uint32_t equipId);   // vanilla placed items a custom one may borrow: C4, mines, Dung, decoys
    bool IsDecoyDonor(std::uint32_t equipId);

    void Set(std::uint32_t equipId, std::uint32_t throwAs, int attackId, int blastId);

    std::uint32_t FamilyFrom(std::uint32_t equipId);
    int  AttackId(std::uint32_t equipId);
    int  BlastId(std::uint32_t equipId);
    bool NativeAction(std::uint32_t equipId);

    std::uint32_t RunningItem(std::uint32_t equipId);
    void SetRunningItem(std::uint32_t equipId);

    void NoteDevelopRow(std::uint32_t equipId, std::uint32_t developId, std::uint32_t baseDevelopId);
    std::uint32_t PickUpLine(std::uint32_t equipId);

    void SetNoticeAs(std::uint32_t equipId, std::uint32_t noticeAs);
    std::uint32_t NoticeAs(std::uint32_t equipId);
    void SetNoticeScope(std::uint32_t noticeAs);
    std::uint32_t NoticeScope();

    void SetPhysics(std::uint32_t equipId, const Physics& physics);
    bool GetPhysics(std::uint32_t equipId, Physics& out);

    void SetDecoyType(std::uint32_t equipId, std::uint32_t decoyType);
    std::uint32_t DecoyType(std::uint32_t equipId);

    void SetGravity(std::uint32_t equipId, float gravity);   // below 0 = the game's own
    float Gravity(std::uint32_t equipId);
}
