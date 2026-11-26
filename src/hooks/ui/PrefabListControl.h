#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace PrefabListControl
{
    constexpr int kMaxVisibleRows = 0x20;   // the list layout holds UI_Prefab_record_1 to _32 at most
    constexpr int kMaxRows        = 1024;
    constexpr int kMaxColumns     = 4;
    constexpr int kMaxTitleColumns = 2;

    using RowText = std::vector<std::string>;   // one string per text node of the row

    struct SetupRequest
    {
        std::uint32_t window = 0;     // V_UixUtility window handle of the page that holds the list prefab
        std::uint64_t list = 0;       // name of the list child window in that page, 48-bit id
        int count = -1;               // -1 = rows.size()
        int visible = 0;              // row nodes the list layout holds
        int cursor = 1;               // 1-based absolute row
        int top = 0;                  // 1-based absolute first visible row, 0 = the list decides
        std::uint32_t flags = 0;      // 0 = open flags of the Missions list
        std::vector<RowText> rows;
        RowText title;
        std::uint64_t rowModel = 0;
        std::vector<std::uint64_t> rowTextNodes;
        std::uint64_t titleModel = 0;
        std::vector<std::uint64_t> titleTextNodes;
    };

    bool IsAvailable();
    std::uint32_t Setup(const SetupRequest& request);   // list id, 0 on failure
    bool SetRows(std::uint32_t list, std::vector<RowText> rows);
    bool GetCursor(std::uint32_t list, int& absolute, int& top);   // both 1-based
    bool Close(std::uint32_t list);
    void Sweep();   // frees the lists whose window is gone; every call above runs it first
}

bool Install_PrefabListControl();
bool Uninstall_PrefabListControl();
