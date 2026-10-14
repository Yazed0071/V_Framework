#include "pch.h"

#include "EquipControllerImpl_InitCurrentSlot.h"

#include <Windows.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "EquipAliasLease.h"

namespace
{
    using InitCurrentSlot_t = void(__fastcall*)(void* impl, std::uint32_t slot, void* buddyService, std::int32_t equipId);
    using Update_t          = void(__fastcall*)(void* iface);
    using SetMotion_t       = void(__fastcall*)(void* iface, std::uint32_t slot, std::int32_t motionIndex, std::int32_t equipId);
    using GetFileForPath_t  = void*(__fastcall*)(void* ctrl, std::uint32_t group, std::uint64_t pathId);
    using GetAnimFile_t     = void*(__fastcall*)(void* mtar, unsigned long long clip);
    using GetQuarkTable_t   = std::uint8_t*(__fastcall*)();
    using SetMotionByPath_t = void(__fastcall*)(void* equipSystem, std::uint32_t handle, std::uint64_t clip, std::uint8_t flags);

    constexpr std::int32_t  kSinfulButterflyEquipId = 0x3D;
    constexpr std::uint32_t kWeaponBlockGroup       = 2;
    constexpr std::uint64_t kQuietLayersMtar        = 0x67005EADFD93D423; // BuddyQuiet2_layers.mtar
    constexpr std::size_t   kImplToIface            = 0x20;

    constexpr std::uint64_t kSinfulClips[buddyoutfit::kQuietMotionSlots] =
    {
        0xFC509685DBDE1D2B, // qui0sr04_q_snr_idl.gani
        0xFC531E21EAF6C39D, // qui0sr04_p_snr_idl.gani
        0xFC524EAE82911090, // qui0sr04_q_rdy_fre.gani
        0xFC5110BBD5B6F9D5, // qui0sr04_p_rdy_fre.gani
        0xFC50EE09978CC5DC, // qui0sr04_q_rdy_rld.gani
        0xFC5306A30981D04A, // qui0sr04_p_rdy_rld.gani
        0xFC5103D55BB04D07, // qui0sr04_q_rdy_idl_25aim.gani
        0xFC5381552894055A, // qui0sr04_p_rdy_idl_25aim.gani
    };
    constexpr std::uint64_t kRifleClips[buddyoutfit::kQuietMotionSlots] =
    {
        0xFC52AC7AFBAE1D66, // qui0non_q_snr_idl.gani
        0xFC5216E3E1CBCE05, // qui0non_p_snr_idl.gani
        0xFC5217C7A91D48D8, // qui0non_q_rdy_fre.gani
        0xFC51551BBB1B699D, // qui0non_p_rdy_fre.gani
        0xFC52C14B1C800FE8, // qui0non_q_rdy_rld.gani
        0xFC5111097996C33F, // qui0non_p_rdy_rld.gani
        0xFC5212996967E6F2, // qui0non_q_rdy_idl_25aim.gani
        0xFC52FBFA37B5A414, // qui0non_p_rdy_idl_25aim.gani
    };

    InitCurrentSlot_t g_OrigInit = nullptr;
    Update_t          g_OrigUpdate = nullptr;
    SetMotion_t       g_OrigSetMotion = nullptr;
    void* g_TargetInit = nullptr;
    void* g_TargetUpdate = nullptr;
    void* g_TargetSetMotion = nullptr;

    std::mutex g_Mutex;
    std::vector<std::unique_ptr<buddyoutfit::QuietWeaponMotion>> g_Store;
    std::unordered_map<std::uint16_t, const buddyoutfit::QuietWeaponMotion*> g_ByEquipId;
    std::unordered_set<std::uint16_t> g_Warned;

    std::atomic<const buddyoutfit::QuietWeaponMotion*> g_Active{ nullptr };
    std::atomic<std::uint32_t> g_ActiveSlot{ 0 };

    const buddyoutfit::QuietWeaponMotion* Lookup(std::int32_t engineEquipId)
    {
        const std::int32_t equipId = equip::FromEngine(engineEquipId);
        if (equipId <= 0 || equipId > 0xFFFF)
            return nullptr;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const auto it = g_ByEquipId.find(static_cast<std::uint16_t>(equipId));
        return it == g_ByEquipId.end() ? nullptr : it->second;
    }

    void* WeaponMtarSEH(std::uint64_t pathId)
    {
        auto getFile = reinterpret_cast<GetFileForPath_t>(
            ResolveGameAddress(gAddr.Buddy2BlockControllerImpl_GetFileForPathFromBuddyBlock));
        auto* ctrlSlot = static_cast<void**>(ResolveGameAddress(gAddr.Buddy2BlockController_Instance));
        if (!getFile || !ctrlSlot || !pathId)
            return nullptr;
        __try
        {
            void* ctrl = *ctrlSlot;
            if (!ctrl || !*reinterpret_cast<void**>(static_cast<std::uint8_t*>(ctrl) + 0x10))
                return nullptr;
            return getFile(ctrl, kWeaponBlockGroup, pathId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    std::uint64_t MtarPathSEH(void* mtar)
    {
        __try
        {
            return *reinterpret_cast<std::uint64_t*>(static_cast<std::uint8_t*>(mtar) + 0x30); // the archive's own path
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void* CallGetAnimFileSEH(void* fn, void* mtar, std::uint64_t clip)
    {
        __try
        {
            return reinterpret_cast<GetAnimFile_t>(fn)(mtar, clip);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool HasAnyBodyClip(const buddyoutfit::QuietWeaponMotion& m)
    {
        for (std::uint64_t c : m.clips)
            if (c)
                return true;
        return false;
    }

    bool BodyClipsResident(const buddyoutfit::QuietWeaponMotion& m)
    {
        void* mtar = WeaponMtarSEH(m.mtar);
        void* getAnimFile = ResolveGameAddress(gAddr.Mtar_GetAnimFile);
        if (!mtar || !getAnimFile)
            return false;
        for (std::uint64_t c : m.clips)
            if (c && !CallGetAnimFileSEH(getAnimFile, mtar, c))
                return false;
        return true;
    }

    void WarnOnce(const buddyoutfit::QuietWeaponMotion& m)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (g_Warned.insert(m.equipId).second)
            LogWarn("SetQuietWeaponMotion equipId:%u: quietMotion mtar or one of its clips is not in the weapon pack, Quiet keeps her rifle animations",
                static_cast<unsigned>(m.equipId));
    }

    void __fastcall hkInitCurrentSlot(void* impl, std::uint32_t slot, void* buddyService, std::int32_t equipId)
    {
        const buddyoutfit::QuietWeaponMotion* m = MissionCodeGuard::ShouldBypassHooks() ? nullptr : Lookup(equipId);
        if (m && HasAnyBodyClip(*m))
        {
            if (BodyClipsResident(*m))
            {
                g_ActiveSlot.store(slot, std::memory_order_relaxed);
                g_Active.store(m, std::memory_order_release);
                g_OrigInit(impl, slot, buddyService, kSinfulButterflyEquipId); // only flips her clip set to the sinful-butterfly slots
                return;
            }
            WarnOnce(*m);
        }
        if (g_ActiveSlot.load(std::memory_order_relaxed) == slot)
            g_Active.store(nullptr, std::memory_order_release);
        g_OrigInit(impl, slot, buddyService, equipId);
    }

    void DropIfReleasingSEH(void* iface)
    {
        if (!g_Active.load(std::memory_order_acquire))
            return;
        __try
        {
            auto* impl = static_cast<std::uint8_t*>(iface) - kImplToIface;
            const std::uint32_t slot = g_ActiveSlot.load(std::memory_order_relaxed);
            if (slot >= *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(iface) + 8))
                return;
            const std::uint8_t state = (*reinterpret_cast<std::uint8_t**>(impl + 0x58))[slot]; // weapon slot state, 4 and 5 = releasing the pack
            if (state != 4 && state != 5)
                return;
            (*reinterpret_cast<std::int32_t**>(impl + 0x38))[slot] = 0; // back to her rifle clip set before the pack unloads
            g_Active.store(nullptr, std::memory_order_release);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkUpdate(void* iface)
    {
        g_OrigUpdate(iface);
        DropIfReleasingSEH(iface);
    }

    bool PlayPartClipSEH(void* iface, std::uint32_t slot, std::uint64_t clip)
    {
        auto getTable = reinterpret_cast<GetQuarkTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (!getTable)
            return false;
        __try
        {
            std::uint8_t* app = *reinterpret_cast<std::uint8_t**>(getTable() + 0x98);
            void* equipSystem = *reinterpret_cast<void**>(app + 0x1E8);
            const std::uint32_t handle = (*reinterpret_cast<std::uint32_t**>(static_cast<std::uint8_t*>(iface) + 0x48))[slot];
            auto setMotion = (*reinterpret_cast<SetMotionByPath_t**>(equipSystem))[0x1B8 / sizeof(void*)];
            setMotion(equipSystem, handle, clip, 0);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hkSetMotion(void* iface, std::uint32_t slot, std::int32_t motionIndex, std::int32_t equipId)
    {
        const buddyoutfit::QuietWeaponMotion* m = MissionCodeGuard::ShouldBypassHooks() ? nullptr : Lookup(equipId);
        if (m && (motionIndex == 0 || motionIndex == 1) && m->partClips[motionIndex]
            && PlayPartClipSEH(iface, slot, m->partClips[motionIndex]))
            return;
        g_OrigSetMotion(iface, slot, motionIndex, equipId);
    }

    bool HookOne(std::uintptr_t addr, void* detour, void** orig, void*& target, const char* what)
    {
        void* t = ResolveGameAddress(addr);
        if (!t || !CreateAndEnableHook(t, detour, orig))
        {
            LogError("BuddyOutfit: %s hook failed or address missing, Quiet weapon animations off", what);
            return false;
        }
        target = t;
        return true;
    }
}

namespace buddyoutfit
{
    void RegisterQuietWeaponMotion(const QuietWeaponMotion& motion)
    {
        auto copy = std::make_unique<QuietWeaponMotion>(motion);
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_ByEquipId[motion.equipId] = copy.get();
        g_Store.push_back(std::move(copy));
        g_Warned.erase(motion.equipId);
    }
}

void* QuietWeaponMotion_GetAnimFile(void* mtar, unsigned long long clip, void* origGetAnimFile)
{
    const buddyoutfit::QuietWeaponMotion* m = g_Active.load(std::memory_order_acquire);
    if (!m || !mtar || !origGetAnimFile)
        return nullptr;
    int slot = -1;
    for (int i = 0; i < buddyoutfit::kQuietMotionSlots; ++i)
        if (kSinfulClips[i] == clip)
            slot = i;
    if (slot < 0 || MtarPathSEH(mtar) != kQuietLayersMtar)
        return nullptr;
    if (m->clips[slot])
        if (void* weaponMtar = WeaponMtarSEH(m->mtar))
            if (void* af = CallGetAnimFileSEH(origGetAnimFile, weaponMtar, m->clips[slot]))
                return af;
    return CallGetAnimFileSEH(origGetAnimFile, mtar, kRifleClips[slot]);
}

bool Install_EquipControllerImpl_InitCurrentSlot_Hooks()
{
    bool ok = true;
    if (!g_TargetInit)
        ok &= HookOne(gAddr.BuddyQuietEquipControllerImpl_InitCurrentSlot, reinterpret_cast<void*>(&hkInitCurrentSlot),
                      reinterpret_cast<void**>(&g_OrigInit), g_TargetInit, "Quiet weapon slot setup");
    if (!g_TargetUpdate)
        ok &= HookOne(gAddr.BuddyQuietEquipControllerImpl_Update, reinterpret_cast<void*>(&hkUpdate),
                      reinterpret_cast<void**>(&g_OrigUpdate), g_TargetUpdate, "Quiet weapon slot update");
    if (!g_TargetSetMotion)
        ok &= HookOne(gAddr.BuddyQuietEquipControllerImpl_SetMotion, reinterpret_cast<void*>(&hkSetMotion),
                      reinterpret_cast<void**>(&g_OrigSetMotion), g_TargetSetMotion, "Quiet weapon part motion");
    return ok;
}

bool Uninstall_EquipControllerImpl_InitCurrentSlot_Hooks()
{
    g_Active.store(nullptr, std::memory_order_release);
    for (void** t : { &g_TargetSetMotion, &g_TargetUpdate, &g_TargetInit })
    {
        if (*t)
        {
            DisableAndRemoveHook(*t);
            *t = nullptr;
        }
    }
    g_OrigInit = nullptr;
    g_OrigUpdate = nullptr;
    g_OrigSetMotion = nullptr;
    return true;
}
