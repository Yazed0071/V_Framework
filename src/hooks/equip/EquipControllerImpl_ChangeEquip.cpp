#include "pch.h"
#include "EquipControllerImpl_ChangeEquip.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "EquipAliasLease.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    // returns out (struct return), callers may read the result through it
    using ChangeEquipFn = std::uint32_t*(__fastcall*)(void* self, std::uint32_t* out, std::uint32_t playerIndex,
                                                      std::uint8_t* signal);

    const std::uint8_t kPrologue[] = {
        0x40, 0x53, 0x55, 0x56, 0x41, 0x54, 0x41, 0x56, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00
    };

    constexpr std::size_t  kSignalEquipId = 0x08;    // u16; the original builds a pickable from id & 0x7FF and matches slots by exact id
    constexpr std::int32_t kNoEquipId     = 0x7FF;

    ChangeEquipFn g_Orig = nullptr;
    void*         g_Addr = nullptr;

    std::uintptr_t ChangeEquipAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:  return 0x1411AC2E0ull;
        default:                                           return 0;
        }
    }

    bool PrologueMatchesSEH(const void* fn)
    {
        __try
        {
            return std::memcmp(fn, kPrologue, sizeof(kPrologue)) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SwapSignalToEngineSEH(std::uint8_t* signal, std::uint16_t& saved, std::uint16_t& written)
    {
        __try
        {
            auto* cell = reinterpret_cast<std::uint16_t*>(signal + kSignalEquipId);
            const std::uint16_t value = *cell;
            if (value == 0)
                return false;
            const std::int32_t realId = equip::ResolveReal(value);
            std::int32_t engineId = equip::ToEngine(realId);
            if (engineId == kNoEquipId && realId != kNoEquipId)
                engineId = 0;
            if (engineId == value)
                return false;
            saved = value;
            written = static_cast<std::uint16_t>(engineId);
            *cell = written;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void RestoreSignalSEH(std::uint8_t* signal, std::uint16_t saved, std::uint16_t written)
    {
        __try
        {
            auto* cell = reinterpret_cast<std::uint16_t*>(signal + kSignalEquipId);
            if (*cell == written)
                *cell = saved;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    std::uint32_t* __fastcall hkChangeEquip(void* self, std::uint32_t* out, std::uint32_t playerIndex, std::uint8_t* signal)
    {
        std::uint16_t saved = 0;
        std::uint16_t written = 0;
        const bool swapped = signal && SwapSignalToEngineSEH(signal, saved, written);
        std::uint32_t* result = g_Orig(self, out, playerIndex, signal);
        if (swapped)
            RestoreSignalSEH(signal, saved, written);
        return result;
    }
}

bool Install_EquipControllerImpl_ChangeEquip()
{
    const std::uintptr_t addr = ChangeEquipAddr();
    if (addr == 0)
        return true;

    void* fn = ResolveGameAddress(addr);
    if (!fn || !PrologueMatchesSEH(fn))
    {
        LogError("EquipAlias: ChangeEquip prologue mismatch on %s, Player.ChangeEquip with equipIds above 0x7FF equips the wrong item",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkChangeEquip),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("EquipAlias: ChangeEquip hook failed on %s, Player.ChangeEquip with equipIds above 0x7FF equips the wrong item",
            GetGameBuildName(gGameBuild));
        return false;
    }
    g_Addr = fn;
    return true;
}

void Uninstall_EquipControllerImpl_ChangeEquip()
{
    if (g_Addr)
    {
        DisableAndRemoveHook(g_Addr);
        g_Addr = nullptr;
    }
    g_Orig = nullptr;
}
