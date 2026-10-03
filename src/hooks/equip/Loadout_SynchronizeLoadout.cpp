#include "pch.h"
#include "Loadout_SynchronizeLoadout.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "EquipIdCompression.h"
#include "EquipPartParams.h"
#include "HookUtils.h"
#include "V_FrameWorkState.h"
#include "log.h"

namespace
{
    using SynchronizeLoadoutFn = bool(__fastcall*)(void* self, const std::uint8_t* info);

    constexpr std::size_t kHandEquip     = 0x04;
    constexpr std::size_t kPrimary1      = 0x18;
    constexpr std::size_t kPrimary2      = 0x2C;
    constexpr std::size_t kSecondary     = 0x40;
    constexpr std::size_t kSupportFirst  = 0x54;
    constexpr std::size_t kItemFirst     = 0x74;
    constexpr std::size_t kLoadoutSize   = 0xE8;
    constexpr int         kSlotsPerKind  = 8;

    const std::uint8_t kPrologue[] = {
        0x40, 0x55, 0x56, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x40,
        0x48, 0xC7, 0x45, 0xF0, 0xFE, 0xFF, 0xFF, 0xFF
    };

    SynchronizeLoadoutFn g_Orig = nullptr;
    void*                g_Addr = nullptr;

    std::uintptr_t SynchronizeLoadoutAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:  return 0x1407F4F90ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:  return 0x1407F4BC0ull;
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:  return 0x1407F5BD0ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:  return 0x1407F5D10ull;
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

    bool CopyLoadoutSEH(const std::uint8_t* info, std::uint8_t* out)
    {
        __try
        {
            std::memcpy(out, info, kLoadoutSize);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t* CellAt(std::uint8_t* info, int index)
    {
        if (index == 0) return reinterpret_cast<std::uint32_t*>(info + kHandEquip);
        if (index == 1) return reinterpret_cast<std::uint32_t*>(info + kPrimary1);
        if (index == 2) return reinterpret_cast<std::uint32_t*>(info + kPrimary2);
        if (index == 3) return reinterpret_cast<std::uint32_t*>(info + kSecondary);
        if (index < 12)
            return reinterpret_cast<std::uint32_t*>(
                info + kSupportFirst + (index - 4) * sizeof(std::uint32_t));
        return reinterpret_cast<std::uint32_t*>(
            info + kItemFirst + (index - 12) * sizeof(std::uint32_t));
    }

    bool IsManagedEquipId(std::uint32_t id)
    {
        return id != 0
            && (V_FrameWorkState::IsClaimedEquipId(static_cast<std::int32_t>(id))
                || EquipIdCompression::IsExtendedEquipId(static_cast<std::int32_t>(id)));
    }

    std::uint32_t ServerSubstitute(int index, std::uint32_t id)
    {
        if (index < 1 || index > 3)
            return 0;
        const std::uint32_t donor = EquipParam_GetWeaponHandlingDonor(id);
        return donor != id && !IsManagedEquipId(donor) ? donor : 0;
    }

    void SanitizeForServer(std::uint8_t* info)
    {
        constexpr int kCellCount = 4 + kSlotsPerKind * 2;

        for (int i = 0; i < kCellCount; ++i)
        {
            std::uint32_t* cell = CellAt(info, i);
            const std::uint32_t id = *cell;
            if (IsManagedEquipId(id))
                *cell = ServerSubstitute(i, id);
        }
    }

    bool __fastcall hkSynchronizeLoadout(void* self, const std::uint8_t* info)
    {
        if (!info)
            return g_Orig(self, info);
        alignas(16) std::uint8_t safe[kLoadoutSize];
        if (!CopyLoadoutSEH(info, safe))
        {
            LogWarn("FobLoadout: loadout upload refused, source unreadable");
            return false;
        }
        SanitizeForServer(safe);
        return g_Orig(self, safe);
    }
}

bool Install_Loadout_SynchronizeLoadout()
{
    const std::uintptr_t addr = SynchronizeLoadoutAddr();
    if (addr == 0)
    {
        LogError("FobLoadout: SynchronizeLoadout address missing for %s, custom equipIds not stripped from FOB uploads",
            GetGameBuildName(gGameBuild));
        return false;
    }

    void* fn = ResolveGameAddress(addr);
    if (!fn || !PrologueMatchesSEH(fn))
    {
        LogError("FobLoadout: SynchronizeLoadout prologue mismatch on %s, custom equipIds not stripped from FOB uploads",
            GetGameBuildName(gGameBuild));
        return false;
    }

    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkSynchronizeLoadout),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("FobLoadout: SynchronizeLoadout hook failed on %s, custom equipIds not stripped from FOB uploads",
            GetGameBuildName(gGameBuild));
        return false;
    }

    g_Addr = fn;
    return true;
}

void Uninstall_Loadout_SynchronizeLoadout()
{
    if (g_Addr)
    {
        DisableAndRemoveHook(g_Addr);
        g_Addr = nullptr;
    }
    g_Orig = nullptr;
}
