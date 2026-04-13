#include "pch.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "tpp/player/appearance/CustomSuitRegistry.h"
#include "tpp/player/appearance/PlayerPartsPathHook.h"

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

    static LoadPlayerPartsParts_t g_OrigLoadPlayerPartsParts = nullptr;
    static LoadPlayerPartsFpk_t g_OrigLoadPlayerPartsFpk = nullptr;
    static FoxPath_Path_t g_FoxPath_Path = nullptr;
    static LoadPartsNew_t g_OrigLoadPartsNew = nullptr;
    static bool g_Installed = false;
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

static std::uint64_t* __fastcall hkLoadPlayerPartsParts(
    std::uint64_t* outPath,
    std::uint32_t playerType,
    std::uint32_t playerPartsType)
{
    const CustomSuitEntry* entry = nullptr;
    if (TryGetCustomSuitByPartsType(static_cast<std::uint8_t>(playerPartsType & 0xFF), &entry) &&
        entry && entry->partsPathCode64Ext != 0)
    {
        Log(
            "[PlayerPartsPath] PARTS custom partsType=%u playerType=%u\n",
            static_cast<unsigned>(entry->customPartsType),
            static_cast<unsigned>(playerType)
        );

        return WriteFoxPath(outPath, entry->partsPathCode64Ext);
    }

    return g_OrigLoadPlayerPartsParts(outPath, playerType, playerPartsType);
}

static std::uint64_t* __fastcall hkLoadPlayerPartsFpk(
    std::uint64_t* outPath,
    std::uint32_t playerType,
    std::uint32_t playerPartsType)
{
    const CustomSuitEntry* entry = nullptr;
    if (TryGetCustomSuitByPartsType(static_cast<std::uint8_t>(playerPartsType & 0xFF), &entry) &&
        entry && entry->fpkPathCode64Ext != 0)
    {
        Log(
            "[PlayerPartsPath] FPK custom partsType=%u playerType=%u\n",
            static_cast<unsigned>(entry->customPartsType),
            static_cast<unsigned>(playerType)
        );

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
    if (playerInfo)
    {
        const CustomSuitEntry* entry = nullptr;
        const bool isCustom =
            TryGetCustomSuitByPartsType(playerInfo->playerPartsType, &entry) && entry;

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
            playerInfo->playerCamoType = entry->enableCamo ? originalCamo : 0;

            // hand: prefer Quark first, preserved second
            if (!entry->enableHand)
            {
                playerInfo->playerArmType = 0;
            }
            else
            {
                if (haveQuark && quark.armType != 0)
                {
                    playerInfo->playerArmType = quark.armType;
                }
                else if (playerInfo->playerArmType == 0 &&
                    havePreserved &&
                    preserved.armType != 0)
                {
                    playerInfo->playerArmType = preserved.armType;
                }
            }

            // head: prefer Quark first, preserved second
            if (!entry->enableHead)
            {
                playerInfo->playerFaceEquipId = 0;
                playerInfo->playerFaceEquipUnk =
                    static_cast<std::uint8_t>(playerInfo->playerFaceEquipUnk & 0xF8);
            }
            else
            {
                if (haveQuark &&
                    (quark.faceEquipId != 0 || quark.faceEquipUnk != 0))
                {
                    playerInfo->playerFaceEquipId = quark.faceEquipId;
                    playerInfo->playerFaceEquipUnk = quark.faceEquipUnk;
                }
                else if (playerInfo->playerFaceEquipId == 0 &&
                    playerInfo->playerFaceEquipUnk == 0 &&
                    havePreserved &&
                    (preserved.faceEquipId != 0 || preserved.faceEquipUnk != 0))
                {
                    playerInfo->playerFaceEquipId = preserved.faceEquipId;
                    playerInfo->playerFaceEquipUnk = preserved.faceEquipUnk;
                }
            }

            Log(
                "[PlayerPartsPath] LoadPartsNew override idx=%u partsType=%u type=%u head=%u hand=%u camo=%u armType=%u faceEquip=%u unk=0x%02X quark{arm=%u face=%u unk=0x%02X head=0x%04X}\n",
                static_cast<unsigned>(playerIndex),
                static_cast<unsigned>(entry->customPartsType),
                static_cast<unsigned>(entry->playerType),
                entry->enableHead ? 1u : 0u,
                entry->enableHand ? 1u : 0u,
                entry->enableCamo ? 1u : 0u,
                static_cast<unsigned>(playerInfo->playerArmType),
                static_cast<unsigned>(playerInfo->playerFaceEquipId),
                static_cast<unsigned>(playerInfo->playerFaceEquipUnk),
                haveQuark ? static_cast<unsigned>(quark.armType) : 0u,
                haveQuark ? static_cast<unsigned>(quark.faceEquipId) : 0u,
                haveQuark ? static_cast<unsigned>(quark.faceEquipUnk) : 0u,
                haveQuark ? static_cast<unsigned>(quark.headOption) : 0u
            );
        }
    }

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

    Log("[Hook] PlayerPartsPath PARTS:        %s\n", okParts ? "OK" : "FAIL");
    Log("[Hook] PlayerPartsPath FPK:          %s\n", okFpk ? "OK" : "FAIL");
    Log("[Hook] PlayerPartsPath LoadPartsNew: %s\n", okLoadPartsNew ? "OK" : "FAIL");

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

    g_OrigLoadPlayerPartsParts = nullptr;
    g_OrigLoadPlayerPartsFpk = nullptr;
    g_OrigLoadPartsNew = nullptr;
    g_FoxPath_Path = nullptr;
    g_Installed = false;

    Log("[Hook] PlayerPartsPath: removed\n");
    return true;
}