#include "pch.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "CustomSuitRegistry.h"
#include "PlayerPartsPathHook.h"

namespace
{
    using LoadPlayerPartsParts_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath,
        std::uint32_t playerType,
        std::uint32_t playerPartsType
        );

    using LoadPlayerPartsFpk_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath,
        std::uint32_t playerType,
        std::uint32_t playerPartsType
        );

    using FoxPath_Path_t = void* (__fastcall*)(
        void* outPath,
        std::uint64_t pathCode64Ext
        );

    using LoadPartsNew_t = void(__fastcall*)(
        void* self,
        std::uint32_t playerIndex,
        LoadPartsPlayerInfo* playerInfo,
        std::uint32_t flags
        );

    using LoadPlayerCamoFpk_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath,
        std::uint32_t playerType,
        std::uint32_t playerPartsType,
        std::uint32_t playerCamoType
        );

    using LoadPlayerSnakeBlackDiamondFpk_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath,
        std::uint32_t playerType,
        std::uint32_t playerPartsType,
        std::uint32_t boolApplyBlackDiamond
        );

    static LoadPlayerPartsParts_t g_OrigLoadPlayerPartsParts = nullptr;
    static LoadPlayerPartsFpk_t g_OrigLoadPlayerPartsFpk = nullptr;
    static FoxPath_Path_t g_FoxPath_Path = nullptr;
    static LoadPartsNew_t g_OrigLoadPartsNew = nullptr;
    static LoadPlayerCamoFpk_t g_OrigLoadPlayerCamoFpk = nullptr;
    static LoadPlayerSnakeBlackDiamondFpk_t g_OrigLoadPlayerSnakeBlackDiamondFpk = nullptr;
    static bool g_Installed = false;

    static constexpr std::uint8_t kCustomPartsTypeMin = 0x40;
    // 0x00..0x19 = NORMAL..SWIMWEAR_H (see mgsvtpp.exe.c:1430322).
    // 0x1A/0x1B exist as avatar-female specials but are never handed to
    // LoadPartsNew through the normal suit flow.
    static constexpr std::uint32_t kMaxVanillaPartsType = 0x19;

    namespace
    {
        using GetQuarkSystemTable_t = void* (__fastcall*)();

        static GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;

        static bool ResolveQuarkApi()
        {
            if (!g_GetQuarkSystemTable)
            {
                g_GetQuarkSystemTable =
                    reinterpret_cast<GetQuarkSystemTable_t>(
                        ResolveGameAddress(gAddr.GetQuarkSystemTable)
                        );
            }

            return g_GetQuarkSystemTable != nullptr;
        }

        struct QuarkStoredAppearance
        {
            bool valid = false;
            std::uint8_t armType = 0;
            std::uint8_t faceEquipId = 0;
            std::uint8_t faceEquipUnk = 0;
            std::uint16_t headOption = 0;
        };

        static bool TryReadQuarkStoredAppearance(
            std::uint8_t playerType,
            QuarkStoredAppearance& out)
        {
            out = {};

            if (!ResolveQuarkApi())
                return false;

            auto* quarkSystemTable =
                reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
            if (!quarkSystemTable)
                return false;

            auto* q98 = *reinterpret_cast<std::uint8_t**>(quarkSystemTable + 0x98);
            if (!q98)
                return false;

            auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
            if (!state)
                return false;

            if (playerType == 0 || playerType == 3)
            {
                out.armType = state[0x1994];
                out.faceEquipId = state[0x1995];
                out.faceEquipUnk = state[0x1998];
                out.headOption = *reinterpret_cast<std::uint16_t*>(state + 0x1996);
            }
            else if (playerType == 1 || playerType == 2)
            {
                out.armType = state[0x1999];
                out.faceEquipId = state[0x199A];
                out.faceEquipUnk = state[0x199E];
                out.headOption = *reinterpret_cast<std::uint16_t*>(state + 0x199C);
            }
            else
            {
                return false;
            }

            out.valid =
                (out.armType != 0) ||
                (out.faceEquipId != 0) ||
                (out.faceEquipUnk != 0) ||
                (out.headOption != 0 && out.headOption != 0xFFFF);

            return out.valid;
        }
    }
    static bool ResolveFoxPathApi()
    {
        if (!g_FoxPath_Path)
        {
            g_FoxPath_Path = reinterpret_cast<FoxPath_Path_t>(
                ResolveGameAddress(gAddr.FoxPath_Path)
                );
        }

        return g_FoxPath_Path != nullptr;
    }

    static std::uint64_t* WriteFoxPath(std::uint64_t* outPath, std::uint64_t pathCode64Ext)
    {
        if (!outPath || pathCode64Ext == 0)
            return outPath;

        if (!ResolveFoxPathApi())
            return outPath;

        g_FoxPath_Path(outPath, pathCode64Ext);
        return outPath;
    }
}

static std::uint64_t* __fastcall hkLoadPlayerCamoFpk(
    std::uint64_t* outPath,
    std::uint32_t playerType,
    std::uint32_t playerPartsType,
    std::uint32_t playerCamoType)
{
    const std::uint8_t effectivePartsType =
        static_cast<std::uint8_t>(playerPartsType & 0xFF);

    const CustomSuitEntry* entry = nullptr;
    if (TryGetCustomSuitByPartsType(effectivePartsType, &entry) &&
        entry && entry->playerType == static_cast<std::uint8_t>(playerType))
    {
        if (entry->IsCamoCustom())
            return WriteFoxPath(outPath, entry->camoFpk);

        // Custom suit with no camo override: return empty path (null hash)
        // to avoid the game indexing camo arrays with an unknown partsType.
        if (ResolveFoxPathApi())
            g_FoxPath_Path(outPath, 0);
        return outPath;
    }

    return g_OrigLoadPlayerCamoFpk(outPath, playerType, playerPartsType, playerCamoType);
}

static std::uint64_t* __fastcall hkLoadPlayerSnakeBlackDiamondFpk(
    std::uint64_t* outPath,
    std::uint32_t playerType,
    std::uint32_t playerPartsType,
    std::uint32_t boolApplyBlackDiamond)
{
    const std::uint8_t effectivePartsType =
        static_cast<std::uint8_t>(playerPartsType & 0xFF);

    if (effectivePartsType >= kCustomPartsTypeMin)
    {
        const CustomSuitEntry* entry = nullptr;
        if (TryGetCustomSuitByPartsType(effectivePartsType, &entry) &&
            entry && entry->playerType == static_cast<std::uint8_t>(playerType) &&
            !entry->IsDiamondEnabled())
        {
            // Custom suit does not want black diamond: return empty path.
            if (ResolveFoxPathApi())
                g_FoxPath_Path(outPath, 0);
            return outPath;
        }
    }

    return g_OrigLoadPlayerSnakeBlackDiamondFpk(outPath, playerType, playerPartsType, boolApplyBlackDiamond);
}

static std::uint64_t* __fastcall hkLoadPlayerPartsParts(
    std::uint64_t* outPath,
    std::uint32_t playerType,
    std::uint32_t playerPartsType)
{
    // Check if hkLoadPartsNew patched partsType to NORMAL and the real
    // custom partsType is communicated via the thread-local.
    const std::uint8_t effectivePartsType =
        static_cast<std::uint8_t>(playerPartsType & 0xFF);

    const CustomSuitEntry* entry = nullptr;
    if (TryGetCustomSuitByPartsType(effectivePartsType, &entry) &&
        entry && entry->partsPathCode64Ext != 0 &&
        entry->playerType == static_cast<std::uint8_t>(playerType))
    {
        return WriteFoxPath(outPath, entry->partsPathCode64Ext);
    }

    return g_OrigLoadPlayerPartsParts(outPath, playerType, playerPartsType);
}

static std::uint64_t* __fastcall hkLoadPlayerPartsFpk(
    std::uint64_t* outPath,
    std::uint32_t playerType,
    std::uint32_t playerPartsType)
{
    const std::uint8_t effectivePartsType =
        static_cast<std::uint8_t>(playerPartsType & 0xFF);

    const CustomSuitEntry* entry = nullptr;
    if (TryGetCustomSuitByPartsType(effectivePartsType, &entry) &&
        entry && entry->fpkPathCode64Ext != 0 &&
        entry->playerType == static_cast<std::uint8_t>(playerType))
    {
        return WriteFoxPath(outPath, entry->fpkPathCode64Ext);
    }

    return g_OrigLoadPlayerPartsFpk(outPath, playerType, playerPartsType);
}

static void __fastcall hkLoadPartsNew(
    void* self,
    std::uint32_t playerIndex,
    LoadPartsPlayerInfo* playerInfo,
    std::uint32_t flags)
{
    const CustomSuitEntry* entry = nullptr;
    const bool isCustom = playerInfo &&
        TryGetCustomSuitByPartsType(playerInfo->playerPartsType, &entry) && entry &&
        entry->playerType == static_cast<std::uint8_t>(playerInfo->playerType);

    if (playerInfo)
    {
        // Safety: if partsType is in the custom range (≥0x40) but doesn't belong
        // to the current playerType, rewrite to vanilla OLIVE_DRAB (0x00) to
        // prevent the game from getting stuck trying to resolve an unknown partsType.
        // Also clear Quark state +0xF8 to prevent the dirty-check loop.
        if (!isCustom && playerInfo->playerPartsType >= 0x40 && playerInfo->playerPartsType <= 0x7F)
        {
            playerInfo->playerPartsType = 0x00; // OLIVE_DRAB — safe fallback
            playerInfo->playerCamoType = 0x00;

            // Clear the Quark live state so the game's dirty-check doesn't
            // see a mismatch and endlessly re-request loading.
            if (ResolveQuarkApi())
            {
                auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
                if (qt)
                {
                    auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
                    if (q98)
                    {
                        auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
                        if (state && state[0xF8] >= 0x40 && state[0xF8] <= 0x7F)
                        {
                            state[0xF8] = 0x00; // partsType
                            state[0xF9] = 0x00; // camoType/selector
                        }
                    }
                }
            }
        }

        if (!isCustom)
        {
            RememberPreservedLoadPartsAppearance(
                playerInfo->playerType,
                playerInfo->playerArmType,
                playerInfo->playerFaceEquipId,
                playerInfo->playerFaceEquipUnk
            );
        }
        else
        {
            PreservedAppearanceState preserved{};
            const bool havePreserved =
                TryGetPreservedAppearance(entry->playerType, preserved);

            QuarkStoredAppearance quark{};
            const bool haveQuark =
                TryReadQuarkStoredAppearance(entry->playerType, quark);

            const std::uint8_t originalCamo = playerInfo->playerCamoType;

            playerInfo->playerType = entry->playerType;
            playerInfo->playerPartsType = entry->customPartsType;

            // camo
            playerInfo->playerCamoType = entry->IsCamoEnabled() ? originalCamo : 0;

            // Validate Quark values — reject obvious garbage from custom suit state corruption.
            // Valid armType: 0-3. Valid faceEquipId: 0-5.
            const bool quarkArmValid = haveQuark && quark.armType <= 10;
            const bool quarkFaceValid = haveQuark && quark.faceEquipId <= 10;

            // hand: prefer valid Quark first, preserved second
            if (!entry->IsArmEnabled())
            {
                playerInfo->playerArmType = 0;
            }
            else
            {
                if (quarkArmValid && quark.armType != 0)
                {
                    playerInfo->playerArmType = quark.armType;
                }
                else if (playerInfo->playerArmType == 0 &&
                    havePreserved &&
                    preserved.armType != 0 && preserved.armType <= 10)
                {
                    playerInfo->playerArmType = preserved.armType;
                }
            }

            // head: for custom suits, default to NONE (bare face with hair).
            // The HEAD OPTION menu will let the player pick balaclava/headgear.
            // Without the menu, inheriting random headgear breaks the face.
            playerInfo->playerFaceEquipId = 0;
            playerInfo->playerFaceEquipUnk =
                static_cast<std::uint8_t>(playerInfo->playerFaceEquipUnk & 0xF8);

        }
    }

    // Pass the real custom partsType to the original LoadPartsNew so the
    // game's dirty-check sees it as a genuine change. The sub-hooks
    // (LoadPlayerPartsParts/Fpk, LoadPlayerCamoFpk, BlackDiamond) handle
    // their own table lookups safely. Functions that index by partsType
    // (DoesNeedFaceFova, DoesNeedBodyFovaForDD, etc.) are either already
    // hooked or degrade gracefully for custom partsTypes >= 0x40.
    g_OrigLoadPartsNew(self, playerIndex, playerInfo, flags);
}

bool Install_PlayerPartsPath_Hook()
{
    if (g_Installed)
    {
        Log("[Hook] PlayerPartsPath: already installed\n");
        return true;
    }

    if (!ResolveFoxPathApi())
    {
        Log("[Hook] PlayerPartsPath: failed to resolve fox::Path::Path\n");
        return false;
    }

    bool okParts = false;
    bool okFpk = false;
    bool okLoadPartsNew = false;
    bool okCamoFpk = false;
    bool okBlackDiamond = false;

    if (void* target = ResolveGameAddress(gAddr.LoadPlayerPartsParts))
    {
        okParts = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkLoadPlayerPartsParts),
            reinterpret_cast<void**>(&g_OrigLoadPlayerPartsParts)
        );
    }

    if (void* target = ResolveGameAddress(gAddr.LoadPlayerPartsFpk))
    {
        okFpk = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkLoadPlayerPartsFpk),
            reinterpret_cast<void**>(&g_OrigLoadPlayerPartsFpk)
        );
    }

    if (void* target = ResolveGameAddress(gAddr.Player2BlockController_LoadPartsNew))
    {
        okLoadPartsNew = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkLoadPartsNew),
            reinterpret_cast<void**>(&g_OrigLoadPartsNew)
        );
    }

    if (gAddr.LoadPlayerCamoFpk != 0)
    {
        if (void* target = ResolveGameAddress(gAddr.LoadPlayerCamoFpk))
        {
            okCamoFpk = CreateAndEnableHook(
                target,
                reinterpret_cast<void*>(&hkLoadPlayerCamoFpk),
                reinterpret_cast<void**>(&g_OrigLoadPlayerCamoFpk)
            );
        }
    }

    if (gAddr.LoadPlayerSnakeBlackDiamondFpk != 0)
    {
        if (void* target = ResolveGameAddress(gAddr.LoadPlayerSnakeBlackDiamondFpk))
        {
            okBlackDiamond = CreateAndEnableHook(
                target,
                reinterpret_cast<void*>(&hkLoadPlayerSnakeBlackDiamondFpk),
                reinterpret_cast<void**>(&g_OrigLoadPlayerSnakeBlackDiamondFpk)
            );
        }
    }

    Log("[Hook] PlayerPartsPath PARTS:        %s\n", okParts ? "OK" : "FAIL");
    Log("[Hook] PlayerPartsPath FPK:          %s\n", okFpk ? "OK" : "FAIL");
    Log("[Hook] PlayerPartsPath LoadPartsNew: %s\n", okLoadPartsNew ? "OK" : "FAIL");
    Log("[Hook] PlayerPartsPath CamoFpk:      %s\n", okCamoFpk ? "OK" : "SKIP");
    Log("[Hook] PlayerPartsPath BlackDiamond:  %s\n", okBlackDiamond ? "OK" : "SKIP");

    g_Installed = okParts && okFpk && okLoadPartsNew;
    return g_Installed;
}

bool Uninstall_PlayerPartsPath_Hook()
{
    if (!g_Installed)
        return true;

    if (void* target = ResolveGameAddress(gAddr.LoadPlayerPartsParts))
        DisableAndRemoveHook(target);

    if (void* target = ResolveGameAddress(gAddr.LoadPlayerPartsFpk))
        DisableAndRemoveHook(target);

    if (void* target = ResolveGameAddress(gAddr.Player2BlockController_LoadPartsNew))
        DisableAndRemoveHook(target);

    if (g_OrigLoadPlayerCamoFpk)
    {
        if (void* target = ResolveGameAddress(gAddr.LoadPlayerCamoFpk))
            DisableAndRemoveHook(target);
    }

    if (g_OrigLoadPlayerSnakeBlackDiamondFpk)
    {
        if (void* target = ResolveGameAddress(gAddr.LoadPlayerSnakeBlackDiamondFpk))
            DisableAndRemoveHook(target);
    }

    g_OrigLoadPlayerPartsParts = nullptr;
    g_OrigLoadPlayerPartsFpk = nullptr;
    g_OrigLoadPartsNew = nullptr;
    g_OrigLoadPlayerCamoFpk = nullptr;
    g_OrigLoadPlayerSnakeBlackDiamondFpk = nullptr;
    g_FoxPath_Path = nullptr;
    g_Installed = false;

    Log("[Hook] PlayerPartsPath: removed\n");
    return true;
}