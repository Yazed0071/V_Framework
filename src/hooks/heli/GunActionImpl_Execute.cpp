#include <pch.h>
#include "GunActionImpl_Execute.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "HeliKey.h"
#include "../soldier/SoldierFaction.h"
#include "../soldier/SoldierFactionHeli.h"

namespace
{
    using Execute_t        = void(__fastcall*)(void* gun, std::uint32_t phase, std::uint32_t slot);
    using GetAimPosition_t = float*(__fastcall*)(void* aim, float* out, std::uint32_t slot);
    using QuarkTable_t     = std::uint8_t*(__fastcall*)();
    using GeoSegment_t     = int(__fastcall*)(void* geo, void* desc, std::uint32_t mask, const float* from,
                                              const float* to, float radius);

    constexpr std::uint32_t kExecutePhase       = 4;
    constexpr std::size_t   kGunHeli            = 0x50;
    constexpr std::size_t   kGunEntries         = 0x60;
    constexpr std::size_t   kGunFirstSlot       = 0x78;
    constexpr std::size_t   kGunAimInterface    = 0x18;
    constexpr std::size_t   kGunSlotExecute     = 0x48;
    constexpr std::size_t   kAimSlotIsAimType   = 0x38;
    constexpr std::size_t   kAimSlotGetPosition = 0x40;
    constexpr std::size_t   kAimVtableAfterGun  = 0x70;   // the aim interface's vtable follows the gun's
    constexpr std::size_t   kEntryStride        = 0x110;
    constexpr std::size_t   kEntryAimMode       = 0x20;
    constexpr std::size_t   kEntryFireType      = 0x28;
    constexpr std::size_t   kEntryHeliIndex     = 0x100;
    constexpr std::size_t   kHeliType           = 0x130;
    constexpr std::size_t   kHeliTransforms     = 0x28;
    constexpr std::size_t   kTransformPositions = 0x20;

    constexpr std::uint8_t kExecutePrologue[] = { 0x40, 0x55, 0x53, 0x41, 0x54, 0x41, 0x57, 0x48, 0x8D, 0xAC };

    constexpr std::uint32_t kGeoSegmentMask  = 0x04010700u;   // what the heli's own attack check passes
    constexpr std::size_t   kGeoSlotSegment  = 0x10;
    constexpr std::size_t   kDescRecordSize  = 0x60;
    constexpr std::size_t   kDescBytes       = 0x70 + 2 * kDescRecordSize;
    constexpr std::uint64_t kDescCapacityOne = 0x10000ull;   // capacity 1 stops at the first hit

    constexpr float         kGunBelowHeli = 5.0f;   // the heli's own check starts 5 m under it
    constexpr float         kTargetChest  = 1.0f;
    constexpr float         kStopShort    = 0.5f;
    constexpr std::uint64_t kRecheckMs    = 150;

    struct alignas(16) GunShot
    {
        float         origin[4];
        std::int32_t* fireType;
        std::uint16_t heliId;
        bool          firesAtPosition;
    };

    Execute_t g_OrigExecute = nullptr;
    void*     g_Target      = nullptr;
    bool      g_Installed   = false;

    std::atomic<std::uint64_t> g_CheckedMs[HeliKey::kCount];
    std::atomic<bool>          g_Blocked[HeliKey::kCount];

    void* FindExecuteSEH(std::uintptr_t getAim, std::uintptr_t isAim)
    {
        void* found = nullptr;
        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
            auto* nt   = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
            IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
            for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++section)
            {
                const DWORD flags = section->Characteristics;
                if (!(flags & IMAGE_SCN_MEM_READ) || (flags & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_WRITE)))
                    continue;   // vtables live in read-only data
                auto* slots = reinterpret_cast<const std::uintptr_t*>(base + section->VirtualAddress);
                const std::size_t count = section->Misc.VirtualSize / sizeof(std::uintptr_t);
                for (std::size_t i = (kAimVtableAfterGun + kAimSlotGetPosition) / sizeof(std::uintptr_t); i < count; ++i)
                {
                    if (slots[i] != getAim)
                        continue;
                    const std::uintptr_t* aimVtable = slots + i - kAimSlotGetPosition / sizeof(std::uintptr_t);
                    if (aimVtable[kAimSlotIsAimType / sizeof(std::uintptr_t)] != isAim)
                        continue;
                    const std::uintptr_t* gunVtable = aimVtable - kAimVtableAfterGun / sizeof(std::uintptr_t);
                    void* execute = reinterpret_cast<void*>(gunVtable[kGunSlotExecute / sizeof(std::uintptr_t)]);
                    if (!execute || (found && found != execute))   // every heli gun class must share one Execute
                        return nullptr;
                    found = execute;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
        return found;
    }

    bool ReadGunShotSEH(void* gun, std::uint32_t slot, GunShot* out)
    {
        __try
        {
            auto* self    = static_cast<std::uint8_t*>(gun);
            auto* heli    = *reinterpret_cast<std::uint8_t**>(self + kGunHeli);
            auto* entries = *reinterpret_cast<std::uint8_t**>(self + kGunEntries);
            if (!heli || !entries)
                return false;
            const std::uint32_t type = *reinterpret_cast<std::uint16_t*>(heli + kHeliType);
            if (!HeliKey::IsHeliType(type))
                return false;
            const std::uint32_t entryIndex = slot - static_cast<std::uint32_t>(*reinterpret_cast<std::int32_t*>(self + kGunFirstSlot));
            std::uint8_t* entry = entries + static_cast<std::size_t>(entryIndex) * kEntryStride;
            const std::uint32_t index = *reinterpret_cast<std::uint32_t*>(entry + kEntryHeliIndex);
            auto* transforms = *reinterpret_cast<std::uint8_t**>(heli + kHeliTransforms);
            auto* positions  = transforms ? *reinterpret_cast<const float**>(transforms + kTransformPositions) : nullptr;
            if (index > 0x1FFu || !positions)
                return false;
            const std::int32_t aimMode = *reinterpret_cast<std::int32_t*>(entry + kEntryAimMode);
            const float* position = positions + static_cast<std::size_t>(index) * 4;
            out->origin[0]       = position[0];
            out->origin[1]       = position[1] - kGunBelowHeli;
            out->origin[2]       = position[2];
            out->origin[3]       = 1.0f;
            out->fireType        = reinterpret_cast<std::int32_t*>(entry + kEntryFireType);
            out->heliId          = static_cast<std::uint16_t>((type << 9) | index);
            out->firesAtPosition = (aimMode == 1 || (aimMode >= 3 && aimMode <= 5))   // the modes IsAimTypePosition accepts
                && (*out->fireType == 1 || *out->fireType == 2);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadAimSEH(void* gun, std::uint32_t slot, float* aim)
    {
        __try
        {
            auto* aimInterface = static_cast<std::uint8_t*>(gun) + kGunAimInterface;
            void** vtbl = *reinterpret_cast<void***>(aimInterface);
            reinterpret_cast<GetAimPosition_t>(vtbl[kAimSlotGetPosition / sizeof(void*)])(aimInterface, aim, slot);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void InitSegmentDesc(std::uint8_t* desc)
    {
        std::memset(desc, 0, kDescBytes);
        *reinterpret_cast<std::int32_t*>(desc + 0x3C)  = -1;
        *reinterpret_cast<std::int32_t*>(desc + 0x64)  = -1;
        *reinterpret_cast<std::uint64_t*>(desc + 0x68) = kDescCapacityOne;
        for (std::uint8_t* record = desc + 0x70; record < desc + kDescBytes; record += kDescRecordSize)
        {
            *reinterpret_cast<std::uint32_t*>(record + 0x0C) = 0xFFFF0000u;
            *reinterpret_cast<std::uint16_t*>(record + 0x1C) = 0xFFFFu;
            *reinterpret_cast<std::uint16_t*>(record + 0x1E) = 0x00FFu;
            *reinterpret_cast<std::uint32_t*>(record + 0x2C) = 0xFF7FFFFFu;
        }
        *reinterpret_cast<std::uint64_t*>(desc + 0x00) = 0xFFFFFFFFFFFFFFFFull;   // match every kind of geometry
        *reinterpret_cast<std::uint64_t*>(desc + 0x18) = 0x2ull;
    }

    bool SegmentBlockedSEH(const float* from, const float* to, bool* blocked)
    {
        auto quarkTable = reinterpret_cast<QuarkTable_t>(ResolveGameAddress(gAddr.Fox_GetQuarkSystemTable));
        if (!quarkTable)
            return false;
        alignas(16) std::uint8_t desc[kDescBytes];
        InitSegmentDesc(desc);
        __try
        {
            std::uint8_t* table  = quarkTable();
            std::uint8_t* holder = table ? *reinterpret_cast<std::uint8_t**>(table + 0x80) : nullptr;
            void* geo = holder ? *reinterpret_cast<void**>(holder + 0x08) : nullptr;
            if (!geo)
                return false;
            void** vtbl = *reinterpret_cast<void***>(geo);
            *blocked = reinterpret_cast<GeoSegment_t>(vtbl[kGeoSlotSegment / sizeof(void*)])(geo, desc, kGeoSegmentMask, from, to, 0.0f) > 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool AimInSight(const GunShot& shot, const float* aim, bool* inSight)
    {
        alignas(16) float target[4] = { aim[0], aim[1] + kTargetChest, aim[2], 1.0f };
        const float dx = shot.origin[0] - target[0];
        const float dy = shot.origin[1] - target[1];
        const float dz = shot.origin[2] - target[2];
        const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (length <= kStopShort)
        {
            *inSight = true;
            return true;
        }
        target[0] += dx / length * kStopShort;   // stop short so the ground right at the target doesn't count
        target[1] += dy / length * kStopShort;
        target[2] += dz / length * kStopShort;
        bool blocked = false;
        if (!SegmentBlockedSEH(shot.origin, target, &blocked))
            return false;
        *inSight = !blocked;
        return true;
    }

    std::int32_t* FireTypeToHold(void* gun, std::uint32_t slot)
    {
        GunShot shot{};
        if (!ReadGunShotSEH(gun, slot, &shot) || !shot.firesAtPosition || !SoldierFaction_HasFaction(shot.heliId))
            return nullptr;
        if (SoldierFactionHeli_IsFighting(shot.heliId))   // aiming at its locked target: same eye ray its attack choice uses
            return SoldierFactionHeli_LineClear(shot.heliId) ? nullptr : shot.fireType;
        const std::uint32_t key = HeliKey::OfGameObjectId(shot.heliId);
        const std::uint64_t now = GetTickCount64();
        if (now - g_CheckedMs[key].load(std::memory_order_relaxed) >= kRecheckMs)
        {
            alignas(16) float aim[4]{};
            bool inSight = true;
            if (!ReadAimSEH(gun, slot, aim) || !AimInSight(shot, aim, &inSight))
                return nullptr;
            g_CheckedMs[key].store(now, std::memory_order_relaxed);
            g_Blocked[key].store(!inSight, std::memory_order_relaxed);
        }
        return g_Blocked[key].load(std::memory_order_relaxed) ? shot.fireType : nullptr;
    }

    bool HoldFireSEH(std::int32_t* fireType, std::int32_t* firing)
    {
        __try
        {
            *firing = *fireType;
            *fireType = 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void ResumeFireSEH(std::int32_t* fireType, std::int32_t firing)
    {
        __try
        {
            if (*fireType == 0)   // Execute never writes it, so a 0 is still ours
                *fireType = firing;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkExecute(void* gun, std::uint32_t phase, std::uint32_t slot)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigExecute, gun, phase, slot);

        std::int32_t* fireType = phase == kExecutePhase && SoldierFaction_AnyFaction() ? FireTypeToHold(gun, slot) : nullptr;
        std::int32_t firing = 0;
        if (fireType && !HoldFireSEH(fireType, &firing))
            fireType = nullptr;
        g_OrigExecute(gun, phase, slot);   // fire type 0 runs the gun's own not-firing frame
        if (fireType)
            ResumeFireSEH(fireType, firing);
    }
}

bool Install_GunActionImpl_Execute_Hook()
{
    if (g_Installed)
        return true;

    void* getAim = gAddr.HeliGunActionImpl_GetAimPosition ? ResolveGameAddress(gAddr.HeliGunActionImpl_GetAimPosition) : nullptr;
    void* isAim  = gAddr.HeliGunActionImpl_IsAimTypePosition ? ResolveGameAddress(gAddr.HeliGunActionImpl_IsAimTypePosition) : nullptr;
    if (!getAim || !isAim)
    {
        LogWarn("GunActionImpl_Execute: address missing for this build, faction helis fire without a line of sight check");
        return false;
    }
    void* execute = FindExecuteSEH(reinterpret_cast<std::uintptr_t>(getAim), reinterpret_cast<std::uintptr_t>(isAim));
    const HookEntry hookEntry = CheckHookEntry(execute, kExecutePrologue, sizeof(kExecutePrologue));
    if (hookEntry == HookEntry::Unknown)
    {
        char entry[96] = "no heli gun vtable";
        if (execute)
            DescribeFunctionEntry(execute, entry, sizeof(entry));
        LogWarn("GunActionImpl_Execute: heli gun Execute not found (%s), faction helis fire without a line of sight check", entry);
        return false;
    }
    if (hookEntry == HookEntry::ForeignHook)
        LogHookedOnTop("GunActionImpl_Execute: heli gun Execute", execute);
    if (!CreateAndEnableHook(execute, reinterpret_cast<void*>(&hkExecute), reinterpret_cast<void**>(&g_OrigExecute)))
    {
        LogError("GunActionImpl_Execute: hook failed, faction helis fire without a line of sight check");
        return false;
    }
    g_Target    = execute;
    g_Installed = true;
    return true;
}

bool Uninstall_GunActionImpl_Execute_Hook()
{
    if (!g_Installed)
        return true;
    DisableAndRemoveHook(g_Target);
    g_OrigExecute = nullptr;
    g_Target      = nullptr;
    g_Installed   = false;
    return true;
}
