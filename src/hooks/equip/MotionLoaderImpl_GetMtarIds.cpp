#include "pch.h"
#include "MotionLoaderImpl_GetMtarIds.h"

#include <cstdint>
#include <map>
#include <mutex>

#include "AddressSet.h"
#include "EquipPartParams.h"
#include "GunBasicInject.h"
#include "HookUtils.h"
#include "RealizedEquipObjectImpl_PostPutMotionExecute.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "log.h"

namespace
{
    constexpr int kFirstOwnedReceiverType = 48;
    constexpr int kLastOwnedReceiverType  = 255;
    constexpr int kVanillaReceiverTypeMax = 46;
    constexpr int kFirstOwnedMtarId       = 128;
    constexpr int kLastOwnedMtarId        = 255;
    constexpr std::uint32_t kMtarIdSlots  = 8;
    constexpr int kReceiverRowWithNoPartMotion = 41;

    struct MotionFamily
    {
        int                 receiverId   = 0;
        int                 receiverType = 0;
        bool                ownsType     = false;
        int                 mtarId       = 0;
        std::uint64_t       playerMtar   = 0;
        std::uint64_t       playerPack   = 0;
        std::uint64_t       weaponMtar   = 0;
        ReceiverMotionClips playerClips;
        ReceiverMotionClips oneHandClips;
        ReceiverMotionClips oneHandCqcClips;
        ReceiverMotionClips horseClips;
        ReceiverMotionClips weaponClips;
        ReceiverPartMotion  partMotion;
        ReceiverPartRowIndices rowIndices;
    };

    std::mutex                 g_Mutex;
    std::map<int, MotionFamily> g_FamilyByReceiver;
    std::map<int, int>         g_ReceiverByType;
    std::map<int, int>         g_ReceiverByMtarId;
    int  g_NextType    = kFirstOwnedReceiverType;
    int  g_NextMtarId  = kFirstOwnedMtarId;
    bool g_HooksArmed  = false;

    thread_local std::uint32_t t_MtarIdsEquip = 0;

    using GetMtarIds_t   = std::uint32_t(__fastcall*)(void*, std::uint8_t*, std::uint32_t*);
    using GetPathById_t  = std::uint64_t*(__fastcall*)(void*, std::uint64_t*, std::uint32_t);

    GetMtarIds_t  g_OrigGetMtarIds          = nullptr;
    GetPathById_t g_OrigGetMtarFilePath     = nullptr;
    GetPathById_t g_OrigGetBlockPackagePath = nullptr;

    const MotionFamily* FamilyForReceiverLocked(int receiverId)
    {
        const auto it = g_FamilyByReceiver.find(receiverId);
        return it == g_FamilyByReceiver.end() ? nullptr : &it->second;
    }

    const MotionFamily* FamilyForEquipLocked(unsigned int equipId)
    {
        const int rc =
            EquipParam_GetReceiverForEquipId(static_cast<int>(equipId));
        return rc > 0 ? FamilyForReceiverLocked(rc) : nullptr;
    }

    const MotionFamily* FamilyForTypeLocked(unsigned int receiverType)
    {
        const auto it = g_ReceiverByType.find(static_cast<int>(receiverType));
        return it == g_ReceiverByType.end() ? nullptr : FamilyForReceiverLocked(it->second);
    }

    std::uint64_t ClipFor(const ReceiverMotionClips& c, int gunUseMotionType, bool roundsLeft)
    {
        switch (gunUseMotionType)
        {
        case 1:  return c.fire ? c.fire : c.hold;
        case 2:  return (roundsLeft || !c.reloadEmpty) ? c.reload : c.reloadEmpty;
        case 3:  return c.cock ? c.cock : c.hold;
        case 4:  return c.dualReload ? c.dualReload : c.reload;
        case kReceiverMotionGrip:             return c.grip;
        case kReceiverMotionMagazineGrip:     return c.magazineGrip;
        case kReceiverMotionMagazineGripDual: return c.magazineGripDual;
        case kReceiverMotionUnderBarrelGrip:  return c.underBarrelGrip;
        case kReceiverMotionStepReload:       return c.stepReload;
        default: return gunUseMotionType < kReceiverMotionGrip ? c.hold : 0;
        }
    }

    std::uint64_t VariantClipFor(const MotionFamily& f, int variant, int gunUseMotionType, bool roundsLeft)
    {
        const ReceiverMotionClips* chain[3] = {};
        int n = 0;
        switch (variant)
        {
        case kReceiverVariantOneHandCqc:
            chain[n++] = &f.oneHandCqcClips;
            chain[n++] = &f.oneHandClips;
            chain[n++] = &f.playerClips;
            break;
        case kReceiverVariantOneHand:
            chain[n++] = &f.oneHandClips;
            chain[n++] = &f.playerClips;
            break;
        case kReceiverVariantHorse:
            chain[n++] = &f.horseClips;
            chain[n++] = &f.playerClips;
            break;
        default:
            chain[n++] = &f.playerClips;
            break;
        }
        for (int i = 0; i < n; ++i)
            if (const std::uint64_t clip = ClipFor(*chain[i], gunUseMotionType, roundsLeft))
                return clip;
        return 0;
    }

    std::uint32_t __fastcall hkGetMtarIds(void* self, std::uint8_t* out, std::uint32_t* equipId)
    {
        const std::uint32_t eq = equipId ? *equipId : 0;
        t_MtarIdsEquip = eq;
        std::uint32_t n = g_OrigGetMtarIds(self, out, equipId);
        t_MtarIdsEquip = 0;
        if (!out || eq == 0)
            return n;
        int ownedId = 0;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            const MotionFamily* f = FamilyForEquipLocked(eq);
            if (f)
                ownedId = f->mtarId;
        }
        if (ownedId == 0)
            return n;
        if (n >= kMtarIdSlots)
        {
            Log("[ReceiverMotion] WARN: equipId=%u already binds %u player archives - "
                "the receiver's own archive was not added, hands play no custom "
                "clips\n", eq, n);
            return n;
        }
        out[n] = static_cast<std::uint8_t>(ownedId);
        return n + 1;
    }

    bool OwnedPathFor(std::uint32_t mtarId, bool pack, std::uint64_t* outPath)
    {
        if (mtarId < static_cast<std::uint32_t>(kFirstOwnedMtarId))
            return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const auto it = g_ReceiverByMtarId.find(static_cast<int>(mtarId));
        if (it == g_ReceiverByMtarId.end())
            return false;
        const MotionFamily* f = FamilyForReceiverLocked(it->second);
        if (!f)
            return false;
        *outPath = pack ? f->playerPack : f->playerMtar;
        return *outPath != 0;
    }

    std::uint64_t* __fastcall hkGetMtarFilePath(void* self, std::uint64_t* out, std::uint32_t mtarId)
    {
        std::uint64_t path = 0;
        if (out && OwnedPathFor(mtarId, false, &path))
        {
            *out = path;
            return out;
        }
        return g_OrigGetMtarFilePath(self, out, mtarId);
    }

    std::uint64_t* __fastcall hkGetBlockPackagePath(void* self, std::uint64_t* out, std::uint32_t mtarId)
    {
        std::uint64_t path = 0;
        if (out && OwnedPathFor(mtarId, true, &path))
        {
            *out = path;
            return out;
        }
        return g_OrigGetBlockPackagePath(self, out, mtarId);
    }

    bool HookOne(uintptr_t addr, void* detour, void** original, const char* what)
    {
        void* target = ResolveGameAddress(addr);
        if (!target)
            return false;
        if (!CreateAndEnableHook(target, detour, original))
        {
            Log("[ReceiverMotion] ERROR: %s hook failed at %p - no-donor receiver "
                "motion families are disabled\n", what, target);
            return false;
        }
        return true;
    }
}

bool ReceiverMotion_Register(const ReceiverMotionDesc& desc)
{
    if (desc.receiverId <= 0)
    {
        Log("[ReceiverMotion] ERROR: SetReceiverMotion without a valid receiverId - "
            "nothing registered\n");
        return false;
    }
    std::lock_guard<std::mutex> lock(g_Mutex);
    MotionFamily& f = g_FamilyByReceiver[desc.receiverId];
    if (f.receiverId == 0)
    {
        f.receiverId = desc.receiverId;
        if (g_NextType > kLastOwnedReceiverType)
        {
            g_FamilyByReceiver.erase(desc.receiverId);
            Log("[ReceiverMotion] ERROR: receiverId=%d needs a new receiver type "
                "but all %d owned types are taken - nothing registered\n",
                desc.receiverId, kLastOwnedReceiverType - kFirstOwnedReceiverType + 1);
            return false;
        }
        f.receiverType = g_NextType++;
        f.ownsType = true;
        g_ReceiverByType[f.receiverType] = desc.receiverId;
    }
    if (desc.playerMtar != 0 && f.mtarId == 0)
    {
        if (g_NextMtarId > kLastOwnedMtarId)
        {
            Log("[ReceiverMotion] ERROR: receiverId=%d needs a player archive id but "
                "all owned ids are taken - hands play no custom clips\n",
                desc.receiverId);
        }
        else
        {
            f.mtarId = g_NextMtarId++;
            g_ReceiverByMtarId[f.mtarId] = desc.receiverId;
        }
    }
    if (desc.playerMtar != 0 && desc.playerPack == 0)
        Log("[ReceiverMotion] WARN: receiverId=%d playerMotion.mtar has no pack - the "
            "engine cannot mount it, hands play no custom clips\n", desc.receiverId);
    f.playerMtar  = desc.playerMtar;
    f.playerPack  = desc.playerPack;
    f.playerClips = desc.playerClips;
    f.oneHandClips = desc.oneHandClips;
    f.oneHandCqcClips = desc.oneHandCqcClips;
    f.horseClips = desc.horseClips;
    f.weaponMtar  = desc.weaponMtar;
    f.weaponClips = desc.weaponClips;
    f.partMotion  = desc.partMotion;
    return true;
}

bool ReceiverMotion_HasFamily(int receiverId)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    return FamilyForReceiverLocked(receiverId) != nullptr;
}

int ReceiverMotion_RowByteFor(int receiverId)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    const MotionFamily* f = FamilyForReceiverLocked(receiverId);
    if (!f)
        return 0;
    return f->rowIndices.present ? kPartMotionRowReceiver
                                : kReceiverRowWithNoPartMotion;
}

void ReceiverMotion_CollectPartMotions(std::vector<ReceiverPartMotionSnapshot>& out)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    for (const auto& kv : g_FamilyByReceiver)
    {
        const MotionFamily& f = kv.second;
        if (!f.partMotion.present)
            continue;
        ReceiverPartMotionSnapshot s;
        s.receiverId = f.receiverId;
        s.motion     = f.partMotion;
        out.push_back(s);
    }
}

void ReceiverMotion_SetPartRowIndices(int receiverId,
                                      const ReceiverPartRowIndices& idx)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    auto it = g_FamilyByReceiver.find(receiverId);
    if (it == g_FamilyByReceiver.end())
        return;
    it->second.rowIndices = idx;
}

bool ReceiverMotion_GetPartRowIndices(int receiverId, ReceiverPartRowIndices& out)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    const MotionFamily* f = FamilyForReceiverLocked(receiverId);
    if (!f || !f->rowIndices.present)
        return false;
    out = f->rowIndices;
    return true;
}

bool ReceiverMotion_EnsurePartRowsPublished(int receiverId)
{
    ReceiverPartMotion motion;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const MotionFamily* f = FamilyForReceiverLocked(receiverId);
        if (!f || !f->partMotion.present)
            return false;
        if (f->rowIndices.present)
            return true;
        motion = f->partMotion;
    }
    ReceiverPartRowIndices idx;
    if (!PartMotionRows_AppendNativeRows(motion, idx))
        return false;
    ReceiverMotion_SetPartRowIndices(receiverId, idx);
    LogDebug("[PartMotionRows] receiverId=%d part-motion rows published natively "
             "(motions %u %u %u %u, poses %u %u %u %u)\n", receiverId,
        idx.motion[0], idx.motion[1], idx.motion[2], idx.motion[3],
        idx.pose[0], idx.pose[1], idx.pose[2], idx.pose[3]);
    return true;
}

bool ReceiverMotion_ReceiverTypeOverride(unsigned int setupEquipId, unsigned int* outType)
{
    if (!outType)
        return false;
    std::lock_guard<std::mutex> lock(g_Mutex);
    if (g_FamilyByReceiver.empty())
        return false;
    if (t_MtarIdsEquip != 0)
    {
        const MotionFamily* f = FamilyForEquipLocked(t_MtarIdsEquip);
        if (f && f->ownsType)
        {
            *outType = 0;
            return true;
        }
        return false;
    }
    if (setupEquipId != 0)
    {
        const MotionFamily* f = FamilyForEquipLocked(setupEquipId);
        if (f)
        {
            *outType = static_cast<unsigned int>(f->receiverType);
            return true;
        }
    }
    return false;
}

std::uint64_t ReceiverMotion_WeaponArchiveForEquip(unsigned int equipId)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    if (g_FamilyByReceiver.empty())
        return 0;
    const MotionFamily* f = FamilyForEquipLocked(equipId);
    return f ? f->weaponMtar : 0;
}

std::uint64_t ReceiverMotion_PlayerClip(unsigned int receiverType, int gunUseMotionType, bool roundsLeft)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    const MotionFamily* f = FamilyForTypeLocked(receiverType);
    return f ? ClipFor(f->playerClips, gunUseMotionType, roundsLeft) : 0;
}

std::uint64_t ReceiverMotion_PlayerClipVariant(unsigned int receiverType, int variant, int gunUseMotionType, bool roundsLeft)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    const MotionFamily* f = FamilyForTypeLocked(receiverType);
    return f ? VariantClipFor(*f, variant, gunUseMotionType, roundsLeft) : 0;
}

std::uint64_t ReceiverMotion_WeaponClip(unsigned int receiverType, int gunUseMotionType, bool roundsLeft)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    const MotionFamily* f = FamilyForTypeLocked(receiverType);
    return f ? ClipFor(f->weaponClips, gunUseMotionType, roundsLeft) : 0;
}

bool Install_MotionLoaderImpl_GetMtarIds_Hook()
{
    if (!gAddr.MotionLoaderImpl_GetMtarIds || !gAddr.MotionLoaderImpl_GetMtarFilePath
        || !gAddr.MotionLoaderImpl_GetBlockPackagePath)
        return true;
    const bool ok =
        HookOne(gAddr.MotionLoaderImpl_GetMtarIds, &hkGetMtarIds,
                reinterpret_cast<void**>(&g_OrigGetMtarIds), "GetMtarIds")
        && HookOne(gAddr.MotionLoaderImpl_GetMtarFilePath, &hkGetMtarFilePath,
                   reinterpret_cast<void**>(&g_OrigGetMtarFilePath), "GetMtarFilePath")
        && HookOne(gAddr.MotionLoaderImpl_GetBlockPackagePath, &hkGetBlockPackagePath,
                   reinterpret_cast<void**>(&g_OrigGetBlockPackagePath), "GetBlockPackagePath");
    g_HooksArmed = ok;
    return ok;
}

void Uninstall_MotionLoaderImpl_GetMtarIds_Hook()
{
    if (g_OrigGetMtarIds)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.MotionLoaderImpl_GetMtarIds));
    if (g_OrigGetMtarFilePath)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.MotionLoaderImpl_GetMtarFilePath));
    if (g_OrigGetBlockPackagePath)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.MotionLoaderImpl_GetBlockPackagePath));
    g_OrigGetMtarIds = nullptr;
    g_OrigGetMtarFilePath = nullptr;
    g_OrigGetBlockPackagePath = nullptr;
    g_HooksArmed = false;
}
