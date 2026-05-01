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

    using LoadPlayerBionicArmFv2_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerHandType);

    using LoadPlayerSnakeFaceFv2_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerFaceId,
        std::uint8_t playerFaceEquipId);

    static FoxPath_Path_t                   g_FoxPath_Path        = nullptr;
    static LoadPlayerCamoFv2_t              g_OrigCamoFv2         = nullptr;
    static LoadPlayerSnakeBlackDiamondFv2_t g_OrigDiamondFv2      = nullptr;
    static LoadPlayerBionicArmFv2_t         g_OrigBionicArmFv2    = nullptr;
    static LoadPlayerSnakeFaceFv2_t         g_OrigSnakeFaceFv2    = nullptr;

    static bool g_InstalledCamo       = false;
    static bool g_InstalledDiamond    = false;
    static bool g_InstalledBionicArmFv2 = false;
    static bool g_InstalledSnakeFaceFv2 = false;

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
        if (entry->playerType != ply) return nullptr;
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


    // 2026-05-01: hkLoadPlayerBionicArmFv2 — substitute partsType for custom
    // Snake/Avatar outfits so orig returns valid Fv2 hash.
    //
    // ROOT CAUSE for Snake arm not rendering:
    //   Orig LoadPlayerBionicArmFv2 (mgsvtpp.exe.c:1430114) validates
    //   partsType against a switch — must be in {0,1,2,7-13,15-18,0x17-0x19}.
    //   For partsType=0x40 (custom), DEFAULT case fires → returns Path(0)
    //   = NULL hash. The orig pipeline (LoadPlayerFv2s at line 1309541+)
    //   then sees armFv2.Hash==0 and SKIPS filling param_2[0] (the arm
    //   render slot). Arm has no DataSetFile2* registered → invisible.
    //
    // Fix: if (PT, partsType) matches a registered custom outfit, substitute
    // partsType=0x00 (vanilla NORMAL — always in the valid switch). orig
    // returns valid arm Fv2 hash. Render slot fills correctly. Arm appears.
    //
    // The orig BionicArm switch only handles PT=0/3 (Snake/Avatar) — Female
    // PT=1/2 returns NULL anyway. Our substitution is safe for all PTs:
    //   - Snake/Avatar custom: substitute → orig returns valid hash.
    //   - Female custom: orig still returns NULL (PT check), substitution
    //     has no effect (and Female doesn't use bionic arm anyway).
    //   - Vanilla equip (any PT): partsType not in custom range, no
    //     substitution. orig behavior unchanged.
    static std::uint64_t* __fastcall hkLoadPlayerBionicArmFv2(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerHandType)
    {
        std::uint32_t effectivePartsType = playerPartsType;
        std::uint32_t effectiveHandType  = playerHandType;
        bool          substituted        = false;
        if (playerPartsType >= outfit::kCustomPartsTypeStart
            && playerPartsType <= outfit::kCustomPartsTypeEnd)
        {
            const auto* entry = FindCustomEntry(playerType, playerPartsType);
            if (entry)
            {
                effectivePartsType = 0x00;

                // Spoof handType to match what hkLoadPartsNew set on
                // info->playerArmType for inner LoadPartsNew. The orig pipeline
                // reads handType from a per-slot byte array at *(pIVar3+0x58)
                // which we don't modify; the orig passes that 0x00 here. For
                // arm to ACTUALLY render, the Fv2 must match what was loaded
                // into shell+0x08 by inner LoadPartsNew (which used the spoofed
                // info->playerArmType, not the per-slot array).
                //
                // Mirror hkLoadPartsNew's armType logic:
                //   - entry->armType != 0xFF: use registered armType
                //   - !IsArmEnabled():        handType=0 (disabled)
                //   - default:                handType=1 (bionic arm)
                if (entry->armType != 0xFF)
                {
                    effectiveHandType = entry->armType;
                }
                else if (!entry->IsArmEnabled())
                {
                    effectiveHandType = 0;
                }
                else if (playerHandType == 0)
                {
                    effectiveHandType = 1;
                }

                substituted = true;
            }
        }

        if (substituted)
        {
            static std::uint32_t s_lastPT          = 0xFFFFFFFFu;
            static std::uint32_t s_lastPartsType   = 0xFFFFFFFFu;
            static std::uint32_t s_lastHandType    = 0xFFFFFFFFu;
            static std::uint32_t s_lastEffective   = 0xFFFFFFFFu;
            if (s_lastPT != playerType
                || s_lastPartsType != playerPartsType
                || s_lastHandType != playerHandType
                || s_lastEffective != effectiveHandType)
            {
                Log("[OutfitFv2Paths] hkLoadPlayerBionicArmFv2: PT=%u "
                    "partsType=0x%02X handType=%u -> substituting "
                    "partsType=0x00 handType=%u so orig returns valid "
                    "arm Fv2 hash matching the FPK loaded at shell+0x08\n",
                    playerType,
                    static_cast<unsigned>(playerPartsType & 0xFF),
                    playerHandType,
                    effectiveHandType);
                s_lastPT = playerType;
                s_lastPartsType = playerPartsType;
                s_lastHandType = playerHandType;
                s_lastEffective = effectiveHandType;
            }
        }

        return g_OrigBionicArmFv2(outPath, playerType, effectivePartsType,
                                  effectiveHandType);
    }


    // 2026-05-01: hkLoadPlayerSnakeFaceFv2 — same fix for Snake face.
    //
    // Orig LoadPlayerSnakeFaceFv2 (mgsvtpp.exe.c:1429910) validates partsType
    // against a switch (PT=0 only). For partsType=0x40 → default case →
    // returns Path(0) = NULL → render slot for face Fv2 unfilled → no head.
    //
    // Fix: substitute partsType=0x00 for custom outfits. orig returns valid
    // hash for vanilla NORMAL face. Block has the file (loaded via face FPK
    // which we already substitute via thread-local spoof). Render slot fills.
    // Head appears.
    static std::uint64_t* __fastcall hkLoadPlayerSnakeFaceFv2(
        std::uint64_t* outPath, std::uint32_t playerType,
        std::uint32_t playerPartsType, std::uint32_t playerFaceId,
        std::uint8_t playerFaceEquipId)
    {
        std::uint32_t effectivePartsType = playerPartsType;
        bool          substituted        = false;
        if (playerPartsType >= outfit::kCustomPartsTypeStart
            && playerPartsType <= outfit::kCustomPartsTypeEnd)
        {
            const auto* entry = FindCustomEntry(playerType, playerPartsType);
            if (entry)
            {
                effectivePartsType = 0x00;
                substituted        = true;
            }
        }

        if (substituted)
        {
            static std::uint32_t s_lastPT = 0xFFFFFFFFu;
            static std::uint32_t s_lastPartsType = 0xFFFFFFFFu;
            static std::uint32_t s_lastFaceId = 0xFFFFFFFFu;
            if (s_lastPT != playerType
                || s_lastPartsType != playerPartsType
                || s_lastFaceId != playerFaceId)
            {
                Log("[OutfitFv2Paths] hkLoadPlayerSnakeFaceFv2: PT=%u "
                    "partsType=0x%02X faceId=%u faceEquipId=%u -> substituting "
                    "partsType to 0x00 so orig returns valid face Fv2 hash\n",
                    playerType,
                    static_cast<unsigned>(playerPartsType & 0xFF),
                    playerFaceId,
                    static_cast<unsigned>(playerFaceEquipId));
                s_lastPT = playerType;
                s_lastPartsType = playerPartsType;
                s_lastFaceId = playerFaceId;
            }
        }

        return g_OrigSnakeFaceFv2(outPath, playerType, effectivePartsType,
                                  playerFaceId, playerFaceEquipId);
    }
}

namespace outfit
{
    bool Install_OutfitFv2Paths_Hooks()
    {
        ResolveFoxPathApi();

        void* tCamoFv2     = ResolveGameAddress(gAddr.LoadPlayerCamoFv2);
        void* tDiamondFv2  = ResolveGameAddress(gAddr.LoadPlayerSnakeBlackDiamondFv2);
        void* tBionicArmFv2 = ResolveGameAddress(gAddr.LoadPlayerBionicArmFv2);
        void* tSnakeFaceFv2 = ResolveGameAddress(gAddr.LoadPlayerSnakeFaceFv2);

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

        if (tBionicArmFv2)
            g_InstalledBionicArmFv2 = CreateAndEnableHook(
                tBionicArmFv2,
                reinterpret_cast<void*>(&hkLoadPlayerBionicArmFv2),
                reinterpret_cast<void**>(&g_OrigBionicArmFv2));

        if (tSnakeFaceFv2)
            g_InstalledSnakeFaceFv2 = CreateAndEnableHook(
                tSnakeFaceFv2,
                reinterpret_cast<void*>(&hkLoadPlayerSnakeFaceFv2),
                reinterpret_cast<void**>(&g_OrigSnakeFaceFv2));

        Log("[OutfitFv2Paths] installed: camoFv2=%s diamondFv2=%s "
            "bionicArmFv2=%s snakeFaceFv2=%s\n",
            g_InstalledCamo         ? "OK" : "skip",
            g_InstalledDiamond      ? "OK" : "skip",
            g_InstalledBionicArmFv2 ? "OK" : "skip",
            g_InstalledSnakeFaceFv2 ? "OK" : "skip");

        return g_InstalledCamo && g_InstalledDiamond;
    }

    void Uninstall_OutfitFv2Paths_Hooks()
    {
        if (g_InstalledCamo)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.LoadPlayerCamoFv2));
        if (g_InstalledDiamond)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.LoadPlayerSnakeBlackDiamondFv2));
        if (g_InstalledBionicArmFv2)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.LoadPlayerBionicArmFv2));
        if (g_InstalledSnakeFaceFv2)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.LoadPlayerSnakeFaceFv2));

        g_OrigCamoFv2          = nullptr;
        g_OrigDiamondFv2       = nullptr;
        g_OrigBionicArmFv2     = nullptr;
        g_OrigSnakeFaceFv2     = nullptr;
        g_FoxPath_Path         = nullptr;
        g_InstalledCamo        = false;
        g_InstalledDiamond     = false;
        g_InstalledBionicArmFv2 = false;
        g_InstalledSnakeFaceFv2 = false;

        Log("[OutfitFv2Paths] removed\n");
    }
}
