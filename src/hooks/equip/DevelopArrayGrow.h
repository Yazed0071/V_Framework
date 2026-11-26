#pragma once

#include <cstddef>
#include <cstdint>

namespace equip
{
    void PreApplyDevelopArrayGrowPatches();
    bool Install_DevelopArrayGrow();
    void Uninstall_DevelopArrayGrow();

    bool DevelopArrayGrowActive();

    bool OwnsDevelopVisibilityHook(const void* target);

    std::uint32_t NativeFlowBound();

    constexpr std::uint16_t kFlowSentinel      = 0x400;
    constexpr std::uint16_t kGunsmithFlowFirst = 0x3FD;
    constexpr std::uint16_t kGunsmithFlowLast  = 0x3FF;
    constexpr std::uint32_t kMaxFlowSlots      = 65535;

    bool GunsmithFlowRowClaimed(std::uint32_t row);

    inline bool IsReservedFlowRow(std::uint32_t v)
    {
        if (v == kFlowSentinel)
            return true;
        if (v < kGunsmithFlowFirst || v > kGunsmithFlowLast)
            return false;
        return GunsmithFlowRowClaimed(v);
    }

    void SetCustomBluePrintRow(std::uint32_t row, std::uint16_t developId);

    std::size_t DevFlagsPtrOffsetBase20();

    void SilenceDevelopRowAnnounce(std::int32_t developId);

    void AssertDevelopRowAnnounced(std::int32_t developId,
                                   std::int32_t flowIndex);

    void InvalidateDevelopVisibilityCache();
    constexpr std::uint8_t kSortiePrepType13Rows     = 0x01; // arm list
    constexpr std::uint8_t kSortiePrepType14Rows     = 0x02; // suit list
    constexpr std::uint8_t kSortiePrepHeadOptionRows = 0x04;
    constexpr std::uint8_t kSortiePrepItemRows       = 0x08;
    constexpr std::uint8_t kSortiePrepType17Rows     = 0x10; // Quiet's costume and weapon lists
    constexpr std::uint8_t kSortiePrepType18Rows     = 0x20;
    constexpr std::uint8_t kSortiePrepType19Rows     = 0x40;

    void ArmSortiePrepDevelopList(std::uint8_t openTabRows);
    void DisarmSortiePrepDevelopList();
    void InvalidateDevelopLookupIndex();
    void BeginDevelopVisibilityCache();
    void EndDevelopVisibilityCache();

    struct DevelopVisibilityScope
    {
        DevelopVisibilityScope();
        ~DevelopVisibilityScope();
        bool owned;
    };

    inline bool IsValidFlowIndex(std::uint32_t v)
    {
        return v < NativeFlowBound() && !IsReservedFlowRow(v);
    }

    void DevFlagsWriteByte(void* controller, std::uint32_t index,
                           std::uint8_t value);

    bool DevFlagsTryReadByte(void* controller, std::uint32_t index,
                             std::uint8_t& out);

    std::uint32_t FirstCustomFlowIndex();
    std::uint32_t TrueVanillaRowCount();
    bool IsDevelopLayoutSettled();
    void SettleDevelopLayout(std::uint32_t firstCustomRow, std::uint32_t clearLoadedRowsBegin,
                             std::uint32_t clearLoadedRowsEnd);
    void PersistDevelopLayoutAfterSaveLoad(std::int32_t firstCustomRow, std::int32_t nativeRowsHash);
    void UseSaveSlotHooksForDevelopLayout();
    void ClearDevelopLayoutRowsNow();

    bool TryReadRowDevelopId(std::uint32_t index, std::uint32_t& out);

    struct DevelopRowInfo
    {
        std::uint16_t developId       = 0;
        std::uint16_t parentDevelopId = 0;
        std::uint16_t equipId         = 0;
        std::uint8_t  grade           = 0;
        std::uint8_t  developType     = 0;
        bool          listed          = false;   // developed, and its display rule shows it in the lists
    };

    bool TryReadDevelopRow(std::uint32_t index, DevelopRowInfo& out);

    void EnsureDevelopBlockArmed(void* base20);

    constexpr std::size_t kVanillaEquipIdBitWords = 0x10000 / 64;

    std::size_t ScanVanillaDevelopEquipIds(std::uint64_t* bits, std::size_t words);

    constexpr std::size_t kDevelopIdBitWords = 0x10000 / 64;

    bool ScanCustomOnlyDevelopIds(std::uint64_t* bits, std::size_t words);

    constexpr std::size_t kDevelopTypeSlots = 0x100;

    bool CountDevelopedRowsByType(std::uint32_t* vanillaCounts, std::uint32_t* customCounts,
                                  std::size_t slots);

    void SyncDevelopFlagsWithSave();

}
