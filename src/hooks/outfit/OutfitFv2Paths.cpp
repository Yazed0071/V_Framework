#include "pch.h"

#include "OutfitFv2Paths.h"
#include "OutfitRegistry.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using FoxPath_Path_t = void* (__fastcall*)(void* outPath, std::uint64_t code64ext);

    using LoadPlayerCamoFv2_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerCamoType);

    using LoadPlayerSnakeBlackDiamondFv2_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t applyBlackDiamond);

    static FoxPath_Path_t                   g_FoxPath_Path        = nullptr;
    static LoadPlayerCamoFv2_t              g_OrigCamoFv2         = nullptr;
    static LoadPlayerSnakeBlackDiamondFv2_t g_OrigDiamondFv2      = nullptr;

    static bool g_InstalledCamo    = false;
    static bool g_InstalledDiamond = false;

    static bool ResolveFoxPathApi()
    {
        if (!g_FoxPath_Path)
        {
            g_FoxPath_Path = reinterpret_cast<FoxPath_Path_t>(
                ResolveGameAddress(gAddr.FoxPath_Path));
        }
        return g_FoxPath_Path != nullptr;
    }

    static std::uint64_t* WriteFoxPath(std::uint64_t* out, std::uint64_t code64ext)
    {
        if (!out || !ResolveFoxPathApi()) return out;
        g_FoxPath_Path(out, code64ext);
        return out;
    }


    static const outfit::OutfitEntry* FindCustomEntry(
        std::uint32_t playerType, std::uint32_t playerPartsType)
    {
        const auto pt  = static_cast<std::uint8_t>(playerPartsType & 0xFF);
        const auto ply = static_cast<std::uint8_t>(playerType & 0xFF);

        if (pt < outfit::kCustomPartsTypeStart || pt > outfit::kCustomPartsTypeEnd)
            return nullptr;

        const outfit::OutfitEntry* entry = nullptr;
        if (!outfit::TryGetOutfitByPartsType(pt, &entry) || !entry) return nullptr;
        // Snake↔Avatar bridging: outfit registered for one applies on the other.
        if (!outfit::IsPlayerTypeCompatible(entry->playerType, ply)) return nullptr;
        return entry;
    }

    static std::uint64_t* __fastcall hkLoadPlayerCamoFv2(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerCamoType)
    {
        const auto* entry = FindCustomEntry(playerType, playerPartsType);
        if (entry)
        {

            if (entry->IsCamoFv2Custom())
                return WriteFoxPath(outPath, entry->camoFv2);


            if (entry->camoFv2 == outfit::kSubAssetDisabled)
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
        }
        return g_OrigCamoFv2(outPath, playerType, playerPartsType, playerCamoType);
    }

    static std::uint64_t* __fastcall hkLoadPlayerSnakeBlackDiamondFv2(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t applyBlackDiamond)
    {
        const auto* entry = FindCustomEntry(playerType, playerPartsType);
        if (entry)
        {
            if (entry->IsDiamondFv2Custom())
                return WriteFoxPath(outPath, entry->diamondFv2);
            if (entry->diamondFv2 == outfit::kSubAssetDisabled)
                return WriteFoxPath(outPath, outfit::kSubAssetDisabled);
        }
        return g_OrigDiamondFv2(outPath, playerType, playerPartsType, applyBlackDiamond);
    }
}

namespace outfit
{
    bool Install_OutfitFv2Paths_Hooks()
    {
        ResolveFoxPathApi();

        void* tCamoFv2    = ResolveGameAddress(gAddr.LoadPlayerCamoFv2);
        void* tDiamondFv2 = ResolveGameAddress(gAddr.LoadPlayerSnakeBlackDiamondFv2);

        if (tCamoFv2)
            g_InstalledCamo = CreateAndEnableHook(
                tCamoFv2,
                reinterpret_cast<void*>(&hkLoadPlayerCamoFv2),
                reinterpret_cast<void**>(&g_OrigCamoFv2));

        if (tDiamondFv2)
            g_InstalledDiamond = CreateAndEnableHook(
                tDiamondFv2,
                reinterpret_cast<void*>(&hkLoadPlayerSnakeBlackDiamondFv2),
                reinterpret_cast<void**>(&g_OrigDiamondFv2));

        Log("[OutfitFv2Paths] installed: camoFv2=%s diamondFv2=%s\n",
            g_InstalledCamo    ? "OK" : "skip",
            g_InstalledDiamond ? "OK" : "skip");

        return g_InstalledCamo && g_InstalledDiamond;
    }

    void Uninstall_OutfitFv2Paths_Hooks()
    {
        if (g_InstalledCamo)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.LoadPlayerCamoFv2));
        if (g_InstalledDiamond)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.LoadPlayerSnakeBlackDiamondFv2));

        g_OrigCamoFv2         = nullptr;
        g_OrigDiamondFv2      = nullptr;
        g_FoxPath_Path        = nullptr;
        g_InstalledCamo       = false;
        g_InstalledDiamond    = false;

        Log("[OutfitFv2Paths] removed\n");
    }
}
