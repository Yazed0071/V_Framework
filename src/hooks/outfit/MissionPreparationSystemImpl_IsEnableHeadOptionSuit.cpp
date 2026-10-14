#include "pch.h"

#include "MissionPreparationSystemImpl_IsEnableHeadOptionSuit.h"
#include "OutfitRegistry.h"
#include "CustomHeadRegistry.h"
#include "PlayerAppearance_LoadPlayerFv2s.h"
#include "FoxPathInternal.h"
#include "UniqueCharacterPartsTypePin.h"
#include "UniqueCharacterDefaultOutfit.h"
#include "StaffControllerImpl_AddStaff.h"

#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "V_FrameWorkState.h"
#include "../equip/DevelopArrayGrow.h"
#include "../equip/EquipAliasLease.h"
#include "../equip/EquipDevelop_SetEquipUndeveloped.h"
#include "../equip/EquipDevelop_AddToEquipDevelopTable.h"
#include "../equip/EquipPartParams.h"

namespace
{


    using IsEnableCurrentHeadOption_t = std::uint8_t (__fastcall*)(void* self);

    static IsEnableCurrentHeadOption_t g_OrigIsEnableHead = nullptr;
    static bool                        g_Installed        = false;


    using IsEnableCurrentSuit_t = std::uint8_t (__fastcall*)(void* self);
    static IsEnableCurrentSuit_t g_OrigIsEnableCurrentSuit       = nullptr;
    static bool                  g_IsEnableCurrentSuitInstalled  = false;


    using IsEnableHeadOptionSuit_t =
        std::uint8_t (__fastcall*)(void* self, std::uint16_t flowIndex);
    static IsEnableHeadOptionSuit_t g_OrigIsEnableHeadOptionSuit       = nullptr;
    static bool                     g_IsEnableHeadOptionSuitInstalled  = false;


    using MenuGetLangText_t = const char* (__fastcall*)(void* mgr, std::uint64_t key);
    static MenuGetLangText_t g_OrigMenuGetLangText   = nullptr;
    static void*             g_MenuGetLangTextTarget = nullptr;
    static std::atomic<bool> g_FobBlockVfwOnly{ false };

    constexpr std::uint64_t kFobBlockVanillaLangKey = 0xEF471B5B1850ull;
    constexpr std::uint64_t kFobBlockVfwLangKey     = 0x45C697D13F30ull;
    constexpr std::uint64_t kLangKeyMask            = 0xFFFFFFFFFFFFull;
    constexpr std::size_t   kMenuUiMgrOffset        = 0x38;
    constexpr std::size_t   kMenuGetLangTextVtblOff = 0x750;

    static const char* __fastcall hkMenuGetLangText(void* mgr, std::uint64_t key)
    {
        if ((key & kLangKeyMask) == kFobBlockVanillaLangKey
            && g_FobBlockVfwOnly.load(std::memory_order_relaxed))
        {
            const char* swapped = nullptr;
            __try
            {
                swapped = g_OrigMenuGetLangText(
                    mgr, (key & ~kLangKeyMask) | kFobBlockVfwLangKey);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                swapped = nullptr;
            }
            if (swapped && swapped[0])
                return swapped;
        }
        return g_OrigMenuGetLangText(mgr, key);
    }

    static void EnsureMenuLangHookSEH(void* menuCallback)
    {
        __try
        {
            void* mgr = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(menuCallback) + kMenuUiMgrOffset);
            if (!mgr)
                return;
            void** vtbl = *reinterpret_cast<void***>(mgr);
            if (!vtbl)
                return;
            void* fn = vtbl[kMenuGetLangTextVtblOff / sizeof(void*)];
            if (!fn)
                return;
            if (CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkMenuGetLangText),
                                    reinterpret_cast<void**>(&g_OrigMenuGetLangText)))
                g_MenuGetLangTextTarget = fn;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }


    using ConverFaceId_t = std::uint16_t (__fastcall*)(
        std::uint8_t playerType, std::int16_t faceId,
        std::uint8_t playerFaceEquipId);
    static ConverFaceId_t g_OrigConverFaceId           = nullptr;
    static bool           g_ConverFaceIdInstalled      = false;

    static bool BranchFpkInstalled(const outfit::DdHeadLayers& branch,
                                   std::uint16_t fallbackFaceId,
                                   std::uint64_t overrideFpk = 0)
    {
        if (fox::detail::PathExistsByCode(overrideFpk ? overrideFpk : branch.branchFpk))
            return true;
        static std::atomic<const char*> s_lastMissing{ nullptr };
        if (s_lastMissing.exchange(branch.name, std::memory_order_relaxed) != branch.name)
            LogWarn("CustomHead: '%s' fpk not installed, using face 0x%X",
                branch.name ? branch.name : "?",
                static_cast<unsigned>(fallbackFaceId));
        return false;
    }

    static std::uint16_t __fastcall hkConverFaceIdWithFaceEquipId(
        std::uint8_t playerType, std::int16_t faceId,
        std::uint8_t playerFaceEquipId)
    {
        outfit::ddheadlayers::DisarmBranchFace();

        if (MissionCodeGuard::ShouldBypassHooks())
        {
            const std::uint8_t safeSlot =
                playerFaceEquipId >= outfit::kCustomHeadSlotBase
                    ? std::uint8_t{0} : playerFaceEquipId;
            return g_OrigConverFaceId
                ? g_OrigConverFaceId(playerType, faceId, safeSlot)
                : faceId;
        }

        if (outfit::IsCustomHeadSlot(playerFaceEquipId))
        {
            if (const auto* head =
                outfit::TryGetCustomHeadBySlot(playerFaceEquipId))
            {
                const std::uint8_t livePT = outfit::ReadLivePartsType();
                const outfit::OutfitEntry* oe = nullptr;
                const bool offered =
                    (livePT >= outfit::kCustomPartsTypeStart
                     && livePT <= outfit::kCustomPartsTypeEnd
                     && outfit::TryGetOutfitByPartsType(livePT, &oe) && oe
                     && oe->HasHeadOptionAnyVariant(head->equipId, playerType))
                    || (livePT < outfit::kCustomPartsTypeStart
                        && outfit::VanillaExtHasHeadOption(livePT, head->equipId,
                                                           playerType,
                                                           outfit::ReadLiveSelectorCode()));
                if (offered)
                {
                    const std::uint8_t pt =
                        (playerType < outfit::kPlayerTypeMax) ? playerType : 0;
                    std::uint16_t fid = head->TppEnemyFaceId[pt];
                    if (fid == 0) fid = head->TppEnemyFaceId[0];

                    outfit::DdHeadLayers branch{};
                    const bool haveBranch = outfit::TryGetDdHeadBranch(
                        playerFaceEquipId, playerType, &branch);
                    if (haveBranch && (branch.flags & outfit::kHeadLayerKeepFace) != 0
                        && outfit::ddheadlayers::IsReady())
                    {
                        const auto faceFpk = branch.paths.faceFovaFpk
                            ? branch.paths.faceFovaFpk : branch.branchFpk;
                        if (branch.paths.faceFova
                            && outfit::ddheadlayers::CanReplaceFace()
                            && outfit::IsFpkPathCode(faceFpk)
                            && BranchFpkInstalled(branch,
                                static_cast<std::uint16_t>(faceId), faceFpk))
                            outfit::ddheadlayers::ArmBranchFace(
                                static_cast<std::uint16_t>(faceId),
                                branch.paths.faceFova, faceFpk);
                        return static_cast<std::uint16_t>(faceId);
                    }

                    if (haveBranch && outfit::ReplacesFaceWithBranchModel(branch)
                        && outfit::ddheadlayers::CanReplaceFace()
                        && BranchFpkInstalled(branch, fid))
                        outfit::ddheadlayers::ArmBranchFace(
                            fid, branch.branchFv2, branch.branchFpk);
                    return fid;
                }
            }
        }
        else if (playerFaceEquipId >= 1 && playerFaceEquipId <= 5)
        {
            const std::uint8_t livePT = outfit::ReadLivePartsType();
            const outfit::OutfitEntry* oe = nullptr;
            if (livePT >= outfit::kCustomPartsTypeStart
                && livePT <= outfit::kCustomPartsTypeEnd
                && outfit::TryGetOutfitByPartsType(livePT, &oe) && oe)
            {
                const std::uint16_t vanEquipId =
                    static_cast<std::uint16_t>(playerFaceEquipId + 0x20D);
                if (!oe->HasHeadOptionAnyVariant(vanEquipId, playerType))
                {
                    return g_OrigConverFaceId
                        ? g_OrigConverFaceId(playerType, faceId, 0)
                        : faceId;
                }
            }
        }

        return g_OrigConverFaceId
            ? g_OrigConverFaceId(playerType, faceId, playerFaceEquipId)
            : faceId;
    }

    using ConvertHeadEquipModelType_t = std::uint64_t (__fastcall*)(
        void* self, std::uint32_t soldierIndex, void* offerList,
        std::uint32_t offerCount);
    static ConvertHeadEquipModelType_t g_OrigConvertHeadEquip      = nullptr;
    static bool                        g_ConvertHeadEquipInstalled = false;
    constexpr std::uint64_t            kNoHeadEquipModelType       = 0x2e;

    static std::uint64_t __fastcall hkConvertHeadEquipModelType(
        void* self, std::uint32_t soldierIndex, void* offerList,
        std::uint32_t offerCount)
    {
        if (self && soldierIndex != 0x1ff)
        {
            __try
            {
                auto* container = *reinterpret_cast<std::uint8_t**>(
                    reinterpret_cast<std::uint8_t*>(self) + 0xF8);
                auto* arrHdr = container
                    ? *reinterpret_cast<std::uint8_t**>(container + 0x98)
                    : nullptr;
                auto* base = arrHdr
                    ? *reinterpret_cast<std::uint8_t**>(arrHdr + 0x8)
                    : nullptr;
                const std::uint32_t count = arrHdr
                    ? *reinterpret_cast<std::uint32_t*>(arrHdr + 0x10)
                    : 0;
                if (base && soldierIndex < count)
                {
                    auto* entry =
                        base + static_cast<std::size_t>(soldierIndex) * 0x70;
                    const std::uint16_t wornId =
                        *reinterpret_cast<std::uint16_t*>(entry + 0x4e);
                    if (wornId != 0)
                    {
                        const outfit::CustomHeadEntry* h =
                            outfit::TryGetCustomHeadByEquipId(wornId);
                        if (h)
                        {
                            if (MissionCodeGuard::ShouldBypassHooks())
                                return kNoHeadEquipModelType;
                            const std::uint8_t livePT =
                                outfit::ReadLivePartsType();
                            const outfit::OutfitEntry* oe = nullptr;
                            const bool offered =
                                (livePT >= outfit::kCustomPartsTypeStart
                                 && livePT <= outfit::kCustomPartsTypeEnd
                                 && outfit::TryGetOutfitByPartsType(livePT, &oe)
                                 && oe
                                 && oe->HasHeadOptionAnyVariant(
                                        h->equipId, outfit::ReadLivePlayerType()))
                                || (livePT < outfit::kCustomPartsTypeStart
                                    && outfit::VanillaExtHasHeadOption(
                                           livePT, h->equipId,
                                           outfit::ReadLivePlayerType(),
                                           outfit::ReadLiveSelectorCode()));
                            if (!offered)
                                return kNoHeadEquipModelType;
                        }
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                static std::atomic<int> s_fault{0};
                if (int n = s_fault.load(std::memory_order_relaxed); n < 8)
                {
                    s_fault.store(n + 1, std::memory_order_relaxed);
                    LogWarn("OutfitHeadOption: ConvertHeadEquipModelType fault on soldier %u, vanilla head used",
                        soldierIndex);
                }
            }
        }
        return g_OrigConvertHeadEquip
            ? g_OrigConvertHeadEquip(self, soldierIndex, offerList, offerCount)
            : kNoHeadEquipModelType;
    }

    constexpr std::size_t kVtblSlot_PrepIsFobSortie     = 0x4F0 / 8;
    constexpr std::size_t kVtblSlot_LoadoutGetWeaponEquipId  = 0x180 / 8;
    constexpr std::size_t kVtblSlot_LoadoutGetSupportEquipId = 0x1C8 / 8;
    constexpr std::size_t kVtblSlot_LoadoutGetItemEquipId    = 0x1D8 / 8;
    constexpr std::size_t kVtblSlot_DevIdxFromEquipId   = 0xF0 / 8;
    constexpr std::size_t kVtblSlot_DevIsFobAvailable   = 0x478 / 8;
    constexpr std::size_t kCallback_PrepSystemOffset    = 0x48;

    static bool IsFobSortieContext(void* self)
    {
        __try
        {
            void* sys = *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(self) + kCallback_PrepSystemOffset);
            if (!sys)
                return false;
            using Ctx_t = std::uint8_t (__fastcall*)(void*);
            auto ctx = reinterpret_cast<Ctx_t>(
                (*reinterpret_cast<void***>(sys))[kVtblSlot_PrepIsFobSortie]);
            return ctx && ctx(sys) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool IsFobSortiePrepSystem(void* sys)
    {
        __try
        {
            if (!sys)
                return false;
            using Ctx_t = std::uint8_t (__fastcall*)(void*);
            auto ctx = reinterpret_cast<Ctx_t>(
                (*reinterpret_cast<void***>(sys))[kVtblSlot_PrepIsFobSortie]);
            return ctx && ctx(sys) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    using PrepGetU16_t = std::uint16_t (__fastcall*)(void*, std::uint8_t);

    static std::uint16_t SafeCallPrepGetU16(PrepGetU16_t fn, void* sys,
                                            std::uint8_t i)
    {
        __try { return fn(sys, i); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0xEEEE; }
    }

    constexpr std::size_t kQuark_AppOffset          = 0x98;
    constexpr std::size_t kApp_LoadoutHolderOffset  = 0x130;

    static void* ResolveLoadoutHolder()
    {
        using GetQuark_t = void* (__fastcall*)();
        auto getQuark = reinterpret_cast<GetQuark_t>(
            ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (!getQuark)
            return nullptr;
        __try
        {
            auto* quark = static_cast<std::uint8_t*>(getQuark());
            if (!quark)
                return nullptr;
            auto* app = *reinterpret_cast<std::uint8_t**>(
                quark + kQuark_AppOffset);
            if (!app)
                return nullptr;
            return *reinterpret_cast<void**>(app + kApp_LoadoutHolderOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    static std::atomic<std::uint16_t> g_LastFobBlockedEquip{ 0 };
    constexpr std::uint32_t kEquipTypeThrowing = 12;
    constexpr std::uint32_t kEquipTypePlaced   = 13;

    static bool FobLoadoutHasBannedManagedEquip(void* self)
    {
        static_cast<void>(self);
        __try
        {
            void* sys = ResolveLoadoutHolder();
            if (!sys)
                return false;
            void** sysVt = *reinterpret_cast<void***>(sys);

            void* ctrl = EquipDevelop_ResolveDevelopController();
            if (!ctrl)
                return false;
            void** ctrlVt = *reinterpret_cast<void***>(ctrl);

            using GetU16_t = PrepGetU16_t;
            using MapIdx_t = std::uint16_t (__fastcall*)(void*, std::uint16_t,
                                                         std::uint8_t);
            using FobBit_t = std::uint8_t (__fastcall*)(void*, std::uint16_t);

            auto mapIdx = reinterpret_cast<MapIdx_t>(
                ctrlVt[kVtblSlot_DevIdxFromEquipId]);
            auto fobBit = reinterpret_cast<FobBit_t>(
                ctrlVt[kVtblSlot_DevIsFobAvailable]);
            if (!mapIdx || !fobBit)
                return false;

            struct SlotRange { std::size_t slot; std::uint8_t count; };
            const SlotRange ranges[] = {
                { kVtblSlot_LoadoutGetWeaponEquipId,  3 },
                { kVtblSlot_LoadoutGetSupportEquipId, 8 },
                { kVtblSlot_LoadoutGetItemEquipId,    8 },
            };

            struct ScanEntry
            {
                std::uint16_t equipId;
                std::uint16_t devIdx;
                std::uint8_t  tainted;
                std::uint8_t  customWeapon;
            };
            ScanEntry entries[19] = {};
            int       entryCount = 0;
            int       bannedAt   = -1;

            for (const SlotRange& r : ranges)
            {
                auto get = reinterpret_cast<GetU16_t>(sysVt[r.slot]);
                if (!get)
                    continue;
                for (std::uint8_t i = 0; i < r.count; ++i)
                {
                    const std::uint16_t equipId = SafeCallPrepGetU16(get, sys, i);
                    if (equipId == 0 || equipId == 0xEEEE)
                        continue;
                    const std::uint16_t idx = mapIdx(ctrl, equipId, 0);
                    const bool weaponSlot = r.slot == kVtblSlot_LoadoutGetWeaponEquipId;
                    const bool supportSlot = r.slot == kVtblSlot_LoadoutGetSupportEquipId;
                    const bool managedRow = equip::IsValidFlowIndex(idx)
                        && EquipDevelopAdd::IsManagedFlowIndex(idx);
                    const std::int32_t realId = equip::ResolveReal(equipId);
                    const bool claimed = V_FrameWorkState::IsClaimedEquipId(realId);
                    const std::uint32_t equipType = EquipParam_GetEquipType(static_cast<std::uint32_t>(realId));
                    const bool supportType = equipType == kEquipTypeThrowing || equipType == kEquipTypePlaced;
                    const bool customWeapon = ((weaponSlot || supportSlot) && (managedRow || claimed))
                        || (!weaponSlot && !supportSlot && claimed && supportType);
                    const bool managed = managedRow || customWeapon;
                    const bool tainted = !managed
                        && EquipParam_IsEquipIdFobTainted(equipId, weaponSlot ? 1 : 0);
                    const bool fobOk = !tainted && !customWeapon
                        && (!managed || fobBit(ctrl, idx) != 0);
                    if (entryCount < 19)
                    {
                        entries[entryCount].equipId = equipId;
                        entries[entryCount].devIdx  = idx;
                        entries[entryCount].tainted = tainted ? 1 : 0;
                        entries[entryCount].customWeapon = customWeapon ? 1 : 0;
                        if ((managed || tainted) && !fobOk && bannedAt < 0)
                            bannedAt = entryCount;
                        ++entryCount;
                    }
                    else if ((managed || tainted) && !fobOk && bannedAt < 0)
                    {
                        bannedAt = 19;
                    }
                }
            }
            if (bannedAt >= 0)
            {
                if (bannedAt < entryCount)
                {
                    const std::uint16_t bannedEquipId = entries[bannedAt].equipId;
                    if (g_LastFobBlockedEquip.exchange(
                            bannedEquipId, std::memory_order_relaxed) != bannedEquipId)
                    {
                        if (entries[bannedAt].customWeapon)
                            LogWarn("FobGuard: deploy blocked, equipId %u is a custom weapon or support weapon",
                                static_cast<unsigned>(bannedEquipId));
                        else if (entries[bannedAt].tainted)
                            LogWarn("FobGuard: deploy blocked, equipId %u uses parts or damage rows a mod changed",
                                static_cast<unsigned>(bannedEquipId));
                        else
                            LogWarn("FobGuard: deploy blocked, equipId %u (flowIndex %u) not FOB-available",
                                static_cast<unsigned>(bannedEquipId),
                                static_cast<unsigned>(entries[bannedAt].devIdx));
                    }
                }
                return true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        return false;
    }

    static std::uint8_t __fastcall hkIsEnableHeadOptionSuit(
        void* self, std::uint16_t param2)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return g_OrigIsEnableHeadOptionSuit(self, param2);

        if (IsFobSortiePrepSystem(self))
            return g_OrigIsEnableHeadOptionSuit(self, param2);

        const std::uint8_t pt     = outfit::ReadLivePartsType();
        const std::uint8_t livePT = outfit::ReadLivePlayerType();

        if (uniquecharpin::IsOwnSuitPartsType(livePT, pt))
            return 0;

        if (pt >= outfit::kCustomPartsTypeStart && pt <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry)
            {
                return entry->HasHeadOptionsForVariant(
                           livePT, outfit::GetActiveVariant(pt)) ? 1 : 0;
            }
        }
        if (pt < outfit::kCustomPartsTypeStart
            && outfit::VanillaExtHasAnyHeadOptions(
                   pt, livePT, outfit::ReadLiveSelectorCode()))
            return 1;

        return g_OrigIsEnableHeadOptionSuit(self, param2);
    }

    static std::uint8_t __fastcall hkIsEnableCurrentHeadOption(void* self)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return g_OrigIsEnableHead(self);

        if (IsFobSortieContext(self))
            return g_OrigIsEnableHead(self);

        const std::uint8_t pt     = outfit::ReadLivePartsType();
        const std::uint8_t livePT = outfit::ReadLivePlayerType();

        if (uniquecharpin::IsOwnSuitPartsType(livePT, pt))
            return 0;

        if (pt >= outfit::kCustomPartsTypeStart && pt <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry)
            {
                return entry->HasHeadOptionsForVariant(
                           livePT, outfit::GetActiveVariant(pt)) ? 1 : 0;
            }
        }
        if (pt < outfit::kCustomPartsTypeStart
            && outfit::VanillaExtHasAnyHeadOptions(
                   pt, livePT, outfit::ReadLiveSelectorCode()))
            return 1;

        return g_OrigIsEnableHead(self);
    }

    static std::uint8_t __fastcall hkIsEnableCurrentSuit(void* self)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return g_OrigIsEnableCurrentSuit(self);

        {
            const bool fobCtx = IsFobSortieContext(self);
            if (fobCtx)
            {
                if (!g_OrigMenuGetLangText)
                    EnsureMenuLangHookSEH(self);
                if (FobLoadoutHasBannedManagedEquip(self))
                {
                    g_FobBlockVfwOnly.store(
                        g_OrigIsEnableCurrentSuit(self) != 0,
                        std::memory_order_relaxed);
                    return 0;
                }
                std::uint16_t customFaceId = 0;
                if (outfit::IsLocalPlayerWearingCustomFace(customFaceId))
                {
                    static std::atomic<std::uint16_t> s_lastBlockedFace{ 0 };
                    if (s_lastBlockedFace.exchange(
                            customFaceId, std::memory_order_relaxed)
                        != customFaceId)
                        LogWarn("FobGuard: deploy blocked, character wears custom face %u",
                            static_cast<unsigned>(customFaceId));
                    g_FobBlockVfwOnly.store(
                        g_OrigIsEnableCurrentSuit(self) != 0,
                        std::memory_order_relaxed);
                    return 0;
                }
                const std::uint16_t wornHeadEquipId =
                    outfit::GetCurrentWornHeadEquipId();
                const std::uint8_t headPlayer = outfit::ReadLivePlayerType();
                const std::uint8_t headParts  = outfit::ReadLivePartsType();
                const outfit::OutfitEntry* headOutfit = nullptr;
                const bool headWorn =
                    wornHeadEquipId != 0
                    && ((headParts >= outfit::kCustomPartsTypeStart
                         && headParts <= outfit::kCustomPartsTypeEnd
                         && outfit::TryGetOutfitByPartsType(headParts, &headOutfit)
                         && headOutfit
                         && headOutfit->HasHeadOptionAnyVariant(wornHeadEquipId,
                                                                headPlayer))
                        || (headParts < outfit::kCustomPartsTypeStart
                            && outfit::VanillaExtHasHeadOption(
                                   headParts, wornHeadEquipId, headPlayer,
                                   outfit::ReadLiveSelectorCode())));
                if (headWorn)
                {
                    static std::atomic<std::uint16_t> s_lastBlockedHead{ 0 };
                    if (s_lastBlockedHead.exchange(
                            wornHeadEquipId, std::memory_order_relaxed)
                        != wornHeadEquipId)
                        LogWarn("FobGuard: deploy blocked, custom head option equipId %u worn",
                            static_cast<unsigned>(wornHeadEquipId));
                    g_FobBlockVfwOnly.store(
                        g_OrigIsEnableCurrentSuit(self) != 0,
                        std::memory_order_relaxed);
                    return 0;
                }
                const std::uint8_t wornPt = outfit::ReadLivePartsType();
                std::uint8_t defOwnerPt = 0;
                const bool wornIsOwnDefault =
                    uniquedefaultoutfit::IsDefaultOutfitPartsType(
                        wornPt, &defOwnerPt)
                    && outfit::ReadLivePlayerType() == defOwnerPt;
                if (wornIsOwnDefault)
                {
                    g_FobBlockVfwOnly.store(false, std::memory_order_relaxed);
                    return 1;
                }
                if (!wornIsOwnDefault
                    && wornPt >= outfit::kCustomPartsTypeStart
                    && wornPt <= outfit::kCustomPartsTypeEnd)
                {
                    static std::atomic<std::uint8_t> s_lastBlockedPt{ 0 };
                    if (s_lastBlockedPt.exchange(
                            wornPt, std::memory_order_relaxed) != wornPt)
                        LogWarn("FobGuard: deploy blocked, custom outfit partsType 0x%02X worn",
                            static_cast<unsigned>(wornPt));
                    g_FobBlockVfwOnly.store(
                        g_OrigIsEnableCurrentSuit(self) != 0,
                        std::memory_order_relaxed);
                    return 0;
                }
                if (wornPt < outfit::kCustomPartsTypeStart
                    && outfit::GetActiveVariant(wornPt) != 0)
                {
                    static std::atomic<std::uint8_t> s_lastBlockedVariantPt{ 0 };
                    if (s_lastBlockedVariantPt.exchange(
                            wornPt, std::memory_order_relaxed) != wornPt)
                        LogWarn("FobGuard: deploy blocked, extended variant active on partsType 0x%02X",
                            static_cast<unsigned>(wornPt));
                    g_FobBlockVfwOnly.store(
                        g_OrigIsEnableCurrentSuit(self) != 0,
                        std::memory_order_relaxed);
                    return 0;
                }
                g_FobBlockVfwOnly.store(false, std::memory_order_relaxed);
                return g_OrigIsEnableCurrentSuit(self);
            }
        }

        const std::uint8_t pt     = outfit::ReadLivePartsType();
        const std::uint8_t livePT = outfit::ReadLivePlayerType();

        if (pt >= outfit::kCustomPartsTypeStart && pt <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry)
            {
                return entry->HasHeadOptionsForVariant(
                           livePT, outfit::GetActiveVariant(pt)) ? 1 : 0;
            }
        }
        if (pt < outfit::kCustomPartsTypeStart
            && outfit::VanillaExtHasAnyHeadOptions(
                   pt, livePT, outfit::ReadLiveSelectorCode()))
            return 1;

        return g_OrigIsEnableCurrentSuit(self);
    }
}

namespace outfit
{
    bool Install_OutfitHeadOption_Hook()
    {
        if (g_Installed) return true;

        void* target = ResolveGameAddress(gAddr.MissionPrep_IsEnableCurrentHeadOption);
        if (target)
            g_Installed = CreateAndEnableHook(
                target,
                reinterpret_cast<void*>(&hkIsEnableCurrentHeadOption),
                reinterpret_cast<void**>(&g_OrigIsEnableHead));
        if (!g_Installed)
            LogError("OutfitHeadOption: IsEnableCurrentHeadOption hook failed, head option menu stays vanilla");

        void* suitTarget = ResolveGameAddress(gAddr.IsEnableCurrentSuit);
        if (suitTarget)
            g_IsEnableCurrentSuitInstalled = CreateAndEnableHook(
                suitTarget,
                reinterpret_cast<void*>(&hkIsEnableCurrentSuit),
                reinterpret_cast<void**>(&g_OrigIsEnableCurrentSuit));
        if (!g_IsEnableCurrentSuitInstalled)
            LogError("OutfitHeadOption: IsEnableCurrentSuit hook failed, suit gate and FOB deploy guard off");

        void* hosTarget = ResolveGameAddress(
            gAddr.MissionPrepSystem_IsEnableHeadOptionSuit);
        if (hosTarget)
            g_IsEnableHeadOptionSuitInstalled = CreateAndEnableHook(
                hosTarget,
                reinterpret_cast<void*>(&hkIsEnableHeadOptionSuit),
                reinterpret_cast<void**>(&g_OrigIsEnableHeadOptionSuit));
        if (!g_IsEnableHeadOptionSuitInstalled)
            LogError("OutfitHeadOption: IsEnableHeadOptionSuit hook failed, head options stay vanilla");

        void* cfTarget = ResolveGameAddress(gAddr.Player_ConverFaceIdWithFaceEquipId);
        if (cfTarget)
            g_ConverFaceIdInstalled = CreateAndEnableHook(
                cfTarget,
                reinterpret_cast<void*>(&hkConverFaceIdWithFaceEquipId),
                reinterpret_cast<void**>(&g_OrigConverFaceId));
        if (!g_ConverFaceIdInstalled)
            LogError("OutfitHeadOption: ConverFaceIdWithFaceEquipId hook failed, custom head faces off");

        void* heTarget =
            ResolveGameAddress(gAddr.RealizedSoldier2Impl_ConvertHeadEquipModelType);
        if (heTarget)
            g_ConvertHeadEquipInstalled = CreateAndEnableHook(
                heTarget,
                reinterpret_cast<void*>(&hkConvertHeadEquipModelType),
                reinterpret_cast<void**>(&g_OrigConvertHeadEquip));
        if (!g_ConvertHeadEquipInstalled)
            LogError("OutfitHeadOption: ConvertHeadEquipModelType hook failed, custom head gate off");

        return g_ConverFaceIdInstalled || g_Installed
            || g_IsEnableCurrentSuitInstalled || g_IsEnableHeadOptionSuitInstalled
            || g_ConvertHeadEquipInstalled;
    }

    void Uninstall_OutfitHeadOption_Hook()
    {
        if (g_ConvertHeadEquipInstalled)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.RealizedSoldier2Impl_ConvertHeadEquipModelType))
                DisableAndRemoveHook(t);
            g_OrigConvertHeadEquip      = nullptr;
            g_ConvertHeadEquipInstalled = false;
        }

        if (g_ConverFaceIdInstalled)
        {
            if (void* t = ResolveGameAddress(gAddr.Player_ConverFaceIdWithFaceEquipId))
                DisableAndRemoveHook(t);
            g_OrigConverFaceId      = nullptr;
            g_ConverFaceIdInstalled = false;
        }

        if (g_IsEnableHeadOptionSuitInstalled)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.MissionPrepSystem_IsEnableHeadOptionSuit))
                DisableAndRemoveHook(t);
            g_OrigIsEnableHeadOptionSuit      = nullptr;
            g_IsEnableHeadOptionSuitInstalled = false;
        }

        if (g_IsEnableCurrentSuitInstalled)
        {
            if (void* t = ResolveGameAddress(gAddr.IsEnableCurrentSuit))
                DisableAndRemoveHook(t);
            g_OrigIsEnableCurrentSuit       = nullptr;
            g_IsEnableCurrentSuitInstalled  = false;
        }

        if (g_MenuGetLangTextTarget)
        {
            DisableAndRemoveHook(g_MenuGetLangTextTarget);
            g_MenuGetLangTextTarget = nullptr;
            g_OrigMenuGetLangText   = nullptr;
        }

        if (!g_Installed) return;
        if (void* t = ResolveGameAddress(gAddr.MissionPrep_IsEnableCurrentHeadOption))
            DisableAndRemoveHook(t);
        g_OrigIsEnableHead = nullptr;
        g_Installed        = false;
    }
}
