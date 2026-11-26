#include <pch.h>
#include "ThrowingImpl_OnOffenseHit.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "ThrowingSystemLayout.h"
#include "log.h"

namespace
{
    using OnOffenseHit_t = std::uint32_t(__fastcall*)(void* impl, std::uint32_t localIndex, void* offense, void* defense,
                                                      const void* result);

    constexpr std::size_t   kTargetGameObjectId = 0x28;   // points at the hit object's u16 GameObjectId
    constexpr std::uint16_t kNoGameObject       = 0xFFFF;
    constexpr std::uint32_t kReboundCountMask   = 0xF00;

    const std::uint8_t kEntry[] = {
        0x40, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xD0, 0x00, 0x00, 0x00,
        0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x33, 0xC4,
        0x48, 0x89, 0x84, 0x24, 0xA0, 0x00, 0x00, 0x00,
    };
    const std::uint8_t kEntryMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 0, 0, 0, 0, 1, 1, 1,   // the stack cookie load, its distance differs per build
        1, 1, 1, 1, 1, 1, 1, 1,
    };
    static_assert(sizeof(kEntry) == sizeof(kEntryMask), "entry and mask differ in size");

    struct Hit
    {
        std::uint32_t handle;
        std::uint16_t equipId;
        std::uint16_t owner;
        std::uint16_t hitId;
        std::uint16_t lastHitId;
        bool          firstFlight;
    };

    OnOffenseHit_t g_Orig   = nullptr;
    void*          g_Target = nullptr;

    bool ReadHitSEH(std::uint8_t* impl, std::uint32_t localIndex, const std::uint8_t* defense, Hit& out)
    {
        __try
        {
            using namespace ThrowingSystemLayout;
            const auto* hitIdAt = Read<const std::uint16_t*>(defense, kTargetGameObjectId);
            if (!hitIdAt || *hitIdAt == kNoGameObject)
                return false;
            const std::uint8_t* record = ImplRecord(impl, localIndex);
            out.hitId       = *hitIdAt;
            out.lastHitId   = LastHitId(impl, localIndex);
            out.owner       = Read<std::uint16_t>(record, kRecordOwner);
            out.firstFlight = (Read<std::uint32_t>(record, kRecordFlags) & kReboundCountMask) == 0;
            out.handle      = HandleOf(impl, localIndex);
            out.equipId     = ImplRealEquipId(impl, localIndex);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint16_t LastHitIdSEH(std::uint8_t* impl, std::uint32_t localIndex)
    {
        __try
        {
            return ThrowingSystemLayout::LastHitId(impl, localIndex);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoGameObject;
        }
    }

    std::uint32_t __fastcall hkOnOffenseHit(void* impl, std::uint32_t localIndex, void* offense, void* defense, const void* result)
    {
        Hit hit{};
        auto* bytes = static_cast<std::uint8_t*>(impl);
        const bool newHit = impl && defense && !MissionCodeGuard::ShouldBypassHooks()
                            && ReadHitSEH(bytes, localIndex, static_cast<const std::uint8_t*>(defense), hit)
                            && hit.hitId != hit.lastHitId
                            && !(hit.firstFlight && hit.hitId == hit.owner);
        const std::uint32_t verdict = g_Orig(impl, localIndex, offense, defense, result);
        if (newHit && LastHitIdSEH(bytes, localIndex) == hit.hitId)
            V_FrameWork::QueueMessage("Throwing", "OnHitThrowing", hit.handle, hit.equipId, hit.owner, hit.hitId);
        return verdict;
    }
}

bool Install_ThrowingImpl_OnOffenseHit_Hook()
{
    g_Target = ResolveGameAddress(gAddr.ThrowingImpl_OnOffenseHit);
    if (!g_Target)
    {
        LogError("ThrowingSystem: OnOffenseHit address missing for this build, OnHitThrowing is off");
        return true;
    }
    const HookEntry entry = CheckHookEntryMasked(g_Target, kEntry, kEntryMask, sizeof(kEntry));
    if (entry == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(g_Target, desc, sizeof(desc));
        LogError("ThrowingSystem: ThrowingImpl::OnOffenseHit differs on this build (%s), OnHitThrowing is off", desc);
        g_Target = nullptr;
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("ThrowingSystem: ThrowingImpl::OnOffenseHit", g_Target);
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkOnOffenseHit), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ThrowingSystem: OnOffenseHit hook failed, OnHitThrowing is off");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_ThrowingImpl_OnOffenseHit_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
