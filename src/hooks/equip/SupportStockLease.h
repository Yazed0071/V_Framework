#pragma once

#include <cstdint>

namespace SupportStockLease
{
    bool Install();
    void Uninstall();
    bool Live();
    void Pump();

    bool          IsReservedStockByte(int value);
    std::int32_t  EquipOfLentByte(std::uint8_t stock);

    std::uint32_t __fastcall StockOf(std::uint32_t equipId);
}
