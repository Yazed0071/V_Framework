#include "pch.h"
#include "ThrowingImpl_UpdateStatus.h"

#include <Windows.h>
#include <cstdint>

#include <atomic>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "SupportWeaponRegistry.h"
#include "ThrowingSystemLayout.h"
#include "log.h"

namespace
{
    using UpdateStatus_t = void(__fastcall*)(void* throwing, void* a2, void* a3, void* a4);

    constexpr std::size_t   kItemCount        = 0x50;
    constexpr std::size_t   kItemEquipIds     = 0x120;   // u16 equipId + u16 owner per item
    constexpr std::size_t   kItemRecords      = 0x128;
    constexpr std::size_t   kItemEquipIdSize  = 4;
    constexpr std::size_t   kItemRecordSize   = 0x24;
    constexpr std::size_t   kItemFlags        = 0x1C;
    constexpr std::uint32_t kFlagLive         = 0x1;
    constexpr std::uint32_t kFlagsAttached    = 0x3000000;
    constexpr std::uint32_t kFlagActivateNow  = 0x10000000;

    constexpr std::uint32_t kTrackedHandles   = 64;

    UpdateStatus_t g_Orig   = nullptr;
    void*          g_Target = nullptr;
    std::atomic<std::uint64_t> g_StopAnnounced{ 0 };

    struct Stop
    {
        std::uint32_t handle;
        std::uint16_t equipId;
        std::uint16_t owner;
    };

    bool ActivatesOnAttach(std::uint32_t familyFrom)
    {
        const bool molotovCocktail = familyFrom >= 0x17E && familyFrom <= 0x181;
        const bool kibidango       = familyFrom >= 0x183 && familyFrom <= 0x185;
        return molotovCocktail || kibidango;
    }

    int CollectStopsSEH(std::uint8_t* impl, Stop* out, int capacity)
    {
        int count = 0;
        __try
        {
            using namespace ThrowingSystemLayout;
            const std::uint32_t items = *reinterpret_cast<std::uint32_t*>(impl + kItemCount);
            for (std::uint32_t local = 0; local < items && count < capacity; ++local)
            {
                const std::uint32_t flags = Read<std::uint32_t>(ImplRecord(impl, local), kItemFlags);
                if ((flags & kFlagLive) == 0 || (flags & kFlagsStopped) != kFlagsStopped)
                    continue;
                const std::uint32_t handle = HandleOf(impl, local);
                if (handle >= kTrackedHandles)
                    continue;
                const std::uint64_t bit = 1ull << handle;
                if (g_StopAnnounced.fetch_or(bit) & bit)
                    continue;
                Stop& s = out[count++];
                s.handle  = handle;
                s.equipId = ImplRealEquipId(impl, local);
                s.owner   = Read<std::uint16_t>(ImplRecord(impl, local), kRecordOwner);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return count;
    }

    void AnnounceStops(void* throwing)
    {
        if (!throwing || MissionCodeGuard::ShouldBypassHooks())
            return;
        Stop stops[kTrackedHandles];
        const int count = CollectStopsSEH(static_cast<std::uint8_t*>(throwing), stops, static_cast<int>(kTrackedHandles));
        for (int i = 0; i < count; ++i)
            V_FrameWork::QueueMessage("Throwing", "OnStopThrowing", stops[i].handle, stops[i].equipId, stops[i].owner);
    }

    void ActivateAttachedCustomItems(void* throwing)
    {
        __try
        {
            auto* base = static_cast<std::uint8_t*>(throwing);
            const std::uint32_t count = *reinterpret_cast<std::uint32_t*>(base + kItemCount);
            auto* equipIds = *reinterpret_cast<std::uint8_t**>(base + kItemEquipIds);
            auto* records  = *reinterpret_cast<std::uint8_t**>(base + kItemRecords);
            if (!equipIds || !records)
                return;
            for (std::uint32_t index = 0; index < count; ++index)
            {
                auto* flags = reinterpret_cast<std::uint32_t*>(records + index * kItemRecordSize + kItemFlags);
                if ((*flags & kFlagLive) == 0 || (*flags & kFlagsAttached) == 0)
                    continue;
                const std::uint16_t equipId = *reinterpret_cast<std::uint16_t*>(equipIds + index * kItemEquipIdSize);
                const std::uint32_t familyFrom = SupportWeapon::FamilyFrom(equipId);
                if (familyFrom != 0 && ActivatesOnAttach(familyFrom))
                    *flags |= kFlagActivateNow;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkUpdateStatus(void* throwing, void* a2, void* a3, void* a4)
    {
        g_Orig(throwing, a2, a3, a4);
        ActivateAttachedCustomItems(throwing);
        AnnounceStops(throwing);
    }
}

void ThrowingImpl_ForgetStop(std::uint32_t handle)
{
    if (handle < kTrackedHandles)
        g_StopAnnounced.fetch_and(~(1ull << handle));
}

bool Install_ThrowingImpl_UpdateStatus_Hook()
{
    g_Target = ResolveGameAddress(gAddr.ThrowingImpl_UpdateStatus);
    if (!g_Target)
    {
        LogError("SupportWeapon: ThrowingImpl::UpdateStatus address missing for this build, custom Molotov/Kibidango items never activate");
        return true;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkUpdateStatus),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("SupportWeapon: ThrowingImpl::UpdateStatus hook failed, custom Molotov/Kibidango items never activate");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_ThrowingImpl_UpdateStatus_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
    g_StopAnnounced.store(0);
}
