#pragma once

#include <cstdint>

namespace outfit
{


    bool Install_OutfitListInject_Hook();
    void Uninstall_OutfitListInject_Hook();

    void SetHeadEquipDecideActive(bool active);

    std::uint8_t VextLookupCellSelector(std::uint16_t flowIndex,
                                        std::uint8_t cellPos);

    struct OutfitEntry;

    struct SortieListCursor
    {
        const OutfitEntry* entry = nullptr;   // null on a row that is not a custom outfit
        std::uint8_t variant = 0;             // the variant of the cell under the cursor
        bool worn = false;                    // that outfit and variant are the ones worn now
    };
    bool GetSortieListCursor(SortieListCursor& out);

    bool DecideSortieListCursor();

    bool RequestSortieListEquip();
    int TakeSortieListEquipResult();   // 1 when it equipped, -1 when it got dropped, 0 nothing new yet
    void CancelSortieListEquip();

    struct CustomHeadEntry;

    struct SortieHeadCursor
    {
        const CustomHeadEntry* head = nullptr;
        bool worn = false;
    };
    bool GetSortieHeadCursor(SortieHeadCursor& out);
    bool RequestSortieHeadEquip();

    void ForgetSortieListPanels();
}
