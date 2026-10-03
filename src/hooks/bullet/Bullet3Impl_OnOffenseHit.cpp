#include "pch.h"

#include <Windows.h>
#include <array>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "Bullet3Impl_ActivateBulletAtEmptyWorkPatch.h"
#include "../soldier/SoldierFaction.h"
#include "log.h"

namespace
{
    using OnOffenseHit_t = std::uint32_t(__fastcall*)(void* bullet3, std::uint32_t bulletIndex, void* offense,
                                                      void* defense, void* result);

    constexpr std::size_t   kBullet3Records      = 0xB0;
    constexpr std::size_t   kBulletRecordStride  = 0x30;
    constexpr std::size_t   kBulletOwner         = 0x24;
    constexpr std::size_t   kBulletGroups        = 0x28;
    constexpr std::size_t   kTargetGroups        = 0x38;
    constexpr std::size_t   kTargetOwnerHandle   = 0x50;
    constexpr std::uint64_t kGroupBandMask       = 0xFFFu;
    constexpr std::uint16_t kGroupEnemy          = 0x0002u;
    constexpr std::uint16_t kNoGameObject        = 0xFFFFu;
    constexpr std::uint32_t kPassThrough         = 2u;
    constexpr std::uint32_t kHandleGameObject    = 1u;
    constexpr std::uint32_t kHandleEquip         = 3u;
    constexpr std::uint64_t kEquipKindMask       = 0x1Fu;
    constexpr std::uint64_t kEquipKindCarried    = 9u;
    constexpr std::size_t   kQuarkGameSystems    = 0x98;
    constexpr std::size_t   kGameSystemsEquip    = 0x1E8;
    constexpr std::size_t   kEquipGetOwnerSlot   = 3;
    constexpr std::uint32_t kSoldierSlotCount    = 0x200u;

    struct BulletContact
    {
        std::uint16_t ownerId;
        std::uint16_t bulletGroups;
        std::uint64_t targetGroups;
        std::uint64_t targetHandle;
    };

    OnOffenseHit_t g_OrigOnOffenseHit = nullptr;
    std::array<std::uint16_t, kSoldierSlotCount> g_loggedHitRival{};
    std::array<std::uint16_t, kSoldierSlotCount> g_loggedPassAlly{};

    bool ReadBulletContact(void* bullet3, std::uint32_t bulletIndex, const void* defense, BulletContact* out)
    {
        __try
        {
            const std::uint8_t* records = *reinterpret_cast<const std::uint8_t* const*>(
                static_cast<const std::uint8_t*>(bullet3) + kBullet3Records);
            if (!records || !defense)
                return false;
            const std::uint8_t* record = records + bulletIndex * kBulletRecordStride;
            const std::uint8_t* target = static_cast<const std::uint8_t*>(defense);
            out->ownerId      = *reinterpret_cast<const std::uint16_t*>(record + kBulletOwner);
            out->bulletGroups = *reinterpret_cast<const std::uint16_t*>(record + kBulletGroups);
            out->targetGroups = *reinterpret_cast<const std::uint64_t*>(target + kTargetGroups) & kGroupBandMask;
            out->targetHandle = *reinterpret_cast<const std::uint64_t*>(target + kTargetOwnerHandle);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint16_t ReadEquipOwnerSEH(std::uint32_t equipHandle)
    {
        using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();
        using GetEquipOwner_t       = void*(__fastcall*)(void* equipSystem, std::uint16_t* owner, std::uint32_t equipHandle);

        __try
        {
            auto getQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
            if (!getQuarkSystemTable)
                return kNoGameObject;
            std::uint8_t* quarkSystemTable = getQuarkSystemTable();
            std::uint8_t* gameSystems = *reinterpret_cast<std::uint8_t**>(quarkSystemTable + kQuarkGameSystems);
            void* equipSystem = *reinterpret_cast<void**>(gameSystems + kGameSystemsEquip);
            if (!equipSystem)
                return kNoGameObject;
            GetEquipOwner_t getOwner = (*reinterpret_cast<GetEquipOwner_t* const*>(equipSystem))[kEquipGetOwnerSlot];
            std::uint16_t owner = kNoGameObject;
            getOwner(equipSystem, &owner, equipHandle);
            return owner;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoGameObject;
        }
    }

    std::uint16_t TargetGameObjectId(std::uint64_t handle)
    {
        const bool valid = (handle >> 63) != 0;
        const std::uint32_t kind = static_cast<std::uint32_t>((handle >> 59) & 0xFu);
        if (kind == kHandleGameObject)
            return valid ? static_cast<std::uint16_t>(handle) : kNoGameObject;
        if (kind == kHandleEquip && (handle & kEquipKindMask) == kEquipKindCarried)
            return ReadEquipOwnerSEH(static_cast<std::uint32_t>(handle));
        return kNoGameObject;
    }

    bool IsSoldierId(std::uint16_t gameObjectId)
    {
        return gameObjectId != kNoGameObject && (gameObjectId >> 9) == 2u;
    }

    void LogOnce(std::array<std::uint16_t, kSoldierSlotCount>& logged, std::uint16_t shooterId,
                 std::uint16_t soldierId, const char* what)
    {
        const std::uint32_t shooter = shooterId & 0x1FFu;
        if (logged[shooter] == soldierId)
            return;
        logged[shooter] = soldierId;
        LogInfo("SoldierFaction: bullet from soldier %u %s soldier %u", shooter, what, soldierId & 0x1FFu);
    }

    std::array<bool, 16> g_loggedUnknownKind{};

    void LogUnknownPassOnce(const BulletContact& contact)
    {
        const std::uint32_t kind = static_cast<std::uint32_t>((contact.targetHandle >> 59) & 0xFu);
        if (g_loggedUnknownKind[kind])
            return;
        g_loggedUnknownKind[kind] = true;
        LogInfo("SoldierFaction: bullet from soldier %u passed through a non-soldier target, handle 0x%016llX groups 0x%03llX",
            contact.ownerId & 0x1FFu, static_cast<unsigned long long>(contact.targetHandle),
            static_cast<unsigned long long>(contact.targetGroups));
    }

    std::uint32_t __fastcall hk_OnOffenseHit(void* bullet3, std::uint32_t bulletIndex, void* offense,
                                             void* defense, void* result)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigOnOffenseHit, bullet3, bulletIndex, offense, defense, result);

        if (!SoldierFaction_AnyFaction())
            return g_OrigOnOffenseHit(bullet3, bulletIndex, offense, defense, result);

        BulletContact contact{};
        if (!ReadBulletContact(bullet3, bulletIndex, defense, &contact))
            return g_OrigOnOffenseHit(bullet3, bulletIndex, offense, defense, result);
        const std::uint16_t shooterId = SoldierFaction_RiderOrSelf(contact.ownerId);
        if (!SoldierFaction_IsFactionSoldier(shooterId))
            return g_OrigOnOffenseHit(bullet3, bulletIndex, offense, defense, result);

        const std::uint16_t victimId = TargetGameObjectId(contact.targetHandle);
        if (IsSoldierId(victimId) && victimId != shooterId)
        {
            if (SoldierFaction_AreHostileSoldiers(shooterId, victimId))
            {
                LogOnce(g_loggedHitRival, shooterId, victimId, "hit rival");
                return g_OrigOnOffenseHit(bullet3, bulletIndex, offense, defense, result);
            }
            if (Get_FriendlyFire())
                return g_OrigOnOffenseHit(bullet3, bulletIndex, offense, defense, result);
            LogOnce(g_loggedPassAlly, shooterId, victimId, "passed through non-rival");
            return kPassThrough;
        }

        const std::uint16_t victimRider = victimId == kNoGameObject ? kNoGameObject : SoldierFaction_RiderOrSelf(victimId);
        if (victimRider != victimId && IsSoldierId(victimRider) && SoldierFaction_AreHostileSoldiers(shooterId, victimRider))
        {
            LogOnce(g_loggedHitRival, shooterId, victimRider, "hit the walker gear or vehicle of rival");
            return g_OrigOnOffenseHit(bullet3, bulletIndex, offense, defense, result);
        }

        const std::uint16_t vanillaGroups = static_cast<std::uint16_t>(contact.bulletGroups & ~kGroupEnemy);
        if ((vanillaGroups & contact.targetGroups) != 0 || victimId == contact.ownerId || Get_FriendlyFire())
            return g_OrigOnOffenseHit(bullet3, bulletIndex, offense, defense, result);
        LogUnknownPassOnce(contact);
        return kPassThrough;
    }
}

bool Install_Bullet3Impl_OnOffenseHit_Hook()
{
    void* target = ResolveGameAddress(gAddr.Bullet3Impl_OnOffenseHit);
    if (!target)
        return false;
    return CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_OnOffenseHit),
                               reinterpret_cast<void**>(&g_OrigOnOffenseHit));
}

bool Uninstall_Bullet3Impl_OnOffenseHit_Hook()
{
    void* target = ResolveGameAddress(gAddr.Bullet3Impl_OnOffenseHit);
    if (target)
        DisableAndRemoveHook(target);
    g_OrigOnOffenseHit = nullptr;
    return true;
}
