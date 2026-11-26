#include <pch.h>
#include "JeepGunEffects.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "AddressSet.h"
#include "EffectControllerImpl_CreateEffect.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "../soldier/RideVehicleActionImpl_State_IdleAction.h"

namespace
{
    using FireSpecs_t           = void(__fastcall*)(void* weapon, std::uint16_t vehicleSlot, void* commInfo);
    using GetConnectPoint_t     = std::int32_t*(__fastcall*)(void* weapon, std::int32_t* error, float* matrix, std::uint16_t vehicleSlot,
                                                             const void* commInfo, std::uint32_t point);
    using GetAttachment_t       = std::uint8_t*(__fastcall*)(void* vehicleSystem, std::uint8_t vehicleCode);
    using GetBuilderMember_t    = std::uint8_t*(__fastcall*)(void* partsBuildDirector, std::uint32_t builderType, std::int32_t index);
    using CreateEffect_t        = void**(__fastcall*)(void** effect, const void* desc);
    using EffectDestroy_t       = std::uint32_t*(__fastcall*)(void* effect, std::uint32_t* err);
    using EffectFlag_t          = std::uint32_t*(__fastcall*)(void* effect, std::uint32_t* err, bool on);
    using SharedAssign_t        = void(__fastcall*)(void** slot, void* value);
    using ThrowCasing_t         = void(__fastcall*)(void* casings, const void* desc);
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();

    constexpr std::size_t   kQuarkApplication      = 0x98;
    constexpr std::size_t   kApplicationVehicles   = 0x2A8;      // the vehicle SystemImpl
    constexpr std::size_t   kApplicationCasings    = 0x240;      // vt[0] throws one casing
    constexpr std::size_t   kSystemGetAttachment   = 0x170;      // vtable: the attachment of a vehicle type code
    constexpr std::size_t   kWeaponGetConnectPoint = 0x40;       // vtable: a connect point of the gun's model, in the world
    constexpr std::size_t   kCommInfoHeading       = 0x2C;       // radians
    constexpr std::size_t   kCommInfoSpeed         = 0x40;       // m/s along the heading
    constexpr std::size_t   kCommInfoCode          = 0x6C;       // vehicle type << 4 | sub type
    constexpr std::size_t   kAttachmentVehicles    = 0xDD;       // how many vehicles can carry the attachment at once
    constexpr std::size_t   kAttachmentDirectors   = 0x108;      // one fox::gk::PartsBuildDirector per such vehicle: the gun part
    constexpr std::uint32_t kEffectBuilders        = 6;          // fox::gk::PartsBuilderIndex of a part's effects
    constexpr std::int32_t  kMostEffectBuilders    = 16;
    constexpr std::size_t   kBuilderDescription    = 0x10;       // fox::gk::EffectDescription
    constexpr std::size_t   kEffectPartName        = 0x88;
    constexpr std::size_t   kEffectFile            = 0x118;
    constexpr std::size_t   kStringIdAt            = 0x10;       // a fox::String keeps its StrCode64 0x10 into its data
    constexpr std::uint64_t kStringIdMask          = 0xFFFFFFFFFFFFull;
    constexpr std::uint64_t kMuzzleFlashPart       = 0x5CFEED1BBBB6ull; // MuzzleFlash
    constexpr std::uint32_t kEjectPoint            = 0xC25AE941; // CNP_EJECT
    constexpr std::size_t   kEffect_Flags          = 0x54;
    constexpr std::uint32_t kEffectAlive           = 4;
    constexpr std::uint64_t kEmptyStringId         = 0xB8A0BF169F98ull; // StrCode64 of ""
    constexpr std::uint32_t kEmptyStrCode32        = 0xBF169F98;
    constexpr std::uint8_t  kEffectDescFlags       = 0x0B;       // what EffectControllerImpl::SetupEffectDesc leaves for a gun's flash request
    constexpr int           kFlashSlots            = 16;         // flashes still playing out while the next shots fire, all armed jeeps
    constexpr std::int32_t  kBeltLink              = 5;          // TppEquip.CASING_BeltLink
    constexpr float         kBeltLinkScale         = 1.2f;
    constexpr float         kEjectSpeed            = 3.7258f;    // 5 m/s over (1 + sin 20 deg), as the cupola gun's code makes it
    constexpr float         kEjectSpread           = 0.34202f;   // sin 20 deg

    struct alignas(16) EffectDesc   // what FoxFxEffect_Create reads, as HeadMotion fills it
    {
        float         matrix[16];
        void*         file;
        std::uint32_t reserved0;
        std::uint32_t pad0;
        std::uint64_t name;
        std::uint64_t reserved1;
        std::uint32_t tag;
        std::uint32_t pad1;
        void*         owner;
        std::uint64_t reserved2;
        std::uint32_t fixedId;
        std::uint32_t id;
        std::uint8_t  flags;
        std::uint8_t  pad2[15];
    };
    static_assert(offsetof(EffectDesc, file) == 0x40 && offsetof(EffectDesc, name) == 0x50 && offsetof(EffectDesc, tag) == 0x60
                      && offsetof(EffectDesc, flags) == 0x80,
                  "EffectDesc layout");

    struct alignas(16) CasingDesc   // what the cupola gun's FireSpecs fills for the casing system
    {
        float        position[4];
        float        rotation[4];
        float        velocity[4];
        float        scale;
        std::int32_t type;
        std::int32_t reserved;
        std::int32_t pad;
    };
    static_assert(offsetof(CasingDesc, scale) == 0x30 && sizeof(CasingDesc) == 0x40, "CasingDesc layout");

    const std::uint8_t kFireSpecsPrologue[] = {
        0x48, 0x8B, 0xC4, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xA8,
        0xC8, 0xFE, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x00, 0x02, 0x00, 0x00
    };
    const std::uint8_t kGetBuilderMemberEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
        0x8B, 0xD8, 0x8B, 0xF2, 0x48, 0x8B, 0xF9, 0x45, 0x85, 0xC0
    };
    const std::uint8_t kCreateEntry[]       = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x48, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30 };
    const std::uint8_t kSendDestroyEntry[]  = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x41, 0x54 };
    const std::uint8_t kSetVisibleEntry[]   = { 0x45, 0x84, 0xC0, 0x74, 0x15, 0x83, 0x49, 0x54, 0x08 };
    const std::uint8_t kSetKillNowEntry[]   = { 0xC7, 0x02, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xC2, 0x45, 0x84, 0xC0 };
    const std::uint8_t kSharedAssignEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA };

    FireSpecs_t                g_OrigFireSpecs       = nullptr;
    void*                      g_HookTarget          = nullptr;
    GetBuilderMember_t         g_GetBuilderMember    = nullptr;
    CreateEffect_t             g_CreateEffect        = nullptr;
    EffectDestroy_t            g_SendDestroy         = nullptr;
    EffectFlag_t               g_SetVisible          = nullptr;
    EffectFlag_t               g_SetKillNow          = nullptr;
    SharedAssign_t             g_SharedAssign        = nullptr;
    GetQuarkSystemTable_t      g_GetQuarkSystemTable = nullptr;
    std::atomic<bool>          g_Ready{ false };
    std::atomic<bool>          g_WarnedNoFlash{ false };
    std::atomic<std::uint32_t> g_LinkSeed{ 0x2545F491u };
    std::mutex                 g_Mutex;
    void*                      g_Flashes[kFlashSlots] = {};
    int                        g_NextFlash            = 0;
#ifdef _DEBUG
    std::atomic<bool>          g_ProbeFirstFlash{ false };
    std::atomic<bool>          g_ProbeFirstLink{ false };
#endif

    // The jeep's gun is the attachment's own part, and that part's MuzzleFlash effect names the .vfx to play
    void* GunFlashFileSEH(const void* commInfo, std::uint16_t vehicleSlot)
    {
        __try
        {
            auto* application = *reinterpret_cast<std::uint8_t**>(g_GetQuarkSystemTable() + kQuarkApplication);
            auto* system = *reinterpret_cast<std::uint8_t**>(application + kApplicationVehicles);
            const auto getAttachment = *reinterpret_cast<GetAttachment_t*>(*reinterpret_cast<std::uint8_t**>(system) + kSystemGetAttachment);
            const std::uint8_t* attachment = getAttachment(system, static_cast<const std::uint8_t*>(commInfo)[kCommInfoCode]);
            if (!attachment || vehicleSlot >= attachment[kAttachmentVehicles])
                return nullptr;
            void* director = (*reinterpret_cast<void* const* const*>(attachment + kAttachmentDirectors))[vehicleSlot];
            if (!director)
                return nullptr;
            for (std::int32_t i = 0; i < kMostEffectBuilders; ++i)
            {
                const std::uint8_t* builder = g_GetBuilderMember(director, kEffectBuilders, i);
                if (!builder)
                    return nullptr;
                const std::uint8_t* description = *reinterpret_cast<const std::uint8_t* const*>(builder + kBuilderDescription);
                const std::uint64_t partName = *reinterpret_cast<const std::uint64_t*>(
                    *reinterpret_cast<const std::uint8_t* const*>(description + kEffectPartName) + kStringIdAt);
                if ((partName & kStringIdMask) != kMuzzleFlashPart)
                    continue;
                return *reinterpret_cast<void* const*>(description + kEffectFile); // null when the .vfx is in no loaded pack, the one case the game's own table refuses too
            }
            return nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void* CreateEffectSEH(const EffectDesc& desc)
    {
        __try
        {
            void* effect = nullptr;
            g_CreateEffect(&effect, &desc);
            if (effect)
            {
                std::uint32_t err = 0;
                g_SetVisible(effect, &err, true);
            }
            return effect;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool AliveSEH(const void* effect)
    {
        __try
        {
            return (*reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(effect) + kEffect_Flags) & kEffectAlive) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void DestroyEffectSEH(void*& effect)
    {
        __try
        {
            std::uint32_t err = 0;
            g_SetKillNow(effect, &err, true);
            g_SendDestroy(effect, &err);
            g_SharedAssign(&effect, nullptr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            effect = nullptr;
        }
    }

    void ReleaseEffectSEH(void*& effect)
    {
        __try
        {
            g_SharedAssign(&effect, nullptr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            effect = nullptr;
        }
    }

    void ShowFlash(void* file, const float* muzzle)
    {
        EffectDesc desc{};
        std::memcpy(desc.matrix, muzzle, sizeof(desc.matrix));
        desc.file = file;
        desc.name = kEmptyStringId;
        desc.tag = kEmptyStrCode32;
        desc.flags = kEffectDescFlags;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (void*& flash : g_Flashes)
            if (flash && !AliveSEH(flash))
                ReleaseEffectSEH(flash);
        void*& slot = g_Flashes[g_NextFlash];
        g_NextFlash = (g_NextFlash + 1) % kFlashSlots;
        if (slot)
            DestroyEffectSEH(slot);
        slot = CreateEffectSEH(desc);
#ifdef _DEBUG
        if (!g_ProbeFirstFlash.exchange(true, std::memory_order_relaxed))
            LogDebug("JeepGunner probe: first muzzle flash %p from .vfx file %p at (%.1f %.1f %.1f)", slot, file, muzzle[12], muzzle[13], muzzle[14]);
#endif
    }

    float NextUnit()   // -1 .. 1
    {
        std::uint32_t x = g_LinkSeed.load(std::memory_order_relaxed);
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 5;
        g_LinkSeed.store(x, std::memory_order_relaxed);
        return static_cast<float>(x >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }

    // The cupola gun these gun models come from throws a belt link out of CNP_EJECT with every shot, along the point's z
    // axis with a 20 degree scatter, on top of the vehicle's own speed (WeaponCupolaHeavyMachineGunImpl::FireSpecs)
    bool ThrowBeltLinkSEH(void* weapon, std::uint16_t vehicleSlot, const void* commInfo, const float* scatter)
    {
        __try
        {
            alignas(16) float point[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
            std::int32_t error = -1;
            const auto getConnectPoint = *reinterpret_cast<GetConnectPoint_t*>(*static_cast<std::uint8_t**>(weapon) + kWeaponGetConnectPoint);
            getConnectPoint(weapon, &error, point, vehicleSlot, commInfo, kEjectPoint);
            if (error != 0)
                return false;
            auto* application = *reinterpret_cast<std::uint8_t**>(g_GetQuarkSystemTable() + kQuarkApplication);
            void* casings = *reinterpret_cast<void**>(application + kApplicationCasings);
            if (!casings)
                return false;
            const auto* info = static_cast<const std::uint8_t*>(commInfo);
            const float heading = *reinterpret_cast<const float*>(info + kCommInfoHeading);
            const float speed = *reinterpret_cast<const float*>(info + kCommInfoSpeed);
            const float moving[3] = { std::sin(heading) * speed, 0.0f, std::cos(heading) * speed };
            CasingDesc link{};
            for (int k = 0; k < 3; ++k)
            {
                link.position[k] = point[12 + k];
                link.velocity[k] = (scatter[0] * point[k] + scatter[1] * point[4 + k] + (1.0f + scatter[2]) * point[8 + k]) * kEjectSpeed + moving[k];
            }
            link.position[3] = 1.0f;
            link.rotation[3] = 1.0f;
            link.scale = kBeltLinkScale;
            link.type = kBeltLink;
            (*reinterpret_cast<ThrowCasing_t*>(*static_cast<std::uint8_t**>(casings)))(casings, &link);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void ThrowBeltLink(void* weapon, std::uint16_t vehicleSlot, const void* commInfo)
    {
        float scatter[3] = {};
        float length = 0.0f;
        for (int tries = 0; tries < 16 && (length < 0.01f || length > 1.0f); ++tries)   // a point inside the unit ball, pushed out to its surface
        {
            scatter[0] = NextUnit();
            scatter[1] = NextUnit();
            scatter[2] = NextUnit();
            length = std::sqrt(scatter[0] * scatter[0] + scatter[1] * scatter[1] + scatter[2] * scatter[2]);
        }
        if (length < 0.01f || length > 1.0f)
            return;
        for (float& axis : scatter)
            axis *= kEjectSpread / length;
        const bool thrown = ThrowBeltLinkSEH(weapon, vehicleSlot, commInfo, scatter);
#ifdef _DEBUG
        if (!g_ProbeFirstLink.exchange(true, std::memory_order_relaxed))
            LogDebug("JeepGunner probe: first belt link %s", thrown ? "thrown from CNP_EJECT" : "not thrown (no CNP_EJECT on the gun model or no casing system)");
#else
        (void)thrown;
#endif
    }

    // A vehicle gun's flash comes from its vehicle's effect table, by a slot the jeep's table lacks. The jeep's gun is a
    // part of its own with a MuzzleFlash effect: that one plays at the muzzle matrix the gun asked with, as the table's would.
    void __fastcall hk_FireSpecs(void* weapon, std::uint16_t vehicleSlot, void* commInfo)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigFireSpecs, weapon, vehicleSlot, commInfo);
        if (!JeepGunner_IsArmedLightVehicle(commInfo))
        {
            g_OrigFireSpecs(weapon, vehicleSlot, commInfo);
            return;
        }
        alignas(16) float muzzle[16] = {};
        EffectGuard_BeginCapture(muzzle);
        g_OrigFireSpecs(weapon, vehicleSlot, commInfo);
        const bool flashDropped = EffectGuard_EndCapture();
        ThrowBeltLink(weapon, vehicleSlot, commInfo);
        if (!flashDropped)
            return;
        void* file = GunFlashFileSEH(commInfo, vehicleSlot);
        if (file)
            ShowFlash(file, muzzle);
        else if (!g_WarnedNoFlash.exchange(true, std::memory_order_relaxed))
            LogWarn("JeepGunner: the jeep gun's part has no loaded MuzzleFlash effect, the gun fires without a flash");
    }

    void* ResolveChecked(std::uintptr_t address, const std::uint8_t* entry, std::size_t size)
    {
        void* fn = address ? ResolveGameAddress(address) : nullptr;
        return fn && CheckHookEntry(fn, entry, size) != HookEntry::Unknown ? fn : nullptr;
    }
}

bool JeepGunEffects_Install()
{
    if (g_HookTarget)
        return true;
    g_GetBuilderMember = reinterpret_cast<GetBuilderMember_t>(
        ResolveChecked(gAddr.PartsBuildDirector_GetBuilderMemberFromTypeIndex, kGetBuilderMemberEntry, sizeof(kGetBuilderMemberEntry)));
    g_CreateEffect = reinterpret_cast<CreateEffect_t>(ResolveChecked(gAddr.FoxFxEffect_Create, kCreateEntry, sizeof(kCreateEntry)));
    g_SendDestroy = reinterpret_cast<EffectDestroy_t>(ResolveChecked(gAddr.FoxFxEffect_SendDestroy, kSendDestroyEntry, sizeof(kSendDestroyEntry)));
    g_SetVisible = reinterpret_cast<EffectFlag_t>(ResolveChecked(gAddr.FoxFxEffect_SetVisible, kSetVisibleEntry, sizeof(kSetVisibleEntry)));
    g_SetKillNow = reinterpret_cast<EffectFlag_t>(ResolveChecked(gAddr.FoxFxEffect_SetKillNow, kSetKillNowEntry, sizeof(kSetKillNowEntry)));
    g_SharedAssign = reinterpret_cast<SharedAssign_t>(ResolveChecked(gAddr.FoxSharedPtrBase_Set, kSharedAssignEntry, sizeof(kSharedAssignEntry)));
    g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
    void* target = ResolveGameAddress(gAddr.VehicleWeaponBulletImpl_FireSpecs);
    const HookEntry entry = target ? CheckHookEntry(target, kFireSpecsPrologue, sizeof(kFireSpecsPrologue)) : HookEntry::Unknown;
    if (!g_GetBuilderMember || !g_CreateEffect || !g_SendDestroy || !g_SetVisible || !g_SetKillNow || !g_SharedAssign
        || !g_GetQuarkSystemTable || entry == HookEntry::Unknown)
    {
        LogWarn("JeepGunner: vehicle gun or effect code does not match %s, the jeep gun fires without a muzzle flash and throws no belt links",
                GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("JeepGunner: vehicle WeaponBulletImpl::FireSpecs", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_FireSpecs), reinterpret_cast<void**>(&g_OrigFireSpecs)))
    {
        LogWarn("JeepGunner: vehicle WeaponBulletImpl::FireSpecs hook failed, the jeep gun fires without a muzzle flash and throws no belt links");
        return false;
    }
    g_HookTarget = target;
    g_Ready.store(true, std::memory_order_release);
    return true;
}

void JeepGunEffects_Drop()
{
    if (!g_Ready.load(std::memory_order_acquire))
        return;
    std::lock_guard<std::mutex> lock(g_Mutex);
    for (void*& flash : g_Flashes)
        if (flash)
            DestroyEffectSEH(flash);
}

void JeepGunEffects_Uninstall()
{
    JeepGunEffects_Drop();
    g_Ready.store(false, std::memory_order_release);
    if (!g_HookTarget)
        return;
    DisableAndRemoveHook(g_HookTarget);
    g_HookTarget = nullptr;
    g_OrigFireSpecs = nullptr;
}
