#include "pch.h"
#include "MotionLoaderImpl_GetMtarIds.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <unordered_map>

#include "AddressSet.h"
#include "EquipPartParams.h"
#include "GunBasicInject.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "RealizedEquipObjectImpl_PostPutMotionExecute.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "log.h"

namespace
{
    constexpr int kFirstOwnedMtarId       = 128;
    constexpr int kLastOwnedMtarId        = 255;
    constexpr std::uint32_t kMtarIdSlots  = 8;
    constexpr int kReceiverRowWithNoPartMotion = 41;
    constexpr std::size_t  kWeaponInfoGunBasicRow = 0x70;
    constexpr std::size_t  kWeaponInfoEquipId     = 0x5E;
    constexpr std::uint8_t kDescStepReload        = 0x20;
    constexpr std::uint8_t kDescRoundsLeft        = 0x10;
    constexpr std::uint8_t kDescPhaseMask         = 0x07;

    struct MotionFamily
    {
        int                 receiverId   = 0;
        int                 mtarId       = 0;
        std::uint64_t       playerMtar   = 0;
        std::uint64_t       playerPack   = 0;
        std::uint64_t       weaponMtar   = 0;
        ReceiverMotionClips playerClips;
        ReceiverMotionClips oneHandClips;
        ReceiverMotionClips oneHandCqcClips;
        ReceiverMotionClips oneHandCrouchClips;
        ReceiverMotionClips horseClips;
        ReceiverMotionClips crouchClips;
        ReceiverMotionClips weaponClips;
        ReceiverPartMotion  partMotion;
        ReceiverPartRowIndices rowIndices;
    };

    std::mutex                 g_Mutex;
    std::map<int, MotionFamily> g_FamilyByReceiver;
    std::map<int, int>         g_ReceiverByMtarId;
    std::unordered_map<std::uint16_t, int> g_ReceiverByRow;
    int  g_NextMtarId  = kFirstOwnedMtarId;

    using GetMtarIds_t   = std::uint32_t(__fastcall*)(void*, std::uint8_t*, std::uint32_t*);
    using GetPathById_t  = std::uint64_t*(__fastcall*)(void*, std::uint64_t*, std::uint32_t);

    GetMtarIds_t  g_OrigGetMtarIds          = nullptr;
    GetPathById_t g_OrigGetMtarFilePath     = nullptr;
    GetPathById_t g_OrigGetBlockPackagePath = nullptr;

    bool ReadU16SEH(const void* base, std::size_t offset, std::uint16_t& out)
    {
        __try
        {
            out = *reinterpret_cast<const std::uint16_t*>(static_cast<const std::uint8_t*>(base) + offset);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

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

    const MotionFamily* FamilyForWeaponInfoLocked(const void* weaponInfo)
    {
        if (!weaponInfo || g_FamilyByReceiver.empty())
            return nullptr;
        if (MissionCodeGuard::ShouldBypassHooks())
            return nullptr;
        std::uint16_t row = 0;
        if (ReadU16SEH(weaponInfo, kWeaponInfoGunBasicRow, row) && row != 0)
        {
            int receiverId = 0;
            const auto it = g_ReceiverByRow.find(row);
            if (it != g_ReceiverByRow.end())
            {
                receiverId = it->second;
            }
            else
            {
                unsigned char bytes[12] = {};
                if (GunBasic_ReadRowBytes(row, bytes))
                    receiverId = bytes[0];
                g_ReceiverByRow[row] = receiverId;
            }
            if (receiverId > 0)
                if (const MotionFamily* f = FamilyForReceiverLocked(receiverId))
                    return f;
        }
        std::uint16_t equipId = 0;
        if (ReadU16SEH(weaponInfo, kWeaponInfoEquipId, equipId) && equipId != 0)
            return FamilyForEquipLocked(equipId);
        return nullptr;
    }

    bool AnyPhase(const std::array<std::uint64_t, 4>& phases)
    {
        for (const std::uint64_t clip : phases)
            if (clip)
                return true;
        return false;
    }

    std::uint64_t ClipFor(const ReceiverMotionClips& c, int gunUseMotionType, std::uint8_t descFlags)
    {
        const bool roundsLeft = (descFlags & kDescRoundsLeft) != 0;
        if ((descFlags & kDescStepReload) && (gunUseMotionType == 2 || gunUseMotionType == 4))
        {
            const std::size_t phase = descFlags & kDescPhaseMask;
            if (phase >= c.stepReload.size())
                return 0;
            return (roundsLeft || !AnyPhase(c.stepReloadEmpty)) ? c.stepReload[phase]
                                                                : c.stepReloadEmpty[phase];
        }
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
        default: return gunUseMotionType < kReceiverMotionGrip ? c.hold : 0;
        }
    }

    std::uint64_t VariantClipFor(const MotionFamily& f, int variant, int gunUseMotionType, std::uint8_t descFlags)
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
        case kReceiverVariantOneHandCrouch:
            chain[n++] = &f.oneHandCrouchClips;
            chain[n++] = &f.oneHandClips;
            chain[n++] = &f.playerClips;
            break;
        case kReceiverVariantHorse:
            chain[n++] = &f.horseClips;
            chain[n++] = &f.playerClips;
            break;
        case kReceiverVariantCrouch:
            chain[n++] = &f.crouchClips;
            chain[n++] = &f.playerClips;
            break;
        default:
            chain[n++] = &f.playerClips;
            break;
        }
        for (int i = 0; i < n; ++i)
            if (const std::uint64_t clip = ClipFor(*chain[i], gunUseMotionType, descFlags))
                return clip;
        return 0;
    }

    std::uint32_t __fastcall hkGetMtarIds(void* self, std::uint8_t* out, std::uint32_t* equipId)
    {
        const std::uint32_t eq = equipId ? *equipId : 0;
        std::uint32_t n = g_OrigGetMtarIds(self, out, equipId);
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
            static std::atomic<std::uint32_t> s_lastFullEquip{ 0 };
            if (s_lastFullEquip.exchange(eq, std::memory_order_relaxed) != eq)
                LogWarn("ReceiverMotion: equipId %u already has %u player archives, custom archive not added", eq, n);
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
            LogError("ReceiverMotion: %s hook failed, SetReceiverMotion archives off", what);
            return false;
        }
        return true;
    }
}

bool ReceiverMotion_Register(const ReceiverMotionDesc& desc)
{
    if (desc.receiverId <= 0)
    {
        LogWarn("SetReceiverMotion: receiverId %d invalid, nothing registered", desc.receiverId);
        return false;
    }
    std::lock_guard<std::mutex> lock(g_Mutex);
    MotionFamily& f = g_FamilyByReceiver[desc.receiverId];
    g_ReceiverByRow.clear();
    if (f.receiverId == 0)
        f.receiverId = desc.receiverId;
    if (desc.playerMtar != 0 && f.mtarId == 0)
    {
        if (g_NextMtarId > kLastOwnedMtarId)
        {
            LogError("SetReceiverMotion: receiverId %d, player archive ids 128-255 exhausted, no custom hand clips",
                desc.receiverId);
        }
        else
        {
            f.mtarId = g_NextMtarId++;
            g_ReceiverByMtarId[f.mtarId] = desc.receiverId;
        }
    }
    if (desc.playerMtar != 0 && desc.playerPack == 0)
        LogWarn("SetReceiverMotion: receiverId %d playerMotion.mtar has no pack, no custom hand clips", desc.receiverId);
    f.playerMtar  = desc.playerMtar;
    f.playerPack  = desc.playerPack;
    f.playerClips = desc.playerClips;
    f.oneHandClips = desc.oneHandClips;
    f.oneHandCqcClips = desc.oneHandCqcClips;
    f.oneHandCrouchClips = desc.oneHandCrouchClips;
    f.horseClips = desc.horseClips;
    f.crouchClips = desc.crouchClips;
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

bool ReceiverMotion_CylinderShotDegrees(unsigned int equipId, double& outDegrees)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    const MotionFamily* f = FamilyForEquipLocked(equipId);
    if (!f || !f->partMotion.present)
        return false;
    const ReceiverPartMotionRow* rows[4] = {
        &f->partMotion.shoot[0], &f->partMotion.shoot[1],
        &f->partMotion.shootLast[0], &f->partMotion.shootLast[1] };
    for (int i = 0; i < 4; ++i)
        if (rows[i]->set && rows[i]->bone.find("CYLINDER") != std::string::npos)
        {
            outDegrees = rows[i]->value;
            return true;
        }
    return false;
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
    return true;
}

std::uint64_t ReceiverMotion_WeaponArchiveForEquip(unsigned int equipId)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    if (g_FamilyByReceiver.empty())
        return 0;
    const MotionFamily* f = FamilyForEquipLocked(equipId);
    return f ? f->weaponMtar : 0;
}

void ReceiverMotion_NoteGunBasicRow(unsigned int gunBasicRow, int receiverId)
{
    if (gunBasicRow == 0 || gunBasicRow > 0xFFFF)
        return;
    std::lock_guard<std::mutex> lock(g_Mutex);
    g_ReceiverByRow[static_cast<std::uint16_t>(gunBasicRow)] =
        (receiverId > 0) ? receiverId : 0;
}

std::uint64_t ReceiverMotion_PlayerClip(const void* weaponInfo, int variant, int gunUseMotionType, std::uint8_t descFlags)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    const MotionFamily* f = FamilyForWeaponInfoLocked(weaponInfo);
    const std::uint64_t clip = f ? VariantClipFor(*f, variant, gunUseMotionType, descFlags) : 0;
    return clip;
}

std::uint64_t ReceiverMotion_WeaponClip(const void* weaponInfo, int gunUseMotionType, std::uint8_t descFlags)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    const MotionFamily* f = FamilyForWeaponInfoLocked(weaponInfo);
    const std::uint64_t clip = f ? ClipFor(f->weaponClips, gunUseMotionType, descFlags) : 0;
    return clip;
}

bool Install_MotionLoaderImpl_GetMtarIds_Hook()
{
    if (!gAddr.MotionLoaderImpl_GetMtarIds || !gAddr.MotionLoaderImpl_GetMtarFilePath
        || !gAddr.MotionLoaderImpl_GetBlockPackagePath)
        return true;
    return HookOne(gAddr.MotionLoaderImpl_GetMtarIds, &hkGetMtarIds,
                   reinterpret_cast<void**>(&g_OrigGetMtarIds), "GetMtarIds")
        && HookOne(gAddr.MotionLoaderImpl_GetMtarFilePath, &hkGetMtarFilePath,
                   reinterpret_cast<void**>(&g_OrigGetMtarFilePath), "GetMtarFilePath")
        && HookOne(gAddr.MotionLoaderImpl_GetBlockPackagePath, &hkGetBlockPackagePath,
                   reinterpret_cast<void**>(&g_OrigGetBlockPackagePath), "GetBlockPackagePath");
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
}
