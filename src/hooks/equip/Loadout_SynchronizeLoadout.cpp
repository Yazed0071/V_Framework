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
#include "../outfit/CustomHeadRegistry.h"
#include "../outfit/OutfitRegistry.h"

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

    constexpr std::size_t kSuitParts       = 0x00;
    constexpr std::size_t kSuitCamo        = 0x01;
    constexpr std::size_t kSuitFace        = 0x03;
    constexpr std::size_t kWeaponPartsDesc = 0x06;   // weapon block: id, isChimera, chimeraSlot, 12 part bytes, paint, colour
    constexpr std::size_t kWeaponBlockSize = 0x14;
    constexpr int         kWeaponBlocks    = 3;
    constexpr int         kEssentialParts  = 3;      // receiver, barrel, magazine
    constexpr int         kUnderBarrelPart = 10;

    constexpr int kDescSpace[12] = {
        kVanillaSpace_Receiver, kVanillaSpace_Barrel, kVanillaSpace_Magazine, kVanillaSpace_Stock,
        -1, kVanillaSpace_MuzzleOption, kVanillaSpace_Sight, kVanillaSpace_Sight,
        kVanillaSpace_Option, kVanillaSpace_Option, kVanillaSpace_UnderBarrel, kVanillaSpace_Magazine
    };

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

    void SanitizeSuit(std::uint8_t* info)
    {
        const std::uint8_t parts = info[kSuitParts];
        if (parts >= outfit::kCustomPartsTypeStart && parts <= outfit::kCustomPartsTypeEnd)
        {
            info[kSuitParts] = 0;
            info[kSuitCamo] = 0;
        }
        else if (info[kSuitCamo] >= outfit::kCamoVirtualIdStart && info[kSuitCamo] <= outfit::kCamoVirtualIdEnd)
        {
            info[kSuitCamo] = 0;
        }
        if (outfit::IsCustomHeadSlot(info[kSuitFace]))
            info[kSuitFace] = 0;
    }

    bool IsCustomPartByte(int space, std::uint8_t value)
    {
        return space >= 0 && value != 0 && value > EquipParam_VanillaPartCount(space);
    }

    void SanitizeWeapon(std::uint8_t* block, bool idWasCustom)
    {
        std::uint8_t* desc = block + kWeaponPartsDesc;
        bool clearBlock = idWasCustom;
        for (int part = 0; part < 12; ++part)
        {
            if (!IsCustomPartByte(kDescSpace[part], desc[part]))
                continue;
            if (part < kEssentialParts)
            {
                clearBlock = true;
                continue;
            }
            desc[part] = 0;
            if (part == kUnderBarrelPart)
                desc[part + 1] = 0;
        }
        if (!clearBlock)
            return;
        std::memset(block + sizeof(std::uint32_t), 0, kWeaponBlockSize - sizeof(std::uint32_t));
        if (!idWasCustom)
            *reinterpret_cast<std::uint32_t*>(block) = 0;
    }

    void SanitizeForServer(std::uint8_t* info)
    {
        constexpr int kCellCount = 4 + kSlotsPerKind * 2;

        bool weaponWasCustom[kWeaponBlocks] = {};
        for (int i = 0; i < kCellCount; ++i)
        {
            std::uint32_t* cell = CellAt(info, i);
            const std::uint32_t id = *cell;
            if (!IsManagedEquipId(id))
                continue;
            if (i >= 1 && i <= kWeaponBlocks)
                weaponWasCustom[i - 1] = true;
            *cell = ServerSubstitute(i, id);
        }

        SanitizeSuit(info);
        for (int w = 0; w < kWeaponBlocks; ++w)
            SanitizeWeapon(reinterpret_cast<std::uint8_t*>(CellAt(info, w + 1)), weaponWasCustom[w]);
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
