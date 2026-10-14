#include "pch.h"
#include "NoticeControllerImpl_CheckForceNotice.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"

namespace
{
    using CheckForceNotice_t = void(__fastcall*)(std::uint8_t* noticeController, std::uint32_t soldierIndex);
    using AddNoticeInfo_t    = bool(__fastcall*)(void* noticeInterface, std::uint32_t soldierIndex, const void* noticeInfo);
    using ResetForceNotice_t = void(__fastcall*)(void* noticeInterface, std::uint32_t soldierIndex);

    constexpr std::size_t   kNoticeInterface           = 0x20;      // the interface soldiers hold sits 0x20 into the controller
    constexpr std::size_t   kAddNoticeInfoVtblIndex    = 0;
    constexpr std::size_t   kResetForceNoticeVtblIndex = 0x110 / 8;

    constexpr std::uint8_t  kSourceIsGameObject        = 1;
    constexpr std::uint8_t  kSourceIsPosition          = 2;
    constexpr std::uint32_t kMaxSoldiers               = 1024;

    struct alignas(16) NoticeInfo
    {
        std::uint8_t   type;
        std::uint8_t   sourceKind;
        std::uint16_t  sourceId;
        std::uint32_t  unused;
        const void*    noise;      // noise notices point at the noise (position first); some readers follow it whatever the source
    };
    static_assert(sizeof(NoticeInfo) == 16, "the game copies notices as 16 bytes");

    struct alignas(16) FakeNoise
    {
        float        position[4];
        std::uint8_t rest[0x30];   // zeroed noise fields past the position
    };

    struct PendingNotice
    {
        std::atomic<bool> waiting{ false };
        std::uint8_t      kind     = 0;
        std::uint16_t     targetId = SoldierNotice::kNoTarget;
    };

    CheckForceNotice_t g_Orig       = nullptr;
    void*              g_HookTarget = nullptr;
    PendingNotice      g_Pending[kMaxSoldiers];
    FakeNoise          g_Noises[kMaxSoldiers];   // stays put for as long as the soldier remembers the notice
    std::atomic<int>   g_PendingCount{ 0 };

    template <typename Fn>
    Fn VirtualAt(void* object, std::size_t index)
    {
        return (*reinterpret_cast<Fn**>(object))[index];
    }

    void PostNotice(std::uint8_t* noticeController, std::uint32_t soldierIndex, const PendingNotice& pending)
    {
        NoticeInfo notice{};
        notice.type     = pending.kind;
        notice.noise    = &g_Noises[soldierIndex];
        notice.sourceId = pending.targetId;
        notice.sourceKind = pending.targetId != SoldierNotice::kNoTarget ? kSourceIsGameObject : kSourceIsPosition;

        void* noticeInterface = noticeController + kNoticeInterface;
        VirtualAt<AddNoticeInfo_t>(noticeInterface, kAddNoticeInfoVtblIndex)(noticeInterface, soldierIndex, &notice);
        VirtualAt<ResetForceNotice_t>(noticeInterface, kResetForceNoticeVtblIndex)(noticeInterface, soldierIndex);
    }

    void __fastcall hk_CheckForceNotice(std::uint8_t* noticeController, std::uint32_t soldierIndex)
    {
        if (g_PendingCount.load(std::memory_order_acquire) <= 0 || soldierIndex >= kMaxSoldiers
            || MissionCodeGuard::ShouldBypassHooks())
        {
            g_Orig(noticeController, soldierIndex);
            return;
        }

        PendingNotice& pending = g_Pending[soldierIndex];
        if (!pending.waiting.load(std::memory_order_acquire))
        {
            g_Orig(noticeController, soldierIndex);
            return;
        }

        PostNotice(noticeController, soldierIndex, pending);
        pending.waiting.store(false, std::memory_order_release);
        g_PendingCount.fetch_sub(1, std::memory_order_acq_rel);
    }
}

bool SoldierNotice::Add(std::uint32_t soldierIndex, int kind, std::uint16_t targetId, const float* position)
{
    if (!g_HookTarget || soldierIndex >= kMaxSoldiers || kind < 0 || kind > kLastKind)
        return false;
    if (targetId == kNoTarget && !position)
        return false;

    PendingNotice& pending = g_Pending[soldierIndex];
    const bool wasWaiting = pending.waiting.exchange(false, std::memory_order_acq_rel);

    pending.kind     = static_cast<std::uint8_t>(kind);
    pending.targetId = targetId;
    g_Noises[soldierIndex] = FakeNoise{};
    if (position)
    {
        g_Noises[soldierIndex].position[0] = position[0];
        g_Noises[soldierIndex].position[1] = position[1];
        g_Noises[soldierIndex].position[2] = position[2];
    }

    pending.waiting.store(true, std::memory_order_release);
    if (!wasWaiting)
        g_PendingCount.fetch_add(1, std::memory_order_acq_rel);
    return true;
}

void SoldierNotice::ClearPending()
{
    for (PendingNotice& pending : g_Pending)
    {
        if (pending.waiting.exchange(false, std::memory_order_acq_rel))
            g_PendingCount.fetch_sub(1, std::memory_order_acq_rel);
    }
}

void CallComrade::DeliverComradeCallRelaxed(std::uint32_t comradeIndex, std::uint16_t callerId)
{
    SoldierNotice::Add(comradeIndex, SoldierNotice::kCalledOver, callerId, nullptr);
}

void CallComrade::DeliverComradeCallOnGuard(std::uint32_t comradeIndex, std::uint16_t callerId)
{
    SoldierNotice::Add(comradeIndex, SoldierNotice::kCalledForHelp, callerId, nullptr);
}

bool Install_NoticeControllerImpl_CheckForceNotice_Hook()
{
    if (g_HookTarget)
        return true;

    if (!gAddr.NoticeControllerImpl_CheckForceNotice)
    {
        LogError("CallComradeCommand: CheckForceNotice address missing for %s, called comrades come over on guard and AddNotice does nothing",
                 GetGameBuildName(gGameBuild));
        return true;
    }

    void* target = ResolveGameAddress(gAddr.NoticeControllerImpl_CheckForceNotice);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_CheckForceNotice),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("CallComradeCommand: CheckForceNotice hook failed at %p, called comrades come over on guard and AddNotice does nothing", target);
        return false;
    }

    g_HookTarget = target;
    return true;
}

void Uninstall_NoticeControllerImpl_CheckForceNotice_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
    SoldierNotice::ClearPending();
}
