#include "pch.h"

#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "NoticeControllerImpl_CheckSightNoticePlayer.h"
#include "SoldierCombatVoice.h"
#include "SoldierIncomingVoice.h"
#include "SoldierStinkVoice.h"
#include "log.h"

namespace
{
    constexpr std::uint32_t kSoldierIndexMask = 0x1FFu;
    constexpr std::size_t   kSoldierSlotCount = 0x200u;
    constexpr std::uint64_t kLiveControllerFreshMs = 1000;

    using SightNotice4_t = void(__fastcall*)(void* self, std::uint32_t soldierIndex,
                                             void* a3, void* a4);

    std::mutex                                    g_mtx;
    std::array<std::uint8_t, kSoldierSlotCount>   g_ignoreMask{};

    SightNotice4_t g_OrigPlayer = nullptr;

    void* g_PlayerTarget = nullptr;

    std::uintptr_t Addr_Player() { return gAddr.NoticeControllerImpl_CheckSightNoticePlayer; }

    thread_local int t_suppressPlayerSight = 0;

    std::atomic<void*>         g_LiveController{ nullptr };
    std::atomic<std::uint64_t> g_LiveControllerStampMs{ 0 };

    void __fastcall hk_Player(void* self, std::uint32_t soldierIndex, void* a3, void* a4)
    {
        if (!g_OrigPlayer)
            return;

        g_LiveController.store(self, std::memory_order_relaxed);
        g_LiveControllerStampMs.store(GetTickCount64(), std::memory_order_relaxed);
        SoldierStinkVoice_OnSightCheck(self, soldierIndex);
        SoldierIncomingVoice_OnSightCheck(self, soldierIndex);
        SoldierCombatVoice_OnSightCheck(self, soldierIndex);

        if (!Soldier_IgnoresNotice(soldierIndex, SoldierNoticeIgnore::kPlayer))
        {
            g_OrigPlayer(self, soldierIndex, a3, a4);
            return;
        }

        ++t_suppressPlayerSight;
        g_OrigPlayer(self, soldierIndex, a3, a4);
        --t_suppressPlayerSight;
    }

    bool InstallOne(std::uintptr_t addr, void* detour, void** orig, void** targetOut,
                    const char* what)
    {
        if (!addr)
            return false;

        void* target = ResolveGameAddress(addr);
        if (!target || !CreateAndEnableHook(target, detour, orig))
        {
            LogError("SoldierNoticeIgnore: %s hook failed, its SetRestrictNotice field has no effect", what);
            return false;
        }

        *targetOut = target;
        return true;
    }
}

void Set_SoldierNoticeIgnoreMask(std::uint32_t gameObjectId, std::uint8_t mask)
{
    const std::uint32_t slot = gameObjectId & kSoldierIndexMask;
    std::lock_guard<std::mutex> lk(g_mtx);
    g_ignoreMask[slot] = mask;
}

bool SoldierNotice_ShouldDropNotice(std::uint32_t soldierIndex, std::uint8_t noticeType,
                                    const void* returnAddress)
{
    UNREFERENCED_PARAMETER(noticeType);
    UNREFERENCED_PARAMETER(returnAddress);

    if (t_suppressPlayerSight > 0)
        return true;

    return Soldier_IgnoresNotice(soldierIndex, SoldierNoticeIgnore::kPlayer);
}

bool Soldier_IgnoresNotice(std::uint32_t soldierIndex, std::uint8_t flag)
{
    const std::uint32_t slot = soldierIndex & kSoldierIndexMask;
    std::lock_guard<std::mutex> lk(g_mtx);
    return (g_ignoreMask[slot] & flag) != 0;
}

void Clear_AllSoldierIgnorePlayer()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    g_ignoreMask.fill(0);
}

void* SoldierNotice_LiveController()
{
    if (GetTickCount64() - g_LiveControllerStampMs.load(std::memory_order_relaxed) > kLiveControllerFreshMs)
        return nullptr;
    return g_LiveController.load(std::memory_order_relaxed);
}

bool Install_CheckSightNoticePlayer_Hook()
{
    if (!Addr_Player())
    {
        LogError("SoldierNoticeIgnore: CheckSightNoticePlayer address missing, SetRestrictNotice ignorePlayer has no effect");
    }

    InstallOne(Addr_Player(), reinterpret_cast<void*>(&hk_Player),
               reinterpret_cast<void**>(&g_OrigPlayer), &g_PlayerTarget,
               "CheckSightNoticePlayer (ignorePlayer)");

    return true;
}

bool Uninstall_CheckSightNoticePlayer_Hook()
{
    if (g_PlayerTarget)
        DisableAndRemoveHook(g_PlayerTarget);

    g_PlayerTarget = nullptr;
    g_OrigPlayer = nullptr;

    Clear_AllSoldierIgnorePlayer();
    return true;
}
