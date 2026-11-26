#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>

namespace mbTopMenuCustomItems
{
    constexpr std::uint8_t kFirstItemId = 0x50;
    constexpr std::uint8_t kLastItemId  = 0xFE;

    struct ItemSpec
    {
        std::uint8_t  parent = 0;
        std::string   name;                  // the identity of the item and the source of its trigger
        std::string   label;                 // text shown on the button
        std::string   description;
        std::uint64_t labelLang = 0;         // optional lang key, used instead of the label when the game knows it
        std::uint64_t descriptionLang = 0;
        bool          group = false;
        std::uint8_t  icon = 0;              // the vanilla entry whose icon this item shows, 0 = its group's icon
        std::uint64_t iconTexture = 0;       // or an .ftex path id: the row shows the cell at iconU/iconV, cut like the game's 512x256 menu sheet
        float         iconU = 0.0f;
        float         iconV = 0.0f;
    };

    bool VanillaEntryByMenuName(const char* menuName, std::uint8_t& id);   // a TppTerminal.MBDVCMENU string -> its entry

    bool Install(HMODULE hGame);
    void Uninstall();

    void NoteTable(void* table);   // a table object seen elsewhere: gives it the cloned vtable

    bool IsReady();
    bool ItemsAllowed();           // false in FOB and in the bypassed missions

    std::uint8_t AddItem(const ItemSpec& spec);   // the item id, 0 when refused
    bool RemoveItem(std::uint8_t id);
    void ClearAll();

    bool FindItemByTrigger(std::uint64_t trigger, std::uint8_t& id);
}
