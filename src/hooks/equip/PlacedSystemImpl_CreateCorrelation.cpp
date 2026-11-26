#include <pch.h>
#include "PlacedSystemImpl_CreateCorrelation.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "DecoySystemImpl_Realize.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SupportWeaponRegistry.h"
#include "ThrowingSystemLayout.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "log.h"

namespace
{
    using CreateCorrelation_t = bool(__fastcall*)(std::uint8_t* placed, std::uint32_t index, std::uint16_t* correlation,
                                                  std::uint8_t flags);

    constexpr std::size_t   kPlacedEquipIds     = 0x38;    // u16 equipId + u16 owner per placed item
    constexpr std::size_t   kDecoySystem        = 0x268;   // in the game system table
    constexpr std::size_t   kDecoyRecords       = 0x50;
    constexpr std::size_t   kDecoyRecordCount   = 0x64;
    constexpr std::size_t   kDecoyRecordSize    = 0x40;
    constexpr std::size_t   kDecoyRecordFlags   = 0x30;
    constexpr std::uint32_t kDecoyLookShift     = 9;       // 3 bits, the look the decoy gets when it inflates
    constexpr std::uint32_t kDecoyLookMask      = 0x7u << kDecoyLookShift;
    constexpr std::uint16_t kNoDecoy            = 0xFFFF;

    const std::uint8_t kEntry[] = {
        0x40, 0x55, 0x53, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x8B, 0xEC, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B,
    };

    CreateCorrelation_t g_Orig   = nullptr;
    void*               g_Target = nullptr;

    std::uint16_t PlacedEquipIdSEH(const std::uint8_t* placed, std::uint32_t index)
    {
        __try
        {
            const std::uint8_t* equipIds = *reinterpret_cast<const std::uint8_t* const*>(placed + kPlacedEquipIds);
            return equipIds ? *reinterpret_cast<const std::uint16_t*>(equipIds + index * 4) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool SetDecoyLookSEH(std::uint16_t decoy, std::uint32_t decoyType)
    {
        __try
        {
            std::uint8_t* gameSystems = ThrowingSystemLayout::GameSystemTable();
            std::uint8_t* decoys = gameSystems ? *reinterpret_cast<std::uint8_t**>(gameSystems + kDecoySystem) : nullptr;
            if (!decoys || decoy >= *reinterpret_cast<const std::uint32_t*>(decoys + kDecoyRecordCount))
                return false;
            std::uint8_t* records = *reinterpret_cast<std::uint8_t**>(decoys + kDecoyRecords);
            auto* flags = reinterpret_cast<std::uint32_t*>(records + decoy * kDecoyRecordSize + kDecoyRecordFlags);
            *flags = (*flags & ~kDecoyLookMask) | ((decoyType << kDecoyLookShift) & kDecoyLookMask);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool __fastcall hkCreateCorrelation(std::uint8_t* placed, std::uint32_t index, std::uint16_t* correlation, std::uint8_t flags)
    {
        const std::uint16_t equipId = (placed && !MissionCodeGuard::ShouldBypassHooks()) ? PlacedEquipIdSEH(placed, index) : 0;
        const bool decoy = equipId != 0
                           && (SupportWeapon::IsDecoyDonor(equipId) || SupportWeapon::IsDecoyDonor(SupportWeapon::FamilyFrom(equipId)));
        V_DecoyModels models{};
        const bool ownFigure = decoy && TppEquip_GetDecoyModels(equipId, &models) && models.figure != 0;
        const std::uint32_t decoyType = (equipId != 0 && !ownFigure) ? SupportWeapon::DecoyType(equipId) : 0;   // the looks are fv2s of the game's figure
        const bool made = g_Orig(placed, index, correlation, flags);
        if (!made || !correlation || *correlation == kNoDecoy)
            return made;
        if (decoy)
            DecoySystemImpl_NoteRecordItem(*correlation, placed, index, equipId, correlation);
        if (decoyType != 0)
            SetDecoyLookSEH(*correlation, decoyType);
        return made;
    }
}

bool Install_PlacedSystemImpl_CreateCorrelation_Hook()
{
    g_Target = ResolveGameAddress(gAddr.PlacedSystemImpl_CreateCorrelation);
    if (!g_Target)
    {
        LogError("EquipCustomPlaced: CreateCorrelation address missing for this build, decoyType is off");
        return true;
    }
    const HookEntry entry = CheckHookEntry(g_Target, kEntry, sizeof(kEntry));
    if (entry == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(g_Target, desc, sizeof(desc));
        LogError("EquipCustomPlaced: PlacedSystemImpl::CreateCorrelation differs on this build (%s), decoyType is off", desc);
        g_Target = nullptr;
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("EquipCustomPlaced: PlacedSystemImpl::CreateCorrelation", g_Target);
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkCreateCorrelation), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("EquipCustomPlaced: CreateCorrelation hook failed, decoyType is off");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_PlacedSystemImpl_CreateCorrelation_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
