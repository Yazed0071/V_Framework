#include <pch.h>
#include "SupportWeaponRegistry.h"

#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <shared_mutex>

#include "EquipAliasLease.h"
#include "MissionCodeGuard.h"
#include "V_FrameWorkState.h"

namespace
{
    std::array<std::atomic<std::uint16_t>, SupportWeapon::kEquipIdCount> g_FamilyFrom{};
    std::array<std::atomic<std::uint16_t>, SupportWeapon::kEquipIdCount> g_AttackId{};
    std::array<std::atomic<std::int16_t>,  SupportWeapon::kEquipIdCount> g_BlastIdPlusOne{};
    std::array<std::atomic<std::uint16_t>, SupportWeapon::kEquipIdCount> g_Line{};             // first grade of the develop chain
    std::array<std::atomic<std::uint16_t>, 0x10000>                       g_EquipOfDevelopId{};
    std::array<std::atomic<std::uint16_t>, SupportWeapon::kEquipIdCount> g_NoticeAs{};
    std::array<std::atomic<std::uint8_t>,  SupportWeapon::kEquipIdCount> g_DecoyType{};
    std::array<std::atomic<float>,         SupportWeapon::kEquipIdCount> g_GravityPlusOne{};   // 0 = the game's own

    std::shared_mutex                               g_PhysicsLock;
    std::map<std::uint32_t, SupportWeapon::Physics> g_Physics;

    thread_local std::uint32_t t_RunningItem = 0;
    thread_local std::uint32_t t_NoticeScope = 0;

    std::uint32_t Real(std::uint32_t equipId)
    {
        return static_cast<std::uint32_t>(equip::FromEngine(static_cast<std::int32_t>(equipId)));
    }

    bool IsLive(std::uint32_t equipId)
    {
        if (equipId >= SupportWeapon::kEquipIdCount)
            return false;
        if (g_FamilyFrom[equipId].load(std::memory_order_relaxed) == 0)
            return false;
        return !MissionCodeGuard::ShouldBypassHooks();
    }
}

namespace SupportWeapon
{
    bool IsVanillaSupportWeaponEquipId(std::uint32_t equipId)
    {
        if (equipId >= 0x600 && equipId <= 0x608)
            return true;
        if (equipId < 0x15D || equipId > 0x1AD)
            return false;
        return equipId != 0x1A8 && equipId != 0x1AC;
    }

    bool IsDecoyDonor(std::uint32_t equipId)
    {
        const bool decoy       = equipId >= 0x18D && equipId <= 0x18F;
        const bool activeDecoy = equipId >= 0x191 && equipId <= 0x193;
        const bool shockDecoy  = (equipId >= 0x195 && equipId <= 0x197) || equipId == 0x17C || equipId == 0x17D;
        return decoy || activeDecoy || shockDecoy;
    }

    bool IsPlaceAsDonor(std::uint32_t equipId)
    {
        const bool c4              = equipId >= 0x187 && equipId <= 0x18B;
        const bool directionalMine = equipId >= 0x19D && equipId <= 0x1A0;
        const bool sleepingGasMine = equipId >= 0x1A1 && equipId <= 0x1A3;
        const bool antitankMine    = equipId >= 0x1A5 && equipId <= 0x1A7;
        const bool netMine         = equipId >= 0x1A9 && equipId <= 0x1AB;
        const bool dung            = equipId == 0x1AD;
        return c4 || directionalMine || sleepingGasMine || antitankMine || netMine || dung || IsDecoyDonor(equipId);
    }

    void Set(std::uint32_t equipId, std::uint32_t throwAs, int attackId, int blastId)
    {
        equipId = Real(equipId);
        if (equipId >= kEquipIdCount)
            return;
        g_AttackId[equipId].store(static_cast<std::uint16_t>(attackId > 0 ? attackId : 0), std::memory_order_relaxed);
        g_BlastIdPlusOne[equipId].store(static_cast<std::int16_t>(blastId >= 0 ? blastId + 1 : 0), std::memory_order_relaxed);
        g_FamilyFrom[equipId].store(static_cast<std::uint16_t>(throwAs), std::memory_order_release);
    }

    std::uint32_t FamilyFrom(std::uint32_t equipId)
    {
        equipId = Real(equipId);
        if (!IsLive(equipId))
            return 0;
        return g_FamilyFrom[equipId].load(std::memory_order_acquire);
    }

    int AttackId(std::uint32_t equipId)
    {
        equipId = Real(equipId);
        if (!IsLive(equipId))
            return 0;
        return g_AttackId[equipId].load(std::memory_order_relaxed);
    }

    int BlastId(std::uint32_t equipId)
    {
        equipId = Real(equipId);
        if (!IsLive(equipId))
            return kNoBlastId;
        return g_BlastIdPlusOne[equipId].load(std::memory_order_relaxed) - 1;
    }

    bool NativeAction(std::uint32_t equipId)
    {
        equipId = Real(equipId);
        if (!IsLive(equipId))
            return true;
        return IsDecoyDonor(g_FamilyFrom[equipId].load(std::memory_order_acquire));
    }

    std::uint32_t RunningItem(std::uint32_t equipId)
    {
        const std::uint32_t running = t_RunningItem;
        if (running != 0 && FamilyFrom(running) == equipId)
            return running;
        return equipId;
    }

    void SetRunningItem(std::uint32_t equipId)
    {
        t_RunningItem = equipId;
    }

    void NoteDevelopRow(std::uint32_t equipId, std::uint32_t developId, std::uint32_t baseDevelopId)
    {
        equipId = Real(equipId);
        if (equipId == 0 || equipId >= kEquipIdCount || developId == 0 || developId >= g_EquipOfDevelopId.size())
            return;
        g_EquipOfDevelopId[developId].store(static_cast<std::uint16_t>(equipId), std::memory_order_relaxed);
        std::uint32_t line = equipId;
        if (baseDevelopId != 0 && baseDevelopId < g_EquipOfDevelopId.size())
        {
            const std::uint16_t baseEquipId = g_EquipOfDevelopId[baseDevelopId].load(std::memory_order_relaxed);
            if (baseEquipId != 0 && baseEquipId != equipId)
            {
                const std::uint16_t baseLine = g_Line[baseEquipId].load(std::memory_order_relaxed);
                line = baseLine != 0 ? baseLine : baseEquipId;
            }
        }
        g_Line[equipId].store(static_cast<std::uint16_t>(line), std::memory_order_relaxed);
    }

    std::uint32_t PickUpLine(std::uint32_t equipId)
    {
        equipId = Real(equipId);
        if (equipId == 0 || equipId >= kEquipIdCount || IsVanillaSupportWeaponEquipId(equipId)
            || !V_FrameWorkState::IsClaimedEquipId(static_cast<std::int32_t>(equipId)))
            return 0;
        const std::uint16_t line = g_Line[equipId].load(std::memory_order_relaxed);
        return line != 0 ? line : equipId;
    }

    void SetNoticeAs(std::uint32_t equipId, std::uint32_t noticeAs)
    {
        equipId = Real(equipId);
        if (equipId >= kEquipIdCount)
            return;
        g_NoticeAs[equipId].store(static_cast<std::uint16_t>(noticeAs), std::memory_order_relaxed);
    }

    std::uint32_t NoticeAs(std::uint32_t equipId)
    {
        equipId = Real(equipId);
        if (!IsLive(equipId))
            return 0;
        return g_NoticeAs[equipId].load(std::memory_order_relaxed);
    }

    void SetNoticeScope(std::uint32_t noticeAs)
    {
        t_NoticeScope = noticeAs;
    }

    std::uint32_t NoticeScope()
    {
        return t_NoticeScope;
    }

    void SetPhysics(std::uint32_t equipId, const Physics& physics)
    {
        equipId = Real(equipId);
        std::unique_lock lock(g_PhysicsLock);
        g_Physics[equipId] = physics;
    }

    bool GetPhysics(std::uint32_t equipId, Physics& out)
    {
        equipId = Real(equipId);
        if (!IsLive(equipId))
            return false;
        std::shared_lock lock(g_PhysicsLock);
        const auto found = g_Physics.find(equipId);
        if (found == g_Physics.end())
            return false;
        out = found->second;
        return true;
    }

    void SetDecoyType(std::uint32_t equipId, std::uint32_t decoyType)
    {
        equipId = Real(equipId);
        if (equipId >= kEquipIdCount)
            return;
        g_DecoyType[equipId].store(static_cast<std::uint8_t>(decoyType), std::memory_order_relaxed);
    }

    std::uint32_t DecoyType(std::uint32_t equipId)
    {
        equipId = Real(equipId);
        if (!IsLive(equipId))
            return 0;
        return g_DecoyType[equipId].load(std::memory_order_relaxed);
    }

    void SetGravity(std::uint32_t equipId, float gravity)
    {
        equipId = Real(equipId);
        if (equipId >= kEquipIdCount)
            return;
        g_GravityPlusOne[equipId].store(gravity >= 0.0f ? gravity + 1.0f : 0.0f, std::memory_order_relaxed);
    }

    float Gravity(std::uint32_t equipId)
    {
        equipId = Real(equipId);
        if (!IsLive(equipId))
            return kGameGravity;
        const float stored = g_GravityPlusOne[equipId].load(std::memory_order_relaxed);
        return stored > 0.0f ? stored - 1.0f : kGameGravity;
    }
}
