#include "pch.h"
#include "UiControllerImpl_UpdateEquipHud.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "BarrierEffectSpawn.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using UpdateEquipHudFn = void(__fastcall*)(void* self);
    using InfoGetterFn     = void*(__fastcall*)(void* hudSystem);

    constexpr int           kEnergyWallEquipId = 0x1E9;
    constexpr std::size_t   kGateOffset        = 0x427;
    constexpr std::size_t   kPlayer            = 0x28;
    constexpr std::size_t   kPlayerFlags       = 0x204;
    constexpr std::uint32_t kFobPlayerFlag     = 0x40000;
    constexpr std::size_t   kHudSystem         = 0x48;
    constexpr std::size_t   kInfoSourceIds     = 0x58;
    constexpr std::size_t   kInfoCounts        = 0x78;
    constexpr std::size_t   kInfoStateIds      = 0x98;
    constexpr std::size_t   kInfoStateFlags    = 0xB8;
    constexpr int           kInfoSlots         = 8;
    constexpr std::uint8_t  kNoUseBit          = 0x01;

    const std::uint8_t kGateBytes[] = { 0x81, 0xFB, 0xE9, 0x01, 0x00, 0x00 };

    UpdateEquipHudFn   g_OrigUpdateEquipHud = nullptr;
    void*              g_UpdateEquipHudAddr = nullptr;
    std::atomic<void*> g_EquipHudInfo{ nullptr };

    std::uintptr_t UpdateEquipHudAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:  return 0x140FE9C50ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:  return 0x140FE9D20ull;
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:  return 0x140FE9430ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:  return 0x140FE9450ull;
        default:                                           return 0;
        }
    }

    bool GateBytesMatchSEH(const void* fn)
    {
        __try
        {
            return std::memcmp(static_cast<const std::uint8_t*>(fn) + kGateOffset,
                               kGateBytes, sizeof(kGateBytes)) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void ClearEnergyWallNoUseSEH(void* info)
    {
        __try
        {
            auto* base   = static_cast<std::uint8_t*>(info);
            auto* source = reinterpret_cast<std::int32_t*>(base + kInfoSourceIds);
            auto* counts = reinterpret_cast<std::int32_t*>(base + kInfoCounts);

            bool hasUsesLeft = false;
            for (int i = 0; i < kInfoSlots; ++i)
            {
                if (source[i] == kEnergyWallEquipId && counts[i] != 0)
                {
                    hasUsesLeft = true;
                    break;
                }
            }
            if (!hasUsesLeft)
                return;

            auto* stateIds = reinterpret_cast<std::int32_t*>(base + kInfoStateIds);
            for (int i = 0; i < kInfoSlots; ++i)
            {
                if (stateIds[i] == kEnergyWallEquipId)
                {
                    base[kInfoStateFlags + i] &= static_cast<std::uint8_t>(~kNoUseBit);
                    return;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool ReadItemNoUseSEH(void* info, int equipId)
    {
        __try
        {
            auto* base     = static_cast<std::uint8_t*>(info);
            auto* stateIds = reinterpret_cast<std::int32_t*>(base + kInfoStateIds);
            for (int i = 0; i < kInfoSlots; ++i)
                if (stateIds[i] == equipId)
                    return (base[kInfoStateFlags + i] & kNoUseBit) != 0;
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t* PlayerFlagsSEH(void* self)
    {
        __try
        {
            auto* player = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(self) + kPlayer);
            if (!player)
                return nullptr;
            auto* flags = reinterpret_cast<std::uint32_t*>(player + kPlayerFlags);
            volatile std::uint32_t touch = *flags;
            (void)touch;
            return flags;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void __fastcall hkUpdateEquipHud(void* self)
    {
        const bool singlePlayer = self && !MissionCodeGuard::ShouldBypassHooks() && Barrier_InSinglePlayer();
        std::uint32_t* playerFlags = singlePlayer ? PlayerFlagsSEH(self) : nullptr;
        const std::uint32_t savedFlags = playerFlags ? *playerFlags : 0;
        if (playerFlags)
            *playerFlags &= ~kFobPlayerFlag; // held by the Energy Wall / wormhole in single-player; the HUD would lock the cigar, stealth camo, reflex medicine and parasite items like in FOB
        g_OrigUpdateEquipHud(self);
        if (playerFlags)
            *playerFlags = (*playerFlags & ~kFobPlayerFlag) | (savedFlags & kFobPlayerFlag);

        if (!self)
            return;
        void* info = EquipHud_ResolveInfo(self);
        if (!info)
            return;
        g_EquipHudInfo.store(info, std::memory_order_release);
        if (!singlePlayer)
            return;
        ClearEnergyWallNoUseSEH(info);
    }
}

bool Install_UiControllerImpl_UpdateEquipHud()
{
    const std::uintptr_t addr = UpdateEquipHudAddr();
    if (addr == 0)
    {
        LogError("Barrier: UpdateEquipHud address missing for this build, Energy Wall unusable in single-player");
        return false;
    }

    void* fn = ResolveGameAddress(addr);
    if (!fn || !GateBytesMatchSEH(fn))
    {
        LogError("Barrier: UpdateEquipHud 0x%llX has no Energy Wall gate, Energy Wall unusable in single-player",
                 static_cast<unsigned long long>(addr));
        return false;
    }

    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkUpdateEquipHud),
                             reinterpret_cast<void**>(&g_OrigUpdateEquipHud)))
    {
        LogError("Barrier: UpdateEquipHud hook failed, Energy Wall unusable in single-player");
        return false;
    }

    g_UpdateEquipHudAddr = fn;
    return true;
}

void Uninstall_UiControllerImpl_UpdateEquipHud()
{
    if (g_UpdateEquipHudAddr)
    {
        DisableAndRemoveHook(g_UpdateEquipHudAddr);
        g_UpdateEquipHudAddr = nullptr;
    }
    g_OrigUpdateEquipHud = nullptr;
    g_EquipHudInfo.store(nullptr, std::memory_order_release);
}

void* EquipHud_ResolveInfo(void* uiController)
{
    __try
    {
        void* hudSystem = *reinterpret_cast<void**>(
            static_cast<std::uint8_t*>(uiController) + kHudSystem);
        if (!hudSystem)
            return nullptr;
        auto** vtbl = *reinterpret_cast<void***>(hudSystem);
        if (!vtbl || !vtbl[0])
            return nullptr;
        return reinterpret_cast<InfoGetterFn>(vtbl[0])(hudSystem);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

bool EquipHud_IsItemUsable(int equipId)
{
    void* info = g_EquipHudInfo.load(std::memory_order_acquire);
    if (!info)
        return true;
    return !ReadItemNoUseSEH(info, equipId);
}
