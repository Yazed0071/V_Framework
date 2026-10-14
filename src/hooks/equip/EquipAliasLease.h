#pragma once

#include <cstddef>
#include <cstdint>

namespace equip
{
    enum class AliasGate : std::uint8_t
    {
        SetUpEquip,
        SetEquipState,
        CopyAndAdjustInfo,
        CreateInstanceFromVars,
        DoPickUpWithPickableInfo,
        Count,
    };

    std::int32_t ToEngine(std::int32_t equipId);
    std::int32_t FromEngine(std::int32_t equipId);
    std::int32_t ResolveReal(std::int32_t equipId);
    void         NoteEquipIdMoved(std::int32_t oldEquipId, std::int32_t newEquipId);
    std::int32_t CurrentAliasOf(std::int32_t wideEquipId);
    bool         IsLeasedAlias(std::int32_t equipId);
    std::size_t  LiveLeaseCount();

    void NoteLocalEquipStateRow(const std::uint16_t* row);
    void NoteEquipSystem(void* equipSystem);
    void NotePickableSystem(void* pickableSystem);
    void NoteAmmoAliasFor(std::int32_t gunEngineId, std::int32_t ammoAlias);
    void NoteGateInstalled(AliasGate gate, bool installed);
    bool WideEquipIdsReady();

    constexpr std::size_t kVarsDroppedWeapons     = 0x1BC2; // vars.droppedWeapons, one u16 per dropped primary/secondary
    constexpr int         kDroppedWeaponCellCount = 3;

    void KeepRealIdsInVarsCells(std::size_t varsOffset, int cellCount);
}
