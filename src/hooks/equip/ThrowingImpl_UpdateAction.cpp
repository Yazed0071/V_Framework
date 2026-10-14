#include "pch.h"
#include "ThrowingImpl_UpdateAction.h"

#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "SupportWeaponRegistry.h"
#include "ThrowingSystemLayout.h"
#include "log.h"

namespace
{
    using UpdateActionGrenade_t         = void(__fastcall*)(void* throwing, std::uint32_t index, std::uint32_t equipId, int supportWeaponType, float blastScale);
    using UpdateActionSmoke_t           = void(__fastcall*)(void* throwing, std::uint32_t index, std::uint32_t equipId, std::uint8_t firstTick);
    using UpdateActionMolotovCocktail_t = void(__fastcall*)(void* throwing, std::uint32_t index, std::uint32_t equipId, float blastScale);
    using UpdateActionDecoy_t           = void(__fastcall*)(void* throwing, std::uint32_t index, std::uint32_t equipId);
    using UpdateActionKibidango_t       = void(__fastcall*)(void* throwing, std::uint32_t index, std::uint32_t equipId);
    using UpdateActionFakeSign_t        = void(__fastcall*)(void* throwing, std::uint32_t index, std::uint32_t equipId, std::uint8_t firstTick);

    constexpr int   kSupportWeaponTypeGrenade     = 1;
    constexpr int   kSupportWeaponTypeStunGrenade = 3;
    constexpr float kVanillaBlastScale            = 1.0f;

    UpdateActionGrenade_t         g_OrigGrenade         = nullptr;
    UpdateActionSmoke_t           g_OrigSmoke           = nullptr;
    UpdateActionMolotovCocktail_t g_OrigMolotovCocktail = nullptr;
    UpdateActionDecoy_t           g_OrigDecoy           = nullptr;
    UpdateActionKibidango_t       g_OrigKibidango       = nullptr;
    UpdateActionFakeSign_t        g_OrigFakeSign        = nullptr;

    void* g_GrenadeTarget         = nullptr;
    void* g_SmokeTarget           = nullptr;
    void* g_MolotovCocktailTarget = nullptr;
    void* g_DecoyTarget           = nullptr;
    void* g_KibidangoTarget       = nullptr;
    void* g_FakeSignTarget        = nullptr;

    std::atomic<std::uint64_t> g_Announced{ 0 };

    template <typename Fn>
    Fn OriginalOr(Fn original, std::uintptr_t address)
    {
        return original ? original : reinterpret_cast<Fn>(ResolveGameAddress(address));
    }

    void AnnounceActivation(void* throwing, std::uint32_t index, std::uint32_t equipId)
    {
        using namespace ThrowingSystemLayout;
        auto* impl = static_cast<std::uint8_t*>(throwing);
        const std::uint32_t handle = HandleOf(impl, index);
        if (handle >= 64)
            return;
        const std::uint64_t bit = 1ull << handle;
        if (g_Announced.fetch_or(bit) & bit)
            return;
        if (MissionCodeGuard::ShouldBypassHooks())
            return;
        const std::uint16_t owner = Read<std::uint16_t>(ImplRecord(impl, index), kRecordOwner);
        V_FrameWork::QueueMessage("Throwing", "OnActivateThrowing", handle,
                                  static_cast<std::uint32_t>(equip::FromEngine(static_cast<std::int32_t>(equipId))), owner);
    }

    void __fastcall hkUpdateActionGrenade(void* throwing, std::uint32_t index, std::uint32_t equipId, int supportWeaponType, float blastScale)
    {
        AnnounceActivation(throwing, index, equipId);
        if (!SupportWeapon::NativeAction(equipId))
            return;
        g_OrigGrenade(throwing, index, equipId, supportWeaponType, blastScale);
    }

    void __fastcall hkUpdateActionSmoke(void* throwing, std::uint32_t index, std::uint32_t equipId, std::uint8_t firstTick)
    {
        AnnounceActivation(throwing, index, equipId);
        if (!SupportWeapon::NativeAction(equipId))
            return;
        g_OrigSmoke(throwing, index, equipId, firstTick);
    }

    void __fastcall hkUpdateActionMolotovCocktail(void* throwing, std::uint32_t index, std::uint32_t equipId, float blastScale)
    {
        AnnounceActivation(throwing, index, equipId);
        if (!SupportWeapon::NativeAction(equipId))
            return;
        g_OrigMolotovCocktail(throwing, index, equipId, blastScale);
    }

    void __fastcall hkUpdateActionDecoy(void* throwing, std::uint32_t index, std::uint32_t equipId)
    {
        AnnounceActivation(throwing, index, equipId);
        if (!SupportWeapon::NativeAction(equipId))
            return;
        g_OrigDecoy(throwing, index, equipId);
    }

    void __fastcall hkUpdateActionKibidango(void* throwing, std::uint32_t index, std::uint32_t equipId)
    {
        AnnounceActivation(throwing, index, equipId);
        if (!SupportWeapon::NativeAction(equipId))
            return;
        g_OrigKibidango(throwing, index, equipId);
    }

    void __fastcall hkUpdateActionFakeSign(void* throwing, std::uint32_t index, std::uint32_t equipId, std::uint8_t firstTick)
    {
        AnnounceActivation(throwing, index, equipId);
        if (!SupportWeapon::NativeAction(equipId))
            return;
        g_OrigFakeSign(throwing, index, equipId, firstTick);
    }

    bool HookOne(std::uintptr_t address, void* detour, void** original, void*& target, const char* name)
    {
        target = ResolveGameAddress(address);
        if (!target)
            return true;
        if (!CreateAndEnableHook(target, detour, original))
        {
            LogError("SupportWeapon: %s hook failed, nativeAction=false and OnActivateThrowing are off for that family", name);
            target = nullptr;
            return false;
        }
        return true;
    }

    void UnhookOne(void*& target, void** original)
    {
        if (target)
            DisableAndRemoveHook(target);
        target    = nullptr;
        *original = nullptr;
    }
}

void ThrowingImpl_ForgetActivation(std::uint32_t handle)
{
    if (handle < 64)
        g_Announced.fetch_and(~(1ull << handle));
}

void ThrowingImpl_RunUpdateAction(ThrowingRequestKind kind, void* impl, std::uint32_t localIndex,
                                  std::uint16_t equipId, int option)
{
    switch (kind)
    {
    case ThrowingRequestKind::UpdateActionGrenade:
        if (auto run = OriginalOr(g_OrigGrenade, gAddr.ThrowingImpl_UpdateActionGrenade))
        {
            const int type = option == kSupportWeaponTypeStunGrenade ? kSupportWeaponTypeStunGrenade : kSupportWeaponTypeGrenade;
            run(impl, localIndex, equipId, type, kVanillaBlastScale);
        }
        break;
    case ThrowingRequestKind::UpdateActionSmoke:
        if (auto run = OriginalOr(g_OrigSmoke, gAddr.ThrowingImpl_UpdateActionSmoke))
        {
            const std::uint32_t familyFrom = SupportWeapon::FamilyFrom(equipId);
            SupportWeapon::SetRunningItem(familyFrom != 0 ? equipId : 0);
            run(impl, localIndex, familyFrom != 0 ? familyFrom : equipId, 1);
            SupportWeapon::SetRunningItem(0);
        }
        break;
    case ThrowingRequestKind::UpdateActionMolotovCocktail:
        if (auto run = OriginalOr(g_OrigMolotovCocktail, gAddr.ThrowingImpl_UpdateActionMolotovCocktail))
            run(impl, localIndex, equipId, kVanillaBlastScale);
        break;
    case ThrowingRequestKind::UpdateActionDecoy:
        if (auto run = OriginalOr(g_OrigDecoy, gAddr.ThrowingImpl_UpdateActionDecoy))
            run(impl, localIndex, equipId);
        break;
    case ThrowingRequestKind::UpdateActionKibidango:
        if (auto run = OriginalOr(g_OrigKibidango, gAddr.ThrowingImpl_UpdateActionKibidango))
            run(impl, localIndex, equipId);
        break;
    case ThrowingRequestKind::UpdateActionFakeSign:
        if (auto run = OriginalOr(g_OrigFakeSign, gAddr.ThrowingImpl_UpdateActionFakeSign))
            run(impl, localIndex, equipId, 1);
        break;
    default:
        break;
    }
}

bool Install_ThrowingImpl_UpdateAction_Hooks()
{
    bool ok = HookOne(gAddr.ThrowingImpl_UpdateActionGrenade, reinterpret_cast<void*>(&hkUpdateActionGrenade),
                      reinterpret_cast<void**>(&g_OrigGrenade), g_GrenadeTarget, "UpdateActionGrenade");
    ok = HookOne(gAddr.ThrowingImpl_UpdateActionSmoke, reinterpret_cast<void*>(&hkUpdateActionSmoke),
                 reinterpret_cast<void**>(&g_OrigSmoke), g_SmokeTarget, "UpdateActionSmoke") && ok;
    ok = HookOne(gAddr.ThrowingImpl_UpdateActionMolotovCocktail, reinterpret_cast<void*>(&hkUpdateActionMolotovCocktail),
                 reinterpret_cast<void**>(&g_OrigMolotovCocktail), g_MolotovCocktailTarget, "UpdateActionMolotovCocktail") && ok;
    ok = HookOne(gAddr.ThrowingImpl_UpdateActionDecoy, reinterpret_cast<void*>(&hkUpdateActionDecoy),
                 reinterpret_cast<void**>(&g_OrigDecoy), g_DecoyTarget, "UpdateActionDecoy") && ok;
    ok = HookOne(gAddr.ThrowingImpl_UpdateActionKibidango, reinterpret_cast<void*>(&hkUpdateActionKibidango),
                 reinterpret_cast<void**>(&g_OrigKibidango), g_KibidangoTarget, "UpdateActionKibidango") && ok;
    ok = HookOne(gAddr.ThrowingImpl_UpdateActionFakeSign, reinterpret_cast<void*>(&hkUpdateActionFakeSign),
                 reinterpret_cast<void**>(&g_OrigFakeSign), g_FakeSignTarget, "UpdateActionFakeSign") && ok;
    if (!g_GrenadeTarget || !g_SmokeTarget || !g_MolotovCocktailTarget || !g_DecoyTarget
        || !g_KibidangoTarget || !g_FakeSignTarget)
        LogError("SupportWeapon: throw activation addresses missing for this build, nativeAction=false, OnActivateThrowing and sleep-gas families are partial");
    return ok;
}

void Uninstall_ThrowingImpl_UpdateAction_Hooks()
{
    UnhookOne(g_GrenadeTarget, reinterpret_cast<void**>(&g_OrigGrenade));
    UnhookOne(g_SmokeTarget, reinterpret_cast<void**>(&g_OrigSmoke));
    UnhookOne(g_MolotovCocktailTarget, reinterpret_cast<void**>(&g_OrigMolotovCocktail));
    UnhookOne(g_DecoyTarget, reinterpret_cast<void**>(&g_OrigDecoy));
    UnhookOne(g_KibidangoTarget, reinterpret_cast<void**>(&g_OrigKibidango));
    UnhookOne(g_FakeSignTarget, reinterpret_cast<void**>(&g_OrigFakeSign));
    g_Announced.store(0);
}
