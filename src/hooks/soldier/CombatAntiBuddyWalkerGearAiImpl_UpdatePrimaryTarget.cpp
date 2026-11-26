#include <pch.h>

#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SoldierFaction.h"
#include "SoldierRealizedSlot.h"
#include "log.h"

namespace
{
    using UpdatePrimaryTarget_t = bool(__fastcall*)(void* self, std::uint32_t soldierIndex, std::uint8_t* knowledge);
    using CharaControl_t        = std::uint8_t*(__fastcall*)(void* animationController, std::uint32_t slot);
    using SetTurnTarget_t       = void(__fastcall*)(std::uint8_t* charaControl, const float* turnTarget);

    constexpr std::size_t   kNodeSoldierCore      = 0x1A0;
    constexpr std::size_t   kKnowledgeTargetPoint = 0x10;
    constexpr std::size_t   kKnowledgeStepTime    = 0x30;   // the step dispatcher zeroes it on every step change
    constexpr std::size_t   kKnowledgeTargetId    = 0x50;
    constexpr std::size_t   kKnowledgeStep        = 0x54;
    constexpr std::size_t   kKnowledgePrevTarget  = 0x60;
    constexpr std::size_t   kKnowledgeMount       = 0x98;
    constexpr std::size_t   kTargetPointSize      = 0x10;
    constexpr std::uint32_t kSoldierSlotCount     = 0x200u;
    constexpr std::uint8_t  kStepStarting         = 10u;

    constexpr std::size_t   kRealizedBlocks       = 0x540;   // RealizedSoldier2Impl blocks, as RealizedSoldier2Impl::ExecuteSerially walks them
    constexpr std::size_t   kRealizedBlockCount   = 0x71C;
    constexpr std::size_t   kBlockSlotRecords     = 0x30;
    constexpr std::size_t   kBlockControllers     = 0x88;
    constexpr std::size_t   kBlockFirstSlot       = 0xAC;
    constexpr std::size_t   kBlockSlotCount       = 0xB0;
    constexpr std::size_t   kSlotRecordStride     = 0x18;
    constexpr std::size_t   kSlotRecordController = 0x14;
    constexpr std::uint16_t kControllersPerBlock  = 2u;
    constexpr std::size_t   kCharaControlSlot     = 0x388 / sizeof(void*);   // AnimationController -> CharaControl of a realized slot
    constexpr std::size_t   kSetTurnTargetSlot    = 0x70 / sizeof(void*);    // copies the quaternion to +0x80 and raises turn flag 0x100
    constexpr std::size_t   kCharaPosition        = 0x20;
    constexpr std::size_t   kCharaRotation        = 0x60;
    constexpr float         kSameSoldierSq        = 2.0f * 2.0f;

    constexpr float         kFacedRad             = 0.26f;   // 15 degrees: the attack aims and fires from here
    constexpr float         kHeldStepTime         = -0.05f;
    constexpr std::uint64_t kTurnHoldMaxMs        = 1500u;
    constexpr float         kTwoPi                = 6.28318531f;

    UpdatePrimaryTarget_t g_OrigUpdatePrimaryTarget = nullptr;
    std::array<std::uint8_t, kSoldierSlotCount>  g_turnStep{};
    std::array<std::uint64_t, kSoldierSlotCount> g_turnSinceMs{};

    void RetargetToRival(void* self, std::uint8_t* knowledge, std::uint16_t rivalId)
    {
        const void* soldierCore = nullptr;
        std::memcpy(&soldierCore, static_cast<const std::uint8_t*>(self) + kNodeSoldierCore, sizeof(soldierCore));

        std::memcpy(knowledge + kKnowledgePrevTarget, knowledge + kKnowledgeTargetPoint, kTargetPointSize);
        std::memcpy(knowledge + kKnowledgeTargetId, &rivalId, sizeof(rivalId));

        float aimPoint[3];
        if (SoldierFaction_ReadRivalAimPoint(soldierCore, rivalId, aimPoint))
            std::memcpy(knowledge + kKnowledgeTargetPoint, aimPoint, sizeof(aimPoint));
    }

    std::uint8_t* CharaControlSEH(const std::uint8_t* soldierCore, std::uint32_t slot)
    {
        __try
        {
            const std::uint8_t* const* blocks = *reinterpret_cast<const std::uint8_t* const* const*>(soldierCore + kRealizedBlocks);
            const std::uint32_t blockCount = *reinterpret_cast<const std::uint32_t*>(soldierCore + kRealizedBlockCount);
            for (std::uint32_t b = 0; blocks && b < blockCount; ++b)
            {
                const std::uint8_t* block = blocks[b];
                if (!block)
                    continue;
                const std::uint32_t first = *reinterpret_cast<const std::uint32_t*>(block + kBlockFirstSlot);
                const std::uint32_t count = *reinterpret_cast<const std::uint32_t*>(block + kBlockSlotCount);
                if (slot < first || slot - first >= count)
                    continue;
                const std::uint8_t* records = *reinterpret_cast<const std::uint8_t* const*>(block + kBlockSlotRecords);
                const std::uint16_t controllerIndex = *reinterpret_cast<const std::uint16_t*>(
                    records + (slot - first) * kSlotRecordStride + kSlotRecordController);
                if (controllerIndex >= kControllersPerBlock)
                    return nullptr;
                void* controller = *reinterpret_cast<void* const*>(block + kBlockControllers + controllerIndex * sizeof(void*));
                if (!controller)
                    return nullptr;
                const auto charaControl = reinterpret_cast<CharaControl_t>((*reinterpret_cast<void* const* const*>(controller))[kCharaControlSlot]);
                return charaControl(controller, slot);
            }
            return nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool ReadFacingSEH(const std::uint8_t* chara, float* position, float* rotation)
    {
        __try
        {
            std::memcpy(position, chara + kCharaPosition, sizeof(float) * 3);
            std::memcpy(rotation, chara + kCharaRotation, sizeof(float) * 4);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SetTurnTargetSEH(std::uint8_t* chara, const float* turnTarget)
    {
        __try
        {
            const auto setTurnTarget = reinterpret_cast<SetTurnTarget_t>((*reinterpret_cast<void* const* const*>(chara))[kSetTurnTargetSlot]);
            setTurnTarget(chara, turnTarget);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void FaceRival(void* self, std::uint32_t soldierIndex, std::uint8_t* knowledge, std::uint16_t rivalId)
    {
        if (soldierIndex >= kSoldierSlotCount)
            return;
        const std::uint8_t step = knowledge[kKnowledgeStep];
        const std::uint64_t now = GetTickCount64();
        if (g_turnStep[soldierIndex] != step)
        {
            g_turnStep[soldierIndex]   = step;
            g_turnSinceMs[soldierIndex] = now;
        }
        if (step != kStepStarting)   // the opening attack: later steps move him, and their body turning is left alone
            return;

        const std::uint8_t* soldierCore = nullptr;
        std::memcpy(&soldierCore, static_cast<const std::uint8_t*>(self) + kNodeSoldierCore, sizeof(soldierCore));
        const std::uint32_t slot = SoldierRealizedSlot::Of(soldierCore, soldierIndex);
        if (slot == SoldierRealizedSlot::kNotRealized)
            return;
        std::uint8_t* chara = CharaControlSEH(soldierCore, slot);
        float listed[3];
        float rival[3];
        float body[3];
        alignas(16) float q[4];
        if (!chara || !ReadFacingSEH(chara, body, q)
            || !SoldierFaction_ReadRivalAimPoint(soldierCore, static_cast<std::uint16_t>(0x400u | soldierIndex), listed)
            || !SoldierFaction_ReadRivalAimPoint(soldierCore, rivalId, rival))
            return;
        const float ox = body[0] - listed[0];
        const float oz = body[2] - listed[2];
        if (ox * ox + oz * oz > kSameSoldierSq)   // not his body: leave him to vanilla
            return;

        const float dx = rival[0] - body[0];
        const float dz = rival[2] - body[2];
        if (dx == 0.0f && dz == 0.0f)
            return;
        const float facing = std::atan2(2.0f * (q[0] * q[2] + q[3] * q[1]), 1.0f - 2.0f * (q[0] * q[0] + q[1] * q[1]));
        const float turn = std::remainder(std::atan2(dx, dz) - facing, kTwoPi);   // yaw from +Z toward +X, as CalcAngleYToTarget
        const std::uint64_t heldMs = now - g_turnSinceMs[soldierIndex];
        if (std::fabs(turn) <= kFacedRad)
            return;

        const float s = std::sin(turn * 0.5f);
        const float c = std::cos(turn * 0.5f);
        alignas(16) const float target[4] = { c * q[0] + s * q[2], c * q[1] + s * q[3], c * q[2] - s * q[0], c * q[3] - s * q[1] };   // turn about Y, then his facing
        SetTurnTargetSEH(chara, target);

        float stepTime = 0.0f;
        std::memcpy(&stepTime, knowledge + kKnowledgeStepTime, sizeof(stepTime));
        if (heldMs < kTurnHoldMaxMs && stepTime > kHeldStepTime)   // his attack step would time out into the slow approach before he faces him
            std::memcpy(knowledge + kKnowledgeStepTime, &kHeldStepTime, sizeof(kHeldStepTime));
    }

    bool __fastcall hk_UpdatePrimaryTarget(void* self, std::uint32_t soldierIndex, std::uint8_t* knowledge)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigUpdatePrimaryTarget, self, soldierIndex, knowledge);

        const std::uint16_t rivalId = SoldierFaction_GetRivalTarget(soldierIndex);
        if (rivalId == 0 || !knowledge)
            return g_OrigUpdatePrimaryTarget(self, soldierIndex, knowledge);

        SoldierFaction_MarkRivalNodeAlive(soldierIndex);
        std::uint16_t mountId = 0;
        std::memcpy(&mountId, knowledge + kKnowledgeMount, sizeof(mountId));
        SoldierFaction_SetMount(soldierIndex, mountId);
        RetargetToRival(self, knowledge, rivalId);
        FaceRival(self, soldierIndex, knowledge, rivalId);
        return true;
    }
}

bool Install_CombatAntiBuddyWalkerGearAiImpl_UpdatePrimaryTarget_Hook()
{
    void* target = ResolveGameAddress(gAddr.CombatAntiBuddyWalkerGearAiImpl_UpdatePrimaryTarget);
    if (!target)
        return false;
    return CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_UpdatePrimaryTarget),
                               reinterpret_cast<void**>(&g_OrigUpdatePrimaryTarget));
}

bool Uninstall_CombatAntiBuddyWalkerGearAiImpl_UpdatePrimaryTarget_Hook()
{
    void* target = ResolveGameAddress(gAddr.CombatAntiBuddyWalkerGearAiImpl_UpdatePrimaryTarget);
    if (target)
        DisableAndRemoveHook(target);
    g_OrigUpdatePrimaryTarget = nullptr;
    return true;
}
