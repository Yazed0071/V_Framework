#pragma once

#include <cstdint>

namespace SupportStockLease
{
    bool Install();
    void Uninstall();
    bool Live();
    void Pump();

    bool          IsReservedStockByte(int value);
    bool          IsStockIdHeld(int stock);   // the save's reserve ammo or a pending supply drop uses this stock id
    std::int32_t  EquipOfLentByte(std::uint8_t stock);

    std::uint32_t __fastcall StockOf(std::uint32_t equipId);
}
