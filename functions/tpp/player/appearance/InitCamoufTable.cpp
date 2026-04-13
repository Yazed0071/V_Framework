#include "pch.h"
#include <array>
#include <cstdint>
#include <cstring>

#include "log.h"
#include "InitCamoufTable.h"

namespace
{
    // From player2_camouf_param.lua:
    static constexpr std::size_t kMaxCamoTypes = 117;
    static constexpr std::size_t kMaxMaterialTypes = 82;

    using CamoRow = std::array<std::int32_t, kMaxMaterialTypes>;
    using CamoTable = std::array<CamoRow, kMaxCamoTypes>;

    static CamoTable g_CustomCamoTable{};
    static bool g_HaveCustomCamoTable = false;

    static bool IsValidCamoType(std::uint32_t camoType)
    {
        return camoType < kMaxCamoTypes;
    }

    static bool IsValidMaterialType(std::uint32_t materialType)
    {
        return materialType < kMaxMaterialTypes;
    }

    // Call this once from your camo init hook before editing if you want vanilla as a base.
    void InitCustomCamoTableFromGame(const void* gameTable)
    {
        if (!gameTable || g_HaveCustomCamoTable)
            return;

        std::memcpy(g_CustomCamoTable.data(), gameTable, sizeof(g_CustomCamoTable));
        g_HaveCustomCamoTable = true;
    }
}

bool Clone_CamoRow(std::uint32_t dstCamoType, std::uint32_t srcCamoType)
{
    if (!IsValidCamoType(dstCamoType) || !IsValidCamoType(srcCamoType))
        return false;

    g_CustomCamoTable[dstCamoType] = g_CustomCamoTable[srcCamoType];
    g_HaveCustomCamoTable = true;

    Log("[PlayerCamoufTable] Clone_CamoRow: dst=%u src=%u\n", dstCamoType, srcCamoType);
    return true;
}

bool Set_CamoValue(std::uint32_t camoType, std::uint32_t materialType, std::int32_t value)
{
    if (!IsValidCamoType(camoType) || !IsValidMaterialType(materialType))
        return false;

    g_CustomCamoTable[camoType][materialType] = value;
    g_HaveCustomCamoTable = true;

    Log("[PlayerCamoufTable] Set_CamoValue: camo=%u material=%u value=%d\n",
        camoType, materialType, value);
    return true;
}