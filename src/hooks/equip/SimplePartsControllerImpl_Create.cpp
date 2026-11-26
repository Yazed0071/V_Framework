#include <pch.h>
#include "SimplePartsControllerImpl_Create.h"

#include <cstdint>
#include <cstring>
#include <intrin.h>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using Create_t = void*(__fastcall*)(const std::uint32_t* desc);
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();
    using GetMissionCode_t = std::uint16_t(__fastcall*)(void* missionSystem);

    // Every gun group's parts controller (RealizedEquipObjectImpl::Initialize) keeps 4 effects per .parts, 7 in heli
    // space and mission 50050; the effects after that are dropped when the .parts loads.
    constexpr std::uint32_t kGunEffectsPerParts = 16;
    constexpr std::size_t kDescEffectsPerParts = 3;
    constexpr std::size_t kDescWords = 7;

    constexpr std::size_t kInitializeEffectsStore = 0x40C; // mov qword [rbp-0x14], 4
    constexpr std::size_t kInitializeCreateCall = 0x480;   // lea rdx,[rbp-0x20] ... call [r8]
    constexpr std::size_t kInitializeCreateReturn = 0x48D;
    constexpr std::size_t kQuarkMissionHolder = 0x98;
    constexpr std::size_t kMissionSystem = 0x100;
    constexpr std::size_t kGetMissionCodeSlot = 0x48;

    constexpr std::uint8_t kCreateEntry[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0x48, 0x8B, 0xD9,
        0xB9, 0xF8, 0x00, 0x00, 0x00,
    };
    constexpr std::uint8_t kEffectsStore[] = { 0x48, 0xC7, 0x45, 0xEC, 0x04, 0x00, 0x00, 0x00 };
    constexpr std::uint8_t kCreateCall[] = { 0x48, 0x8D, 0x55, 0xE0, 0x4C, 0x8B, 0x00, 0x48, 0x8B, 0xC8, 0x41, 0xFF, 0x10 };

    Create_t g_Orig = nullptr;
    void* g_Target = nullptr;
    void* g_GunGroupReturn = nullptr;
    GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;

    bool MissionCodeSEH(std::uint16_t* code) // the mission system RealizedEquipObjectImpl::Initialize asks for its 7
    {
        __try
        {
            std::uint8_t* table = g_GetQuarkSystemTable();
            std::uint8_t* holder = table ? *reinterpret_cast<std::uint8_t**>(table + kQuarkMissionHolder) : nullptr;
            void* missionSystem = holder ? *reinterpret_cast<void**>(holder + kMissionSystem) : nullptr;
            if (!missionSystem)
                return false;
            void** vtable = *reinterpret_cast<void***>(missionSystem);
            *code = reinterpret_cast<GetMissionCode_t>(vtable[kGetMissionCodeSlot / sizeof(void*)])(missionSystem);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void* __fastcall hkCreate(const std::uint32_t* desc)
    {
        if (_ReturnAddress() != g_GunGroupReturn || !desc || desc[kDescEffectsPerParts] >= kGunEffectsPerParts)
            return g_Orig(desc);
        std::uint16_t missionCode = 0;
        if (!MissionCodeSEH(&missionCode) || MissionCodeGuard::IsMissionBlocked(missionCode))
        {
            return g_Orig(desc);
        }
        std::uint32_t raised[kDescWords];
        std::memcpy(raised, desc, sizeof(raised));
        raised[kDescEffectsPerParts] = kGunEffectsPerParts;
        return g_Orig(raised);
    }

    bool BytesMatch(const void* at, const std::uint8_t* expected, std::size_t size)
    {
        __try
        {
            return std::memcmp(at, expected, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

bool Install_SimplePartsControllerImpl_Create_Hook()
{
    if (g_Target)
        return true;
    auto* initialize = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.RealizedEquipObjectImpl_Initialize));
    void* target = ResolveGameAddress(gAddr.SimplePartsControllerImpl_Create);
    g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
    if (!initialize || !target || !g_GetQuarkSystemTable)
    {
        LogError("PartsEffectSlots: addresses missing for this build, guns keep 4 effects per .parts");
        return false;
    }
    if (!BytesMatch(initialize + kInitializeEffectsStore, kEffectsStore, sizeof(kEffectsStore))
        || !BytesMatch(initialize + kInitializeCreateCall, kCreateCall, sizeof(kCreateCall)))
    {
        LogError("PartsEffectSlots: RealizedEquipObjectImpl::Initialize does not match, guns keep 4 effects per .parts");
        return false;
    }
    const HookEntry entry = CheckHookEntry(target, kCreateEntry, sizeof(kCreateEntry));
    if (entry == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(target, found, sizeof(found));
        LogError("PartsEffectSlots: SimplePartsController create does not match (%s), guns keep 4 effects per .parts",
                 found);
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("PartsEffectSlots: SimplePartsController create", target);
    g_GunGroupReturn = initialize + kInitializeCreateReturn;
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkCreate), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("PartsEffectSlots: SimplePartsController create hook failed @ %p, guns keep 4 effects per .parts", target);
        g_GunGroupReturn = nullptr;
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_SimplePartsControllerImpl_Create_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
    g_GunGroupReturn = nullptr;
    g_GetQuarkSystemTable = nullptr;
}
