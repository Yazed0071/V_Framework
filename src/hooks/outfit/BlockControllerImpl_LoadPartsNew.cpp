#include "pch.h"

#include "BlockControllerImpl_LoadPartsNew.h"
#include "OutfitRegistry.h"
#include "CustomHeadRegistry.h"
#include "FaceIdRegistry.h"
#include "StaffControllerImpl_AddStaff.h"
#include "Player2UtilityImpl_SetSuitAndHandConditionWithLoadoutInfo.h"
#include "ShadowState.h"
#include "FoxPathInternal.h"
#include "MissionCodeGuard.h"
#include "../equip/EquipDevelop_AddToEquipDevelopTable.h"
#include "../equip/EquipDevelop_SetEquipUndeveloped.h"
#include "../player/FobPlayerCharacters.h"
#include "UniqueCharacterPartsTypePin.h"
#include "AdditionalMotionTable_GetMtarPathId.h"
#include "PlayerAppearance_LoadPlayerFv2s.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <intrin.h>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "../player/PlayerAttachInDemo.h"

static inline bool V_WriteOutfitPartsLoad(std::uint8_t p, std::uint8_t c,
                                  std::uint8_t pt)
{
    return outfit::WriteLivePlayerOutfit(
        p, c, pt, outfit::OutfitWriteSource::PartsLoad);
}

#pragma intrinsic(_ReturnAddress)

namespace
{
    using FoxPath_Path_t = void* (__fastcall*)(void* outPath, std::uint64_t code64ext);

    using LoadPlayerPartsParts_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t playerType, std::uint32_t playerPartsType);
    using LoadPlayerPartsFpk_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t playerType, std::uint32_t playerPartsType);
    using LoadPlayerCamoFpk_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerCamoType);
    using LoadPlayerSnakeBlackDiamondFpk_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t applyBlackDiamond);
    using LoadPlayerBionicArm_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerHandType);
    using LoadPlayerSnakeFace_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerFaceId, char playerFaceEquipId);
    using LoadAvatarFace_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t avatarFaceA, std::uint32_t avatarFaceB);
    using LoadAvatarHeadOption_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t faceEquipId);
    using AvatarFaceEditUpdate_t = void (__fastcall*)(
        void* self, void* blockGroup, std::uint32_t blockIndex);

    struct LoadPartsPlayerInfo
    {
        std::uint8_t  playerType;
        std::uint8_t  playerPartsType;
        std::uint8_t  playerCamoType;
        std::uint8_t  playerArmType;
        std::int16_t  playerFaceId;
        std::uint8_t  playerFaceEquipId;
        std::uint8_t  reserved07;
        std::uint8_t  reserved08[0x4C];
        std::uint8_t  reserved54;
        std::uint8_t  reserved55;
        std::uint8_t  reserved56;
        std::uint8_t  reserved57;
    };
    static_assert(sizeof(LoadPartsPlayerInfo) == 0x58,
                  "LoadPartsPlayerInfo size must match retail layout");

    using LoadPartsNew_t           = void (__fastcall*)(void*, std::uint32_t, LoadPartsPlayerInfo*, std::uint32_t);
    using DoesNeedFaceFova_t       = std::uint8_t (__fastcall*)(std::uint32_t);
    using SetHandSlotEnabled_t     = void (__fastcall*)(void*, std::uint32_t, std::uint8_t);
    using IsArtificialHandEnabled_t            = std::uint8_t (__fastcall*)(std::uint32_t, std::uint32_t);
    using IsArtificialHandEnabledForCurrent_t  = std::uint8_t (__fastcall*)();
    using ProcessSignal_t          = void (__fastcall*)(void*, void*, std::uint32_t, std::uint64_t*);
    using UpdatePartsStatus_t      = void (__fastcall*)(void*);
    using Player2ImplSetUpParts_t  = bool (__fastcall*)(void*, std::uint32_t, std::uint32_t,
                                                       std::uint32_t, std::uint32_t, std::uint32_t,
                                                       std::uint32_t, void*);
    using PluginFacialApplyMotion_t = void (__fastcall*)(void*, void*, void*, float);
    using GetPartsTypeAtCamoType_t  = std::uint32_t (__fastcall*)(void*, std::uint32_t);

    constexpr std::uint64_t kSignalRefreshFv2s             = 0x8483a342fa61ull;
    constexpr std::size_t   kP2GO_OffPerPlayerStruct       = 0x80;
    constexpr std::size_t   kP2GO_OffStateMachinePtr       = 0xb0;
    constexpr std::size_t   kP2GO_OffSlotCount             = 0x228;
    constexpr std::size_t   kP2GO_OffLocalPlayerSlot       = 0x234;
    constexpr std::size_t   kPP_OffPlayerTypeArr           = 0x40;
    constexpr std::size_t   kPP_OffPartsTypeArr            = 0x48;
    constexpr std::size_t   kPP_OffCamoTypeArr             = 0x50;
    constexpr std::size_t   kPP_OffArmTypeArr              = 0x58;
    constexpr std::size_t   kPP_OffFaceEquipArr            = 0x68;
    constexpr std::size_t   kPP_OffStateChangedBits        = 0x180;
    constexpr std::size_t   kPP_OffVariantChangedBits      = 0x184;
    constexpr std::size_t   kPP_OffAltStateBits            = 0x184;
    constexpr std::size_t   kPP_OffLoadoutReq              = 0xc0;
    constexpr std::size_t   kPP_LoadoutReqStride           = 0x3a;
    constexpr std::size_t   kPP_LoadoutReqEquipHashOff     = 0x8;
    constexpr std::uint8_t  kProcessSignalSpoofPartsType   = 0x01;
    constexpr std::uint32_t kBionicArmVanillaPartsTypeSubstitute = 0x01;

    static FoxPath_Path_t                       g_FoxPath_Path                       = nullptr;
    static LoadPlayerPartsParts_t               g_OrigLoadPartsParts                 = nullptr;
    static LoadPlayerPartsFpk_t                 g_OrigLoadPartsFpk                   = nullptr;
    static LoadPlayerCamoFpk_t                  g_OrigLoadCamoFpk                    = nullptr;
    static LoadPlayerSnakeBlackDiamondFpk_t     g_OrigLoadDiamondFpk                 = nullptr;
    static LoadPlayerCamoFpk_t                  g_OrigLoadCamoFv2                    = nullptr;
    static LoadPlayerSnakeBlackDiamondFpk_t     g_OrigLoadDiamondFv2                 = nullptr;
    static LoadPlayerBionicArm_t                g_OrigLoadBionicArmFv2               = nullptr;
    static LoadPlayerBionicArm_t                g_OrigLoadBionicArmFpk               = nullptr;
    static LoadPlayerSnakeFace_t                g_OrigLoadSnakeFaceFv2               = nullptr;
    static LoadPlayerSnakeFace_t                g_OrigLoadSnakeFaceFpk               = nullptr;
    static LoadAvatarFace_t                     g_OrigLoadAvatarFaceFv2              = nullptr;
    static LoadAvatarFace_t                     g_OrigLoadAvatarFaceFpk              = nullptr;
    static LoadAvatarHeadOption_t               g_OrigLoadAvatarHeadOptionFv2        = nullptr;
    static LoadAvatarHeadOption_t               g_OrigLoadAvatarHeadOptionFpk        = nullptr;
    static AvatarFaceEditUpdate_t               g_OrigAvatarFaceEditUpdate           = nullptr;
    static LoadPartsNew_t                       g_OrigLoadPartsNew                   = nullptr;
    static DoesNeedFaceFova_t                   g_OrigDoesNeedFaceFova               = nullptr;
    static DoesNeedFaceFova_t                   g_OrigDoesNeedFaceFovaForAvatar      = nullptr;
    static SetHandSlotEnabled_t                 g_OrigSetHandSlotEnabled             = nullptr;
    static IsArtificialHandEnabled_t            g_OrigIsArtificialHandEnabled        = nullptr;
    static IsArtificialHandEnabledForCurrent_t  g_OrigIsArtificialHandForCurrent     = nullptr;
    static ProcessSignal_t                      g_OrigProcessSignal                  = nullptr;
    static UpdatePartsStatus_t                  g_OrigUpdatePartsStatus              = nullptr;
    static Player2ImplSetUpParts_t              g_OrigPlayer2ImplSetUpParts          = nullptr;
    static PluginFacialApplyMotion_t            g_OrigPluginFacialApplyMotion        = nullptr;
    static GetPartsTypeAtCamoType_t             g_OrigGetPartsTypeAtCamoType         = nullptr;

    static bool g_InstalledParts                 = false;
    static bool g_InstalledFpk                   = false;
    static bool g_InstalledCamo                  = false;
    static bool g_InstalledDiamond               = false;
    static bool g_InstalledCamoFv2               = false;
    static bool g_InstalledDiamondFv2            = false;
    static bool g_InstalledBionicArmFv2          = false;
    static bool g_InstalledBionicArmFpk          = false;
    static bool g_InstalledSnakeFaceFv2          = false;
    static bool g_InstalledSnakeFaceFpk          = false;
    static bool g_InstalledAvatarFaceFv2         = false;
    static bool g_InstalledAvatarFaceFpk         = false;
    static bool g_InstalledAvatarHeadOptionFv2   = false;
    static bool g_InstalledAvatarHeadOptionFpk   = false;
    static bool g_InstalledAvatarFaceEdit        = false;
    static bool g_InstalledLpn                   = false;
    static bool g_InstalledDoesNeedFace          = false;
    static bool g_InstalledDoesNeedFaceForAvatar = false;
    static bool g_InstalledSetHandSlotEnabled    = false;
    static bool g_InstalledIsArtificialHand      = false;
    static bool g_InstalledIsArtHandForCurrent   = false;
    static bool g_InstalledProcessSignal         = false;
    static bool g_InstalledUpdatePartsStatus     = false;
    static bool g_InstalledPlayer2ImplSetUpParts = false;
    static bool g_InstalledFacialCrashGuard      = false;
    static bool g_InstalledPartsAtCamo           = false;

    static void* g_CapturedBlockController = nullptr;
    static std::atomic<std::uint32_t> g_LocalPartsSlot{ 0 };

    static std::atomic<std::uint8_t>  g_RealizedLocalHeadSlot{ 0 };
    static std::atomic<std::uint32_t> g_DroppedLocalHeadKey{ 0 };
    static std::atomic<std::uint32_t> g_HeadReloadLatchKey{ 0 };

    static thread_local std::uint8_t t_ActiveCustomFaceSlot = 0;

    static std::atomic<std::uint8_t> g_LastCustomFaceSlot{ 0 };

    static std::atomic<void*>         g_AvatarHideBc{ nullptr };
    static std::atomic<std::uint32_t> g_AvatarHideSlotMask{ 0 };

    static const outfit::CustomHeadEntry* ResolveAvatarCustomHead();

    static bool ResolveImplBcAndSlot(void* outerSelf, std::uint32_t outerSlot,
                                     void** outImpl, std::uint32_t* outSlot)
    {
        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(outerSelf);
            void* impl = *reinterpret_cast<void**>(base + 0x10);
            if (!impl) return false;
            const std::uint32_t pivot =
                *reinterpret_cast<std::uint32_t*>(base + 0x18);
            std::uint32_t implSlot = outerSlot;
            if (outerSlot == pivot)     implSlot = 0;
            else if (outerSlot < pivot) implSlot = outerSlot + 1;
            *outImpl = impl;
            *outSlot = implSlot;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool BuildLocalStaffFaceInfo(const LoadPartsPlayerInfo& source,
                                        std::uint32_t implSlot,
                                        LoadPartsPlayerInfo& visualInfo)
    {
        if (implSlot != 0 || MissionCodeGuard::ShouldBypassHooks()
            || !outfit::IsDdPlayerType(source.playerType)
            || !outfit::ddheadlayers::IsReady())
            return false;
        std::uint16_t visualFaceId = 0;
        if (!outfit::TryResolveLocalPlayerStaffFace(
                static_cast<std::uint16_t>(source.playerFaceId), visualFaceId)
            || visualFaceId == static_cast<std::uint16_t>(source.playerFaceId))
            return false;
        visualInfo = source;
        visualInfo.playerFaceId = static_cast<std::int16_t>(visualFaceId);
        return true;
    }

    static bool         g_CaseDArmUnpinActive = false;
    static void*        g_CaseDTrampoline     = nullptr;
    static void*        g_CaseDPatchSite      = nullptr;
    static std::uint8_t g_CaseDOrigBytes[9]   = {};
    static std::uint8_t* g_CaseDArmTable = nullptr;

    static void RefreshCaseDArmFlag(std::uint8_t partsType, bool armEnabled)
    {
        if (g_CaseDArmTable
            && partsType >= outfit::kCustomPartsTypeStart
            && partsType <= outfit::kCustomPartsTypeEnd)
            g_CaseDArmTable[partsType - outfit::kCustomPartsTypeStart] =
                armEnabled ? std::uint8_t{1} : std::uint8_t{0};
    }

    static void* AllocExecNear(std::uintptr_t nearAddr, std::size_t size)
    {
        if (void* arena = HookArena::AllocateNear(nearAddr, size))
            return arena;
        SYSTEM_INFO si{}; GetSystemInfo(&si);
        const std::uintptr_t gran    = si.dwAllocationGranularity;
        const std::uintptr_t rounded = nearAddr & ~(gran - 1);
        for (std::uintptr_t off = gran; off < 0x60000000ull; off += gran)
        {
            if (rounded >= off)
                if (void* p = VirtualAlloc(reinterpret_cast<LPVOID>(rounded - off),
                        size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))
                    return p;
            if (void* p2 = VirtualAlloc(reinterpret_cast<LPVOID>(rounded + off),
                    size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))
                return p2;
        }
        return nullptr;
    }

    static bool InstallCaseDArmUnpin()
    {
        if (g_CaseDArmUnpinActive) return true;
        const std::uintptr_t ups = reinterpret_cast<std::uintptr_t>(
            ResolveGameAddress(gAddr.UpdatePartsStatus));
        if (!ups) { LogError("CaseDArmUnpin: UpdatePartsStatus address missing, arm-type fallback used"); return false; }

        const std::uintptr_t site   = ups + 0xc77;
        const std::uintptr_t caseD3 = ups + 0xcfe;
        const std::uintptr_t resume = ups + 0xc80;
        static const std::uint8_t kExpect[9] =
            { 0x40,0x0f,0xb6,0xc7, 0x83,0xf8,0x19, 0x77,0x7e };
        bool ok = false;
        __try { ok = (std::memcmp(reinterpret_cast<void*>(site), kExpect, 9) == 0); }
        __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
        if (!ok)
        { LogError("CaseDArmUnpin: site bytes mismatch at %p, arm-type fallback used",
              reinterpret_cast<void*>(site)); return false; }

        constexpr std::size_t kTableOff  = 0x80;
        constexpr std::size_t kBandWidth =
            static_cast<std::size_t>(outfit::kCustomPartsTypeEnd)
          - static_cast<std::size_t>(outfit::kCustomPartsTypeStart) + 1;
        constexpr std::size_t kTrampSize = 0x200;
        static_assert(outfit::kCustomPartsTypeStart <= 0x80,
            "band start must fit the lea disp8 encoding");
        static_assert(kTableOff + kBandWidth <= kTrampSize,
            "arm table overflows the trampoline allocation");

        std::uint8_t* tr =
            reinterpret_cast<std::uint8_t*>(AllocExecNear(site, kTrampSize));
        if (!tr) { LogError("CaseDArmUnpin: trampoline alloc failed, arm-type fallback used"); return false; }
        std::memset(tr, 0xCC, kTrampSize);

        std::memset(tr + kTableOff, 0x01, kBandWidth);

        std::size_t o = 0;
        const auto emit = [&](std::initializer_list<std::uint8_t> b)
        { for (std::uint8_t x : b) tr[o++] = x; };
        const auto emitRel32To = [&](std::uintptr_t target)
        { std::int32_t rel = static_cast<std::int32_t>(static_cast<std::int64_t>(target)
              - (reinterpret_cast<std::int64_t>(tr) + static_cast<std::int64_t>(o) + 4));
          std::memcpy(tr+o,&rel,4); o += 4; };
        const auto emitImm32 = [&](std::uint32_t v)
        { std::memcpy(tr+o,&v,4); o += 4; };
        emit({0x40,0x0f,0xb6,0xc7});
        emit({0x8d,0x48,
              static_cast<std::uint8_t>(0x100u - outfit::kCustomPartsTypeStart)});
        emit({0x81,0xf9});
        emitImm32(static_cast<std::uint32_t>(kBandWidth - 1));
        emit({0x77,0x15});
        emit({0x48,0x8d,0x05});
        emitRel32To(reinterpret_cast<std::uintptr_t>(tr) + kTableOff);
        emit({0x0f,0xb6,0x04,0x08});
        emit({0x84,0xc0});
        tr[o++]=0x0f; tr[o++]=0x84;
        emitRel32To(caseD3);
        emit({0xb0,0x01});
        emit({0x83,0xf8,0x19});
        tr[o++]=0x0f; tr[o++]=0x87;
        emitRel32To(caseD3);
        tr[o++]=0xe9;
        emitRel32To(resume);

        const std::int64_t jrel =
            reinterpret_cast<std::int64_t>(tr) - (static_cast<std::int64_t>(site) + 5);
        if (jrel < INT32_MIN || jrel > INT32_MAX)
        { LogError("CaseDArmUnpin: trampoline out of rel32 range, arm-type fallback used");
          VirtualFree(tr,0,MEM_RELEASE); return false; }

        std::uint8_t patch[9];
        patch[0]=0xe9; std::int32_t jr = static_cast<std::int32_t>(jrel);
        std::memcpy(patch+1,&jr,4);
        patch[5]=patch[6]=patch[7]=patch[8]=0x90;

        DWORD oldp=0;
        if (!VirtualProtect(reinterpret_cast<void*>(site),9,PAGE_EXECUTE_READWRITE,&oldp))
        { LogError("CaseDArmUnpin: VirtualProtect failed, arm-type fallback used");
          VirtualFree(tr,0,MEM_RELEASE); return false; }
        std::memcpy(g_CaseDOrigBytes, reinterpret_cast<void*>(site), 9);
        std::memcpy(reinterpret_cast<void*>(site), patch, 9);
        DWORD tmp=0; VirtualProtect(reinterpret_cast<void*>(site),9,oldp,&tmp);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site), 9);

        g_CaseDTrampoline     = tr;
        g_CaseDPatchSite      = reinterpret_cast<void*>(site);
        g_CaseDArmTable       = tr + kTableOff;
        g_CaseDArmUnpinActive = true;
        return true;
    }


    static bool ResolveFoxPathApi()
    {
        if (!g_FoxPath_Path)
        {
            g_FoxPath_Path = reinterpret_cast<FoxPath_Path_t>(
                ResolveGameAddress(gAddr.FoxPath_Path));
        }
        return g_FoxPath_Path != nullptr;
    }

    static std::uint64_t* WriteFoxPath(std::uint64_t* outPath, std::uint64_t code64ext)
    {
        if (!outPath || !ResolveFoxPathApi()) return outPath;
        g_FoxPath_Path(outPath, code64ext);
        return outPath;
    }

    static std::uint32_t EffectivePartsType(std::uint32_t paramPartsType)
    {
        if (outfit::shadow::HasCurrentSlot())
        {
            outfit::shadow::Slot s;
            if (outfit::shadow::Get(outfit::shadow::GetCurrentSlot(), &s))
                return s.realPartsType;
        }
        return paramPartsType;
    }

    static std::uint32_t EffectivePartsTypeFor(std::uint32_t playerType,
                                               std::uint32_t paramPartsType)
    {
        if (!outfit::shadow::HasCurrentSlot()) return paramPartsType;

        outfit::shadow::Slot s;
        if (!outfit::shadow::Get(outfit::shadow::GetCurrentSlot(), &s))
            return paramPartsType;

        if (s.used
         && s.realPlayerType != static_cast<std::uint8_t>(playerType & 0xFF))
        {
            static std::atomic<int> s_mismatch{ 0 };
            if (s_mismatch.fetch_add(1, std::memory_order_relaxed) < 8)
                LogWarn("OutfitRuntimeParts: parts slot holds playerType %u, engine asked %u, engine partsType kept",
                    static_cast<unsigned>(s.realPlayerType),
                    static_cast<unsigned>(playerType & 0xFF));
            return paramPartsType;
        }
        return s.realPartsType;
    }

    constexpr std::uint32_t kCamoDonorVanillaPartsType = 0x17;

    static bool UsesSnakeCamoDonor(std::uint32_t playerType,
                                   const outfit::OutfitEntry* entry)
    {
        if (!entry) return false;
        return outfit::IsUniqueCharacterPlayerType(
            static_cast<std::uint8_t>(playerType & 0xFF));
    }

    static std::uint32_t VanillaClampPartsType(std::uint32_t partsType)
    {
        const auto pt = static_cast<std::uint8_t>(partsType & 0xFF);
        if (pt >= outfit::kCustomPartsTypeStart && pt <= outfit::kCustomPartsTypeEnd)
            return kBionicArmVanillaPartsTypeSubstitute;
        return partsType;
    }

    static std::atomic<bool> g_AssetCheckTrusted{ false };

    static bool ShouldFallBackOnMissingAsset(std::uint64_t pathCode64)
    {
        if (pathCode64 == 0) return false;
        if ((pathCode64 >> 51) == 0) return true;
        if (fox::detail::PathExistsByCode(pathCode64))
        {
            g_AssetCheckTrusted.store(true, std::memory_order_relaxed);
            return false;
        }
        return g_AssetCheckTrusted.load(std::memory_order_relaxed);
    }

    static bool ResolveCustomEntry(std::uint32_t playerType, std::uint32_t playerPartsType,
                                   const outfit::OutfitEntry** outEntry)
    {
        const auto pt  = static_cast<std::uint8_t>(playerPartsType & 0xFF);
        const auto ply = static_cast<std::uint8_t>(playerType & 0xFF);

        if (pt < outfit::kCustomPartsTypeStart || pt > outfit::kCustomPartsTypeEnd)
            return false;

        const outfit::OutfitEntry* entry = nullptr;
        if (!outfit::TryGetOutfitByPartsType(pt, &entry) || !entry) return false;
        if (!entry->IsPlayerTypeSupported(ply)) return false;

        if (!entry->DeclaresPlayerType(ply))
        {
            if (!outfit::DeclaresBranchInPartsGroupOf(*entry, ply))
            {
                static std::atomic<int> refused{ 0 };
                if (refused.fetch_add(1, std::memory_order_relaxed) < 8)
                    LogWarn("OutfitRuntimeParts: outfit developId=%u partsType=0x%02X has no branch for playerType %u, slot reverted to vanilla",
                        static_cast<unsigned>(entry->developId),
                        static_cast<unsigned>(entry->partsType),
                        static_cast<unsigned>(ply));
                return false;
            }
        }

        if (outEntry) *outEntry = entry;
        return true;
    }

    static std::uint8_t TranslateEquipHashToArmTier(std::uint16_t equipHash)
    {
        switch (equipHash)
        {
        case 0x203: return 2;
        case 0x204: return 3;
        case 0x205: return 4;
        case 0x206: return 5;
        case 0x208: return 6;
        case 0x209: return 7;
        default:    return 1;
        }
    }

    static std::uint8_t ReadLiveArmTierFromLoadoutRequest(void* p2go, std::size_t slot)
    {
        if (!p2go) return 0;
        std::uint8_t result = 0;
        __try
        {
            void* perPlayer = *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(p2go) + kP2GO_OffPerPlayerStruct);
            if (!perPlayer) return 0;

            void* loadoutReqArr = *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffLoadoutReq);
            if (!loadoutReqArr) return 0;

            std::uint16_t equipHash = *reinterpret_cast<std::uint16_t*>(
                reinterpret_cast<std::uint8_t*>(loadoutReqArr)
                + slot * kPP_LoadoutReqStride
                + kPP_LoadoutReqEquipHashOff);
            result = TranslateEquipHashToArmTier(equipHash);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { result = 0; }
        return result;
    }

    static std::uint8_t ResolveSlotPartsTypeByteFromShadow(std::size_t slot,
                                                          std::uint8_t fallback)
    {
        outfit::shadow::Slot s;
        if (outfit::shadow::Get(slot, &s)) return s.realPartsType;
        return fallback;
    }

    static std::atomic<std::uint8_t> g_VextServedPt[outfit::kPlayerTypeMax];
    static std::atomic<std::uint8_t> g_VextServedVar[outfit::kPlayerTypeMax];
    static std::atomic<std::uint8_t> g_UniqueServedParts[outfit::shadow::kMaxSlots] = {
        std::atomic<std::uint8_t>{0xFF}, std::atomic<std::uint8_t>{0xFF},
        std::atomic<std::uint8_t>{0xFF}, std::atomic<std::uint8_t>{0xFF}
    };

    constexpr int kRestreamMaxDeferTicks = 120;
    static std::atomic<int> g_RestreamPendingTicks{ 0 };

    static const outfit::VanillaSuitVariantAsset* ResolveVanillaExtActiveVariant(
        std::uint32_t playerType, std::uint32_t effectivePartsType)
    {
        const auto vpt = static_cast<std::uint8_t>(effectivePartsType & 0xFF);
        if (vpt >= outfit::kCustomPartsTypeStart) return nullptr;
        const std::uint8_t v = outfit::GetActiveVariant(vpt);
        if (v == 0) return nullptr;
        if (MissionCodeGuard::ShouldBypassHooks()) return nullptr;
        const auto pt = static_cast<std::uint8_t>(playerType & 0xFF);
        return outfit::VanillaExtGetVariantBridged(vpt, pt, v);
    }

    static std::uint64_t ResolveVanillaExtVariantParts(std::uint32_t playerType,
                                                       std::uint32_t effectivePartsType)
    {
        const auto* var =
            ResolveVanillaExtActiveVariant(playerType, effectivePartsType);
        return var ? var->partsPathCode64 : 0;
    }

    static std::uint64_t ResolveVanillaExtVariantFpk(std::uint32_t playerType,
                                                     std::uint32_t effectivePartsType)
    {
        const auto* var =
            ResolveVanillaExtActiveVariant(playerType, effectivePartsType);
        return var ? var->fpkPathCode64 : 0;
    }

    static const outfit::CustomHeadEntry* ResolveVanillaSuitCustomHead(
        std::uint8_t pt, std::uint32_t playerPartsType, std::uint8_t faceEquipId)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return nullptr;
        const outfit::CustomHeadEntry* head =
            outfit::TryGetCustomHeadBySlot(faceEquipId);
        if (!head)
            head = outfit::TryGetCustomHeadBySlot(t_ActiveCustomFaceSlot);
        if (!head)
            head = outfit::TryGetCustomHeadBySlot(outfit::GetWornCustomHeadSlot());
        if (!head || pt >= outfit::kPlayerTypeMax)
            return nullptr;
        if (!outfit::VanillaExtHasHeadOption(
                static_cast<std::uint8_t>(playerPartsType & 0xFF),
                head->equipId, pt, outfit::ReadLiveSelectorCode()))
            return nullptr;
        return head;
    }

    static std::uint8_t VanillaExtNeedsFaceFova(std::uint32_t effective)
    {
        if (effective >= outfit::kCustomPartsTypeStart)
            return 0;
        const auto livePT = outfit::ReadLivePlayerType();
        std::uint8_t askedSlot = t_ActiveCustomFaceSlot;
        if (askedSlot == 0)
        {
            const std::uint16_t worn = outfit::ReadLiveWornHeadCategory();
            if (worn != 0xFFFFu)
                askedSlot = static_cast<std::uint8_t>(worn & 0xFFu);
        }
        const outfit::CustomHeadEntry* head = ResolveVanillaSuitCustomHead(
            livePT, effective, askedSlot);
        return head ? std::uint8_t{1} : std::uint8_t{0};
    }


    static std::uint64_t* __fastcall hkLoadPlayerPartsParts(
        std::uint64_t* outPath, std::uint32_t playerType, std::uint32_t playerPartsType)
    {
        const std::uint32_t effectivePartsType =
            EffectivePartsTypeFor(playerType, playerPartsType);

        const outfit::OutfitEntry* entry = nullptr;
        const bool uniqueResolved =
            ResolveCustomEntry(playerType, effectivePartsType, &entry);

        if (uniqueResolved)
        {
            const auto pt = static_cast<std::uint8_t>(playerType & 0xFF);
            const std::uint8_t v = entry->HasVariants()
                ? outfit::GetActiveVariant(entry->partsType) : 0;
            const std::uint64_t path = entry->GetVariantPartsPath(pt, v);
            if (path != 0)
            {
                if (ShouldFallBackOnMissingAsset(path))
                {
                    static std::atomic<std::uint64_t> s_loggedMissingParts{ 0 };
                    if (s_loggedMissingParts.exchange(path) != path)
                        LogWarn("OutfitRuntimeParts: .parts 0x%016llX not installed (playerType %u), using vanilla",
                            static_cast<unsigned long long>(path),
                            static_cast<unsigned>(playerType));
                    return g_OrigLoadPartsParts(outPath, playerType,
                                                kBionicArmVanillaPartsTypeSubstitute);
                }
                return WriteFoxPath(outPath, path);
            }
        }
        if (const std::uint64_t path = ResolveVanillaExtVariantParts(
                playerType, effectivePartsType); path != 0)
        {
            const std::uint64_t siblingFpk =
                ResolveVanillaExtVariantFpk(playerType, effectivePartsType);
            const bool vfallback = ShouldFallBackOnMissingAsset(path)
                || (siblingFpk != 0
                    && (ShouldFallBackOnMissingAsset(siblingFpk)
                        || !fox::detail::PathExistsByCode(siblingFpk)));
            const bool vexists   = fox::detail::PathExistsByCode(path);
            if (!vfallback && vexists)
                return WriteFoxPath(outPath, path);
        }
        return g_OrigLoadPartsParts(outPath, playerType, VanillaClampPartsType(playerPartsType));
    }

    static std::uint64_t* __fastcall hkLoadPlayerPartsFpk(
        std::uint64_t* outPath, std::uint32_t playerType, std::uint32_t playerPartsType)
    {
        const std::uint32_t effectivePartsType =
            EffectivePartsTypeFor(playerType, playerPartsType);

        const outfit::OutfitEntry* entry = nullptr;
        const bool uniqueFpkResolved =
            ResolveCustomEntry(playerType, effectivePartsType, &entry);

        if (uniqueFpkResolved)
        {
            const auto pt = static_cast<std::uint8_t>(playerType & 0xFF);
            const std::uint8_t v = entry->HasVariants()
                ? outfit::GetActiveVariant(entry->partsType) : 0;
            const std::uint64_t path = entry->GetVariantFpkPath(pt, v);
            if (path != 0)
            {
                if (ShouldFallBackOnMissingAsset(path))
                {
                    static std::atomic<std::uint64_t> s_loggedMissingFpk{ 0 };
                    if (s_loggedMissingFpk.exchange(path) != path)
                        LogWarn("OutfitRuntimeParts: .fpk 0x%016llX not installed (playerType %u), using vanilla",
                            static_cast<unsigned long long>(path),
                            static_cast<unsigned>(playerType));
                    return g_OrigLoadPartsFpk(outPath, playerType,
                                              kBionicArmVanillaPartsTypeSubstitute);
                }
                return WriteFoxPath(outPath, path);
            }
        }
        if (effectivePartsType < outfit::kCustomPartsTypeStart
            && playerType < outfit::kPlayerTypeMax)
        {
            const auto servedIdx = static_cast<std::size_t>(playerType);
            const auto servedPt  = static_cast<std::uint8_t>(effectivePartsType & 0xFF);
            g_VextServedPt[servedIdx].store(servedPt, std::memory_order_relaxed);
            g_VextServedVar[servedIdx].store(outfit::GetActiveVariant(servedPt),
                                             std::memory_order_relaxed);
        }
        if (const std::uint64_t path = ResolveVanillaExtVariantFpk(
                playerType, effectivePartsType); path != 0)
        {
            const bool vfallback = ShouldFallBackOnMissingAsset(path);
            const bool vexists   = fox::detail::PathExistsByCode(path);
            if (!vfallback && vexists)
                return WriteFoxPath(outPath, path);
        }
        return g_OrigLoadPartsFpk(outPath, playerType, VanillaClampPartsType(playerPartsType));
    }

    static std::uint32_t ClampVanillaCamo(std::uint32_t camo)
    {
        if (camo < 0x75) return camo;
        static std::atomic<int> s_log{0};
        if (int n = s_log.load(std::memory_order_relaxed); n < 4)
        {
            s_log.store(n + 1, std::memory_order_relaxed);
            LogWarn("OutfitRuntimeParts: camo 0x%02X outside the vanilla camo table, clamped to 0",
                camo);
        }
        return 0;
    }

    static std::atomic<std::uint64_t> g_ServedColorFpk{ 0 };
    static std::atomic<std::uint64_t> g_ServedColorFv2{ 0 };
    static std::atomic<bool> g_ColorReloadPending{ false };
    static std::atomic<bool> g_RestreamRequested{ false };
    static std::atomic<std::uint32_t> g_ColorReloadSlots{ 0 };

    static bool FovaModelReadySEH(std::uint8_t* system)
    {
        __try
        {
            auto* impl = *reinterpret_cast<std::uint8_t**>(system + 0x8);
            if (!impl)
                return false;
            const std::uint32_t index = *reinterpret_cast<std::uint32_t*>(impl + 0x218);
            void* owner = *reinterpret_cast<void**>(impl + 0x58);
            if (!owner)
                return false;
            using GetFova2Controller_t = std::uint8_t* (__fastcall*)(void*);
            auto* controller = reinterpret_cast<GetFova2Controller_t>(
                (*reinterpret_cast<void***>(owner))[0x178 / sizeof(void*)])(owner);
            if (!controller)
                return false;
            const std::uint8_t first = *(controller + 0x68);
            auto* works = *reinterpret_cast<std::uint8_t**>(controller + 0x30);
            if (!works || index < first)
                return false;
            auto* work = works + static_cast<std::size_t>(index - first) * 0x160;
            auto* models = *reinterpret_cast<std::uint8_t**>(work + 0xC8);
            if (!models || *(models + 0x59) == 0)
                return false;
            auto** list = *reinterpret_cast<std::uint8_t***>(models + 0x48);
            std::uint8_t* model = list ? list[*(models + 0x6A)] : nullptr;
            return model && *reinterpret_cast<void**>(model + 0x10) != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static const outfit::OutfitColor* PackColorToServe(const outfit::OutfitEntry* entry,
                                                       std::uint8_t playerType, std::uint8_t variant)
    {
        const outfit::OutfitColor* color = outfit::GetPackColor(*entry, playerType, variant);
        if (!color || ShouldFallBackOnMissingAsset(color->fpkCode)
            || !fox::detail::PathExistsByCode(color->fpkCode))
            return nullptr;
        return color;
    }

    static std::uint64_t* __fastcall hkLoadPlayerCamoFpk(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerCamoType)
    {
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);

        const outfit::OutfitEntry* entry = nullptr;
        if (ResolveCustomEntry(playerType, effectivePartsType, &entry))
        {
            const auto pt = static_cast<std::uint8_t>(playerType & 0xFF);

            const std::uint8_t v = entry->HasVariants()
                ? outfit::GetActiveVariant(entry->partsType) : 0;
            if (const outfit::OutfitColor* color = PackColorToServe(entry, pt, v))
            {
                g_ServedColorFv2.store(color->fv2Code, std::memory_order_relaxed);
                if (g_ServedColorFpk.exchange(color->fpkCode, std::memory_order_acq_rel) != color->fpkCode)
                    LogInfo("OutfitColor: camo slot loads '%s'", color->fv2.c_str());
                return WriteFoxPath(outPath, color->fpkCode);
            }
            g_ServedColorFpk.store(0, std::memory_order_release);
            g_ServedColorFv2.store(0, std::memory_order_relaxed);
            const std::uint64_t camo = entry->GetVariantCamoFpk(pt, v);
            if (camo > outfit::kSubAssetUseVanilla)
            {
                if (ShouldFallBackOnMissingAsset(camo))
                    return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
                return WriteFoxPath(outPath, camo);
            }
            return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
        }

        if (playerCamoType >= outfit::kCustomSelectorStart
            && playerCamoType <= outfit::kCustomSelectorEnd)
        {
            std::uint8_t vselPt = 0, vselIdx = 0;
            if (outfit::TryGetVanillaExtByVariantSelector(
                    static_cast<std::uint8_t>(playerCamoType), &vselPt, &vselIdx)
                && vselPt == static_cast<std::uint8_t>(effectivePartsType & 0xFF))
            {
                outfit::SetActiveVariant(vselPt, vselIdx);
                if (const auto* var = outfit::VanillaExtGetVariant(
                        vselPt, static_cast<std::uint8_t>(playerType & 0xFF), vselIdx))
                {
                    if (var->camoFpk > outfit::kSubAssetUseVanilla
                        && !ShouldFallBackOnMissingAsset(var->camoFpk)
                        && fox::detail::PathExistsByCode(var->camoFpk))
                        return WriteFoxPath(outPath, var->camoFpk);
                    const std::uint8_t vBaseCamo =
                        outfit::VanillaExtGetVariantSourceCamo(vselPt, vselIdx);
                    if (vBaseCamo != 0xFF)
                        return g_OrigLoadCamoFpk(outPath, playerType,
                                                 VanillaClampPartsType(playerPartsType),
                                                 ClampVanillaCamo(vBaseCamo));
                }
            }
        }
        if (const outfit::VanillaSuitVariantAsset* var =
                ResolveVanillaExtActiveVariant(playerType, effectivePartsType))
        {
            if (var->camoFpk > outfit::kSubAssetUseVanilla
                && !ShouldFallBackOnMissingAsset(var->camoFpk)
                && fox::detail::PathExistsByCode(var->camoFpk))
                return WriteFoxPath(outPath, var->camoFpk);
            const auto vpt = static_cast<std::uint8_t>(effectivePartsType & 0xFF);
            const std::uint8_t src = outfit::VanillaExtGetVariantSourceCamo(
                vpt, outfit::GetActiveVariant(vpt));
            if (src != 0xFF)
                return g_OrigLoadCamoFpk(outPath, playerType,
                                         VanillaClampPartsType(playerPartsType),
                                         ClampVanillaCamo(src));
        }
        if (UsesSnakeCamoDonor(playerType, entry))
            return g_OrigLoadCamoFpk(outPath, outfit::kPlayerType_Snake,
                                     kCamoDonorVanillaPartsType,
                                     ClampVanillaCamo(playerCamoType));
        return g_OrigLoadCamoFpk(outPath, playerType, VanillaClampPartsType(playerPartsType),
                                 ClampVanillaCamo(playerCamoType));
    }

    static std::uint64_t* __fastcall hkLoadPlayerSnakeBlackDiamondFpk(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t applyBlackDiamond)
    {
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);

        const outfit::OutfitEntry* entry = nullptr;
        if (ResolveCustomEntry(playerType, effectivePartsType, &entry))
        {
            const auto pt = static_cast<std::uint8_t>(playerType & 0xFF);
            const std::uint8_t v = entry->HasVariants()
                ? outfit::GetActiveVariant(entry->partsType) : 0;
            const std::uint64_t diamond = entry->GetVariantDiamondFpk(pt, v);
            if (diamond == outfit::kSubAssetDisabled)
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
            if (diamond > outfit::kSubAssetUseVanilla)
            {
                if (ShouldFallBackOnMissingAsset(diamond))
                    return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
                return WriteFoxPath(outPath, diamond);
            }
        }
        if (const outfit::VanillaSuitVariantAsset* var =
                ResolveVanillaExtActiveVariant(playerType, effectivePartsType))
        {
            if (var->diamondFpk == outfit::kSubAssetDisabled)
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
            if (var->diamondFpk > outfit::kSubAssetUseVanilla
                && !ShouldFallBackOnMissingAsset(var->diamondFpk)
                && fox::detail::PathExistsByCode(var->diamondFpk))
                return WriteFoxPath(outPath, var->diamondFpk);
        }
        return g_OrigLoadDiamondFpk(outPath, playerType, VanillaClampPartsType(playerPartsType), applyBlackDiamond);
    }

    static std::uint64_t* __fastcall hkLoadPlayerCamoFv2(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerCamoType)
    {
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);

        const outfit::OutfitEntry* entry = nullptr;
        if (ResolveCustomEntry(playerType, effectivePartsType, &entry))
        {
            const auto pt = static_cast<std::uint8_t>(playerType & 0xFF);

            const std::uint8_t v = entry->HasVariants()
                ? outfit::GetActiveVariant(entry->partsType) : 0;
            if (g_ServedColorFpk.load(std::memory_order_acquire) != 0)   // this load's camo slot holds a colour package
            {
                if (const std::uint64_t fv2 = g_ServedColorFv2.load(std::memory_order_relaxed))
                    return WriteFoxPath(outPath, fv2);
            }
            const std::uint64_t camo = entry->GetVariantCamoFv2(pt, v);
            if (camo > outfit::kSubAssetUseVanilla)
            {
                const std::uint64_t camoPackage = entry->GetVariantCamoFpk(pt, v);
                if (ShouldFallBackOnMissingAsset(camo)
                    || (camoPackage > outfit::kSubAssetUseVanilla
                        && ShouldFallBackOnMissingAsset(camoPackage)))
                    return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
                return WriteFoxPath(outPath, camo);
            }
            if (camo == outfit::kSubAssetDisabled)
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
        }

        if (playerCamoType >= outfit::kCustomSelectorStart
            && playerCamoType <= outfit::kCustomSelectorEnd)
        {
            std::uint8_t vselPt = 0, vselIdx = 0;
            if (outfit::TryGetVanillaExtByVariantSelector(
                    static_cast<std::uint8_t>(playerCamoType), &vselPt, &vselIdx)
                && vselPt == static_cast<std::uint8_t>(effectivePartsType & 0xFF))
            {
                outfit::SetActiveVariant(vselPt, vselIdx);
                if (const auto* var = outfit::VanillaExtGetVariant(
                        vselPt, static_cast<std::uint8_t>(playerType & 0xFF), vselIdx))
                {
                    if (var->camoFv2 > outfit::kSubAssetUseVanilla
                        && !ShouldFallBackOnMissingAsset(var->camoFv2)
                        && fox::detail::PathExistsByCode(var->camoFv2)
                        && (var->camoFpk <= outfit::kSubAssetUseVanilla
                            || (!ShouldFallBackOnMissingAsset(var->camoFpk)
                                && fox::detail::PathExistsByCode(var->camoFpk))))
                        return WriteFoxPath(outPath, var->camoFv2);
                    const std::uint8_t vBaseCamo =
                        outfit::VanillaExtGetVariantSourceCamo(vselPt, vselIdx);
                    if (vBaseCamo != 0xFF)
                        return g_OrigLoadCamoFv2(outPath, playerType,
                                                 VanillaClampPartsType(playerPartsType),
                                                 ClampVanillaCamo(vBaseCamo));
                }
            }
        }
        if (const outfit::VanillaSuitVariantAsset* var =
                ResolveVanillaExtActiveVariant(playerType, effectivePartsType))
        {
            if (var->camoFv2 > outfit::kSubAssetUseVanilla
                && !ShouldFallBackOnMissingAsset(var->camoFv2)
                && fox::detail::PathExistsByCode(var->camoFv2)
                && (var->camoFpk <= outfit::kSubAssetUseVanilla
                    || (!ShouldFallBackOnMissingAsset(var->camoFpk)
                        && fox::detail::PathExistsByCode(var->camoFpk))))
                return WriteFoxPath(outPath, var->camoFv2);
            const auto vpt = static_cast<std::uint8_t>(effectivePartsType & 0xFF);
            const std::uint8_t src = outfit::VanillaExtGetVariantSourceCamo(
                vpt, outfit::GetActiveVariant(vpt));
            if (src != 0xFF)
                return g_OrigLoadCamoFv2(outPath, playerType,
                                         VanillaClampPartsType(playerPartsType),
                                         ClampVanillaCamo(src));
        }
        if (UsesSnakeCamoDonor(playerType, entry))
            return g_OrigLoadCamoFv2(outPath, outfit::kPlayerType_Snake,
                                     kCamoDonorVanillaPartsType,
                                     ClampVanillaCamo(playerCamoType));
        return g_OrigLoadCamoFv2(outPath, playerType, VanillaClampPartsType(playerPartsType),
                                 ClampVanillaCamo(playerCamoType));
    }

    static std::uint64_t* __fastcall hkLoadPlayerSnakeBlackDiamondFv2(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t applyBlackDiamond)
    {
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);

        const outfit::OutfitEntry* entry = nullptr;
        if (ResolveCustomEntry(playerType, effectivePartsType, &entry))
        {
            const auto pt = static_cast<std::uint8_t>(playerType & 0xFF);
            const std::uint8_t v = entry->HasVariants()
                ? outfit::GetActiveVariant(entry->partsType) : 0;
            const std::uint64_t diamond = entry->GetVariantDiamondFv2(pt, v);
            if (diamond == outfit::kSubAssetDisabled)
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
            if (diamond > outfit::kSubAssetUseVanilla)
            {
                const std::uint64_t diamondPackage = entry->GetVariantDiamondFpk(pt, v);
                if (ShouldFallBackOnMissingAsset(diamond)
                    || (diamondPackage > outfit::kSubAssetUseVanilla
                        && ShouldFallBackOnMissingAsset(diamondPackage)))
                    return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
                return WriteFoxPath(outPath, diamond);
            }
        }
        if (const outfit::VanillaSuitVariantAsset* var =
                ResolveVanillaExtActiveVariant(playerType, effectivePartsType))
        {
            if (var->diamondFv2 == outfit::kSubAssetDisabled)
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
            if (var->diamondFv2 > outfit::kSubAssetUseVanilla
                && !ShouldFallBackOnMissingAsset(var->diamondFv2)
                && fox::detail::PathExistsByCode(var->diamondFv2)
                && (var->diamondFpk <= outfit::kSubAssetUseVanilla
                    || (!ShouldFallBackOnMissingAsset(var->diamondFpk)
                        && fox::detail::PathExistsByCode(var->diamondFpk))))
                return WriteFoxPath(outPath, var->diamondFv2);
        }
        return g_OrigLoadDiamondFv2(outPath, playerType, VanillaClampPartsType(playerPartsType), applyBlackDiamond);
    }

    static std::uint32_t RecoverArmTierForLeaf(std::uint32_t playerType,
                                               std::uint32_t passedHandType)
    {
        if (passedHandType != 0) return passedHandType;
        bool captured = false;
        std::uint8_t cachedTier = outfit::shadow::GetArmTier(playerType, &captured);
        return captured ? static_cast<std::uint32_t>(cachedTier) : 1u;
    }

    static std::uint64_t* __fastcall hkLoadPlayerBionicArmFv2(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerHandType)
    {
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);
        const auto pt = static_cast<std::uint8_t>(playerType & 0xFF);

        const outfit::OutfitEntry* entry = nullptr;
        if (ResolveCustomEntry(playerType, effectivePartsType, &entry))
        {
            if (!entry->IsArmEnabled(pt))
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
            const std::uint32_t hand = RecoverArmTierForLeaf(playerType, playerHandType);
            return g_OrigLoadBionicArmFv2(outPath, playerType,
                                          kBionicArmVanillaPartsTypeSubstitute, hand);
        }
        if (static_cast<std::uint8_t>(effectivePartsType & 0xFF) < outfit::kCustomPartsTypeStart)
        {
            bool armEnable = true;
            if (outfit::VanillaExtGetSuitArm(
                    static_cast<std::uint8_t>(effectivePartsType & 0xFF),
                    pt, outfit::ReadLiveSelectorCode(), &armEnable)
                && !armEnable)
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
        }
        return g_OrigLoadBionicArmFv2(outPath, playerType, VanillaClampPartsType(playerPartsType), playerHandType);
    }

    static std::uint64_t* __fastcall hkLoadPlayerBionicArmFpk(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerHandType)
    {
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);
        const auto pt = static_cast<std::uint8_t>(playerType & 0xFF);

        const outfit::OutfitEntry* entry = nullptr;
        if (ResolveCustomEntry(playerType, effectivePartsType, &entry))
        {
            if (!entry->IsArmEnabled(pt))
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
            const std::uint32_t hand = RecoverArmTierForLeaf(playerType, playerHandType);
            return g_OrigLoadBionicArmFpk(outPath, playerType,
                                          kBionicArmVanillaPartsTypeSubstitute, hand);
        }
        if (static_cast<std::uint8_t>(effectivePartsType & 0xFF) < outfit::kCustomPartsTypeStart)
        {
            bool armEnable = true;
            if (outfit::VanillaExtGetSuitArm(
                    static_cast<std::uint8_t>(effectivePartsType & 0xFF),
                    pt, outfit::ReadLiveSelectorCode(), &armEnable)
                && !armEnable)
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
        }
        return g_OrigLoadBionicArmFpk(outPath, playerType, VanillaClampPartsType(playerPartsType), playerHandType);
    }

    static std::uint64_t* __fastcall hkLoadPlayerSnakeFaceFv2(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerFaceId, char playerFaceEquipId)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return g_OrigLoadSnakeFaceFv2(outPath, playerType,
                                          VanillaClampPartsType(playerPartsType),
                                          playerFaceId, playerFaceEquipId);
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);
        const auto pt = static_cast<std::uint8_t>(playerType & 0xFF);
        const outfit::OutfitEntry* entry = nullptr;
        if (ResolveCustomEntry(playerType, effectivePartsType, &entry))
        {
            if (!entry->IsHeadEnabled(pt))
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);

            const outfit::CustomHeadEntry* head =
                outfit::TryGetCustomHeadBySlot(static_cast<std::uint8_t>(playerFaceEquipId));
            if (!head)
                head = outfit::TryGetCustomHeadBySlot(t_ActiveCustomFaceSlot);
            if (!head)
                head = outfit::TryGetCustomHeadBySlot(outfit::GetWornCustomHeadSlot());
            if (head && (!entry->IsFaceEnabled(pt)
                         || !entry->HasHeadOptionAnyVariant(head->equipId, pt)))
                head = nullptr;

            if (head && pt < outfit::kPlayerTypeMax)
            {
                std::uint64_t code = head->faceFv2Code[pt];
                if (pt == outfit::kPlayerType_Snake)
                    if (const std::uint64_t st = outfit::GetCustomHeadSnakeStageFv2(
                            head->name, playerFaceId); st != 0)
                        code = st;
                if (code != 0)
                    return WriteFoxPath(outPath, code);
            }

            return g_OrigLoadSnakeFaceFv2(outPath, playerType,
                                          kBionicArmVanillaPartsTypeSubstitute,
                                          playerFaceId, playerFaceEquipId);
        }
        if (static_cast<std::uint8_t>(effectivePartsType & 0xFF) < outfit::kCustomPartsTypeStart)
        {
            bool headEnable = true;
            if (outfit::VanillaExtGetSuitHead(
                    static_cast<std::uint8_t>(effectivePartsType & 0xFF),
                    pt, outfit::ReadLiveSelectorCode(), &headEnable)
                && !headEnable)
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
        }
        const outfit::CustomHeadEntry* vextHead = ResolveVanillaSuitCustomHead(
            pt, playerPartsType, static_cast<std::uint8_t>(playerFaceEquipId));
        if (const outfit::CustomHeadEntry* head = vextHead)
        {
            std::uint64_t code = head->faceFv2Code[pt];
            if (pt == outfit::kPlayerType_Snake)
                if (const std::uint64_t st = outfit::GetCustomHeadSnakeStageFv2(
                        head->name, playerFaceId); st != 0)
                    code = st;
            if (code != 0)
            {
                std::uint64_t siblingFpk = head->faceFpkCode[pt];
                if (pt == outfit::kPlayerType_Snake)
                    if (const std::uint64_t st = outfit::GetCustomHeadSnakeStageFpk(
                            head->name, playerFaceId); st != 0)
                        siblingFpk = st;
                if (!ShouldFallBackOnMissingAsset(code)
                    && fox::detail::PathExistsByCode(code)
                    && (siblingFpk == 0
                        || (!ShouldFallBackOnMissingAsset(siblingFpk)
                            && fox::detail::PathExistsByCode(siblingFpk))))
                    return WriteFoxPath(outPath, code);

                static std::atomic<std::uint64_t> s_loggedMissingFv2{ 0 };
                if (s_loggedMissingFv2.exchange(code) != code)
                    LogWarn("OutfitHeadOption: head '%s' face fv2 0x%016llX or fpk 0x%016llX not installed (playerType %u), using vanilla face",
                        head->name, static_cast<unsigned long long>(code),
                        static_cast<unsigned long long>(siblingFpk),
                        static_cast<unsigned>(pt));
            }
        }
        return g_OrigLoadSnakeFaceFv2(outPath, playerType, VanillaClampPartsType(playerPartsType),
                                      playerFaceId, playerFaceEquipId);
    }

    static std::uint64_t* __fastcall hkLoadPlayerSnakeFaceFpk(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerFaceId, char playerFaceEquipId)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return g_OrigLoadSnakeFaceFpk(outPath, playerType,
                                          VanillaClampPartsType(playerPartsType),
                                          playerFaceId, playerFaceEquipId);
        const std::uint32_t effectivePartsType = EffectivePartsType(playerPartsType);
        const auto pt = static_cast<std::uint8_t>(playerType & 0xFF);
        const outfit::OutfitEntry* entry = nullptr;
        if (ResolveCustomEntry(playerType, effectivePartsType, &entry))
        {
            if (!entry->IsHeadEnabled(pt))
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);


            const std::uint8_t activeSlot = t_ActiveCustomFaceSlot;
            const outfit::CustomHeadEntry* head =
                outfit::TryGetCustomHeadBySlot(activeSlot);
            if (!head)
                head = outfit::TryGetCustomHeadBySlot(
                    static_cast<std::uint8_t>(playerFaceEquipId));
            if (!head)
                head = outfit::TryGetCustomHeadBySlot(outfit::GetWornCustomHeadSlot());
            if (head && (!entry->IsFaceEnabled(pt)
                         || !entry->HasHeadOptionAnyVariant(head->equipId, pt)))
                head = nullptr;

            if (head && pt < outfit::kPlayerTypeMax)
            {
                std::uint64_t code = head->faceFpkCode[pt];
                if (pt == outfit::kPlayerType_Snake)
                    if (const std::uint64_t st = outfit::GetCustomHeadSnakeStageFpk(
                            head->name, playerFaceId); st != 0)
                        code = st;
                if (code != 0)
                    return WriteFoxPath(outPath, code);
            }

            return g_OrigLoadSnakeFaceFpk(outPath, playerType,
                                          kBionicArmVanillaPartsTypeSubstitute,
                                          playerFaceId, playerFaceEquipId);
        }
        if (static_cast<std::uint8_t>(effectivePartsType & 0xFF) < outfit::kCustomPartsTypeStart)
        {
            bool headEnable = true;
            if (outfit::VanillaExtGetSuitHead(
                    static_cast<std::uint8_t>(effectivePartsType & 0xFF),
                    pt, outfit::ReadLiveSelectorCode(), &headEnable)
                && !headEnable)
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
        }
        if (const outfit::CustomHeadEntry* head = ResolveVanillaSuitCustomHead(
                pt, playerPartsType, static_cast<std::uint8_t>(playerFaceEquipId)))
        {
            std::uint64_t code = head->faceFpkCode[pt];
            if (pt == outfit::kPlayerType_Snake)
                if (const std::uint64_t st = outfit::GetCustomHeadSnakeStageFpk(
                        head->name, playerFaceId); st != 0)
                    code = st;
            if (code != 0)
            {
                if (!ShouldFallBackOnMissingAsset(code)
                    && fox::detail::PathExistsByCode(code))
                    return WriteFoxPath(outPath, code);

                static std::atomic<std::uint64_t> s_loggedMissingFpk{ 0 };
                if (s_loggedMissingFpk.exchange(code) != code)
                    LogWarn("OutfitHeadOption: head '%s' face fpk 0x%016llX not installed (playerType %u), using vanilla face",
                        head->name, static_cast<unsigned long long>(code),
                        static_cast<unsigned>(pt));
            }
        }
        return g_OrigLoadSnakeFaceFpk(outPath, playerType, VanillaClampPartsType(playerPartsType),
                                      playerFaceId, playerFaceEquipId);
    }

    using FoxModelFromHandle_t = long long (__fastcall*)(long long handle,
                                                         std::uint32_t idx);
    static FoxModelFromHandle_t g_FoxModelFromHandle = nullptr;
    constexpr std::uint32_t kFoxModelHidden  = 0xFFFFFFFFu;

    static const outfit::CustomHeadEntry* ResolveAvatarCustomHead()
    {
        if (!g_InstalledAvatarHeadOptionFv2 || !g_InstalledAvatarHeadOptionFpk)
            return nullptr;

        const outfit::CustomHeadEntry* head =
            outfit::TryGetCustomHeadBySlot(t_ActiveCustomFaceSlot);
        if (!head)
            head = outfit::TryGetCustomHeadBySlot(
                g_LastCustomFaceSlot.load(std::memory_order_relaxed));

        if (head
            && (head->faceFv2Code[outfit::kPlayerType_Avatar] == 0
                || head->faceFpkCode[outfit::kPlayerType_Avatar] == 0))
            return nullptr;
        return head;
    }

    static std::uint64_t* __fastcall hkLoadAvatarFaceFv2(
        std::uint64_t* outPath, std::uint32_t avatarFaceA, std::uint32_t avatarFaceB)
    {
        return g_OrigLoadAvatarFaceFv2(outPath, avatarFaceA, avatarFaceB);
    }

    static std::uint64_t* __fastcall hkLoadAvatarFaceFpk(
        std::uint64_t* outPath, std::uint32_t avatarFaceA, std::uint32_t avatarFaceB)
    {
        return g_OrigLoadAvatarFaceFpk(outPath, avatarFaceA, avatarFaceB);
    }

    static std::uint64_t* __fastcall hkLoadAvatarHeadOptionFv2(
        std::uint64_t* outPath, std::uint32_t faceId)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return g_OrigLoadAvatarHeadOptionFv2(outPath, faceId);
        if (const outfit::CustomHeadEntry* head = ResolveAvatarCustomHead())
        {
            return WriteFoxPath(outPath,
                head->faceFv2Code[outfit::kPlayerType_Avatar]);
        }
        return g_OrigLoadAvatarHeadOptionFv2(outPath, faceId);
    }

    static std::uint64_t* __fastcall hkLoadAvatarHeadOptionFpk(
        std::uint64_t* outPath, std::uint32_t faceId)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return g_OrigLoadAvatarHeadOptionFpk(outPath, faceId);
        if (const outfit::CustomHeadEntry* head = ResolveAvatarCustomHead())
        {
            return WriteFoxPath(outPath,
                head->faceFpkCode[outfit::kPlayerType_Avatar]);
        }
        return g_OrigLoadAvatarHeadOptionFpk(outPath, faceId);
    }

    static void __fastcall hkAvatarFaceEditUpdate(
        void* self, void* blockGroup, std::uint32_t blockIndex)
    {
        if (g_OrigAvatarFaceEditUpdate)
            g_OrigAvatarFaceEditUpdate(self, blockGroup, blockIndex);
    }

    static void HideAvatarCreatorFacesPerFrame()
    {
        void* bcv = g_AvatarHideBc.load(std::memory_order_relaxed);
        const std::uint32_t mask =
            g_AvatarHideSlotMask.load(std::memory_order_relaxed);
        if (!bcv || mask == 0 || !g_FoxModelFromHandle)
            return;
        if (ResolveAvatarCustomHead() == nullptr)
            return;

        __try
        {
            auto* bc = reinterpret_cast<std::uint8_t*>(bcv);
            for (std::uint32_t i = 0; i < 32; ++i)
            {
                if ((mask & (1u << i)) == 0)
                    continue;

                const std::uint32_t seqState =
                    *reinterpret_cast<std::uint32_t*>(bc + 0x10c0 + i * 4);
                if (seqState != 3u && seqState != 1u)
                    continue;
                long long controller =
                    reinterpret_cast<long long>(bc + 0x228 + i * 0x98);
                long long handle = *reinterpret_cast<long long*>(controller + 0x60);
                if (!handle) continue;
                long long model = g_FoxModelFromHandle(handle, 0);
                if (!model) continue;
                *reinterpret_cast<std::uint32_t*>(model + 0x1a4) = kFoxModelHidden;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }


    static std::uint8_t __fastcall hkDoesNeedFaceFova(std::uint32_t playerPartsType)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return g_OrigDoesNeedFaceFova
                ? g_OrigDoesNeedFaceFova(playerPartsType) : 0;
        const std::uint32_t effective = EffectivePartsType(playerPartsType);
        if (effective >= outfit::kCustomPartsTypeStart && effective <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            const auto pt     = static_cast<std::uint8_t>(effective & 0xFF);
            const auto livePT = outfit::ReadLivePlayerType();
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry)
                return entry->IsHeadEnabled(livePT) ? std::uint8_t{1} : std::uint8_t{0};
        }
        if (effective < outfit::kCustomPartsTypeStart)
        {
            bool headEnable = true;
            if (outfit::VanillaExtGetSuitHead(
                    static_cast<std::uint8_t>(effective & 0xFF),
                    outfit::ReadLivePlayerType(), outfit::ReadLiveSelectorCode(),
                    &headEnable)
                && !headEnable)
                return 0;
        }
        const std::uint8_t orig =
            g_OrigDoesNeedFaceFova ? g_OrigDoesNeedFaceFova(playerPartsType) : 0;
        if (orig == 0)
            return VanillaExtNeedsFaceFova(effective);
        return orig;
    }

    static std::uint8_t __fastcall hkDoesNeedFaceFovaForAvatar(std::uint32_t playerPartsType)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return g_OrigDoesNeedFaceFovaForAvatar
                ? g_OrigDoesNeedFaceFovaForAvatar(playerPartsType) : 0;
        const std::uint32_t effective = EffectivePartsType(playerPartsType);
        if (effective >= outfit::kCustomPartsTypeStart && effective <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            const auto pt     = static_cast<std::uint8_t>(effective & 0xFF);
            const auto livePT = outfit::ReadLivePlayerType();
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry)
                return entry->IsHeadEnabled(livePT) ? std::uint8_t{1} : std::uint8_t{0};
        }
        if (effective < outfit::kCustomPartsTypeStart)
        {
            bool headEnable = true;
            if (outfit::VanillaExtGetSuitHead(
                    static_cast<std::uint8_t>(effective & 0xFF),
                    outfit::ReadLivePlayerType(), outfit::ReadLiveSelectorCode(),
                    &headEnable)
                && !headEnable)
                return 0;
        }
        const std::uint8_t orig = g_OrigDoesNeedFaceFovaForAvatar
             ? g_OrigDoesNeedFaceFovaForAvatar(playerPartsType) : 0;
        if (orig == 0)
            return VanillaExtNeedsFaceFova(effective);
        return orig;
    }

    static void __fastcall hkSetHandSlotEnabled(void* self_equipController,
                                                std::uint32_t slot, std::uint8_t enabled)
    {
        if (enabled != 0)
        {
            const std::uint8_t livePT        = outfit::ReadLivePlayerType();
            const std::uint8_t livePartsType = outfit::ReadLivePartsType();
            if (livePartsType >= outfit::kCustomPartsTypeStart
             && livePartsType <= outfit::kCustomPartsTypeEnd)
            {
                const outfit::OutfitEntry* entry = nullptr;
                if (outfit::TryGetOutfitByPartsType(livePartsType, &entry) && entry
                    && entry->IsPlayerTypeSupported(livePT)
                    && !entry->IsArmEnabled(livePT))
                {
                    if (g_OrigSetHandSlotEnabled)
                        g_OrigSetHandSlotEnabled(self_equipController, slot, 0);
                    return;
                }
            }
            if (g_OrigSetHandSlotEnabled)
                g_OrigSetHandSlotEnabled(self_equipController, slot, enabled);
            return;
        }

        const std::uint8_t livePT        = outfit::ReadLivePlayerType();
        const std::uint8_t livePartsType = outfit::ReadLivePartsType();

        const bool liveIsSnakeOrAvatar =
               (livePT == outfit::kPlayerType_Snake)
            || (livePT == outfit::kPlayerType_Avatar);
        const bool liveIsCustomPartsType =
               (livePartsType >= outfit::kCustomPartsTypeStart
             && livePartsType <= outfit::kCustomPartsTypeEnd);

        if (liveIsSnakeOrAvatar && liveIsCustomPartsType)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(livePartsType, &entry) && entry
                && entry->IsPlayerTypeSupported(livePT)
                && entry->IsArmEnabled(livePT))
            {
                if (g_OrigSetHandSlotEnabled)
                    g_OrigSetHandSlotEnabled(self_equipController, slot, 1);
                return;
            }
        }
        if (g_OrigSetHandSlotEnabled)
            g_OrigSetHandSlotEnabled(self_equipController, slot, enabled);
    }

    static std::uint8_t __fastcall hkIsArtificialHandEnabled(
        std::uint32_t playerType, std::uint32_t playerPartsType)
    {
        if (playerPartsType >= outfit::kCustomPartsTypeStart
         && playerPartsType <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            const auto pt  = static_cast<std::uint8_t>(playerPartsType & 0xFF);
            const auto ply = static_cast<std::uint8_t>(playerType & 0xFF);
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry
                && entry->IsPlayerTypeSupported(ply)
                && entry->IsArmEnabled(ply))
                return 1;
        }
        return g_OrigIsArtificialHandEnabled
             ? g_OrigIsArtificialHandEnabled(playerType, playerPartsType) : 0;
    }

    static std::uint8_t __fastcall hkIsArtificialHandEnabledForCurrentPlayerType()
    {
        const std::uint8_t livePT        = outfit::ReadLivePlayerType();
        const std::uint8_t livePartsType = outfit::ReadLivePartsType();

        if (livePartsType >= outfit::kCustomPartsTypeStart
         && livePartsType <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(livePartsType, &entry) && entry
                && entry->IsPlayerTypeSupported(livePT)
                && entry->IsArmEnabled(livePT))
                return 1;
        }
        return g_OrigIsArtificialHandForCurrent ? g_OrigIsArtificialHandForCurrent() : 0;
    }


    static void __fastcall hkProcessSignal(void* p1, void* p2,
                                           std::uint32_t slot, std::uint64_t* signalPtr)
    {
        if (!signalPtr || *signalPtr != kSignalRefreshFv2s)
        {
            Note_PlayerGameObjectImpl(p1);
            if (g_OrigProcessSignal) g_OrigProcessSignal(p1, p2, slot, signalPtr);
            return;
        }

        std::uint8_t* partsTypeArr = nullptr;
        std::uint8_t  origByte     = 0;
        bool          spoofWritten = false;

        __try
        {
            void* perPlayer = *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(p1) + kP2GO_OffPerPlayerStruct);
            if (perPlayer)
            {
                partsTypeArr = *reinterpret_cast<std::uint8_t**>(
                    reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffPartsTypeArr);
                if (partsTypeArr)
                {
                    origByte = partsTypeArr[slot];
                    if (origByte >= outfit::kCustomPartsTypeStart
                     && origByte <= outfit::kCustomPartsTypeEnd)
                    {
                        partsTypeArr[slot] = kProcessSignalSpoofPartsType;
                        outfit::shadow::SetCurrentSlot(slot);
                        spoofWritten = true;
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { partsTypeArr = nullptr; }

        __try
        {
            Note_PlayerGameObjectImpl(p1);
            if (g_OrigProcessSignal) g_OrigProcessSignal(p1, p2, slot, signalPtr);
        }
        __finally
        {
            if (spoofWritten) outfit::shadow::ClearCurrentSlot();
        }

        if (spoofWritten)
        {
            __try { partsTypeArr[slot] = origByte; }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            outfit::shadow::ClearCurrentSlot();
        }

        if (slot == g_LocalPartsSlot.load(std::memory_order_relaxed))
            outfit::RequestColorReapply(true);   // the refresh put the outfit's own fv2 textures back
    }


    constexpr int kPartsSettleTicks = 45;
    constexpr int kPartsStallTicks  = 900;
    constexpr int kFacialUnstickTicks = 60;

    struct PartsPipeline
    {
        bool          valid = false;
        bool          settled = false;
        bool          busy  = false;
        std::uint32_t slot  = 0;
        std::uint32_t count = 0;
        std::uint8_t  state[8] = {};
    };

    static void ReadPartsPipeline(void* self, PartsPipeline* out)
    {
        __try
        {
            std::uint8_t* base = reinterpret_cast<std::uint8_t*>(self);
            const std::uint8_t* stateArr =
                *reinterpret_cast<std::uint8_t**>(base + kP2GO_OffStateMachinePtr);
            const std::uint32_t count =
                *reinterpret_cast<std::uint32_t*>(base + kP2GO_OffSlotCount);
            const std::uint32_t slot =
                *reinterpret_cast<std::uint32_t*>(base + kP2GO_OffLocalPlayerSlot);
            if (!stateArr || count > 64u || slot >= count) return;

            out->valid = true;
            out->slot  = slot;
            if (slot < outfit::shadow::kMaxSlots)
                g_LocalPartsSlot.store(slot, std::memory_order_relaxed);
            out->count = count;
            for (std::uint32_t i = 0; i < count && i < 8u; ++i)
                out->state[i] = stateArr[i];
            const std::uint8_t st = stateArr[slot];
            out->busy = (st == 1u || st == 2u);
            out->settled = (st == 3u);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            out->valid = false; out->busy = false; out->settled = false;
        }
    }

    constexpr std::size_t kP2BC_OffImpl         = 0x10;
    constexpr std::size_t kP2BC_OffLocalIndex   = 0x18;
    constexpr std::size_t kBCI_OffResidentCount = 0x168;
    constexpr std::size_t kBCI_OffStreamCount   = 0x16c;
    constexpr std::size_t kBCI_OffRequestState  = 0x17c;
    constexpr std::size_t kBCI_OffFacialGroup   = 0x160;
    constexpr std::size_t kBCI_OffFacialReq     = 0x214;
    constexpr std::size_t kBCI_OffFacialState   = 0x218;

    constexpr int kPartsGroupsHoldMaxTicks = 240;

    struct PartsBlockView
    {
        bool          read     = false;
        std::uint32_t resident = 0;
        std::uint32_t streamed = 0;
        std::uint32_t implSlot = 0;
        std::int32_t  req      = -1;
        std::int32_t  req4[4]  = { -1, -1, -1, -1 };
        bool          facialGroup = false;
        std::int32_t  facialReq   = -1;
        std::int32_t  facialState = -1;
    };

    static void ReadPartsBlockView(std::uint32_t playerSlot, PartsBlockView* out)
    {
        void* wrapper = g_CapturedBlockController;
        if (!wrapper) return;
        __try
        {
            auto* w = reinterpret_cast<std::uint8_t*>(wrapper);
            auto* impl = *reinterpret_cast<std::uint8_t**>(w + kP2BC_OffImpl);
            if (!impl) return;

            const std::uint32_t localIndex =
                *reinterpret_cast<std::uint32_t*>(w + kP2BC_OffLocalIndex);

            std::uint32_t block = playerSlot;
            if (playerSlot == localIndex)     block = 0u;
            else if (playerSlot < localIndex) block = playerSlot + 1u;

            out->implSlot = block;
            out->resident =
                *reinterpret_cast<std::uint32_t*>(impl + kBCI_OffResidentCount);
            out->streamed =
                *reinterpret_cast<std::uint32_t*>(impl + kBCI_OffStreamCount);

            for (int i = 0; i < 4; ++i)
                out->req4[i] = *reinterpret_cast<std::int32_t*>(
                    impl + kBCI_OffRequestState + static_cast<std::size_t>(i) * 4u);

            if (block < 4u) out->req = out->req4[block];

            out->facialGroup =
                *reinterpret_cast<void**>(impl + kBCI_OffFacialGroup) != nullptr;
            out->facialReq =
                *reinterpret_cast<std::int32_t*>(impl + kBCI_OffFacialReq);
            out->facialState =
                *reinterpret_cast<std::int32_t*>(impl + kBCI_OffFacialState);

            out->read = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { out->read = false; }
    }

    static bool ReleaseAdditionalFacialBlock()
    {
        void* wrapper = g_CapturedBlockController;
        if (!wrapper) return false;
        __try
        {
            auto* w = reinterpret_cast<std::uint8_t*>(wrapper);
            auto* impl = *reinterpret_cast<std::uint8_t**>(w + kP2BC_OffImpl);
            if (!impl) return false;
            if (!*reinterpret_cast<void**>(impl + kBCI_OffFacialGroup)) return false;
            *reinterpret_cast<std::int32_t*>(impl + kBCI_OffFacialReq) = 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool PartsGroupsLive(const PartsBlockView& v)
    {
        if (!v.read) return true;
        if (v.implSlot >= v.resident + v.streamed) return false;
        return v.req != 0;
    }

    static void PushIdentityToEngineArrays(void* self, const PartsPipeline& pp)
    {
        if (!pp.valid) return;

        const std::uint8_t livePT    = outfit::ReadLivePlayerType();
        const std::uint8_t liveParts = outfit::ReadLivePartsType();
        const std::uint8_t liveCamo  = outfit::ReadLiveSelectorCode();

        if (!outfit::IsUniqueCharacterPlayerType(livePT)) return;
        if (liveParts < outfit::kCustomPartsTypeStart
         || liveParts > outfit::kCustomPartsTypeEnd) return;
        if (liveCamo == 0) return;

        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(self);
            void* perPlayer =
                *reinterpret_cast<void**>(base + kP2GO_OffPerPlayerStruct);
            if (!perPlayer) return;
            auto* pps = reinterpret_cast<std::uint8_t*>(perPlayer);

            auto* ptArr =
                *reinterpret_cast<std::uint8_t**>(pps + kPP_OffPlayerTypeArr);
            auto* partsArr =
                *reinterpret_cast<std::uint8_t**>(pps + kPP_OffPartsTypeArr);
            auto* camoArr =
                *reinterpret_cast<std::uint8_t**>(pps + kPP_OffCamoTypeArr);
            if (!ptArr || !partsArr || !camoArr) return;

            std::uint32_t syncCount = pp.count;
            if (syncCount == 0 || syncCount > outfit::shadow::kMaxSlots)
                syncCount = 1;
            if (pp.slot < outfit::shadow::kMaxSlots && syncCount <= pp.slot)
                syncCount = pp.slot + 1;

            if (pp.slot >= outfit::shadow::kMaxSlots)
            {
                static std::atomic<int> s_slotOob{ 0 };
                if (s_slotOob.fetch_add(1, std::memory_order_relaxed) < 8)
                    LogWarn("OutfitRuntimeParts: parts slot %u past the %zu-slot mirror, suit sync skipped",
                        pp.slot, outfit::shadow::kMaxSlots);
                return;
            }

            for (std::uint32_t i = 0; i < syncCount; ++i)
            {
                if (i != pp.slot)
                {
                    const bool sameCharacter = (ptArr[i] == livePT);
                    const bool otherUnique =
                        outfit::IsUniqueCharacterPlayerType(ptArr[i]);
                    const bool staleCustom =
                        partsArr[i] >= outfit::kCustomPartsTypeStart
                     && partsArr[i] <= outfit::kCustomPartsTypeEnd;
                    if (!sameCharacter && !otherUnique && !staleCustom)
                        continue;
                }

                if (ptArr[i] == livePT
                 && partsArr[i] == liveParts
                 && camoArr[i] == liveCamo) continue;

                ptArr[i]    = livePT;
                partsArr[i] = liveParts;
                camoArr[i]  = liveCamo;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    constexpr int kHeadReloadPollTicks = 15;

    static void NoteLocalHeadRealize(std::uint8_t realized, std::uint8_t dropped,
                                     std::uint8_t playerType, std::uint8_t partsType,
                                     std::uint8_t camo)
    {
        g_RealizedLocalHeadSlot.store(
            realized >= outfit::kCustomHeadSlotBase ? realized : std::uint8_t{0},
            std::memory_order_relaxed);

        const std::uint32_t key = (dropped == 0) ? 0u
            : (static_cast<std::uint32_t>(dropped)
               | (static_cast<std::uint32_t>(playerType) << 8)
               | (static_cast<std::uint32_t>(partsType) << 16)
               | (static_cast<std::uint32_t>(camo) << 24));
        g_DroppedLocalHeadKey.store(key, std::memory_order_relaxed);

        const std::uint32_t latch =
            g_HeadReloadLatchKey.load(std::memory_order_relaxed);
        if (key != 0 && key == latch)
        {
            static std::atomic<std::uint32_t> s_loggedKey{ 0 };
            if (s_loggedKey.exchange(key, std::memory_order_relaxed) != key)
                LogWarn("OutfitRuntimeParts: head slot 0x%02X dropped again after reload (partsType=0x%02X camo=0x%02X playerType=%u), invisible until next suit change",
                    static_cast<unsigned>(dropped), static_cast<unsigned>(partsType),
                    static_cast<unsigned>(camo), static_cast<unsigned>(playerType));
        }
        else if (key != latch)
        {
            g_HeadReloadLatchKey.store(0, std::memory_order_relaxed);
        }
    }

    static int ReadLocalFaceEquipCellSeh(void* self, std::uint32_t slot)
    {
        __try
        {
            void* perPlayer = *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(self) + kP2GO_OffPerPlayerStruct);
            if (!perPlayer)
                return -1;
            const std::uint8_t* faceEquipArr = *reinterpret_cast<std::uint8_t**>(
                reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffFaceEquipArr);
            return faceEquipArr ? faceEquipArr[slot] : -1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    static bool ClearLocalFaceEquipCellSeh(void* self, std::uint32_t slot,
                                           std::uint8_t expected)
    {
        __try
        {
            void* perPlayer = *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(self) + kP2GO_OffPerPlayerStruct);
            if (!perPlayer)
                return false;
            std::uint8_t* faceEquipArr = *reinterpret_cast<std::uint8_t**>(
                reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffFaceEquipArr);
            if (!faceEquipArr || faceEquipArr[slot] != expected)
                return false;
            faceEquipArr[slot] = 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void ReloadLocalHeadOnceRealizable(void* self, const PartsPipeline& pp)
    {
        static std::atomic<int> s_servedTicks{ 0 };

        if (MissionCodeGuard::ShouldBypassHooks())
        {
            g_DroppedLocalHeadKey.store(0, std::memory_order_relaxed);
            s_servedTicks.store(0, std::memory_order_relaxed);
            return;
        }

        const std::uint32_t key = g_DroppedLocalHeadKey.load(std::memory_order_relaxed);
        if (key == 0 || key == g_HeadReloadLatchKey.load(std::memory_order_relaxed)
            || !pp.valid || pp.count == 0 || pp.count > 8u)
        {
            s_servedTicks.store(0, std::memory_order_relaxed);
            return;
        }
        for (std::uint32_t i = 0; i < pp.count; ++i)
        {
            if (pp.state[i] != 3u)
            {
                s_servedTicks.store(0, std::memory_order_relaxed);
                return;
            }
        }

        const int served = s_servedTicks.fetch_add(1, std::memory_order_relaxed) + 1;
        if (served < kPartsSettleTicks
            || (served - kPartsSettleTicks) % kHeadReloadPollTicks != 0)
            return;

        PartsBlockView blockView;
        ReadPartsBlockView(pp.slot, &blockView);
        if (!PartsGroupsLive(blockView))
            return;

        const std::uint8_t slot       = static_cast<std::uint8_t>(key & 0xFFu);
        const std::uint8_t playerType = static_cast<std::uint8_t>((key >> 8) & 0xFFu);
        const std::uint16_t worn = outfit::ReadLiveWornHeadCategory();
        if (worn == 0xFFFFu || static_cast<std::uint8_t>(worn & 0xFFu) != slot
            || outfit::ReadLivePlayerType() != playerType)
            return;

        const outfit::CustomHeadEntry* head = outfit::TryGetCustomHeadBySlot(slot);
        if (!head || !outfit::IsHeadOptionOfferedByLiveSuit(head->equipId))
            return;
        if (ReadLocalFaceEquipCellSeh(self, pp.slot) != static_cast<int>(slot))
            return;

        g_HeadReloadLatchKey.store(key, std::memory_order_relaxed);
        if (!ClearLocalFaceEquipCellSeh(self, pp.slot, slot))
        {
            static std::atomic<int> s_writeFailLogged{ 0 };
            if (s_writeFailLogged.fetch_add(1, std::memory_order_relaxed) < 4)
                LogWarn("OutfitRuntimeParts: head slot 0x%02X reload request failed, invisible until next suit change",
                    static_cast<unsigned>(slot));
            return;
        }
    }

    static void __fastcall hkUpdatePartsStatus(void* self)
    {
        PartsPipeline pp;
        ReadPartsPipeline(self, &pp);
        if (pp.valid)
            outfit::NoteColorPipeline(pp.busy, pp.settled);

        static std::atomic<int> s_partsIdleTicks { kPartsSettleTicks };
        static std::atomic<int> s_partsStallTicks{ 0 };
        static std::atomic<int> s_partsStallLogged{ 0 };
        static std::atomic<int> s_partsUnservedTicks { 0 };
        static std::atomic<int> s_partsUnservedLogged{ 0 };

        if (pp.busy)
        {
            s_partsIdleTicks.store(0, std::memory_order_relaxed);
            const int held =
                s_partsStallTicks.fetch_add(1, std::memory_order_relaxed) + 1;

            if (held >= kFacialUnstickTicks && pp.valid && pp.state[pp.slot] == 1u)
            {
                PartsBlockView fv;
                ReadPartsBlockView(pp.slot, &fv);
                if (fv.read && fv.facialGroup
                 && fv.facialReq == 1 && fv.facialState == 1
                 && ReleaseAdditionalFacialBlock())
                {
                    static std::atomic<int> s_facialFreed{ 0 };
                    if (s_facialFreed.fetch_add(1, std::memory_order_relaxed) < 8)
                        LogWarn("OutfitRuntimeParts: stuck facial block released (playerType %u) so the load can finish",
                            static_cast<unsigned>(outfit::ReadLivePlayerType()));
                }
            }

            if (held == kPartsStallTicks
             && s_partsStallLogged.fetch_add(1, std::memory_order_relaxed) < 4)
                LogWarn("OutfitRuntimeParts: player parts load stalled %d ticks (partsType=0x%02X camo=0x%02X playerType=%u), load may hang",
                    kPartsStallTicks,
                    static_cast<unsigned>(outfit::ReadLivePartsType()),
                    static_cast<unsigned>(outfit::ReadLiveSelectorCode()),
                    static_cast<unsigned>(outfit::ReadLivePlayerType()));
        }
        else
        {
            s_partsStallTicks.store(0, std::memory_order_relaxed);
            const int idle =
                s_partsIdleTicks.fetch_add(1, std::memory_order_relaxed) + 1;

            PartsBlockView bv;
            if (pp.valid) ReadPartsBlockView(pp.slot, &bv);
            bool partsAlive = PartsGroupsLive(bv);

            static std::atomic<int> s_groupsHeldTicks{ 0 };
            if (!partsAlive)
            {
                const int held =
                    s_groupsHeldTicks.fetch_add(1, std::memory_order_relaxed) + 1;
                if (held > kPartsGroupsHoldMaxTicks)
                {
                    partsAlive = true;
                    static std::atomic<int> s_releaseLogged{ 0 };
                    if (s_releaseLogged.fetch_add(1, std::memory_order_relaxed) < 8)
                        LogWarn("OutfitRuntimeParts: parts block %u unloadable for %d ticks, outfit reassert hold released",
                            bv.implSlot, held);
                }
            }
            else
            {
                s_groupsHeldTicks.store(0, std::memory_order_relaxed);
            }

            const bool servedSlot = partsAlive &&
                pp.valid && (pp.settled || pp.state[pp.slot] == 0u);

            if (idle >= kPartsSettleTicks && servedSlot)
            {
                s_partsUnservedTicks.store(0, std::memory_order_relaxed);

                fobchars::ReassertSelectedCharacter();

                uniquecharpin::ReassertAfterRestore();

                PushIdentityToEngineArrays(self, pp);
            }
            else if (idle >= kPartsSettleTicks)
            {
                const int held =
                    s_partsUnservedTicks.fetch_add(
                        1, std::memory_order_relaxed) + 1;
                if (held == kPartsStallTicks
                 && s_partsUnservedLogged.fetch_add(
                        1, std::memory_order_relaxed) < 4)
                    LogWarn("OutfitRuntimeParts: parts slot at load state %u for %d ticks, outfit reassert held (partsType=0x%02X playerType=%u)",
                        pp.valid ? pp.state[pp.slot] : 0xFFu, held,
                        static_cast<unsigned>(outfit::ReadLivePartsType()),
                        static_cast<unsigned>(outfit::ReadLivePlayerType()));
            }
        }

        uniquecharpin::SyncSupport(outfit::ReadLivePartsType(),
                                   outfit::ReadLiveSelectorCode(),
                                   MissionCodeGuard::ShouldBypassHooks());

        EquipDevelopAdd::MaybeRefreshDynamicGates();

        ReloadLocalHeadOnceRealizable(self, pp);

        struct SlotOverride { bool active; std::uint8_t restoreValue; };
        SlotOverride  overrides[outfit::shadow::kMaxSlots] = {};
        std::uint8_t* armTypeArr = nullptr;

        __try
        {
            void* perPlayer = *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(self) + kP2GO_OffPerPlayerStruct);
            std::uint8_t* stateMachineArr = *reinterpret_cast<std::uint8_t**>(
                reinterpret_cast<std::uint8_t*>(self) + kP2GO_OffStateMachinePtr);

            if (perPlayer)
            {
                std::uint8_t* partsTypeArr = *reinterpret_cast<std::uint8_t**>(
                    reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffPartsTypeArr);
                std::uint8_t* playerTypeArr = *reinterpret_cast<std::uint8_t**>(
                    reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffPlayerTypeArr);
                armTypeArr = *reinterpret_cast<std::uint8_t**>(
                    reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffArmTypeArr);
                std::uint8_t* camoTypeArr = *reinterpret_cast<std::uint8_t**>(
                    reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffCamoTypeArr);
                std::uint32_t* stateChangedBits = reinterpret_cast<std::uint32_t*>(
                    reinterpret_cast<std::uint8_t*>(perPlayer) + kPP_OffStateChangedBits);

                if (partsTypeArr && playerTypeArr && armTypeArr)
                {
                    std::uint32_t vextRestream = 0;
                    if (!MissionCodeGuard::ShouldBypassHooks())
                        for (std::size_t i = 0;
                             i < outfit::shadow::kMaxSlots; ++i)
                        {
                            const std::uint8_t pt = partsTypeArr[i];
                            if (pt >= outfit::kCustomPartsTypeStart) continue;
                            if (!stateMachineArr || stateMachineArr[i] != 3)
                                continue;
                            if (outfit::VanillaExtVariantSlotCount(pt) == 0)
                                continue;
                            const auto servedIdx =
                                static_cast<std::size_t>(playerTypeArr[i]);
                            if (servedIdx >= outfit::kPlayerTypeMax) continue;
                            if (g_VextServedPt[servedIdx].load(
                                    std::memory_order_relaxed) != pt) continue;
                            const std::uint8_t want =
                                outfit::GetActiveVariant(pt);
                            const std::uint8_t have =
                                g_VextServedVar[servedIdx].load(
                                    std::memory_order_relaxed);
                            if (have == want) continue;
                            vextRestream |= (1u << i);
                        }
                    if (!MissionCodeGuard::ShouldBypassHooks() && stateMachineArr)
                    {
                        bool uniqueChanged = false;
                        bool anyBusy       = false;
                        for (std::size_t i = 0;
                             i < outfit::shadow::kMaxSlots; ++i)
                        {
                            if (stateMachineArr[i] == 1
                             || stateMachineArr[i] == 2) anyBusy = true;
                            if (!outfit::IsUniqueCharacterPlayerType(
                                    playerTypeArr[i])) continue;
                            if (stateMachineArr[i] != 3) continue;
                            if (g_UniqueServedParts[i].load(
                                    std::memory_order_relaxed)
                                != partsTypeArr[i]) uniqueChanged = true;
                        }

                        outfit::NotePartsPipelineBusy(anyBusy);

                        static int s_colorWaitTicks = 0;
                        if (g_RestreamRequested.load(std::memory_order_acquire))
                        {
                            if (!anyBusy || ++s_colorWaitTicks >= kRestreamMaxDeferTicks)
                            {
                                g_RestreamRequested.store(false, std::memory_order_release);
                                std::uint32_t mask = 0;
                                for (std::size_t i = 0; i < outfit::shadow::kMaxSlots; ++i)
                                    if (stateMachineArr[i] == 3)
                                        mask |= (1u << i);
                                const std::uint32_t local = g_LocalPartsSlot.load(std::memory_order_relaxed);
                                if (local < outfit::shadow::kMaxSlots)
                                    mask |= (1u << local);
                                vextRestream |= mask;
                                g_ColorReloadSlots.fetch_or(mask, std::memory_order_acq_rel);
                                LogInfo("OutfitColor: colour package changed, parts slots 0x%X reload%s", mask,
                                    anyBusy ? " (a slot was still loading)" : "");
                                s_colorWaitTicks = 0;
                            }
                        }
                        static int s_colorStaleTicks = 0;
                        if (g_ColorReloadSlots.load(std::memory_order_relaxed) != 0 && !anyBusy)
                        {
                            if (++s_colorStaleTicks >= 180)
                            {
                                LogWarn("OutfitColor: parts slots 0x%X did not load again, colour applies go on",
                                    g_ColorReloadSlots.exchange(0, std::memory_order_acq_rel));
                                g_ColorReloadPending.store(false, std::memory_order_relaxed);
                                s_colorStaleTicks = 0;
                            }
                        }
                        else
                        {
                            s_colorStaleTicks = 0;
                        }

                        int pending =
                            g_RestreamPendingTicks.load(std::memory_order_relaxed);
                        if (uniqueChanged && pending == 0)
                            pending = kRestreamMaxDeferTicks;

                        if (pending > 0)
                        {
                            const bool deadline = (pending == 1);
                            if (!anyBusy || deadline)
                            {
                                for (std::size_t i = 0;
                                     i < outfit::shadow::kMaxSlots; ++i)
                                {
                                    if (stateMachineArr[i] == 3)
                                        vextRestream |= (1u << i);
                                    g_UniqueServedParts[i].store(
                                        partsTypeArr[i],
                                        std::memory_order_relaxed);
                                }
                                pending = 0;

                                if (deadline && anyBusy)
                                {
                                    static std::atomic<int> s_lateLogged{ 0 };
                                    if (s_lateLogged.fetch_add(
                                            1, std::memory_order_relaxed) < 8)
                                        LogWarn("OutfitRuntimeParts: slot still loading %d ticks after suit change, it keeps the previous suit until its next load",
                                            kRestreamMaxDeferTicks);
                                }
                            }
                            else
                            {
                                --pending;
                            }
                        }

                        g_RestreamPendingTicks.store(
                            pending, std::memory_order_relaxed);
                    }

                    if (vextRestream)
                    {
                        *reinterpret_cast<std::uint32_t*>(
                            reinterpret_cast<std::uint8_t*>(perPlayer)
                            + kPP_OffVariantChangedBits) |= vextRestream;
                        if (stateChangedBits)
                            *stateChangedBits |= vextRestream;
                    }

                    for (std::size_t i = 0; i < outfit::shadow::kMaxSlots; ++i)
                    {
                        const std::uint8_t pt  = partsTypeArr[i];
                        const std::uint8_t ply = playerTypeArr[i];

                        if (pt < outfit::kCustomPartsTypeStart
                         || pt > outfit::kCustomPartsTypeEnd) continue;

                        const outfit::OutfitEntry* entry = nullptr;
                        if (!outfit::TryGetOutfitByPartsType(pt, &entry) || !entry) continue;
                        if (!entry->IsPlayerTypeSupported(ply)) continue;

                        if (camoTypeArr)
                        {
                            std::uint8_t camo = camoTypeArr[i];
                            if (camo < outfit::kCustomSelectorStart
                             || camo > outfit::kCustomSelectorEnd)
                            {
                                outfit::shadow::Slot sh{};
                                if (outfit::shadow::Get(i, &sh)
                                 && sh.used
                                 && sh.realPartsType == pt
                                 && sh.realCamoType >= outfit::kCustomSelectorStart
                                 && sh.realCamoType <= outfit::kCustomSelectorEnd)
                                {
                                    camo = sh.realCamoType;
                                }
                                else
                                {
                                    continue;
                                }
                            }
                        }

                        RefreshCaseDArmFlag(pt, entry->IsArmEnabled(ply));

                        if (!entry->IsArmEnabled(ply)) continue;

                        const std::uint8_t liveTier =
                            ReadLiveArmTierFromLoadoutRequest(self, i);

                        bool cachedFlag = false;
                        const std::uint8_t cachedTier =
                            outfit::shadow::GetArmTier(ply, &cachedFlag);

                        const std::uint8_t resolvedTier =
                            (liveTier > 0) ? liveTier
                                           : (cachedFlag ? cachedTier : std::uint8_t{1});

                        if (liveTier > 0)
                            outfit::shadow::SetArmTier(ply, liveTier);

                        outfit::shadow::Slot ss;
                        if (!outfit::shadow::Get(i, &ss))
                            ss = outfit::shadow::Slot{};
                        ss.realPartsType  = pt;
                        ss.realCamoType   = entry->selectorCode;
                        ss.realArmType    = resolvedTier;
                        ss.realPlayerType = ply;
                        ss.developId      = entry->developId;
                        ss.variantIdx     = entry->HasVariants()
                                          ? outfit::GetActiveVariant(pt) : 0;
                        outfit::shadow::Set(i, ss);

                        overrides[i].active       = true;
                        overrides[i].restoreValue = resolvedTier;

                        if (!g_CaseDArmUnpinActive && armTypeArr) armTypeArr[i] = 0;
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            for (std::size_t i = 0; i < outfit::shadow::kMaxSlots; ++i)
                overrides[i].active = false;
            armTypeArr = nullptr;
        }

        g_OrigUpdatePartsStatus(self);

        if (armTypeArr && !g_CaseDArmUnpinActive)
        {
            __try
            {
                for (std::size_t i = 0; i < outfit::shadow::kMaxSlots; ++i)
                    if (overrides[i].active)
                        armTypeArr[i] = overrides[i].restoreValue;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        HideAvatarCreatorFacesPerFrame();
    }


    static bool __fastcall hkPlayer2ImplSetUpParts(
        void* self, std::uint32_t slot,
        std::uint32_t playerType, std::uint32_t partsType,
        std::uint32_t camo, std::uint32_t armType,
        std::uint32_t faceId, void* avatarInfo)
    {
        if (!g_OrigPlayer2ImplSetUpParts) return false;

        std::uint32_t effectiveArmType = armType;
        if (partsType >= outfit::kCustomPartsTypeStart
         && partsType <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(
                    static_cast<std::uint8_t>(partsType & 0xFF), &entry)
                && entry
                && entry->IsPlayerTypeSupported(
                       static_cast<std::uint8_t>(playerType & 0xFF)))
            {
                if (!entry->IsArmEnabled(static_cast<std::uint8_t>(playerType & 0xFF)))
                {
                    effectiveArmType = 0;
                }
                else if (armType == 0)
                {
                    bool cachedFlag = false;
                    std::uint8_t cachedTier =
                        outfit::shadow::GetArmTier(playerType, &cachedFlag);
                    effectiveArmType = cachedFlag ? static_cast<std::uint32_t>(cachedTier) : 1u;
                }
            }
        }

        return g_OrigPlayer2ImplSetUpParts(self, slot, playerType, partsType,
                                           camo, effectiveArmType, faceId, avatarInfo);
    }

    static std::uint32_t __fastcall hkGetPartsTypeAtCamoType(
        void* self, std::uint32_t camo)
    {
        const std::uint32_t r = g_OrigGetPartsTypeAtCamoType
            ? g_OrigGetPartsTypeAtCamoType(self, camo)
            : 0;

        const auto ra = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        const auto stateInBox = reinterpret_cast<std::uintptr_t>(
            ResolveGameAddress(gAddr.SupplyCboxActionPluginImpl_StateInBox));
        const bool fromCratePickup =
            stateInBox != 0 && ra >= stateInBox && ra < stateInBox + 0xB90;

        if (fromCratePickup && camo < outfit::kCustomSelectorStart)
        {
            outfit::ResetAllVanillaExtVariants();
        }

        if (r == 0
            && camo >= outfit::kCustomSelectorStart
            && camo <= outfit::kCustomSelectorEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            std::uint8_t variantIdx = 0;
            if (outfit::TryGetOutfitByVariantSelector(
                    static_cast<std::uint8_t>(camo), &entry, &variantIdx)
                && entry)
            {
                const bool activate = fromCratePickup;
                if (activate)
                {
                    if (outfit::PeekPendingSupplyDropDevelopId()
                            == entry->developId)
                    {
                        variantIdx =
                            outfit::ConsumePendingSupplyDropVariantIdx();
                        outfit::ConsumePendingSupplyDropDevelopId();
                        outfit::SetCrateDeliveredVariant(entry->developId,
                                                         variantIdx);
                    }
                    else if (outfit::PeekCrateDeliveredDevelopId()
                             == entry->developId)
                    {
                        variantIdx = outfit::PeekCrateDeliveredVariantIdx();
                    }
                    outfit::SetActiveVariant(entry->partsType, variantIdx);
                }
                return entry->partsType;
            }

            std::uint8_t vpt = 0, vidx = 0;
            if (outfit::TryGetVanillaExtByVariantSelector(
                    static_cast<std::uint8_t>(camo), &vpt, &vidx))
            {
                if (fromCratePickup)
                    outfit::SetActiveVariant(vpt, vidx);
                return vpt;
            }
        }

        return r;
    }


    static void __fastcall hkLoadPartsNew(
        void* self, std::uint32_t playerIndex,
        LoadPartsPlayerInfo* info, std::uint32_t flags)
    {
        if (self && g_CapturedBlockController != self)
            g_CapturedBlockController = self;

        if (!info)
        {
            g_OrigLoadPartsNew(self, playerIndex, info, flags);
            return;
        }

        if ((info->playerType == outfit::kPlayerType_DDMale
             || info->playerType == outfit::kPlayerType_DDFemale)
            && outfit::IsFaceIdInPool(info->playerFaceId))
            outfit::PreparePersistedFaceFallbacks();

        if (info->playerFaceEquipId >= outfit::kCustomHeadSlotBase
            && !outfit::IsCustomHeadSlot(info->playerFaceEquipId)
            && outfit::IsCustomHeadSlotReserved(info->playerFaceEquipId))
            outfit::DrainPendingHeads();

        const bool isRealPlayerSlot =
            (info->playerType == outfit::ReadLivePlayerType());

        auto feedShadow = [&](const outfit::OutfitEntry* e, std::uint8_t variantIdx) {
            outfit::shadow::Slot ss{};
            ss.realPartsType  = e->partsType;
            ss.realCamoType   = e->selectorCode;
            ss.realArmType    = info->playerArmType;
            ss.realPlayerType = info->playerType;
            ss.developId      = e->developId;
            ss.variantIdx     = variantIdx;
            outfit::shadow::Set(playerIndex, ss);
        };

        if (info->playerPartsType == 0)
        {
            const std::uint8_t camo = info->playerCamoType;
            if (camo >= outfit::kCustomSelectorStart && camo <= outfit::kCustomSelectorEnd)
            {
                const outfit::OutfitEntry* entry = nullptr;
                std::uint8_t variantIdx = 0;
                if (outfit::TryGetOutfitByVariantSelector(camo, &entry, &variantIdx)
                    && entry
                    && entry->IsPlayerTypeSupported(info->playerType))
                {
                    if (outfit::PeekPendingSupplyDropDevelopId()
                            == entry->developId)
                    {
                        variantIdx =
                            outfit::ConsumePendingSupplyDropVariantIdx();
                        outfit::ConsumePendingSupplyDropDevelopId();
                        outfit::SetCrateDeliveredVariant(entry->developId,
                                                         variantIdx);
                        if (isRealPlayerSlot)
                            outfit::ResetAllVanillaExtVariants();
                    }
                    else if (outfit::PeekCrateDeliveredDevelopId()
                             == entry->developId)
                    {
                        variantIdx = outfit::PeekCrateDeliveredVariantIdx();
                    }
                    const std::uint8_t persistSel =
                        entry->GetVariantSelectorCode(variantIdx);
                    info->playerPartsType = entry->partsType;
                    info->playerCamoType  = persistSel;
                    outfit::SetActiveVariant(entry->partsType, variantIdx);
                    if (isRealPlayerSlot)
                        V_WriteOutfitPartsLoad(entry->partsType,
                                                      persistSel,
                                                      info->playerType);
                    feedShadow(entry, variantIdx);
                    outfit::ClearPendingOutfitDevelopId();
                }
                else
                {
                    static std::atomic<int> s_selMissLogged{ 0 };
                    if (s_selMissLogged.fetch_add(
                            1, std::memory_order_relaxed) < 12)
                        LogWarn("OutfitRuntimeParts: selector 0x%02X: %s for playerType %u, healed to vanilla",
                            static_cast<unsigned>(camo),
                            entry ? "outfit has no branch" : "no outfit registered",
                            static_cast<unsigned>(info->playerType));
                }
            }
            else if (camo == 0xFF)
            {
                const outfit::OutfitEntry* byPending = nullptr;
                const std::uint16_t pendingDevId = outfit::GetPendingOutfitDevelopId();
                if (pendingDevId != 0 && !outfit::IsOutfitBound(pendingDevId))
                    outfit::BindOutfit(pendingDevId, true, "pending-render");
                if (pendingDevId != 0
                    && outfit::TryGetOutfitByDevelopId(pendingDevId, &byPending) && byPending
                    && byPending->bound
                    && byPending->IsPlayerTypeSupported(info->playerType))
                {
                    const std::uint8_t pendVar = byPending->HasVariants()
                        ? outfit::GetActiveVariant(byPending->partsType)
                        : std::uint8_t{0};
                    const std::uint8_t persistSel =
                        byPending->GetVariantSelectorCode(pendVar);
                    info->playerPartsType = byPending->partsType;
                    info->playerCamoType  = persistSel;
                    if (isRealPlayerSlot)
                        V_WriteOutfitPartsLoad(byPending->partsType,
                                                      persistSel,
                                                      info->playerType);
                    feedShadow(byPending, pendVar);
                    outfit::ClearPendingOutfitDevelopId();
                }
                else
                {
                    LogWarn("OutfitRuntimeParts: broken custom suit on playerType %u with no pending developId, healed to vanilla",
                        static_cast<unsigned>(info->playerType));
                    info->playerPartsType    = kBionicArmVanillaPartsTypeSubstitute;
                    info->playerCamoType     = 0;
                    info->playerFaceEquipId  = 0;
                    if (isRealPlayerSlot)
                        V_WriteOutfitPartsLoad(
                            kBionicArmVanillaPartsTypeSubstitute, 0, info->playerType);
                    outfit::shadow::Clear(playerIndex);
                }
            }
        }

        if (info->playerPartsType != 0
            && (info->playerPartsType < outfit::kCustomPartsTypeStart
             || info->playerPartsType > outfit::kCustomPartsTypeEnd))
        {
            const std::uint8_t camo = info->playerCamoType;
            if (camo >= outfit::kCustomSelectorStart
             && camo <= outfit::kCustomSelectorEnd)
            {
                const outfit::OutfitEntry* mixEntry = nullptr;
                std::uint8_t variantIdx = 0;
                if (outfit::TryGetOutfitByVariantSelector(camo, &mixEntry, &variantIdx)
                    && mixEntry
                    && mixEntry->IsPlayerTypeSupported(info->playerType))
                {
                    if (outfit::PeekPendingSupplyDropDevelopId()
                            == mixEntry->developId)
                    {
                        variantIdx =
                            outfit::ConsumePendingSupplyDropVariantIdx();
                        outfit::ConsumePendingSupplyDropDevelopId();
                        outfit::SetCrateDeliveredVariant(mixEntry->developId,
                                                         variantIdx);
                        if (isRealPlayerSlot)
                            outfit::ResetAllVanillaExtVariants();
                    }
                    else if (outfit::PeekCrateDeliveredDevelopId()
                             == mixEntry->developId)
                    {
                        variantIdx = outfit::PeekCrateDeliveredVariantIdx();
                    }
                    const std::uint8_t persistSel =
                        mixEntry->GetVariantSelectorCode(variantIdx);
                    info->playerPartsType = mixEntry->partsType;
                    info->playerCamoType  = persistSel;
                    outfit::SetActiveVariant(mixEntry->partsType, variantIdx);
                    if (isRealPlayerSlot)
                        V_WriteOutfitPartsLoad(mixEntry->partsType,
                                                      persistSel,
                                                      info->playerType);
                    feedShadow(mixEntry, variantIdx);
                    outfit::ClearPendingOutfitDevelopId();
                }
            }
        }

        const outfit::OutfitEntry* entry = nullptr;
        bool isCustom = ResolveCustomEntry(info->playerType,
                                           info->playerPartsType, &entry);

        if (isCustom && entry)
        {
            if (outfit::PeekPendingSupplyDropDevelopId() == entry->developId)
            {
                const std::uint8_t orderedVar =
                    outfit::ConsumePendingSupplyDropVariantIdx();
                outfit::ConsumePendingSupplyDropDevelopId();
                outfit::SetCrateDeliveredVariant(entry->developId, orderedVar);
                outfit::SetActiveVariant(entry->partsType, orderedVar);
                if (isRealPlayerSlot)
                    outfit::ResetAllVanillaExtVariants();
            }
            else if (outfit::PeekCrateDeliveredDevelopId()
                     == entry->developId)
            {
                const std::uint8_t deliveredVar =
                    outfit::PeekCrateDeliveredVariantIdx();
                outfit::SetActiveVariant(entry->partsType, deliveredVar);
            }
        }

        if (isCustom && entry)
        {
            const std::uint8_t livePT =
                static_cast<std::uint8_t>(info->playerType & 0xFF);
            const std::uint8_t v = entry->HasVariants()
                ? outfit::GetActiveVariant(entry->partsType) : 0;
            const std::uint64_t declParts = entry->GetVariantPartsPath(livePT, v);
            const std::uint64_t declFpk   = entry->GetVariantFpkPath(livePT, v);
            const bool partsInvalid = declParts && (declParts >> 51) == 0;
            const bool fpkInvalid   = declFpk   && (declFpk   >> 51) == 0;
            const bool partsPresent =
                declParts && !partsInvalid && fox::detail::PathExistsByCode(declParts);
            const bool fpkPresent =
                declFpk   && !fpkInvalid   && fox::detail::PathExistsByCode(declFpk);
            const bool anyPresent = partsPresent || fpkPresent;
            if (anyPresent)
                g_AssetCheckTrusted.store(true, std::memory_order_relaxed);
            const bool partsMissing = declParts && !partsPresent;
            const bool fpkMissing   = declFpk   && !fpkPresent;
            const bool structurallyInvalid = partsInvalid || fpkInvalid;
            if ((partsMissing || fpkMissing) && (anyPresent || structurallyInvalid))
            {
                LogWarn("OutfitRuntimeParts: outfit developId=%u partsType=0x%02X bad asset (parts %s, fpk %s, playerType %u), using vanilla",
                    static_cast<unsigned>(entry->developId),
                    static_cast<unsigned>(entry->partsType),
                    partsInvalid ? "bad ext" : (partsMissing ? "missing" : "ok"),
                    fpkInvalid   ? "bad ext" : (fpkMissing   ? "missing" : "ok"),
                    static_cast<unsigned>(info->playerType));
                info->playerPartsType    = kBionicArmVanillaPartsTypeSubstitute;
                info->playerCamoType     = 0;
                info->playerFaceEquipId  = 0;
                outfit::shadow::Clear(playerIndex);
                if (isRealPlayerSlot)
                    V_WriteOutfitPartsLoad(
                        kBionicArmVanillaPartsTypeSubstitute, 0, info->playerType);
                isCustom = false;
                entry    = nullptr;
            }
        }

        if (!isCustom)
        {
            const std::uint8_t pt  = info->playerPartsType;
            const std::uint8_t sel = info->playerCamoType;
            const bool ptIsCustomRange =
                pt >= outfit::kCustomPartsTypeStart && pt <= outfit::kCustomPartsTypeEnd;

            std::uint8_t vextPartsType = 0;
            std::uint8_t vextVariantIdx = 0;
            if (!ptIsCustomRange
                && outfit::TryGetVanillaExtByVariantSelector(
                       sel, &vextPartsType, &vextVariantIdx)
                && (pt == vextPartsType
                    || (pt == 0
                        && outfit::GetActiveVariant(vextPartsType) != 0)))
            {
                const outfit::VanillaSuitVariantAsset* vextVar =
                    outfit::VanillaExtGetVariant(
                        vextPartsType,
                        static_cast<std::uint8_t>(info->playerType & 0xFF),
                        vextVariantIdx);
                const std::uint64_t vp = vextVar ? vextVar->partsPathCode64 : 0;
                const std::uint64_t vf = vextVar ? vextVar->fpkPathCode64 : 0;
                const bool vpInvalid = vp && (vp >> 51) == 0;
                const bool vfInvalid = vf && (vf >> 51) == 0;
                const bool vpPresent =
                    vp && !vpInvalid && fox::detail::PathExistsByCode(vp);
                const bool vfPresent =
                    vf && !vfInvalid && fox::detail::PathExistsByCode(vf);
                const bool anyPresent = vpPresent || vfPresent;
                if (anyPresent)
                    g_AssetCheckTrusted.store(true, std::memory_order_relaxed);
                const bool missing = (vp && !vpPresent) || (vf && !vfPresent);
                const bool structurallyInvalid = vpInvalid || vfInvalid;
                const bool assetsBad =
                    !vextVar || (missing && (anyPresent || structurallyInvalid));
                if (assetsBad)
                {
                    const std::uint8_t srcCamo =
                        outfit::VanillaExtGetVariantSourceCamo(vextPartsType,
                                                               vextVariantIdx);
                    info->playerPartsType = vextPartsType;
                    info->playerCamoType  =
                        (srcCamo != 0xFF) ? srcCamo : std::uint8_t{0};
                    LogWarn("OutfitRuntimeParts: vanilla partsType=0x%02X variant %u asset missing or invalid, using source camo 0x%02X",
                        static_cast<unsigned>(vextPartsType),
                        static_cast<unsigned>(vextVariantIdx),
                        static_cast<unsigned>(info->playerCamoType));
                    if (isRealPlayerSlot)
                        V_WriteOutfitPartsLoad(vextPartsType,
                                                      info->playerCamoType,
                                                      info->playerType);
                }
                else
                {
                    outfit::SetActiveVariant(vextPartsType, vextVariantIdx);
                    info->playerPartsType = vextPartsType;
                    const std::uint8_t serveCamo =
                        outfit::VanillaExtGetVariantSourceCamo(vextPartsType,
                                                               vextVariantIdx);
                    if (serveCamo != 0xFF)
                        info->playerCamoType = serveCamo;
                    if (isRealPlayerSlot)
                        V_WriteOutfitPartsLoad(vextPartsType, sel,
                                                      info->playerType);
                }
            }
            else
            {
                const bool danglingPT = ptIsCustomRange;
                const bool danglingSel =
                    sel >= outfit::kCustomSelectorStart && sel <= outfit::kCustomSelectorEnd;
                if (danglingPT || danglingSel)
                {
                    const std::uint8_t healPartsType =
                        (!danglingPT && pt != 0)
                            ? pt : kBionicArmVanillaPartsTypeSubstitute;
                    LogWarn("OutfitRuntimeParts: unresolved custom suit partsType=0x%02X selector=0x%02X (playerType %u), healed to vanilla partsType=0x%02X",
                        static_cast<unsigned>(pt), static_cast<unsigned>(sel),
                        static_cast<unsigned>(info->playerType),
                        static_cast<unsigned>(healPartsType));
                    info->playerPartsType = healPartsType;
                    info->playerCamoType  = 0;
                    info->playerFaceEquipId  = 0;
                    if (isRealPlayerSlot)
                        V_WriteOutfitPartsLoad(healPartsType, 0, info->playerType);
                    outfit::shadow::Clear(playerIndex);
                }
            }
        }

        if (isCustom)
        {
            const auto livePT = info->playerType;

            RefreshCaseDArmFlag(entry->partsType, entry->IsArmEnabled(livePT));

            if (!entry->IsArmEnabled(livePT))
            {
                info->playerArmType = 0;
            }
            else if (info->playerArmType == 0)
            {
                bool cachedFlag = false;
                std::uint8_t cachedTier = outfit::shadow::GetArmTier(livePT, &cachedFlag);
                info->playerArmType = cachedFlag ? cachedTier : std::uint8_t{1};
            }

            if (!entry->IsFaceEnabled(livePT))
            {
                info->playerFaceEquipId = 0;
            }

            if (isRealPlayerSlot)
                outfit::RememberPlayerTypeOutfit(
                    livePT, info->playerPartsType, info->playerCamoType);

            feedShadow(entry, entry->HasVariants()
                              ? outfit::GetActiveVariant(entry->partsType) : 0);
        }

        std::uint8_t droppedHead = 0;
        {
            const std::uint8_t wornHead = info->playerFaceEquipId;
            if (wornHead >= outfit::kCustomHeadSlotBase
                && outfit::IsCustomHeadSlot(wornHead))
            {
                const outfit::CustomHeadEntry* h =
                    outfit::TryGetCustomHeadBySlot(wornHead);
                const auto vpt =
                    static_cast<std::uint8_t>(info->playerPartsType & 0xFF);
                const bool offered = h
                    && ((isCustom && entry
                         && entry->HasHeadOptionAnyVariant(h->equipId,
                                                           info->playerType))
                        || (!isCustom
                            && vpt < outfit::kCustomPartsTypeStart
                            && outfit::VanillaExtHasHeadOption(
                                   vpt, h->equipId, info->playerType,
                                   info->playerCamoType)));
                if (!offered || MissionCodeGuard::ShouldBypassHooks())
                {
                    if (isRealPlayerSlot && !MissionCodeGuard::ShouldBypassHooks())
                        LogWarn("OutfitRuntimeParts: head slot 0x%02X (equipId %u) not offered by partsType 0x%02X playerType %u, loaded without it",
                            static_cast<unsigned>(wornHead),
                            static_cast<unsigned>(h ? h->equipId : 0),
                            static_cast<unsigned>(vpt),
                            static_cast<unsigned>(info->playerType));
                    if (!MissionCodeGuard::ShouldBypassHooks())
                        droppedHead = wornHead;
                    info->playerFaceEquipId = 0;
                }
            }
            else if (wornHead >= outfit::kCustomHeadSlotBase)
            {
                LogWarn("OutfitRuntimeParts: head slot 0x%02X has no registered head, loaded without it",
                    static_cast<unsigned>(wornHead));
                if (!MissionCodeGuard::ShouldBypassHooks())
                    droppedHead = wornHead;
                info->playerFaceEquipId = 0;
            }
            else if (isCustom && entry
                     && wornHead >= 1 && wornHead <= 5)
            {
                const std::uint16_t vanEquipId =
                    static_cast<std::uint16_t>(wornHead + 0x20D);
                if (!entry->HasHeadOptionAnyVariant(vanEquipId, info->playerType))
                {
                    info->playerFaceEquipId = 0;
                }
            }
        }

        if (playerIndex == g_LocalPartsSlot.load(std::memory_order_relaxed))
            NoteLocalHeadRealize(info->playerFaceEquipId, droppedHead,
                                 info->playerType, info->playerPartsType,
                                 info->playerCamoType);

        if (info->playerArmType != 0)
            outfit::shadow::SetArmTier(info->playerType, info->playerArmType);

        t_ActiveCustomFaceSlot = 0;
        if (info->playerType == outfit::kPlayerType_Snake
            || info->playerType == outfit::kPlayerType_Avatar)
        {
            if (info->playerFaceEquipId >= outfit::kCustomHeadSlotBase
                && outfit::IsCustomHeadSlot(info->playerFaceEquipId))
            {
                if (MissionCodeGuard::ShouldBypassHooks())
                {
                    info->playerFaceEquipId = 0;
                    g_LastCustomFaceSlot.store(0, std::memory_order_relaxed);
                }
                else
                {
                t_ActiveCustomFaceSlot = info->playerFaceEquipId;
                g_LastCustomFaceSlot.store(info->playerFaceEquipId,
                                           std::memory_order_relaxed);
                info->playerFaceEquipId = 1;
                }
            }
            else
            {
                g_LastCustomFaceSlot.store(0, std::memory_order_relaxed);
            }

            constexpr bool kHideAvatarCreatorFace = false;
            if (info->playerType == outfit::kPlayerType_Avatar)
            {
                void* implBc = nullptr;
                std::uint32_t implSlot = 0;
                if (ResolveImplBcAndSlot(self, playerIndex, &implBc, &implSlot)
                    && implSlot < 32)
                {
                    if (kHideAvatarCreatorFace && t_ActiveCustomFaceSlot != 0)
                    {
                        g_AvatarHideBc.store(implBc, std::memory_order_relaxed);
                        g_AvatarHideSlotMask.fetch_or(1u << implSlot,
                                                      std::memory_order_relaxed);
                    }
                    else
                    {
                        g_AvatarHideSlotMask.fetch_and(~(1u << implSlot),
                                                       std::memory_order_relaxed);
                    }
                }
            }
        }

        const bool             spoofPartsType = isCustom && entry;
        const std::uint8_t     origPartsType  = info->playerPartsType;

        if (spoofPartsType)
        {
            outfit::shadow::SetCurrentSlot(playerIndex);

            std::uint8_t spoofTarget = 0x00;
            if (info->playerType == outfit::kPlayerType_Snake) spoofTarget = 0x01;
            info->playerPartsType = spoofTarget;
        }

        LoadPartsPlayerInfo staffVisualInfo{};
        LoadPartsPlayerInfo* loadInfo = info;
        outfit::ddheadlayers::PartsLoadProbe layerProbe{};
        void* staffLoadImpl = nullptr;
        std::uint32_t staffLoadSlot = 0;
        {
            void* implBc = nullptr;
            std::uint32_t implSlot = 0;
            if (ResolveImplBcAndSlot(self, playerIndex, &implBc, &implSlot))
            {
                staffLoadImpl = implBc;
                staffLoadSlot = implSlot;
                if (BuildLocalStaffFaceInfo(*info, implSlot, staffVisualInfo))
                    loadInfo = &staffVisualInfo;
                const std::uint32_t slotBit = playerIndex < 32 ? (1u << playerIndex) : 0u;
                if (slotBit
                    && (g_ColorReloadSlots.fetch_and(~slotBit, std::memory_order_acq_rel) & slotBit))
                {
                    const bool forgot = outfit::ddheadlayers::ForgetShellTypeInfo(
                        implBc, implSlot, reinterpret_cast<const std::uint8_t*>(loadInfo));
                    if (playerIndex == g_LocalPartsSlot.load(std::memory_order_relaxed))
                        g_ColorReloadPending.store(false, std::memory_order_relaxed);
                    LogInfo("OutfitColor: parts slot %u loads again for the colour package (shell %u%s)",
                        playerIndex, implSlot, forgot ? "" : ", stored type info not reached");
                }
                layerProbe = outfit::ddheadlayers::BeginPartsLoad(
                    implBc, implSlot, reinterpret_cast<const std::uint8_t*>(loadInfo));
            }
        }

        if (playerIndex == g_LocalPartsSlot.load(std::memory_order_relaxed))
            outfit::RequestColorReapply(true);

        __try
        {
            g_OrigLoadPartsNew(self, playerIndex, loadInfo, flags);
        }
        __finally
        {
            if (spoofPartsType) outfit::shadow::ClearCurrentSlot();
        }

        outfit::ddheadlayers::RecordLocalStaffFaceLoad(
            staffLoadImpl, staffLoadSlot, reinterpret_cast<const std::uint8_t*>(info),
            reinterpret_cast<const std::uint8_t*>(loadInfo));
        outfit::ddheadlayers::EndPartsLoad(
            layerProbe, reinterpret_cast<const std::uint8_t*>(loadInfo));

        t_ActiveCustomFaceSlot = 0;

        if (spoofPartsType)
        {
            info->playerPartsType = origPartsType;
            outfit::shadow::ClearCurrentSlot();
        }

    }

    using ApplyQuietSuitShaderParams_t = void (__fastcall*)(void*, int);

    static ApplyQuietSuitShaderParams_t g_OrigApplyQuietSuitShaderParams = nullptr;
    static bool g_InstalledQuietSuitShaderParams = false;

    using SuitShaderProduce_t    = void (__fastcall*)(void*, int);
    using SuitShaderBlendVec_t   = void (__fastcall*)(void*, int, const void*, float);
    using SuitShaderBlendPair_t  = void (__fastcall*)(void*, int, float, float);

    static SuitShaderProduce_t   g_OrigSuitShaderProduce   = nullptr;
    static void* g_SuitShaderProduceTarget = nullptr;
    static SuitShaderBlendVec_t  g_OrigSuitShaderBlendVec  = nullptr;
    static SuitShaderBlendPair_t g_OrigSuitShaderBlendPair = nullptr;
    static void* g_SuitShaderBlendVecTarget  = nullptr;
    static void* g_SuitShaderBlendPairTarget = nullptr;

    static std::atomic<int>  g_QspBlendAttempts{ 0 };
    static std::atomic<bool> g_QspBlendArmed{ false };
    static std::atomic<bool> g_QspBlendGaveUp{ false };
    static std::atomic<bool> g_QspFaultLogged{ false };
    static std::atomic<bool> g_QspBlendFaultLogged{ false };

    constexpr std::size_t    kQSP_OffPlayer2GameObject = 0x138;
    constexpr std::size_t    kQSP_OffPartsControl      = 0x110;
    constexpr std::size_t    kQSP_OffModelArray        = 0x30;
    constexpr std::size_t    kQSP_OffParamEntryCache   = 0x38;
    constexpr std::size_t    kQSP_OffModelsPerSlot     = 0x40;
    constexpr std::size_t    kQSP_OffSlotCount         = 0x54;
    constexpr std::uint32_t  kQSP_KeysPerModel         = 6;
    constexpr std::size_t    kSPT_OffKeyCount          = 0x20;
    constexpr std::size_t    kSPT_OffKeyData           = 0x28;
    constexpr std::size_t    kSPT_OffValueCount        = 0x30;
    constexpr std::size_t    kSPT_OffValueCapacity     = 0x34;
    constexpr std::size_t    kSPT_OffValueData         = 0x38;
    constexpr std::uint32_t  kQSP_MaxSaneModelsPerSlot = 0x100;
    constexpr std::uint32_t  kQSP_MaxSaneSlotCount     = 0x20;
    constexpr std::uint32_t  kSPT_MaxSaneParams        = 0x4000;
    constexpr std::uint32_t  kSPT_EntryIndexMask       = 0x1FFFFFFFu;
    constexpr std::uintptr_t kQSP_UserAddressCeiling   = 0x0000800000000000ull;
    constexpr std::size_t    kQSP_VtblProduce          = 1;
    constexpr std::size_t    kQSP_VtblBlendPair        = 2;
    constexpr std::size_t    kQSP_VtblBlendVec         = 3;
    constexpr std::size_t    kQSP_FnScanBytes          = 0x200;
    constexpr std::size_t    kQSP_FnScanStep           = 0x10;
    constexpr std::size_t    kQSP_FnHeadBytes          = 0x50;
    constexpr int            kQSP_MaxBlendAttempts     = 8;
    constexpr std::uint8_t   kQSP_OpJmpRel32           = 0xE9;
    constexpr std::size_t    kQSP_JmpRel32Len          = 5;
    constexpr int            kQSP_MaxThunkHops         = 4;

    static bool ShaderParamRangeIsSane(std::uintptr_t data, std::uint32_t count,
                                       std::uint32_t stride)
    {
        if (count > kSPT_MaxSaneParams) return false;
        if (count == 0) return true;
        if (data == 0 || data >= kQSP_UserAddressCeiling) return false;
        return data + static_cast<std::uintptr_t>(count) * stride
               < kQSP_UserAddressCeiling;
    }

    static bool ShaderParamTableIsLive(const std::uint8_t* tbl)
    {
        if (reinterpret_cast<std::uintptr_t>(tbl) >= kQSP_UserAddressCeiling)
            return false;

        if (!ShaderParamRangeIsSane(
                *reinterpret_cast<const std::uintptr_t*>(tbl + kSPT_OffKeyData),
                *reinterpret_cast<const std::uint32_t*>(tbl + kSPT_OffKeyCount), 8u))
            return false;

        const std::uint32_t valCount =
            *reinterpret_cast<const std::uint32_t*>(tbl + kSPT_OffValueCount);
        const std::uint32_t valCap =
            *reinterpret_cast<const std::uint32_t*>(tbl + kSPT_OffValueCapacity);
        if (valCount > valCap) return false;

        return ShaderParamRangeIsSane(
            *reinterpret_cast<const std::uintptr_t*>(tbl + kSPT_OffValueData),
            valCap, 16u);
    }

    static bool CachedEntryIsStale(const std::uint8_t* tbl, const void* cached)
    {
        if (!cached) return false;

        if (*reinterpret_cast<const std::uintptr_t*>(tbl + kSPT_OffValueData) == 0)
            return true;

        const std::uintptr_t keyData =
            *reinterpret_cast<const std::uintptr_t*>(tbl + kSPT_OffKeyData);
        const std::uint32_t keyCount =
            *reinterpret_cast<const std::uint32_t*>(tbl + kSPT_OffKeyCount);
        if (keyData == 0 || keyCount == 0) return true;

        const std::uintptr_t c = reinterpret_cast<std::uintptr_t>(cached);
        if (c < keyData) return true;
        if (c >= keyData + static_cast<std::uintptr_t>(keyCount) * 8u) return true;
        if (((c - keyData) % 8u) != 0) return true;

        const std::uint32_t valCap =
            *reinterpret_cast<const std::uint32_t*>(tbl + kSPT_OffValueCapacity);
        const std::uint32_t idx =
            *reinterpret_cast<const std::uint32_t*>(
                reinterpret_cast<const std::uint8_t*>(cached) + 4)
            & kSPT_EntryIndexMask;
        return idx >= valCap;
    }

    static void* ResolveGateParts(void* self)
    {
        void* parts = nullptr;
        __try
        {
            auto* go = *reinterpret_cast<std::uint8_t**>(
                reinterpret_cast<std::uint8_t*>(self) + kQSP_OffPlayer2GameObject);
            if (go) parts = *reinterpret_cast<void**>(go + kQSP_OffPartsControl);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { parts = nullptr; }
        return parts;
    }

    static void SanitizeShaderParamSlot(void* partsControl, int slot)
    {
        __try
        {
            auto* parts = reinterpret_cast<std::uint8_t*>(partsControl);
            if (parts)
            {
                const std::uint32_t slotCount =
                    *reinterpret_cast<std::uint8_t*>(parts + kQSP_OffSlotCount);
                const std::uint32_t perSlot = *reinterpret_cast<std::uint32_t*>(
                    parts + kQSP_OffModelsPerSlot);
                auto** tables =
                    *reinterpret_cast<void***>(parts + kQSP_OffModelArray);
                auto** cache =
                    *reinterpret_cast<void***>(parts + kQSP_OffParamEntryCache);

                if (slot >= 0 && slotCount != 0
                 && slotCount <= kQSP_MaxSaneSlotCount
                 && static_cast<std::uint32_t>(slot) < slotCount
                 && tables && perSlot != 0
                 && perSlot <= kQSP_MaxSaneModelsPerSlot)
                {
                    const std::uint32_t base =
                        static_cast<std::uint32_t>(slot) * perSlot;
                    for (std::uint32_t i = 0; i < perSlot; ++i)
                    {
                        auto* tbl =
                            reinterpret_cast<std::uint8_t*>(tables[base + i]);
                        if (!tbl) continue;

                        const std::uint32_t first =
                            (base + i) * kQSP_KeysPerModel;

                        if (!ShaderParamTableIsLive(tbl))
                        {
                            tables[base + i] = nullptr;
                            if (cache)
                                for (std::uint32_t k = 0; k < kQSP_KeysPerModel; ++k)
                                    cache[first + k] = nullptr;
                            continue;
                        }

                        if (!cache) continue;
                        for (std::uint32_t k = 0; k < kQSP_KeysPerModel; ++k)
                        {
                            if (!CachedEntryIsStale(tbl, cache[first + k])) continue;
                            cache[first + k] = nullptr;
                        }
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static std::size_t SafeCopyBoundedCode(const void* fn, std::uint8_t* dst,
                                           std::size_t n)
    {
        std::size_t got = 0;
        while (got + kQSP_FnScanStep <= n)
        {
            __try
            {
                std::memcpy(dst + got,
                            static_cast<const std::uint8_t*>(fn) + got,
                            kQSP_FnScanStep);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { break; }
            got += kQSP_FnScanStep;
        }
        return got;
    }

    static bool QspCodeInImage(const std::uint8_t* p, std::size_t n)
    {
        const std::uintptr_t base = GetExeBase();
        const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(p);
        if (!base || addr <= base) return false;
        __try
        {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                base + static_cast<std::uintptr_t>(dos->e_lfanew));
            if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
            return addr + n <= base + nt->OptionalHeader.SizeOfImage;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static void* FollowQspThunk(void* fn)
    {
        auto* p = static_cast<std::uint8_t*>(fn);
        for (int hop = 0; hop <= kQSP_MaxThunkHops; ++hop)
        {
            if (!QspCodeInImage(p, kQSP_JmpRel32Len)) return nullptr;

            bool isJump = false;
            std::int32_t rel = 0;
            __try
            {
                isJump = p[0] == kQSP_OpJmpRel32;
                if (isJump) rel = *reinterpret_cast<const std::int32_t*>(p + 1);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }

            if (!isJump) return p;
            p = p + kQSP_JmpRel32Len + rel;
        }
        return nullptr;
    }

    static bool SuitShaderBlendPatternMatches(const void* fn)
    {
        if (!fn) return false;

        std::uint8_t code[kQSP_FnScanBytes];
        std::size_t len = SafeCopyBoundedCode(fn, code, sizeof code);
        if (len < kQSP_FnHeadBytes) return false;

        for (std::size_t i = 0; i + 4 <= len; ++i)
            if (code[i] == 0xCC && code[i + 1] == 0xCC
             && code[i + 2] == 0xCC && code[i + 3] == 0xCC) { len = i; break; }

        const std::size_t head = len < kQSP_FnHeadBytes ? len : kQSP_FnHeadBytes;
        bool readsModelsPerSlot = false;
        for (std::size_t i = 0; i + 3 <= head; ++i)
            if (code[i] == 0x8B && code[i + 1] == 0x41 && code[i + 2] == 0x40)
            { readsModelsPerSlot = true; break; }
        if (!readsModelsPerSlot) return false;

        for (std::size_t i = 0; i + 6 <= len; ++i)
            if (code[i] == 0x81 && code[i + 1] == 0xE1
             && code[i + 2] == 0xFF && code[i + 3] == 0xFF
             && code[i + 4] == 0xFF && code[i + 5] == 0x1F)
                return true;
        return false;
    }

    static bool SuitShaderProducePatternMatches(const void* fn)
    {
        if (!fn) return false;

        std::uint8_t code[kQSP_FnScanBytes];
        std::size_t len = SafeCopyBoundedCode(fn, code, sizeof code);
        if (len < kQSP_FnHeadBytes) return false;

        for (std::size_t i = 0; i + 4 <= len; ++i)
            if (code[i] == 0xCC && code[i + 1] == 0xCC
             && code[i + 2] == 0xCC && code[i + 3] == 0xCC) { len = i; break; }

        const std::size_t head = len < kQSP_FnHeadBytes ? len : kQSP_FnHeadBytes;
        bool readsModelsPerSlot = false;
        for (std::size_t i = 0; i + 3 <= head; ++i)
            if (code[i] == 0x8B && code[i + 1] == 0x41 && code[i + 2] == 0x40)
            { readsModelsPerSlot = true; break; }
        if (!readsModelsPerSlot) return false;

        for (std::size_t i = 0; i + 6 <= len; ++i)
            if (code[i] == 0x81 && code[i + 1] == 0xE1
             && code[i + 2] == 0xFF && code[i + 3] == 0xFF
             && code[i + 4] == 0xFF && code[i + 5] == 0x1F)
                return false;

        for (std::size_t i = 0; i + 4 <= len; ++i)
            if (code[i] == 0x48 && code[i + 1] == 0x89
             && code[i + 2] == 0x04 && code[i + 3] == 0xD1)
                return true;
        return false;
    }

    static bool g_QspSanitizeInProducer = true;

    static void CallSuitShaderProduceGuarded(void* partsControl, int slot)
    {
        __try
        {
            g_OrigSuitShaderProduce(partsControl, slot);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (!g_QspBlendFaultLogged.exchange(true))
                LogWarn("OutfitRuntimeParts: suit shader-param rebuild faulted on slot %d, caught, last frame's params kept", slot);
        }
    }

    static void __fastcall hkSuitShaderProduce(void* partsControl, int slot)
    {
        if (!g_OrigSuitShaderProduce) return;

        if (g_QspSanitizeInProducer)
            SanitizeShaderParamSlot(partsControl, slot);
        CallSuitShaderProduceGuarded(partsControl, slot);
    }

    static void CallSuitShaderBlendVecGuarded(void* partsControl, int slot,
                                              const void* colour, float weight)
    {
        __try
        {
            g_OrigSuitShaderBlendVec(partsControl, slot, colour, weight);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (!g_QspBlendFaultLogged.exchange(true))
                LogWarn("OutfitRuntimeParts: suit shader-param blend faulted on slot %d, caught, last frame's params kept", slot);
        }
    }

    static void CallSuitShaderBlendPairGuarded(void* partsControl, int slot,
                                               float a, float b)
    {
        __try
        {
            g_OrigSuitShaderBlendPair(partsControl, slot, a, b);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (!g_QspBlendFaultLogged.exchange(true))
                LogWarn("OutfitRuntimeParts: suit shader-param blend faulted on slot %d, caught, last frame's params kept", slot);
        }
    }

    static void __fastcall hkSuitShaderBlendVec(void* partsControl, int slot,
                                                const void* colour, float weight)
    {
        if (!g_OrigSuitShaderBlendVec) return;
        SanitizeShaderParamSlot(partsControl, slot);
        CallSuitShaderBlendVecGuarded(partsControl, slot, colour, weight);
    }

    static void __fastcall hkSuitShaderBlendPair(void* partsControl, int slot,
                                                 float a, float b)
    {
        if (!g_OrigSuitShaderBlendPair) return;
        SanitizeShaderParamSlot(partsControl, slot);
        CallSuitShaderBlendPairGuarded(partsControl, slot, a, b);
    }

    static bool ReadPartsControlVtbl(void* partsControl, void** outVec,
                                     void** outPair, void** outProduce)
    {
        bool ok = false;
        __try
        {
            void** vtbl = *reinterpret_cast<void***>(partsControl);
            if (vtbl)
            {
                *outVec  = vtbl[kQSP_VtblBlendVec];
                *outPair = vtbl[kQSP_VtblBlendPair];
                *outProduce = vtbl[kQSP_VtblProduce];
                ok = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
        return ok;
    }

    static void InstallSuitShaderBlendGuards(void* partsControl)
    {
        if (g_QspBlendArmed.load(std::memory_order_relaxed)) return;
        if (g_QspBlendAttempts.fetch_add(1, std::memory_order_relaxed)
                >= kQSP_MaxBlendAttempts)
        {
            if (!g_QspBlendGaveUp.exchange(true))
                LogError("OutfitRuntimeParts: suit shader-param blend not found on the parts vtable, blend guard off");
            return;
        }

        void* fnVec     = nullptr;
        void* fnPair    = nullptr;
        void* fnProduce = nullptr;
        if (!ReadPartsControlVtbl(partsControl, &fnVec, &fnPair, &fnProduce))
            return;
        fnVec     = FollowQspThunk(fnVec);
        fnPair    = FollowQspThunk(fnPair);
        fnProduce = FollowQspThunk(fnProduce);
        if (!SuitShaderBlendPatternMatches(fnVec)) return;
        if (!SuitShaderBlendPatternMatches(fnPair)) return;
        if (!SuitShaderProducePatternMatches(fnProduce)) return;

        const bool okVec = CreateAndEnableHook(
            fnVec, reinterpret_cast<void*>(&hkSuitShaderBlendVec),
            reinterpret_cast<void**>(&g_OrigSuitShaderBlendVec));
        const bool okPair = CreateAndEnableHook(
            fnPair, reinterpret_cast<void*>(&hkSuitShaderBlendPair),
            reinterpret_cast<void**>(&g_OrigSuitShaderBlendPair));
        const bool okProduce = CreateAndEnableHook(
            fnProduce, reinterpret_cast<void*>(&hkSuitShaderProduce),
            reinterpret_cast<void**>(&g_OrigSuitShaderProduce));

        if (!okVec || !okPair || !okProduce)
        {
            if (okProduce)
            {
                DisableAndRemoveHook(fnProduce);
                g_OrigSuitShaderProduce = nullptr;
            }
            if (okVec)
            {
                DisableAndRemoveHook(fnVec);
                g_OrigSuitShaderBlendVec = nullptr;
            }
            if (okPair)
            {
                DisableAndRemoveHook(fnPair);
                g_OrigSuitShaderBlendPair = nullptr;
            }
            LogError("OutfitRuntimeParts: suit shader-param blend guard hook failed (vec=%s pair=%s rebuild=%s)",
                okVec ? "OK" : "fail", okPair ? "OK" : "fail",
                okProduce ? "OK" : "fail");
            return;
        }

        g_SuitShaderBlendVecTarget  = fnVec;
        g_SuitShaderBlendPairTarget = fnPair;
        g_SuitShaderProduceTarget   = fnProduce;
        g_QspBlendArmed.store(true, std::memory_order_release);
    }

    static void CallQuietSuitShaderParamsGuarded(void* self, int slot)
    {
        __try
        {
            g_OrigApplyQuietSuitShaderParams(self, slot);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (!g_QspFaultLogged.exchange(true))
                LogWarn("OutfitRuntimeParts: suit shader-param pass faulted on slot %d, caught, last frame's params kept", slot);
        }
    }

    static void __fastcall hkApplyQuietSuitShaderParams(void* self, int slot)
    {
        if (!g_OrigApplyQuietSuitShaderParams) return;

        if (self)
        {
            void* parts = ResolveGateParts(self);
            if (parts)
            {
                InstallSuitShaderBlendGuards(parts);
                SanitizeShaderParamSlot(parts, slot);
            }
        }

        CallQuietSuitShaderParamsGuarded(self, slot);
    }
}

namespace outfit
{
    void RequestPartsRestream()
    {
        g_ColorReloadPending.store(true, std::memory_order_relaxed);
        g_RestreamRequested.store(true, std::memory_order_release);
    }

    const OutfitColor* GetServablePackColor(const OutfitEntry& entry, std::uint8_t playerType, std::uint8_t variant)
    {
        return PackColorToServe(&entry, playerType, variant);
    }

    std::uint64_t GetServedColorFpk()
    {
        return g_ServedColorFpk.load(std::memory_order_relaxed);
    }

    namespace fv2ontop
    {
        using GetFile_t = void* (__fastcall*)(void* blockCtrl, std::uint32_t index, std::uint64_t path);
        using Apply_t = void (__fastcall*)(void* file, void* builder, void* director, void* p4, void* p5, void* p6,
                                           std::uint32_t p7, bool reset, bool p9);
        using WorkExtra_t = void (__fastcall*)(void* work);
        using PathFromText_t = void* (__fastcall*)(std::uint64_t* path, const char* text);
        using PathDtor_t = void (__fastcall*)(std::uint64_t* path);
        using GetController_t = std::uint8_t* (__fastcall*)(void*);

        constexpr std::uint8_t kGetFileCode[] = { 0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE,
                                                  0xFF, 0xFF, 0xFF, 0x48, 0x89, 0x5C, 0x24, 0x48, 0x49, 0x8B, 0xF8 };
        constexpr std::uint8_t kApplyCode[] = { 0x48, 0x83, 0xEC, 0x58, 0x48, 0x8B, 0x42, 0x10, 0x4C, 0x8B, 0x52, 0x28,
                                                0x4D, 0x8B, 0xD8, 0x48, 0x8B, 0x90, 0xB0, 0x00, 0x00, 0x00 };
        constexpr std::uint8_t kWorkExtraCode[] = { 0x40, 0x55, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x38, 0x48, 0x8B, 0x69, 0x58,
                                                    0x4C, 0x8B, 0xF1, 0x48, 0x85, 0xED, 0x0F, 0x84 };
        constexpr std::uint8_t kWorkTypeExtra = 0x11;

        struct Fns
        {
            GetFile_t getFile = nullptr;
            void* blockCtrlSlot = nullptr;
            Apply_t apply = nullptr;
            WorkExtra_t extra = nullptr;
            PathFromText_t pathFromText = nullptr;
            PathDtor_t pathDtor = nullptr;
        };

        bool CodeIsSEH(const void* code, const std::uint8_t* expect, std::size_t size)
        {
            __try { return code && std::memcmp(code, expect, size) == 0; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        const Fns* Resolve()
        {
            static Fns s_fns;
            static std::atomic<int> s_state{ 0 };   // 0 not tried, 1 ready, -1 off
            const int state = s_state.load(std::memory_order_acquire);
            if (state != 0)
                return state > 0 ? &s_fns : nullptr;
            Fns f;
            f.getFile = reinterpret_cast<GetFile_t>(ResolveGameAddress(gAddr.Fv2_GetFileFromPartsBlock));
            f.blockCtrlSlot = ResolveGameAddress(gAddr.Fv2_PartsBlockController);
            f.apply = reinterpret_cast<Apply_t>(ResolveGameAddress(gAddr.Fv2_ApplyFormVariation));
            f.extra = reinterpret_cast<WorkExtra_t>(ResolveGameAddress(gAddr.Fv2_WorkApplyExtra));
            f.pathFromText = reinterpret_cast<PathFromText_t>(ResolveGameAddress(gAddr.FoxPath_FromCString));
            f.pathDtor = reinterpret_cast<PathDtor_t>(ResolveGameAddress(gAddr.Fox_Path_Dtor));
            const bool ok = f.getFile && f.blockCtrlSlot && f.apply && f.extra && f.pathFromText && f.pathDtor
                && CodeIsSEH(reinterpret_cast<void*>(f.getFile), kGetFileCode, sizeof(kGetFileCode))
                && CodeIsSEH(reinterpret_cast<void*>(f.apply), kApplyCode, sizeof(kApplyCode))
                && CodeIsSEH(reinterpret_cast<void*>(f.extra), kWorkExtraCode, sizeof(kWorkExtraCode));
            if (ok)
                s_fns = f;
            else
                LogWarn("OutfitColor: the fv2 apply without reset is not ported or not the known code, colour channels "
                        "cannot stack: only one shows (the last applied)");
            s_state.store(ok ? 1 : -1, std::memory_order_release);
            return ok ? &s_fns : nullptr;
        }

        struct Local
        {
            std::uint8_t* controller = nullptr;
            std::uint8_t* work = nullptr;
            std::uint32_t index = 0;
        };

        bool LocalWorkSEH(std::uint8_t* system, Local& out)
        {
            __try
            {
                auto* impl = *reinterpret_cast<std::uint8_t**>(system + 0x8);
                if (!impl)
                    return false;
                out.index = *reinterpret_cast<std::uint32_t*>(impl + 0x218);
                void* owner = *reinterpret_cast<void**>(impl + 0x58);
                if (!owner)
                    return false;
                out.controller = reinterpret_cast<GetController_t>(
                    (*reinterpret_cast<void***>(owner))[0x178 / sizeof(void*)])(owner);   // ask the owner for its Fova2 controller
                if (!out.controller)
                    return false;
                const std::uint8_t first = *(out.controller + 0x68);   // index of the controller's first work
                auto* works = *reinterpret_cast<std::uint8_t**>(out.controller + 0x30);
                if (!works || out.index < first)
                    return false;
                out.work = works + static_cast<std::size_t>(out.index - first) * 0x160;   // every work is 0x160 bytes
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool ApplySEH(const Fns& f, const Local& l, std::uint64_t path)
        {
            __try
            {
                void* blockCtrl = *reinterpret_cast<void**>(f.blockCtrlSlot);
                if (!blockCtrl)
                    return false;
                void* file = f.getFile(blockCtrl, l.index, path);
                if (!file)
                    return false;
                auto* director = *reinterpret_cast<std::uint8_t**>(l.work + 0xC8);
                if (!director || *(director + 0x59) == 0)   // zero when the director has no builder
                    return false;
                auto** builders = *reinterpret_cast<void***>(director + 0x48);
                void* builder = builders ? builders[*(director + 0x6A)] : nullptr;   // the director's main builder
                if (!builder)
                    return false;
                f.apply(file, builder, director, nullptr, nullptr, nullptr, 0, false, false);

                auto* tbl = *reinterpret_cast<std::uint8_t**>(
                    *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(l.work + 0xC0) + 0x78) + 0x240);
                const std::uint32_t slot = static_cast<std::uint32_t>(*(l.work + 0x120))
                    - *reinterpret_cast<std::uint32_t*>(tbl + 0x14);
                auto* entry = *reinterpret_cast<std::uint8_t**>(tbl + 0x8) + (static_cast<std::size_t>(slot) << 7);
                *(entry + 0x79) |= 2;   // flag the work for a redraw

                auto* types = *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(
                    *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(l.controller + 0x38) + 0x138) + 0x60) + 0x50);
                if (types && types[l.index] == kWorkTypeExtra)
                    f.extra(l.work);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool MakePathSEH(const Fns& f, std::uint64_t& path, const char* text)
        {
            __try { f.pathFromText(&path, text); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        void FreePathSEH(const Fns& f, std::uint64_t& path)
        {
            __try { f.pathDtor(&path); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    namespace fv2onhead
    {
        using ApplyOnly_t = void (__fastcall*)(void* file, void* model, const std::uint32_t* groups, const int* methods,
                                               std::uint32_t count, bool reset, bool resetSim);
        using ModelAt_t = void* (__fastcall*)(void* container, std::uint32_t index);

        constexpr std::uint8_t kApplyOnlyCode[] = { 0x48, 0x83, 0xEC, 0x58, 0x0F, 0xB6, 0x84, 0x24 };
        constexpr std::size_t kHornSlot = 5;
        constexpr std::size_t kFaceSlot = 1;
        constexpr std::size_t kFaceDecoSlot = 2;
        constexpr std::uint32_t kMaxModels = 64;

        struct Fns
        {
            ApplyOnly_t apply = nullptr;
            ModelAt_t modelAt = nullptr;
        };

        const Fns* Resolve()
        {
            static Fns s_fns;
            static std::atomic<int> s_state{ 0 };   // 0 not tried, 1 ready, -1 off
            const int state = s_state.load(std::memory_order_acquire);
            if (state != 0)
                return state > 0 ? &s_fns : nullptr;
            Fns f;
            f.apply = reinterpret_cast<ApplyOnly_t>(
                ResolveGameAddress(gAddr.FormVariationFile2_ApplyOnlyMeshAndTextureVariation));
            f.modelAt = reinterpret_cast<ModelAt_t>(ResolveGameAddress(gAddr.Fox_ModelFromHandle));
            const bool ok = f.apply && f.modelAt
                && fv2ontop::CodeIsSEH(reinterpret_cast<void*>(f.apply), kApplyOnlyCode, sizeof(kApplyOnlyCode));
            if (ok)
                s_fns = f;
            else
                LogWarn("OutfitColor: the head model apply is not ported or not the known code, head colours are off");
            s_state.store(ok ? 1 : -1, std::memory_order_release);
            return ok ? &s_fns : nullptr;
        }

        bool IsLoadedFile(const std::uint8_t* file)
        {
            return file && file[0x40] == 2 && *reinterpret_cast<void* const*>(file + 0x60) != nullptr;   // state 2 = loaded, +0x60 its data
        }

        bool IsHeadFile(const std::uint8_t* file, const std::uint64_t* codes, std::size_t count)
        {
            const std::uint64_t path = *reinterpret_cast<const std::uint64_t*>(file + 0x30);
            for (std::size_t i = 0; i < count; ++i)
                if (codes[i] && codes[i] == path)
                    return true;
            return false;
        }

        HeadFv2 ApplySEH(const Fns& f, std::uint8_t* work, void* file, const std::uint64_t* codes, std::size_t count,
                         bool resetHead)
        {
            __try
            {
                if ((work[0x152] & 1) == 0)   // bit 0: the work's apply is done
                    return HeadFv2::NotReady;
                for (const std::size_t slot : { kHornSlot, kFaceSlot })
                {
                    const auto* headFile = *reinterpret_cast<const std::uint8_t**>(work + slot * 8);
                    auto* container = *reinterpret_cast<std::uint8_t**>(work + 0x58 + slot * 8);
                    if (!headFile || !container || !IsHeadFile(headFile, codes, count))
                        continue;
                    std::uint32_t models = *reinterpret_cast<const std::uint32_t*>(container + 0x20);
                    if (models > kMaxModels)
                        models = kMaxModels;
                    const std::uint64_t active = *reinterpret_cast<const std::uint64_t*>(container);
                    const std::uint32_t subOffset = *reinterpret_cast<const std::uint32_t*>(container + 0x58);   // per-model fv2 table
                    auto* const* subs = subOffset ? reinterpret_cast<std::uint8_t* const*>(container + subOffset) : nullptr;
                    auto* deco = slot == kFaceSlot ? *reinterpret_cast<std::uint8_t**>(work + kFaceDecoSlot * 8) : nullptr;
                    bool applied = false;
                    for (std::uint32_t i = 0; i < models; ++i)
                    {
                        if (((active >> i) & 1) == 0)
                            continue;
                        void* model = f.modelAt(container, i);
                        if (!model)
                            continue;
                        bool reset = false;
                        if (resetHead)
                        {
                            std::uint8_t* own = i == 0 && IsLoadedFile(deco) ? deco : (subs ? subs[i] : nullptr);
                            if (IsLoadedFile(own))
                                f.apply(own, model, nullptr, nullptr, 0, true, true);
                            else
                                reset = true;
                        }
                        f.apply(file, model, nullptr, nullptr, 0, reset, false);
                        applied = true;
                    }
                    return applied ? HeadFv2::Applied : HeadFv2::NoHeadModel;
                }
                return HeadFv2::NoHeadModel;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return HeadFv2::Failed;
            }
        }
    }

    namespace fv2iris
    {
        using ApplyByGroup_t = bool (__fastcall*)(void* file, void* builder, void* director, void* resMgr,
                                                  const std::uint64_t* settings, std::uint32_t count, void* out,
                                                  std::uint32_t outCapacity, bool reset, bool resetSim);

        constexpr std::uintptr_t kApplyByGroup_En154 = 0x1404E1ED0ull;
        constexpr std::uint8_t kApplyByGroupCode[] = { 0x48, 0x83, 0xEC, 0x68, 0x48, 0x8B, 0x42, 0x10, 0x4C, 0x8B, 0x52,
                                                       0x28, 0x4D, 0x8B, 0xD8, 0x48, 0x8B, 0x90, 0xB0, 0x00, 0x00, 0x00 };
        constexpr std::uint8_t  kIrisGroup     = 110;
        constexpr std::uint8_t  kSkinGroup     = 100;
        constexpr std::uint64_t kMethodUseMask = 1ull << 16;
        constexpr std::uint32_t kNoUnit        = 0x7FFF;

        ApplyByGroup_t Resolve()
        {
            static ApplyByGroup_t s_fn = nullptr;
            static std::atomic<int> s_state{ 0 };   // 0 not tried, 1 ready, -1 off
            const int state = s_state.load(std::memory_order_acquire);
            if (state != 0)
                return state > 0 ? s_fn : nullptr;
            ApplyByGroup_t fn = nullptr;
            if (IsEn154Family(gGameBuild))
                fn = reinterpret_cast<ApplyByGroup_t>(ResolveGameAddress(kApplyByGroup_En154));
            const bool ok = fn && fv2ontop::CodeIsSEH(reinterpret_cast<void*>(fn), kApplyByGroupCode,
                                                      sizeof(kApplyByGroupCode));
            if (ok)
                s_fn = fn;
            else
                LogWarn("OutfitIris: the fv2 group apply is not ported or not the known code, eyeFv2 is off");
            s_state.store(ok ? 1 : -1, std::memory_order_release);
            return ok ? s_fn : nullptr;
        }

        struct Groups
        {
            std::uint32_t irisUnits = 0;
            std::uint32_t skinUnits = 0;
            bool other = false;
        };

        bool ScanGroupsSEH(const std::uint8_t* file, Groups& out)
        {
            __try
            {
                const auto* data = *reinterpret_cast<const std::uint8_t* const*>(file + 0x60);
                const std::uint16_t tableOffset = *reinterpret_cast<const std::uint16_t*>(data + 0x8);
                const std::uint16_t groups = *reinterpret_cast<const std::uint16_t*>(data + 0xC);
                for (std::uint16_t i = 0; i < groups; ++i)
                {
                    const std::uint8_t* group = data + tableOffset + static_cast<std::size_t>(i) * 0x10;
                    const std::uint8_t units = group[2];   // +0 id, +1 type, +2 unit count
                    if (group[0] == kIrisGroup && units && !out.irisUnits)
                        out.irisUnits = units;
                    else if (group[0] == kSkinGroup && units && !out.skinUnits)
                        out.skinUnits = units;
                    else
                        out.other = true;
                }
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        IrisFv2 ApplySEH(ApplyByGroup_t apply, std::uint8_t* work, void* file, std::uint8_t eyeColor)
        {
            __try
            {
                if ((work[0x152] & 1) == 0)   // bit 0: the work's apply is done
                    return IrisFv2::NotReady;
                auto* director = *reinterpret_cast<std::uint8_t**>(work + 0xC8);
                if (!director || director[0x59] == 0)
                    return IrisFv2::NotReady;
                auto** builders = *reinterpret_cast<void***>(director + 0x48);
                auto* builder = builders ? static_cast<std::uint8_t*>(builders[director[0x6A]]) : nullptr;
                if (!builder || !*reinterpret_cast<void**>(builder + 0x10) || !*reinterpret_cast<void**>(builder + 0x28))
                    return IrisFv2::NotReady;

                Groups groups;
                if (!ScanGroupsSEH(static_cast<const std::uint8_t*>(file), groups) || !groups.irisUnits)
                    return IrisFv2::NoIrisGroup;
                if (groups.other)
                    return IrisFv2::OtherGroups;

                const bool staff = IsDdPlayerType(work[0x123]) && *reinterpret_cast<void**>(work + 0x8);   // the DD face branch filled +0xB0..+0xB8
                const std::uint32_t unit = eyeColor == kEyeColorStaff
                    ? *reinterpret_cast<const std::uint32_t*>(work + 0xB0)   // the staff face's iris unit
                    : static_cast<std::uint32_t>(eyeColor) - 1;
                if ((eyeColor == kEyeColorStaff && !staff) || unit >= groups.irisUnits || unit >= 32)
                    return IrisFv2::NoStaffIris;

                std::uint64_t settings[2] = { ((1ull << unit) << 32) | kMethodUseMask | kIrisGroup };
                std::uint32_t count = 1;
                if (groups.skinUnits)
                {
                    const std::uint32_t skin = *reinterpret_cast<const std::uint32_t*>(work + 0xB4);   // the staff face's skin unit
                    if (!staff || skin >= groups.skinUnits || skin >= 32)
                        return IrisFv2::OtherGroups;
                    settings[count++] = ((1ull << skin) << 32) | kMethodUseMask | kSkinGroup;
                }
                apply(file, builder, director, nullptr, settings, count, nullptr, 0, false, false);
                return IrisFv2::Applied;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return IrisFv2::Failed;
            }
        }
    }

    IrisFv2 ApplyIrisFv2(std::uint64_t fv2Code, std::uint8_t eyeColor)
    {
        if (!fv2Code || eyeColor == kEyeColorOff || MissionCodeGuard::ShouldBypassHooks())
            return IrisFv2::Failed;
        const fv2iris::ApplyByGroup_t apply = fv2iris::Resolve();
        if (!apply)
            return IrisFv2::Failed;
        if (!IsLocalFovaModelReady())
            return IrisFv2::NotReady;
        void* file = ddheadlayers::FindLocalHeadFile(fv2Code);
        if (!file)
            return IrisFv2::NotLoaded;
        auto* system = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.Player2System));
        fv2ontop::Local local;
        if (!system || !fv2ontop::LocalWorkSEH(system, local))
            return IrisFv2::NotReady;
        return fv2iris::ApplySEH(apply, local.work, file, eyeColor);
    }

    bool CanApplyFv2OnTop()
    {
        return fv2ontop::Resolve() != nullptr;
    }

    HeadFv2 ApplyFv2OnHead(std::uint64_t fv2Code, const std::uint64_t* headFv2Codes, std::size_t headFv2Count,
                           bool resetHead)
    {
        if (!fv2Code || !headFv2Codes || !headFv2Count || MissionCodeGuard::ShouldBypassHooks())
            return HeadFv2::Failed;
        const fv2onhead::Fns* f = fv2onhead::Resolve();
        if (!f)
            return HeadFv2::Failed;
        if (!IsLocalFovaModelReady())
            return HeadFv2::NotReady;
        void* file = ddheadlayers::FindLocalHeadFile(fv2Code);
        if (!file)
            return HeadFv2::NotLoaded;
        auto* system = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.Player2System));
        fv2ontop::Local local;
        if (!system || !fv2ontop::LocalWorkSEH(system, local))
            return HeadFv2::NotReady;
        return fv2onhead::ApplySEH(*f, local.work, file, headFv2Codes, headFv2Count, resetHead);
    }

    bool ApplyFv2OnTop(const char* fv2Path)
    {
        if (!fv2Path || !*fv2Path || MissionCodeGuard::ShouldBypassHooks())
            return false;
        const fv2ontop::Fns* f = fv2ontop::Resolve();
        auto* system = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.Player2System));
        fv2ontop::Local local;
        if (!f || !system || !fv2ontop::LocalWorkSEH(system, local))
            return false;
        std::uint64_t path = 0;
        if (!fv2ontop::MakePathSEH(*f, path, fv2Path))
            return false;
        const bool ok = fv2ontop::ApplySEH(*f, local, path);
        fv2ontop::FreePathSEH(*f, path);
        return ok;
    }

    bool IsLocalFovaModelReady()
    {
        if (g_ColorReloadPending.load(std::memory_order_relaxed)
            || g_RestreamRequested.load(std::memory_order_relaxed)
            || g_ColorReloadSlots.load(std::memory_order_relaxed) != 0
            || g_RestreamPendingTicks.load(std::memory_order_relaxed) > 0)
            return false;   // a reload is on its way: whatever is applied now is thrown away, or lands mid-teardown
        auto* system = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.Player2System));
        return system && FovaModelReadySEH(system);
    }

    std::uint8_t ResolveVanillaPartsTypeForCamo(std::uint8_t camoType)
    {
        if (camoType > kVanillaCamoTypeMax) return 0xFF;
        GetPartsTypeAtCamoType_t fn = g_OrigGetPartsTypeAtCamoType;
        if (!fn)
            fn = reinterpret_cast<GetPartsTypeAtCamoType_t>(ResolveGameAddress(
                gAddr.PlayerInfoInterfaceImpl_GetPartsTypeAtCamoType));
        if (!fn) return 0xFF;
        const std::uint32_t pt = fn(nullptr, camoType);
        if (pt >= kCustomPartsTypeStart) return 0xFF;
        return static_cast<std::uint8_t>(pt);
    }

    static bool FacialBindingDereferenceable(void* self) noexcept
    {
        __try
        {
            const std::uint8_t* binding =
                *reinterpret_cast<std::uint8_t* const*>(
                    reinterpret_cast<const std::uint8_t*>(self) + 0x10);
            const volatile std::uint64_t probe =
                *reinterpret_cast<const volatile std::uint64_t*>(binding + 0xa0 + 0x50);
            (void)probe;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void __fastcall hkPluginFacialApplyMotion(void* self, void* a2, void* a3, float a4)
    {
        if (self && !FacialBindingDereferenceable(self))
        {
            static std::atomic<std::uint32_t> s_skips{ 0 };
            const std::uint32_t n = s_skips.fetch_add(1) + 1;
            if (n <= 8 || (n % 256) == 0)
                LogWarn("OutfitFacialGuard: facial apply skipped, bad AnimControl binding (skip #%u)",
                    static_cast<unsigned>(n));
            return;
        }
        if (g_OrigPluginFacialApplyMotion)
            g_OrigPluginFacialApplyMotion(self, a2, a3, a4);
    }

    std::uint32_t GetLocalPartsSlot()
    {
        return g_LocalPartsSlot.load(std::memory_order_relaxed);
    }

    std::uint8_t GetRealizedLocalHeadSlot()
    {
        return g_RealizedLocalHeadSlot.load(std::memory_order_relaxed);
    }

    bool Install_OutfitRuntimeParts_Hooks()
    {
        ResolveFoxPathApi();

        struct H { void* tgt; void* hk; void** orig; bool* installed; };
        H hooks[] = {
            { ResolveGameAddress(gAddr.LoadPlayerPartsParts),
              reinterpret_cast<void*>(&hkLoadPlayerPartsParts),
              reinterpret_cast<void**>(&g_OrigLoadPartsParts), &g_InstalledParts },
            { ResolveGameAddress(gAddr.LoadPlayerPartsFpk),
              reinterpret_cast<void*>(&hkLoadPlayerPartsFpk),
              reinterpret_cast<void**>(&g_OrigLoadPartsFpk), &g_InstalledFpk },
            { ResolveGameAddress(gAddr.LoadPlayerCamoFpk),
              reinterpret_cast<void*>(&hkLoadPlayerCamoFpk),
              reinterpret_cast<void**>(&g_OrigLoadCamoFpk), &g_InstalledCamo },
            { ResolveGameAddress(gAddr.LoadPlayerSnakeBlackDiamondFpk),
              reinterpret_cast<void*>(&hkLoadPlayerSnakeBlackDiamondFpk),
              reinterpret_cast<void**>(&g_OrigLoadDiamondFpk), &g_InstalledDiamond },
            { ResolveGameAddress(gAddr.LoadPlayerCamoFv2),
              reinterpret_cast<void*>(&hkLoadPlayerCamoFv2),
              reinterpret_cast<void**>(&g_OrigLoadCamoFv2), &g_InstalledCamoFv2 },
            { ResolveGameAddress(gAddr.LoadPlayerSnakeBlackDiamondFv2),
              reinterpret_cast<void*>(&hkLoadPlayerSnakeBlackDiamondFv2),
              reinterpret_cast<void**>(&g_OrigLoadDiamondFv2), &g_InstalledDiamondFv2 },
            { ResolveGameAddress(gAddr.LoadPlayerBionicArmFv2),
              reinterpret_cast<void*>(&hkLoadPlayerBionicArmFv2),
              reinterpret_cast<void**>(&g_OrigLoadBionicArmFv2), &g_InstalledBionicArmFv2 },
            { ResolveGameAddress(gAddr.LoadPlayerBionicArmFpk),
              reinterpret_cast<void*>(&hkLoadPlayerBionicArmFpk),
              reinterpret_cast<void**>(&g_OrigLoadBionicArmFpk), &g_InstalledBionicArmFpk },
            { ResolveGameAddress(gAddr.LoadPlayerSnakeFaceFv2),
              reinterpret_cast<void*>(&hkLoadPlayerSnakeFaceFv2),
              reinterpret_cast<void**>(&g_OrigLoadSnakeFaceFv2), &g_InstalledSnakeFaceFv2 },
            { ResolveGameAddress(gAddr.LoadPlayerSnakeFaceFpk),
              reinterpret_cast<void*>(&hkLoadPlayerSnakeFaceFpk),
              reinterpret_cast<void**>(&g_OrigLoadSnakeFaceFpk), &g_InstalledSnakeFaceFpk },
            { ResolveGameAddress(gAddr.Player2BlockController_LoadPartsNew),
              reinterpret_cast<void*>(&hkLoadPartsNew),
              reinterpret_cast<void**>(&g_OrigLoadPartsNew), &g_InstalledLpn },
            { ResolveGameAddress(gAddr.ResourceTable_DoesNeedFaceFova),
              reinterpret_cast<void*>(&hkDoesNeedFaceFova),
              reinterpret_cast<void**>(&g_OrigDoesNeedFaceFova), &g_InstalledDoesNeedFace },
            { ResolveGameAddress(gAddr.ResourceTable_DoesNeedFaceFovaForAvatar),
              reinterpret_cast<void*>(&hkDoesNeedFaceFovaForAvatar),
              reinterpret_cast<void**>(&g_OrigDoesNeedFaceFovaForAvatar), &g_InstalledDoesNeedFaceForAvatar },
            { ResolveGameAddress(gAddr.EquipController_SetHandSlotEnabled),
              reinterpret_cast<void*>(&hkSetHandSlotEnabled),
              reinterpret_cast<void**>(&g_OrigSetHandSlotEnabled), &g_InstalledSetHandSlotEnabled },
            { ResolveGameAddress(gAddr.Sys_IsArtificialHandEnabled),
              reinterpret_cast<void*>(&hkIsArtificialHandEnabled),
              reinterpret_cast<void**>(&g_OrigIsArtificialHandEnabled), &g_InstalledIsArtificialHand },
            { ResolveGameAddress(gAddr.Sys_IsArtificialHandEnabledForCurrentPlayerType),
              reinterpret_cast<void*>(&hkIsArtificialHandEnabledForCurrentPlayerType),
              reinterpret_cast<void**>(&g_OrigIsArtificialHandForCurrent), &g_InstalledIsArtHandForCurrent },
            { ResolveGameAddress(gAddr.Player2GameObjectImpl_ProcessSignal),
              reinterpret_cast<void*>(&hkProcessSignal),
              reinterpret_cast<void**>(&g_OrigProcessSignal), &g_InstalledProcessSignal },
            { ResolveGameAddress(gAddr.UpdatePartsStatus),
              reinterpret_cast<void*>(&hkUpdatePartsStatus),
              reinterpret_cast<void**>(&g_OrigUpdatePartsStatus), &g_InstalledUpdatePartsStatus },
            { ResolveGameAddress(gAddr.Player2Impl_SetUpParts),
              reinterpret_cast<void*>(&hkPlayer2ImplSetUpParts),
              reinterpret_cast<void**>(&g_OrigPlayer2ImplSetUpParts), &g_InstalledPlayer2ImplSetUpParts },
            { ResolveGameAddress(gAddr.PluginFacial_ApplyMotion),
              reinterpret_cast<void*>(&hkPluginFacialApplyMotion),
              reinterpret_cast<void**>(&g_OrigPluginFacialApplyMotion), &g_InstalledFacialCrashGuard },
            { ResolveGameAddress(gAddr.PlayerInfoInterfaceImpl_GetPartsTypeAtCamoType),
              reinterpret_cast<void*>(&hkGetPartsTypeAtCamoType),
              reinterpret_cast<void**>(&g_OrigGetPartsTypeAtCamoType), &g_InstalledPartsAtCamo },
            { ResolveGameAddress(gAddr.LoadAvatarFaceFv2),
              reinterpret_cast<void*>(&hkLoadAvatarFaceFv2),
              reinterpret_cast<void**>(&g_OrigLoadAvatarFaceFv2), &g_InstalledAvatarFaceFv2 },
            { ResolveGameAddress(gAddr.LoadAvatarFaceFpk),
              reinterpret_cast<void*>(&hkLoadAvatarFaceFpk),
              reinterpret_cast<void**>(&g_OrigLoadAvatarFaceFpk), &g_InstalledAvatarFaceFpk },
            { ResolveGameAddress(gAddr.AvatarFaceEditUpdate),
              reinterpret_cast<void*>(&hkAvatarFaceEditUpdate),
              reinterpret_cast<void**>(&g_OrigAvatarFaceEditUpdate), &g_InstalledAvatarFaceEdit },
            { ResolveGameAddress(gAddr.LoadAvatarHeadOptionFv2),
              reinterpret_cast<void*>(&hkLoadAvatarHeadOptionFv2),
              reinterpret_cast<void**>(&g_OrigLoadAvatarHeadOptionFv2), &g_InstalledAvatarHeadOptionFv2 },
            { ResolveGameAddress(gAddr.LoadAvatarHeadOptionFpk),
              reinterpret_cast<void*>(&hkLoadAvatarHeadOptionFpk),
              reinterpret_cast<void**>(&g_OrigLoadAvatarHeadOptionFpk), &g_InstalledAvatarHeadOptionFpk },
            { ResolveGameAddress(gAddr.Player2BlockController_ApplyQuietSuitShaderParams),
              reinterpret_cast<void*>(&hkApplyQuietSuitShaderParams),
              reinterpret_cast<void**>(&g_OrigApplyQuietSuitShaderParams),
              &g_InstalledQuietSuitShaderParams },
        };
        for (auto& h : hooks)
        {
            if (h.tgt) *h.installed = CreateAndEnableHook(h.tgt, h.hk, h.orig);
        }

        if (ResolveGameAddress(
                gAddr.Player2BlockController_ApplyQuietSuitShaderParams)
            && !g_InstalledQuietSuitShaderParams)
            LogError("OutfitRuntimeParts: Quiet suit shader-param hook failed, suit changes may crash");

        g_FoxModelFromHandle = reinterpret_cast<FoxModelFromHandle_t>(
            ResolveGameAddress(gAddr.Fox_ModelFromHandle));

        if (g_InstalledLpn)
            outfit::ddheadlayers::Install();

        if (g_InstalledUpdatePartsStatus)
        {
            InstallCaseDArmUnpin();
            uniquecharpin::Install();
        }

        return g_InstalledParts || g_InstalledFpk || g_InstalledCamo
            || g_InstalledDiamond || g_InstalledBionicArmFv2 || g_InstalledBionicArmFpk
            || g_InstalledSnakeFaceFv2 || g_InstalledSnakeFaceFpk || g_InstalledLpn;
    }

    void Uninstall_OutfitRuntimeParts_Hooks()
    {
        outfit::ddheadlayers::Uninstall();

        struct U { bool* installed; void* tgt; };
        U hooks[] = {
            { &g_InstalledParts,                 ResolveGameAddress(gAddr.LoadPlayerPartsParts) },
            { &g_InstalledFpk,                   ResolveGameAddress(gAddr.LoadPlayerPartsFpk) },
            { &g_InstalledCamo,                  ResolveGameAddress(gAddr.LoadPlayerCamoFpk) },
            { &g_InstalledDiamond,               ResolveGameAddress(gAddr.LoadPlayerSnakeBlackDiamondFpk) },
            { &g_InstalledCamoFv2,               ResolveGameAddress(gAddr.LoadPlayerCamoFv2) },
            { &g_InstalledDiamondFv2,            ResolveGameAddress(gAddr.LoadPlayerSnakeBlackDiamondFv2) },
            { &g_InstalledBionicArmFv2,          ResolveGameAddress(gAddr.LoadPlayerBionicArmFv2) },
            { &g_InstalledBionicArmFpk,          ResolveGameAddress(gAddr.LoadPlayerBionicArmFpk) },
            { &g_InstalledSnakeFaceFv2,          ResolveGameAddress(gAddr.LoadPlayerSnakeFaceFv2) },
            { &g_InstalledSnakeFaceFpk,          ResolveGameAddress(gAddr.LoadPlayerSnakeFaceFpk) },
            { &g_InstalledAvatarFaceFv2,         ResolveGameAddress(gAddr.LoadAvatarFaceFv2) },
            { &g_InstalledAvatarFaceFpk,         ResolveGameAddress(gAddr.LoadAvatarFaceFpk) },
            { &g_InstalledAvatarFaceEdit,        ResolveGameAddress(gAddr.AvatarFaceEditUpdate) },
            { &g_InstalledAvatarHeadOptionFv2,   ResolveGameAddress(gAddr.LoadAvatarHeadOptionFv2) },
            { &g_InstalledAvatarHeadOptionFpk,   ResolveGameAddress(gAddr.LoadAvatarHeadOptionFpk) },
            { &g_InstalledLpn,                   ResolveGameAddress(gAddr.Player2BlockController_LoadPartsNew) },
            { &g_InstalledDoesNeedFace,          ResolveGameAddress(gAddr.ResourceTable_DoesNeedFaceFova) },
            { &g_InstalledDoesNeedFaceForAvatar, ResolveGameAddress(gAddr.ResourceTable_DoesNeedFaceFovaForAvatar) },
            { &g_InstalledSetHandSlotEnabled,    ResolveGameAddress(gAddr.EquipController_SetHandSlotEnabled) },
            { &g_InstalledIsArtificialHand,      ResolveGameAddress(gAddr.Sys_IsArtificialHandEnabled) },
            { &g_InstalledIsArtHandForCurrent,   ResolveGameAddress(gAddr.Sys_IsArtificialHandEnabledForCurrentPlayerType) },
            { &g_InstalledProcessSignal,         ResolveGameAddress(gAddr.Player2GameObjectImpl_ProcessSignal) },
            { &g_InstalledUpdatePartsStatus,     ResolveGameAddress(gAddr.UpdatePartsStatus) },
            { &g_InstalledPlayer2ImplSetUpParts, ResolveGameAddress(gAddr.Player2Impl_SetUpParts) },
            { &g_InstalledFacialCrashGuard,      ResolveGameAddress(gAddr.PluginFacial_ApplyMotion) },
            { &g_InstalledPartsAtCamo,           ResolveGameAddress(gAddr.PlayerInfoInterfaceImpl_GetPartsTypeAtCamoType) },
            { &g_InstalledQuietSuitShaderParams, ResolveGameAddress(gAddr.Player2BlockController_ApplyQuietSuitShaderParams) },
        };
        for (auto& h : hooks)
        {
            if (*h.installed && h.tgt) DisableAndRemoveHook(h.tgt);
            *h.installed = false;
        }

        if (g_QspBlendArmed.exchange(false, std::memory_order_acq_rel))
        {
            if (g_SuitShaderBlendVecTarget)
                DisableAndRemoveHook(g_SuitShaderBlendVecTarget);
            if (g_SuitShaderBlendPairTarget)
                DisableAndRemoveHook(g_SuitShaderBlendPairTarget);
            if (g_SuitShaderProduceTarget)
                DisableAndRemoveHook(g_SuitShaderProduceTarget);
            g_SuitShaderBlendVecTarget  = nullptr;
            g_SuitShaderBlendPairTarget = nullptr;
            g_SuitShaderProduceTarget   = nullptr;
            g_OrigSuitShaderBlendVec    = nullptr;
            g_OrigSuitShaderBlendPair   = nullptr;
            g_OrigSuitShaderProduce     = nullptr;
        }

        if (g_CaseDArmUnpinActive && g_CaseDPatchSite)
        {
            DWORD oldp = 0;
            if (VirtualProtect(g_CaseDPatchSite, 9, PAGE_EXECUTE_READWRITE, &oldp))
            {
                std::memcpy(g_CaseDPatchSite, g_CaseDOrigBytes, 9);
                DWORD tmp = 0;
                VirtualProtect(g_CaseDPatchSite, 9, oldp, &tmp);
                FlushInstructionCache(GetCurrentProcess(), g_CaseDPatchSite, 9);
                if (g_CaseDTrampoline)
                    VirtualFree(g_CaseDTrampoline, 0, MEM_RELEASE);
            }
            else
            {
                LogWarn("CaseDArmUnpin: patch site restore failed at uninstall, trampoline leaked");
            }
            g_CaseDTrampoline     = nullptr;
            g_CaseDPatchSite      = nullptr;
            g_CaseDArmUnpinActive = false;
        }

        g_OrigLoadPartsParts            = nullptr;
        g_OrigLoadPartsFpk              = nullptr;
        g_OrigLoadCamoFpk               = nullptr;
        g_OrigLoadDiamondFpk            = nullptr;
        g_OrigLoadCamoFv2              = nullptr;
        g_OrigLoadDiamondFv2           = nullptr;
        g_OrigLoadBionicArmFv2          = nullptr;
        g_OrigLoadBionicArmFpk          = nullptr;
        g_OrigLoadSnakeFaceFv2          = nullptr;
        g_OrigLoadSnakeFaceFpk          = nullptr;
        g_OrigLoadAvatarFaceFv2         = nullptr;
        g_OrigLoadAvatarFaceFpk         = nullptr;
        g_OrigLoadAvatarHeadOptionFv2   = nullptr;
        g_OrigLoadAvatarHeadOptionFpk   = nullptr;
        g_OrigAvatarFaceEditUpdate      = nullptr;
        g_FoxModelFromHandle            = nullptr;
        g_OrigLoadPartsNew              = nullptr;
        g_OrigDoesNeedFaceFova          = nullptr;
        g_OrigDoesNeedFaceFovaForAvatar = nullptr;
        g_OrigSetHandSlotEnabled        = nullptr;
        g_OrigIsArtificialHandEnabled   = nullptr;
        g_OrigIsArtificialHandForCurrent = nullptr;
        g_OrigProcessSignal             = nullptr;
        g_OrigUpdatePartsStatus         = nullptr;
        g_OrigPlayer2ImplSetUpParts     = nullptr;
        g_OrigPluginFacialApplyMotion   = nullptr;
        g_OrigGetPartsTypeAtCamoType    = nullptr;
        g_FoxPath_Path                  = nullptr;
        g_CapturedBlockController       = nullptr;

        uniquecharpin::Uninstall();

        outfit::shadow::ResetAll("Uninstall_OutfitRuntimeParts_Hooks");
        outfit::shadow::ResetArmTierCache();
    }
}
