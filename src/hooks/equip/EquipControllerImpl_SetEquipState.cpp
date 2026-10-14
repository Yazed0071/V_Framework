#include "pch.h"
#include "EquipControllerImpl_SetEquipState.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "EquipAliasLease.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using SetEquipStateFn = void(__fastcall*)(void* self, std::uint32_t playerIndex,
                                              std::int32_t slotType, std::uint32_t slotIndex,
                                              std::uint32_t equipId, std::uint8_t level,
                                              bool isInitial, std::uint8_t itemExtra);

    const std::uint8_t kPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x6C, 0x24, 0x20,
        0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83
    };

    constexpr std::size_t kControllerOffset   = 0x18;
    constexpr std::size_t kScriptVarsOffset   = 0x20;
    constexpr std::size_t kLocalPlayerIndex   = 0x214; // controller field holding the local player's index
    constexpr std::size_t kPlayersOffset      = 0x60;
    constexpr std::size_t kEquipStateRows     = 0xC0;  // EquipState rows, one per player
    constexpr std::size_t kEquipStateRowSize  = 0x3A;
    constexpr std::size_t kVarsWeapons        = 0x548; // vars.weapons, one u16 per primary/secondary slot
    constexpr std::size_t kVarsSupportWeapons = 0x584;
    constexpr std::size_t kVarsItems          = 0x5A4;
    constexpr std::size_t kVarsHandEquip      = 0x5D4;
    constexpr std::int32_t kNoEquipId         = 0x7FF; // empty for weapon cells; support, item and hand cells use 0

    enum SlotType : std::int32_t
    {
        kSlotPrimary1     = 0,
        kSlotPrimary2     = 1,
        kSlotSecondary    = 2,
        kSlotSupport      = 3,
        kSlotItem         = 4,
        kSlotHand         = 6,
    };

    SetEquipStateFn g_Orig = nullptr;
    void*           g_Addr = nullptr;

    std::uintptr_t SetEquipStateAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:  return 0x1411AFF30ull;
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

    std::size_t VarsCellOffset(std::int32_t slotType, std::uint32_t slotIndex, bool& known)
    {
        known = true;
        switch (slotType)
        {
        case kSlotPrimary1:
        case kSlotPrimary2:
        case kSlotSecondary: return kVarsWeapons + static_cast<std::size_t>(slotType) * 2;
        case kSlotSupport:   return kVarsSupportWeapons + static_cast<std::size_t>(slotIndex) * 2;
        case kSlotItem:      return kVarsItems + static_cast<std::size_t>(slotIndex) * 2;
        case kSlotHand:      return kVarsHandEquip;
        default:             known = false; return 0;
        }
    }

    void KeepRealIdInVarsSEH(void* self, std::uint32_t playerIndex, std::int32_t slotType,
                             std::uint32_t slotIndex, std::int32_t realId)
    {
        bool known = false;
        const std::size_t cell = VarsCellOffset(slotType, slotIndex, known);
        if (!known)
            return;
        __try
        {
            auto* bytes = static_cast<std::uint8_t*>(self);
            auto* controller = *reinterpret_cast<std::uint8_t**>(bytes + kControllerOffset);
            auto* vars = *reinterpret_cast<std::uint8_t**>(bytes + kScriptVarsOffset);
            if (!controller || !vars)
                return;
            if (*reinterpret_cast<std::uint32_t*>(controller + kLocalPlayerIndex) != playerIndex)
                return;
            *reinterpret_cast<std::uint16_t*>(vars + cell) = static_cast<std::uint16_t>(realId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    const std::uint16_t* LocalEquipStateRowSEH(void* self)
    {
        __try
        {
            auto* controller = *reinterpret_cast<std::uint8_t**>(
                static_cast<std::uint8_t*>(self) + kControllerOffset);
            if (!controller)
                return nullptr;
            auto* players = *reinterpret_cast<std::uint8_t**>(controller + kPlayersOffset);
            auto* rows = players ? *reinterpret_cast<std::uint8_t**>(players + kEquipStateRows) : nullptr;
            if (!rows)
                return nullptr;
            const std::uint32_t local = *reinterpret_cast<std::uint32_t*>(controller + kLocalPlayerIndex);
            return reinterpret_cast<const std::uint16_t*>(rows + static_cast<std::size_t>(local) * kEquipStateRowSize);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void __fastcall hkSetEquipState(void* self, std::uint32_t playerIndex, std::int32_t slotType,
                                    std::uint32_t slotIndex, std::uint32_t equipId,
                                    std::uint8_t level, bool isInitial, std::uint8_t itemExtra)
    {
        equip::NoteLocalEquipStateRow(LocalEquipStateRowSEH(self));
        const std::int32_t realId = equip::ResolveReal(static_cast<std::int32_t>(equipId));
        std::int32_t engineId = equip::ToEngine(realId);
        if (engineId == kNoEquipId && realId != kNoEquipId
            && (slotType == kSlotSupport || slotType == kSlotItem || slotType == kSlotHand))
            engineId = 0;
        g_Orig(self, playerIndex, slotType, slotIndex, static_cast<std::uint32_t>(engineId),
               level, isInitial, itemExtra);
        if (realId != engineId || realId != static_cast<std::int32_t>(equipId))
            KeepRealIdInVarsSEH(self, playerIndex, slotType, slotIndex, realId);
    }
}

bool Install_EquipControllerImpl_SetEquipState()
{
    const std::uintptr_t addr = SetEquipStateAddr();
    if (addr == 0)
        return true;

    void* fn = ResolveGameAddress(addr);
    if (!fn || !PrologueMatchesSEH(fn))
    {
        LogError("EquipAlias: SetEquipState prologue mismatch on %s, equipIds above 0x7FF stay off",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkSetEquipState),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("EquipAlias: SetEquipState hook failed on %s, equipIds above 0x7FF stay off",
            GetGameBuildName(gGameBuild));
        return false;
    }
    g_Addr = fn;
    equip::NoteGateInstalled(equip::AliasGate::SetEquipState, true);
    return true;
}

void Uninstall_EquipControllerImpl_SetEquipState()
{
    equip::NoteGateInstalled(equip::AliasGate::SetEquipState, false);
    if (g_Addr)
    {
        DisableAndRemoveHook(g_Addr);
        g_Addr = nullptr;
    }
    g_Orig = nullptr;
}
