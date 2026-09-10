#include "pch.h"

#include "LaserSight_UpdatePrim.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    struct TweakSlot
    {
        const char*   name;
        std::size_t   offset;
    };

    const TweakSlot kTweaks[kLaserTweakCount] = {
        { "laserThickness",        0x50 },
        { "laserFadeIn",           0x54 },
        { "laserOvershoot",        0x58 },
        { "laserOvershootBlocked", 0x5C },
        { "laserScrollU",          0x60 },
        { "laserScrollV",          0x64 },
        { "laserScrollU2",         0x68 },
        { "laserScrollV2",         0x6C },
        { "laserTilingAlong",      0x70 },
        { "laserTilingAcross",     0x74 },
        { "laserTilingAlong2",     0x78 },
        { "laserTilingAcross2",    0x7C },
        { "laserDotCone",          0x84 },
        { "laserDotLuminance",     0x88 },
        { "laserDotLuminanceMin",  0x8C },
        { "laserExposureMax",      0x90 },
        { "laserExposureMin",      0x94 },
    };

    struct OptionEntry
    {
        bool        hasDefault = false;
        LaserColor  def{};
        LaserTweaks tweaks{};
        bool        hasTweaks = false;
        std::map<int, LaserColor> byEquip;
    };

    std::mutex                  g_Mutex;
    std::map<int, OptionEntry>  g_Options;
    std::atomic<bool>           g_Any{ false };

    std::mutex          g_OptionMutex;
    std::map<int, int>  g_WeaponLaserOption;

    std::atomic<void*> g_PlayerCtx{ nullptr };

    constexpr std::size_t kCtx_PlayerMgr   = 0x138;
    constexpr std::size_t kPlayerMgr_Parts = 0x60;
    constexpr std::size_t kPlayerMgr_Slot  = 0x214;
    constexpr std::size_t kParts_EquipRecs = 0xC0;
    constexpr std::size_t kEquipRecStride  = 0x3A;
    constexpr std::uint32_t kMaxCharSlots  = 64;

    constexpr std::size_t kLaserSight_Color     = 0x40;
    constexpr std::size_t kUpdatePrimAnchorBack = 0x7A;
    constexpr std::size_t kCtorAnchorBack       = 0x33;

    const std::uint8_t kUpdatePrimAnchor[] = {
        0x48, 0x8B, 0xF9, 0x48, 0x8B, 0x81, 0xC8, 0x00,
        0x00, 0x00, 0x48, 0x85, 0xC0, 0x0F, 0x84 };

    const std::uint8_t kCtorAnchor[] = {
        0x48, 0x8B, 0x03, 0x48, 0x89, 0x47, 0x30,
        0x48, 0x8B, 0x43, 0x08, 0x48, 0x89, 0x47, 0x38,
        0x0F, 0x28, 0x43, 0x10, 0x0F, 0x11, 0x47, 0x40 };

    using UpdatePrim_t = void(__fastcall*)(void*);
    using CreateLsc_t  = void(__fastcall*)(void*);
    using LaserCtor_t  = void*(__fastcall*)(void*, void*);

    UpdatePrim_t g_OrigUpdatePrim = nullptr;
    CreateLsc_t  g_OrigCreateLsc  = nullptr;
    LaserCtor_t  g_OrigLaserCtor  = nullptr;
    void*        g_UpdatePrimAddr = nullptr;
    void*        g_CreateLscAddr  = nullptr;
    void*        g_LaserCtorAddr  = nullptr;

    std::uintptr_t CreateLaserSightControllerAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:  return 0x14971E750ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:  return 0x14A18E8D0ull;
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:  return 0x14103C750ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:  return 0x14103C7D0ull;
        default:                                           return 0;
        }
    }

    bool LooksLikeCreateLaserSightController(const std::uint8_t* fn)
    {
        static const std::uint8_t kSeedImm[4] = { 0xC5, 0x9D, 0x1C, 0x81 };
        __try
        {
            for (std::size_t i = 0; i + sizeof(kSeedImm) <= 0x80; ++i)
                if (std::memcmp(fn + i, kSeedImm, sizeof(kSeedImm)) == 0)
                    return true;
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool RegionMatchesSEH(const std::uint8_t* at, const std::uint8_t* pat, std::size_t n)
    {
        __try
        {
            return std::memcmp(at, pat, n) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    const std::uint8_t* ScanUniqueCode(const std::uint8_t* pat, std::size_t n, int* hitsOut)
    {
        *hitsOut = 0;
        const auto* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        if (!base)
            return nullptr;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return nullptr;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return nullptr;

        const auto* sec = IMAGE_FIRST_SECTION(nt);
        const std::uint8_t* hit = nullptr;
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            if ((sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;
            const std::uint8_t* s = base + sec[i].VirtualAddress;
            const std::size_t   len = sec[i].Misc.VirtualSize;
            if (len < n)
                continue;
            for (std::size_t o = 0; o + n <= len; ++o)
            {
                if (s[o] != pat[0])
                    continue;
                if (!RegionMatchesSEH(s + o, pat, n))
                    continue;
                if (++(*hitsOut) > 1)
                    return nullptr;
                hit = s + o;
            }
        }
        return (*hitsOut == 1) ? hit : nullptr;
    }

    int CurrentDrawnEquipIdSEH()
    {
        void* ctx = g_PlayerCtx.load(std::memory_order_acquire);
        if (!ctx)
            return 0;
        __try
        {
            auto* mgr = *reinterpret_cast<std::uint8_t**>(
                static_cast<std::uint8_t*>(ctx) + kCtx_PlayerMgr);
            if (!mgr)
                return 0;
            const std::uint32_t slot =
                *reinterpret_cast<std::uint32_t*>(mgr + kPlayerMgr_Slot);
            if (slot >= kMaxCharSlots)
                return 0;
            auto* parts = *reinterpret_cast<std::uint8_t**>(mgr + kPlayerMgr_Parts);
            if (!parts)
                return 0;
            auto* recs = *reinterpret_cast<std::uint8_t**>(parts + kParts_EquipRecs);
            if (!recs)
                return 0;
            return *reinterpret_cast<std::uint16_t*>(
                recs + static_cast<std::size_t>(slot) * kEquipRecStride);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool ApplySEH(void* self, const LaserColor* color, const LaserTweaks* tweaks)
    {
        __try
        {
            auto* base = static_cast<std::uint8_t*>(self);
            if (color)
            {
                auto* f = reinterpret_cast<float*>(base + kLaserSight_Color);
                f[0] = color->r;
                f[1] = color->g;
                f[2] = color->b;
                f[3] = color->a;
            }
            if (tweaks)
            {
                for (int i = 0; i < kLaserTweakCount; ++i)
                    if (tweaks->set[i])
                        *reinterpret_cast<float*>(base + kTweaks[i].offset) =
                            tweaks->value[i];
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    int OptionForDrawnWeapon(int* equipIdOut)
    {
        const int equipId = CurrentDrawnEquipIdSEH();
        if (equipIdOut)
            *equipIdOut = equipId;
        if (equipId <= 0)
            return 0;
        std::lock_guard<std::mutex> lock(g_OptionMutex);
        const auto it = g_WeaponLaserOption.find(equipId);
        return (it != g_WeaponLaserOption.end()) ? it->second : 0;
    }

    void StampInstance(void* self)
    {
        if (!self || !g_Any.load(std::memory_order_acquire))
            return;

        int equipId = 0;
        const int optionId = OptionForDrawnWeapon(&equipId);

        LaserColor color{};
        bool haveColor = (optionId > 0) && LaserColor_Resolve(optionId, equipId, color);
        if (!haveColor)
            haveColor = LaserColor_LoneDefault(color);

        LaserTweaks tweaks{};
        const bool haveTweaks = (optionId > 0) && LaserTweaks_Resolve(optionId, tweaks);

        if (!haveColor && !haveTweaks)
            return;

        static std::atomic<bool> s_faulted{ false };
        if (!ApplySEH(self, haveColor ? &color : nullptr, haveTweaks ? &tweaks : nullptr)
            && !s_faulted.exchange(true))
            Log("[LaserColor] the write to the live laser faulted - every laser keeps "
                "the engine's own values for this session\n");
    }

    void __fastcall hkUpdatePrim(void* self)
    {
        StampInstance(self);
        g_OrigUpdatePrim(self);
    }

    void* __fastcall hkLaserSightCtor(void* self, void* desc)
    {
        void* r = g_OrigLaserCtor(self, desc);
        StampInstance(self);
        return r;
    }

    void __fastcall hkCreateLaserSightController(void* self)
    {
        if (self)
        {
            __try
            {
                void* ctx = *reinterpret_cast<void**>(
                    static_cast<std::uint8_t*>(self) + 8);
                if (ctx)
                    g_PlayerCtx.store(ctx, std::memory_order_release);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
        g_OrigCreateLsc(self);
    }
}

const char* LaserTweak_NameForIndex(int index)
{
    if (index < 0 || index >= kLaserTweakCount)
        return nullptr;
    return kTweaks[index].name;
}

void LaserColor_SetDefaultForOption(int optionId, const LaserColor& color)
{
    if (optionId <= 0)
        return;
    std::lock_guard<std::mutex> lock(g_Mutex);
    OptionEntry& entry = g_Options[optionId];
    entry.hasDefault = true;
    entry.def = color;
    g_Any.store(true, std::memory_order_release);
}

void LaserColor_SetForOptionEquip(int optionId, int equipId, const LaserColor& color)
{
    if (optionId <= 0 || equipId <= 0)
        return;
    std::lock_guard<std::mutex> lock(g_Mutex);
    g_Options[optionId].byEquip[equipId] = color;
    g_Any.store(true, std::memory_order_release);
}

void LaserTweak_SetForOption(int optionId, int index, float value)
{
    if (optionId <= 0 || index < 0 || index >= kLaserTweakCount)
        return;
    std::lock_guard<std::mutex> lock(g_Mutex);
    OptionEntry& entry = g_Options[optionId];
    entry.tweaks.value[index] = value;
    entry.tweaks.set[index] = true;
    entry.hasTweaks = true;
    g_Any.store(true, std::memory_order_release);
}

void LaserColor_ClearOption(int optionId)
{
    if (optionId <= 0)
        return;
    std::lock_guard<std::mutex> lock(g_Mutex);
    g_Options.erase(optionId);
    g_Any.store(!g_Options.empty(), std::memory_order_release);
}

bool LaserColor_Resolve(int optionId, int equipId, LaserColor& out)
{
    if (optionId <= 0 || !g_Any.load(std::memory_order_acquire))
        return false;
    std::lock_guard<std::mutex> lock(g_Mutex);
    const auto it = g_Options.find(optionId);
    if (it == g_Options.end())
        return false;
    if (equipId > 0)
    {
        const auto perEquip = it->second.byEquip.find(equipId);
        if (perEquip != it->second.byEquip.end())
        {
            out = perEquip->second;
            return true;
        }
    }
    if (!it->second.hasDefault)
        return false;
    out = it->second.def;
    return true;
}

bool LaserTweaks_Resolve(int optionId, LaserTweaks& out)
{
    if (optionId <= 0 || !g_Any.load(std::memory_order_acquire))
        return false;
    std::lock_guard<std::mutex> lock(g_Mutex);
    const auto it = g_Options.find(optionId);
    if (it == g_Options.end() || !it->second.hasTweaks)
        return false;
    out = it->second.tweaks;
    return true;
}

bool LaserColor_AnyRegistered()
{
    return g_Any.load(std::memory_order_acquire);
}

bool LaserColor_LoneDefault(LaserColor& out)
{
    if (!g_Any.load(std::memory_order_acquire))
        return false;
    std::lock_guard<std::mutex> lock(g_Mutex);
    const LaserColor* found = nullptr;
    for (const auto& kv : g_Options)
    {
        if (!kv.second.hasDefault)
            continue;
        if (found)
            return false;
        found = &kv.second.def;
    }
    if (!found)
        return false;
    out = *found;
    return true;
}

void LaserColor_NoteWeaponOption(int equipId, int optionId)
{
    if (equipId <= 0)
        return;
    std::lock_guard<std::mutex> lock(g_OptionMutex);
    if (optionId > 0)
        g_WeaponLaserOption[equipId] = optionId;
    else
        g_WeaponLaserOption.erase(equipId);
}

bool LaserSight_UpdatePrim_Install()
{
    const std::uintptr_t createAddr = CreateLaserSightControllerAddr();
    if (createAddr == 0)
    {
        Log("[LaserColor] not armed: this build has no verified "
            "CreateLaserSightController address, so laserColor and the laser tuning "
            "fields are ignored and every laser keeps the engine's own values\n");
        return true;
    }

    int hits = 0;
    const std::uint8_t* anchor = ScanUniqueCode(
        kUpdatePrimAnchor, sizeof(kUpdatePrimAnchor), &hits);
    if (!anchor)
    {
        Log("[LaserColor] not armed: the LaserSight::UpdatePrim anchor matched %d "
            "time(s) instead of once, so there is no safe target and every laser keeps "
            "the engine's own values\n", hits);
        return true;
    }

    void* updatePrim = const_cast<std::uint8_t*>(anchor) - kUpdatePrimAnchorBack;
    if (!CreateAndEnableHook(updatePrim, &hkUpdatePrim,
                             reinterpret_cast<void**>(&g_OrigUpdatePrim)))
    {
        Log("[LaserColor] not armed: the LaserSight::UpdatePrim hook at %p was refused, "
            "so every laser keeps the engine's own values\n", updatePrim);
        return true;
    }
    g_UpdatePrimAddr = updatePrim;

    void* createLsc = ResolveGameAddress(createAddr);
    if (createLsc
        && !LooksLikeCreateLaserSightController(static_cast<const std::uint8_t*>(createLsc)))
    {
        DisableAndRemoveHook(g_UpdatePrimAddr);
        g_UpdatePrimAddr = nullptr;
        g_OrigUpdatePrim = nullptr;
        Log("[LaserColor] not armed: 0x%llX does not hold CreateLaserSightController on "
            "this build (its 0x811C9DC5 seed store is absent), so the address is stale "
            "and every laser keeps the engine's own values\n",
            static_cast<unsigned long long>(createAddr));
        return true;
    }
    if (!createLsc || !CreateAndEnableHook(createLsc, &hkCreateLaserSightController,
                                           reinterpret_cast<void**>(&g_OrigCreateLsc)))
    {
        DisableAndRemoveHook(g_UpdatePrimAddr);
        g_UpdatePrimAddr = nullptr;
        g_OrigUpdatePrim = nullptr;
        Log("[LaserColor] not armed: CreateLaserSightController at 0x%llX was refused, "
            "so the drawn weapon cannot be identified and every laser keeps the "
            "engine's own values\n", static_cast<unsigned long long>(createAddr));
        return true;
    }
    g_CreateLscAddr = createLsc;

    int ctorHits = 0;
    const std::uint8_t* ctorAnchor = ScanUniqueCode(
        kCtorAnchor, sizeof(kCtorAnchor), &ctorHits);
    if (ctorAnchor)
    {
        void* ctor = const_cast<std::uint8_t*>(ctorAnchor) - kCtorAnchorBack;
        if (CreateAndEnableHook(ctor, &hkLaserSightCtor,
                                reinterpret_cast<void**>(&g_OrigLaserCtor)))
            g_LaserCtorAddr = ctor;
    }
    if (!g_LaserCtorAddr)
        Log("[LaserColor] the LaserSight constructor could not be hooked (anchor matched "
            "%d time(s)) - the beam keeps the vertex colours baked at creation, so only "
            "the dot follows laserColor\n", ctorHits);

    LogDebug("[LaserColor] armed: UpdatePrim=%p CreateLaserSightController=%p ctor=%p\n",
        updatePrim, createLsc, g_LaserCtorAddr);
    return true;
}

void LaserSight_UpdatePrim_Uninstall()
{
    if (g_UpdatePrimAddr)
    {
        DisableAndRemoveHook(g_UpdatePrimAddr);
        g_UpdatePrimAddr = nullptr;
        g_OrigUpdatePrim = nullptr;
    }
    if (g_CreateLscAddr)
    {
        DisableAndRemoveHook(g_CreateLscAddr);
        g_CreateLscAddr = nullptr;
        g_OrigCreateLsc = nullptr;
    }
    if (g_LaserCtorAddr)
    {
        DisableAndRemoveHook(g_LaserCtorAddr);
        g_LaserCtorAddr = nullptr;
        g_OrigLaserCtor = nullptr;
    }
    g_PlayerCtx.store(nullptr, std::memory_order_release);
}
