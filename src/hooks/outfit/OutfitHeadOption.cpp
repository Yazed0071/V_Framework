#include "pch.h"

#include "OutfitHeadOption.h"
#include "OutfitRegistry.h"
#include "CustomHeadRegistry.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    // ---------------------------------------------------------------
    // MissionPreparationCallbackImpl::IsEnableCurrentHeadOption
    // (retail 0x14A56BA20). Functional gate for phase 0x1c head-option
    // submenu. Force-true when live partsType is a registered custom
    // outfit declaring HasHeadOptions(), else fall through.
    using IsEnableCurrentHeadOption_t = std::uint8_t (__fastcall*)(void* self);

    static IsEnableCurrentHeadOption_t g_OrigIsEnableHead = nullptr;
    static bool                        g_Installed        = false;

    // ---------------------------------------------------------------
    // MissionPreparationCallbackImpl::IsEnableCurrentSuit
    // (retail 0x14A56BFA0, mgsvtpp.exe.c:2966957).
    //
    // Companion gate to IsEnableCurrentHeadOption inside
    // DecideActTargetSelWindow. Short-circuited check — both must pass:
    //
    //   if (iVar6 == 0x1c) {
    //       cVar3 = vtable[0x4f0](self+0x48);     // submenu-active gate
    //       if ((cVar3 != '\0') &&
    //           ((cVar3 = IsEnableCurrentSuit(self), cVar3 == '\0' ||
    //            (bVar4 = IsEnableCurrentHeadOption(self), !bVar4)))) {
    //         return 0x16;                         // REJECT
    //       }
    //       ... return 0x1c;                       // proceed to submenu
    //   }
    //
    // If IsEnableCurrentSuit returns false, IsEnableCurrentHeadOption
    // is never called. We force-return 1 for registered custom outfits
    // with HasHeadOptions() so cursor 0xf/0x10/0x11 -> phase 0x1C reaches
    // the head-option submenu.
    using IsEnableCurrentSuit_t = std::uint8_t (__fastcall*)(void* self);
    static IsEnableCurrentSuit_t g_OrigIsEnableCurrentSuit       = nullptr;
    static bool                  g_IsEnableCurrentSuitInstalled  = false;

    // ---------------------------------------------------------------
    // MissionPreparationSystemImpl::IsEnableHeadOptionSuit
    // (retail 0x1460B9FA0, vtable slot 0x460 — JMP'd from named-build
    // 0x140957D30 via thunk 0x1409575D0).
    //
    // Upstream visibility filter for the HEAD OPTION row in SORTIE PREP.
    // Retail body has an inlined switch over a translated equipId,
    // accepting only vanilla suits in 0x4A60..0x4A8B. For our custom
    // outfits the orig returns 0 -> the HEAD OPTION row hides.
    //
    // GetSelectionNum (retail 0x1416BC2C0) calls this gate as
    //   uVar3 = vtable[0x1f8](self+0x48);   // current-suit "id"
    //   cVar2 = vtable[0x460](self+0x48, uVar3);
    // For our custom outfits, vtable[0x1f8] returns 0x400 (NONE
    // sentinel) because Lua sets equipID = TppEquip.EQP_SUIT — there's
    // no specific vanilla equipId, so per-flowIndex translation
    // collapses to 0x400. We key off live partsType instead of param2.
    using IsEnableHeadOptionSuit_t =
        std::uint8_t (__fastcall*)(void* self, std::uint16_t flowIndex);
    static IsEnableHeadOptionSuit_t g_OrigIsEnableHeadOptionSuit       = nullptr;
    static bool                     g_IsEnableHeadOptionSuitInstalled  = false;

    // ---------------------------------------------------------------
    // ConverFaceIdWithFaceEquipId (retail 0x14622A3B0).
    //
    // Translates 1-byte playerFaceEquipId slot → 2-byte face id
    // per playerType. Vanilla orig handles slots 3/4/5 returning the
    // appropriate dds_balaclava{0..5} index; everything else returns
    // the input faceId unchanged.
    //
    // For our custom-head slots (>= 0x06) we return the head's
    // registered `TppEnemyFaceId` (a real vanilla face id, NOT a
    // sentinel — sentinels >= 900 hang the asset loader). The visual
    // is whichever vanilla balaclava the modder picked.
    using ConverFaceId_t = std::uint16_t (__fastcall*)(
        std::uint8_t playerType, std::int16_t faceId,
        std::uint8_t playerFaceEquipId);
    static ConverFaceId_t g_OrigConverFaceId           = nullptr;
    static bool           g_ConverFaceIdInstalled      = false;

    static std::uint16_t __fastcall hkConverFaceIdWithFaceEquipId(
        std::uint8_t playerType, std::int16_t faceId,
        std::uint8_t playerFaceEquipId)
    {
        // Custom-head override: when the slot byte is one we own,
        // return the head's REGISTERED TppEnemyFaceId (e.g. 0x22D
        // for DDFemale BALACLAVA). NOT a sentinel — sentinel face
        // ids outside the FaceUnit table hang the orig's
        // load-completion bookkeeping (verified 2026-04-30).
        //
        // Effect: the head visually renders as the chosen vanilla
        // balaclava, but the click / iDroid label / icon stay
        // customized via our equipId↔name mapping and the modder's
        // AddToEquipDevelopTable row.
        if (outfit::IsCustomHeadSlot(playerFaceEquipId))
        {
            if (const auto* head =
                outfit::TryGetCustomHeadBySlot(playerFaceEquipId))
            {
                return head->TppEnemyFaceId;
            }
        }

        return g_OrigConverFaceId
            ? g_OrigConverFaceId(playerType, faceId, playerFaceEquipId)
            : faceId;
    }

    static std::uint8_t __fastcall hkIsEnableHeadOptionSuit(
        void* self, std::uint16_t param2)
    {
        const std::uint8_t pt = outfit::ReadLivePartsType();

        if (pt >= outfit::kCustomPartsTypeStart && pt <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry
                && entry->HasHeadOptions())
            {
                return 1;
            }
        }

        return g_OrigIsEnableHeadOptionSuit(self, param2);
    }

    static std::uint8_t __fastcall hkIsEnableCurrentHeadOption(void* self)
    {
        const std::uint8_t pt = outfit::ReadLivePartsType();

        if (pt >= outfit::kCustomPartsTypeStart && pt <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry)
            {
                return entry->HasHeadOptions() ? 1 : 0;
            }
        }

        return g_OrigIsEnableHead(self);
    }

    static std::uint8_t __fastcall hkIsEnableCurrentSuit(void* self)
    {
        const std::uint8_t pt = outfit::ReadLivePartsType();

        if (pt >= outfit::kCustomPartsTypeStart && pt <= outfit::kCustomPartsTypeEnd)
        {
            const outfit::OutfitEntry* entry = nullptr;
            if (outfit::TryGetOutfitByPartsType(pt, &entry) && entry
                && entry->HasHeadOptions())
            {
                return 1;
            }
        }

        return g_OrigIsEnableCurrentSuit(self);
    }
}

namespace outfit
{
    bool Install_OutfitHeadOption_Hook()
    {
        if (g_Installed) return true;

        void* target = ResolveGameAddress(gAddr.MissionPrep_IsEnableCurrentHeadOption);
        if (!target)
        {
            Log("[OutfitHeadOption] target unresolved; module disabled\n");
            return false;
        }

        g_Installed = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkIsEnableCurrentHeadOption),
            reinterpret_cast<void**>(&g_OrigIsEnableHead));

        Log("[OutfitHeadOption] installed: %s (target=%p)\n",
            g_Installed ? "OK" : "FAIL", target);

        void* suitTarget = ResolveGameAddress(gAddr.IsEnableCurrentSuit);
        if (suitTarget)
        {
            g_IsEnableCurrentSuitInstalled = CreateAndEnableHook(
                suitTarget,
                reinterpret_cast<void*>(&hkIsEnableCurrentSuit),
                reinterpret_cast<void**>(&g_OrigIsEnableCurrentSuit));
            Log("[OutfitHeadOption:SuitGate] override hook %s (target=%p)\n",
                g_IsEnableCurrentSuitInstalled ? "installed" : "FAILED",
                suitTarget);
        }
        else
        {
            Log("[OutfitHeadOption:SuitGate] target unresolved; skipped\n");
        }

        void* hosTarget = ResolveGameAddress(
            gAddr.MissionPrepSystem_IsEnableHeadOptionSuit);
        if (hosTarget)
        {
            g_IsEnableHeadOptionSuitInstalled = CreateAndEnableHook(
                hosTarget,
                reinterpret_cast<void*>(&hkIsEnableHeadOptionSuit),
                reinterpret_cast<void**>(&g_OrigIsEnableHeadOptionSuit));
            Log("[OutfitHeadOption:HOSuit] override hook %s (target=%p)\n",
                g_IsEnableHeadOptionSuitInstalled ? "installed" : "FAILED",
                hosTarget);
        }
        else
        {
            Log("[OutfitHeadOption:HOSuit] target unresolved; skipped\n");
        }

        // ConverFaceIdWithFaceEquipId — slot byte → face-fv2 translator.
        // Vanilla path + custom-head sentinel return.
        void* cfTarget = ResolveGameAddress(gAddr.Player_ConverFaceIdWithFaceEquipId);
        if (cfTarget)
        {
            g_ConverFaceIdInstalled = CreateAndEnableHook(
                cfTarget,
                reinterpret_cast<void*>(&hkConverFaceIdWithFaceEquipId),
                reinterpret_cast<void**>(&g_OrigConverFaceId));
            Log("[OutfitHeadOption:ConverFace] hook %s (target=%p)\n",
                g_ConverFaceIdInstalled ? "installed" : "FAILED", cfTarget);
        }
        else
        {
            Log("[OutfitHeadOption:ConverFace] target unresolved; skipped\n");
        }

        return g_Installed;
    }

    void Uninstall_OutfitHeadOption_Hook()
    {
        if (g_ConverFaceIdInstalled)
        {
            if (void* t = ResolveGameAddress(gAddr.Player_ConverFaceIdWithFaceEquipId))
                DisableAndRemoveHook(t);
            g_OrigConverFaceId      = nullptr;
            g_ConverFaceIdInstalled = false;
        }

        if (g_IsEnableHeadOptionSuitInstalled)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.MissionPrepSystem_IsEnableHeadOptionSuit))
                DisableAndRemoveHook(t);
            g_OrigIsEnableHeadOptionSuit      = nullptr;
            g_IsEnableHeadOptionSuitInstalled = false;
        }

        if (g_IsEnableCurrentSuitInstalled)
        {
            if (void* t = ResolveGameAddress(gAddr.IsEnableCurrentSuit))
                DisableAndRemoveHook(t);
            g_OrigIsEnableCurrentSuit       = nullptr;
            g_IsEnableCurrentSuitInstalled  = false;
        }

        if (!g_Installed) return;
        if (void* t = ResolveGameAddress(gAddr.MissionPrep_IsEnableCurrentHeadOption))
            DisableAndRemoveHook(t);
        g_OrigIsEnableHead = nullptr;
        g_Installed        = false;
        Log("[OutfitHeadOption] removed\n");
    }
}
