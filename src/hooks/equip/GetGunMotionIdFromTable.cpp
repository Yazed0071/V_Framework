#include "pch.h"
#include "GetGunMotionIdFromTable.h"

#include <cstdint>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MotionLoaderImpl_GetMtarIds.h"
#include "log.h"

namespace
{
    constexpr std::size_t  kWeaponInfoReceiverType = 0xB9;
    constexpr std::size_t  kWeaponInfoFlags        = 0xCC;
    constexpr unsigned int kFirstOwnedReceiverType = 48;
    constexpr std::uint8_t kGunUseDescRoundsLeft   = 0x10;
    constexpr std::size_t  kGunUseDescHoldType     = 2;
    constexpr int          kHoldTypeOneHand        = 1;
    constexpr int          kHoldTypeCqc            = 2;
    constexpr int          kHoldTypeHorse          = 3;
    constexpr int          kHoldTypeChase          = 6;
    constexpr std::uint32_t kFlagsOneHand          = 0x1800;
    constexpr std::uint32_t kFlagsDualMagazine     = 0x20000;
    constexpr std::size_t  kWorkWeaponInfo         = 0x1B0;
    constexpr std::size_t  kWorkFlags              = 0x27C;
    constexpr std::size_t  kWorkGripSlot           = 0x2D0;
    constexpr int          kVariantVanilla         = -1;

    using MotionSelector_t       = std::uint64_t(__fastcall*)(int, void*, void*);
    using MagazineGripSelector_t = std::uint64_t(__fastcall*)(void*);
    using PrepareGrip_t          = void(__fastcall*)(void*, std::uint8_t*, unsigned int, void*, std::uint8_t);

    MotionSelector_t       g_OrigGetGunMotionIdFromTable            = nullptr;
    MotionSelector_t       g_OrigGetEquipMotionIdFromTable          = nullptr;
    MotionSelector_t       g_OrigGetNormalGripMotionId              = nullptr;
    MagazineGripSelector_t g_OrigGetNormalMagazineGripMotionId      = nullptr;
    MotionSelector_t       g_OrigTypeGrip[8]                        = {};
    MotionSelector_t       g_OrigGetAssaultUnderBarrelGripMotionId  = nullptr;
    MotionSelector_t       g_OrigGetHandgunOneHandGripMotionId      = nullptr;
    MotionSelector_t       g_OrigGetHandgunOneHandMotionId          = nullptr;
    MotionSelector_t       g_OrigGetSubMachinegunOneHandMotionId    = nullptr;
    MotionSelector_t       g_OrigGetStepReloadHandgunOneHandMotionId = nullptr;
    PrepareGrip_t          g_OrigResetMissionPrepareGripMotion      = nullptr;
    MotionSelector_t       g_OrigTwoHandTable[11]                   = {};
    std::vector<void*>     g_HookedTargets;

    int ReadByteSEH(const void* base, std::size_t offset)
    {
        __try
        {
            return *(static_cast<const std::uint8_t*>(base) + offset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    std::uint32_t ReadU32SEH(const void* base, std::size_t offset)
    {
        __try
        {
            return *reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(base) + offset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool WriteU64SEH(void* base, std::size_t offset, std::uint64_t value)
    {
        __try
        {
            *reinterpret_cast<std::uint64_t*>(static_cast<std::uint8_t*>(base) + offset) = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    unsigned int OwnedReceiverType(const void* weaponInfo)
    {
        const int t = weaponInfo ? ReadByteSEH(weaponInfo, kWeaponInfoReceiverType) : -1;
        return (t >= static_cast<int>(kFirstOwnedReceiverType)) ? static_cast<unsigned int>(t) : 0;
    }

    bool RoundsLeft(const void* gunUseMotionDesc)
    {
        const int b = gunUseMotionDesc ? ReadByteSEH(gunUseMotionDesc, 0) : 0;
        return b > 0 && (b & kGunUseDescRoundsLeft) != 0;
    }

    int HoldType(const void* gunUseMotionDesc)
    {
        const int b = gunUseMotionDesc ? ReadByteSEH(gunUseMotionDesc, kGunUseDescHoldType) : 0;
        return b < 0 ? 0 : b;
    }

    int OneHandVariant(const void* gunUseMotionDesc)
    {
        return HoldType(gunUseMotionDesc) == kHoldTypeCqc ? kReceiverVariantOneHandCqc : kReceiverVariantOneHand;
    }

    int VariantFor(const void* weaponInfo, const void* gunUseMotionDesc)
    {
        const int hold = HoldType(gunUseMotionDesc);
        if (hold == kHoldTypeChase)
            return kVariantVanilla;
        if (hold == kHoldTypeHorse)
            return kReceiverVariantHorse;
        if (hold == kHoldTypeOneHand || hold == kHoldTypeCqc)
            return OneHandVariant(gunUseMotionDesc);
        if (ReadU32SEH(weaponInfo, kWeaponInfoFlags) & kFlagsOneHand)
            return OneHandVariant(gunUseMotionDesc);
        return kReceiverVariantTwoHand;
    }

    std::uint64_t __fastcall hkGetGunMotionIdFromTable(int gunUseMotionType, void* weaponInfo, void* desc)
    {
        const unsigned int owned = OwnedReceiverType(weaponInfo);
        if (owned)
        {
            const int variant = VariantFor(weaponInfo, desc);
            if (variant != kVariantVanilla)
            {
                const std::uint64_t clip =
                    ReceiverMotion_PlayerClipVariant(owned, variant, gunUseMotionType, RoundsLeft(desc));
                if (clip)
                    return clip;
            }
        }
        return g_OrigGetGunMotionIdFromTable(gunUseMotionType, weaponInfo, desc);
    }

    std::uint64_t __fastcall hkGetEquipMotionIdFromTable(int gunUseMotionType, void* weaponInfo, void* desc)
    {
        const unsigned int owned = OwnedReceiverType(weaponInfo);
        if (owned && gunUseMotionType >= 0 && gunUseMotionType <= 4)
        {
            const std::uint64_t clip =
                ReceiverMotion_WeaponClip(owned, gunUseMotionType, RoundsLeft(desc));
            if (clip)
                return clip;
        }
        return g_OrigGetEquipMotionIdFromTable(gunUseMotionType, weaponInfo, desc);
    }

    std::uint64_t TwoHandGrip(void* weaponInfo)
    {
        const unsigned int owned = OwnedReceiverType(weaponInfo);
        return owned ? ReceiverMotion_PlayerClip(owned, kReceiverMotionGrip, true) : 0;
    }

    std::uint64_t __fastcall hkGetNormalGripMotionId(int gripType, void* weaponInfo, void* desc)
    {
        if (const std::uint64_t clip = TwoHandGrip(weaponInfo))
            return clip;
        return g_OrigGetNormalGripMotionId(gripType, weaponInfo, desc);
    }

    std::uint64_t __fastcall hkGetNormalMagazineGripMotionId(void* weaponInfo)
    {
        const unsigned int owned = OwnedReceiverType(weaponInfo);
        if (owned)
        {
            std::uint64_t clip = 0;
            if (ReadU32SEH(weaponInfo, kWeaponInfoFlags) & kFlagsDualMagazine)
                clip = ReceiverMotion_PlayerClip(owned, kReceiverMotionMagazineGripDual, true);
            if (!clip)
                clip = ReceiverMotion_PlayerClip(owned, kReceiverMotionMagazineGrip, true);
            if (clip)
                return clip;
        }
        return g_OrigGetNormalMagazineGripMotionId(weaponInfo);
    }

    template <int N>
    std::uint64_t __fastcall hkTypeGrip(int gripType, void* weaponInfo, void* desc)
    {
        if (const std::uint64_t clip = TwoHandGrip(weaponInfo))
            return clip;
        return g_OrigTypeGrip[N](gripType, weaponInfo, desc);
    }

    std::uint64_t __fastcall hkGetAssaultUnderBarrelGripMotionId(int gripType, void* weaponInfo, void* desc)
    {
        const unsigned int owned = OwnedReceiverType(weaponInfo);
        if (owned)
        {
            const std::uint64_t clip = ReceiverMotion_PlayerClip(owned, kReceiverMotionUnderBarrelGrip, true);
            if (clip)
                return clip;
        }
        return g_OrigGetAssaultUnderBarrelGripMotionId(gripType, weaponInfo, desc);
    }

    std::uint64_t __fastcall hkGetHandgunOneHandGripMotionId(int gripType, void* weaponInfo, void* desc)
    {
        const unsigned int owned = OwnedReceiverType(weaponInfo);
        if (owned)
        {
            const std::uint64_t clip =
                ReceiverMotion_PlayerClipVariant(owned, OneHandVariant(desc), kReceiverMotionGrip, true);
            if (clip)
                return clip;
        }
        return g_OrigGetHandgunOneHandGripMotionId(gripType, weaponInfo, desc);
    }

    std::uint64_t OneHandClip(void* weaponInfo, void* desc, int gunUseMotionType, bool roundsLeft)
    {
        const unsigned int owned = OwnedReceiverType(weaponInfo);
        if (!owned)
            return 0;
        return ReceiverMotion_PlayerClipVariant(owned, OneHandVariant(desc), gunUseMotionType, roundsLeft);
    }

    std::uint64_t __fastcall hkGetHandgunOneHandMotionId(int gunUseMotionType, void* weaponInfo, void* desc)
    {
        if (const std::uint64_t clip = OneHandClip(weaponInfo, desc, gunUseMotionType, RoundsLeft(desc)))
            return clip;
        return g_OrigGetHandgunOneHandMotionId(gunUseMotionType, weaponInfo, desc);
    }

    std::uint64_t __fastcall hkGetSubMachinegunOneHandMotionId(int gunUseMotionType, void* weaponInfo, void* desc)
    {
        if (const std::uint64_t clip = OneHandClip(weaponInfo, desc, gunUseMotionType, RoundsLeft(desc)))
            return clip;
        return g_OrigGetSubMachinegunOneHandMotionId(gunUseMotionType, weaponInfo, desc);
    }

    std::uint64_t __fastcall hkGetStepReloadHandgunOneHandMotionId(int gunUseMotionType, void* weaponInfo, void* desc)
    {
        if (const std::uint64_t clip = OneHandClip(weaponInfo, desc, kReceiverMotionStepReload, true))
            return clip;
        return g_OrigGetStepReloadHandgunOneHandMotionId(gunUseMotionType, weaponInfo, desc);
    }

    template <int N>
    std::uint64_t __fastcall hkTwoHandTable(int gunUseMotionType, void* weaponInfo, void* desc)
    {
        const unsigned int owned = OwnedReceiverType(weaponInfo);
        if (owned)
        {
            const std::uint64_t clip = ReceiverMotion_PlayerClip(owned, gunUseMotionType, RoundsLeft(desc));
            if (clip)
                return clip;
        }
        return g_OrigTwoHandTable[N](gunUseMotionType, weaponInfo, desc);
    }

    void __fastcall hkResetMissionPrepareGripMotion(void* self, std::uint8_t* work, unsigned int playerIndex,
                                                    void* adjust, std::uint8_t restoreDefault)
    {
        g_OrigResetMissionPrepareGripMotion(self, work, playerIndex, adjust, restoreDefault);
        if (restoreDefault || !work)
            return;
        const unsigned int owned = OwnedReceiverType(work + kWorkWeaponInfo);
        if (!owned || !(ReadU32SEH(work, kWorkFlags) & kFlagsOneHand))
            return;
        const std::uint64_t clip =
            ReceiverMotion_PlayerClipVariant(owned, kReceiverVariantOneHand, kReceiverMotionGrip, true);
        if (clip)
            WriteU64SEH(work, kWorkGripSlot, clip);
    }

    bool HookSelector(uintptr_t addr, void* detour, void** original, const char* what)
    {
        void* target = ResolveGameAddress(addr);
        if (!target)
            return true;
        if (CreateAndEnableHook(target, detour, original))
        {
            g_HookedTargets.push_back(target);
            return true;
        }
        Log("[ReceiverMotion] ERROR: %s hook failed at %p - no-donor receivers get no "
            "clips from that selector\n", what, target);
        return false;
    }
}

bool Install_GetGunMotionIdFromTable_Hook()
{
    if (!gAddr.AttackAction_GetGunMotionIdFromTable
        || !gAddr.AttackAction_GetEquipMotionIdFromTable)
        return true;

    struct Entry
    {
        uintptr_t   addr;
        void*       detour;
        void**      orig;
        const char* name;
    };
    const Entry entries[] = {
        { gAddr.AttackAction_GetGunMotionIdFromTable, &hkGetGunMotionIdFromTable,
          reinterpret_cast<void**>(&g_OrigGetGunMotionIdFromTable), "GetGunMotionIdFromTable" },
        { gAddr.AttackAction_GetEquipMotionIdFromTable, &hkGetEquipMotionIdFromTable,
          reinterpret_cast<void**>(&g_OrigGetEquipMotionIdFromTable), "GetEquipMotionIdFromTable" },
        { gAddr.AttackAction_GetNormalGripMotionId, &hkGetNormalGripMotionId,
          reinterpret_cast<void**>(&g_OrigGetNormalGripMotionId), "GetNormalGripMotionId" },
        { gAddr.AttackAction_GetNormalMagazineGripMotionId, &hkGetNormalMagazineGripMotionId,
          reinterpret_cast<void**>(&g_OrigGetNormalMagazineGripMotionId), "GetNormalMagazineGripMotionId" },
        { gAddr.AttackAction_GetAssaultGripMotionId, &hkTypeGrip<0>,
          reinterpret_cast<void**>(&g_OrigTypeGrip[0]), "GetAssaultGripMotionId" },
        { gAddr.AttackAction_GetHandgunGripMotionId, &hkTypeGrip<1>,
          reinterpret_cast<void**>(&g_OrigTypeGrip[1]), "GetHandgunGripMotionId" },
        { gAddr.AttackAction_GetSubMachinegunGripMotionId, &hkTypeGrip<2>,
          reinterpret_cast<void**>(&g_OrigTypeGrip[2]), "GetSubMachinegunGripMotionId" },
        { gAddr.AttackAction_GetSniperRifleGripMotionId, &hkTypeGrip<3>,
          reinterpret_cast<void**>(&g_OrigTypeGrip[3]), "GetSniperRifleGripMotionId" },
        { gAddr.AttackAction_GetShotgunGripMotionId, &hkTypeGrip<4>,
          reinterpret_cast<void**>(&g_OrigTypeGrip[4]), "GetShotgunGripMotionId" },
        { gAddr.AttackAction_GetMachinegunGripMotionId, &hkTypeGrip<5>,
          reinterpret_cast<void**>(&g_OrigTypeGrip[5]), "GetMachinegunGripMotionId" },
        { gAddr.AttackAction_GetGrenadeLauncherGripMotionId, &hkTypeGrip<6>,
          reinterpret_cast<void**>(&g_OrigTypeGrip[6]), "GetGrenadeLauncherGripMotionId" },
        { gAddr.AttackAction_GetMissileGripMotionId, &hkTypeGrip<7>,
          reinterpret_cast<void**>(&g_OrigTypeGrip[7]), "GetMissileGripMotionId" },
        { gAddr.AttackAction_GetAssaultUnderBarrelGripMotionId, &hkGetAssaultUnderBarrelGripMotionId,
          reinterpret_cast<void**>(&g_OrigGetAssaultUnderBarrelGripMotionId), "GetAssaultUnderBarrelGripMotionId" },
        { gAddr.AttackAction_GetHandgunOneHandGripMotionId, &hkGetHandgunOneHandGripMotionId,
          reinterpret_cast<void**>(&g_OrigGetHandgunOneHandGripMotionId), "GetHandgunOneHandGripMotionId" },
        { gAddr.AttackAction_GetHandgunOneHandMotionId, &hkGetHandgunOneHandMotionId,
          reinterpret_cast<void**>(&g_OrigGetHandgunOneHandMotionId), "GetHandgunOneHandMotionId" },
        { gAddr.AttackAction_GetSubMachinegunOneHandMotionId, &hkGetSubMachinegunOneHandMotionId,
          reinterpret_cast<void**>(&g_OrigGetSubMachinegunOneHandMotionId), "GetSubMachinegunOneHandMotionId" },
        { gAddr.AttackAction_GetStepReloadHandgunOneHandMotionId, &hkGetStepReloadHandgunOneHandMotionId,
          reinterpret_cast<void**>(&g_OrigGetStepReloadHandgunOneHandMotionId), "GetStepReloadHandgunOneHandMotionId" },
        { gAddr.AttackAction_ResetMissionPrepareGripMotion, &hkResetMissionPrepareGripMotion,
          reinterpret_cast<void**>(&g_OrigResetMissionPrepareGripMotion), "ResetMissionPrepareGripMotion" },
        { gAddr.AttackAction_GetNormalMotionIdByCategory, &hkTwoHandTable<0>,
          reinterpret_cast<void**>(&g_OrigTwoHandTable[0]), "GetNormalMotionIdByCategory" },
        { gAddr.AttackAction_GetAssaultMotionIdTable, &hkTwoHandTable<1>,
          reinterpret_cast<void**>(&g_OrigTwoHandTable[1]), "GetAssaultMotionIdTable" },
        { gAddr.AttackAction_GetHandgunMotionIdTable, &hkTwoHandTable<2>,
          reinterpret_cast<void**>(&g_OrigTwoHandTable[2]), "GetHandgunMotionIdTable" },
        { gAddr.AttackAction_GetSubMachinegunMotionIdTable, &hkTwoHandTable<3>,
          reinterpret_cast<void**>(&g_OrigTwoHandTable[3]), "GetSubMachinegunMotionIdTable" },
        { gAddr.AttackAction_GetSniperRifleMotionIdTable, &hkTwoHandTable<4>,
          reinterpret_cast<void**>(&g_OrigTwoHandTable[4]), "GetSniperRifleMotionIdTable" },
        { gAddr.AttackAction_GetShotgunMotionIdTable, &hkTwoHandTable<5>,
          reinterpret_cast<void**>(&g_OrigTwoHandTable[5]), "GetShotgunMotionIdTable" },
        { gAddr.AttackAction_GetShotgunStepReloadMotionIdTable, &hkTwoHandTable<6>,
          reinterpret_cast<void**>(&g_OrigTwoHandTable[6]), "GetShotgunStepReloadMotionIdTable" },
        { gAddr.AttackAction_GetMachinegunMotionIdTable, &hkTwoHandTable<7>,
          reinterpret_cast<void**>(&g_OrigTwoHandTable[7]), "GetMachinegunMotionIdTable" },
        { gAddr.AttackAction_GetGrenadeLauncherMotionIdTable, &hkTwoHandTable<8>,
          reinterpret_cast<void**>(&g_OrigTwoHandTable[8]), "GetGrenadeLauncherMotionIdTable" },
        { gAddr.AttackAction_GetMissileMotionIdTable, &hkTwoHandTable<9>,
          reinterpret_cast<void**>(&g_OrigTwoHandTable[9]), "GetMissileMotionIdTable" },
        { gAddr.AttackAction_GetChaseMotionIdTable, &hkTwoHandTable<10>,
          reinterpret_cast<void**>(&g_OrigTwoHandTable[10]), "GetChaseMotionIdTable" },
    };

    bool ok = true;
    for (const Entry& e : entries)
        ok = HookSelector(e.addr, e.detour, e.orig, e.name) && ok;
    return ok;
}

void Uninstall_GetGunMotionIdFromTable_Hook()
{
    for (void* target : g_HookedTargets)
        DisableAndRemoveHook(target);
    g_HookedTargets.clear();
    g_OrigGetGunMotionIdFromTable = nullptr;
    g_OrigGetEquipMotionIdFromTable = nullptr;
    g_OrigGetNormalGripMotionId = nullptr;
    g_OrigGetNormalMagazineGripMotionId = nullptr;
    for (MotionSelector_t& o : g_OrigTypeGrip)
        o = nullptr;
    g_OrigGetAssaultUnderBarrelGripMotionId = nullptr;
    g_OrigGetHandgunOneHandGripMotionId = nullptr;
    g_OrigGetHandgunOneHandMotionId = nullptr;
    g_OrigGetSubMachinegunOneHandMotionId = nullptr;
    g_OrigGetStepReloadHandgunOneHandMotionId = nullptr;
    g_OrigResetMissionPrepareGripMotion = nullptr;
    for (MotionSelector_t& o : g_OrigTwoHandTable)
        o = nullptr;
}
