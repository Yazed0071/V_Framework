#include "pch.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "CharacterSelectorPreserveHook.h"
#include "CustomSuitRegistry.h"

namespace
{
    using StoreCurrentCharacterSuitAndHeadPartsInfo_t =
        void(__fastcall*)(void* self);

    using GetQuarkSystemTable_t =
        void* (__fastcall*)();

    static StoreCurrentCharacterSuitAndHeadPartsInfo_t
        g_OrigStoreCurrentCharacterSuitAndHeadPartsInfo = nullptr;

    static GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;
    static bool g_Installed = false;

    static bool ResolveApis()
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

    static std::uint8_t* GetPlayerQuarkState()
    {
        if (!ResolveApis())
            return nullptr;

        auto* quarkSystemTable =
            reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
        if (!quarkSystemTable)
            return nullptr;

        auto* q98 = *reinterpret_cast<std::uint8_t**>(quarkSystemTable + 0x98);
        if (!q98)
            return nullptr;

        auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
        return state;
    }

    static bool IsCustomLiveAppearance(const std::uint8_t* state)
    {
        if (!state)
            return false;

        const std::uint8_t partsType = state[0xF8];
        const std::uint8_t selector = state[0xF9];

        return
            (partsType >= 0x40 && partsType <= 0x7F) ||
            (selector >= 0x80 && selector <= 0xFE);
    }

    static void CapturePreservedAppearanceFromQuark()
    {
        auto* state = GetPlayerQuarkState();
        if (!state)
            return;

        const std::uint8_t playerType = state[0xFB];
        const std::uint16_t faceId = *reinterpret_cast<std::uint16_t*>(state + 0xFC);

        std::uint8_t armType = 0;
        std::uint8_t faceEquipId = 0;
        std::uint8_t faceEquipUnk = 0;
        std::uint16_t headOption = 0;

        // StoreCurrentCharacterSuitAndHeadPartsInfo writes:
        // playerType 0 or 3 -> 0x1994 / 0x1995 / 0x1998 / 0x1996
        // playerType 1 or 2 -> 0x1999 / 0x199A / 0x199E / 0x199C
        if (playerType == 0 || playerType == 3)
        {
            armType = state[0x1994];
            faceEquipId = state[0x1995];
            faceEquipUnk = state[0x1998];
            headOption = *reinterpret_cast<std::uint16_t*>(state + 0x1996);
        }
        else if (playerType == 1 || playerType == 2)
        {
            armType = state[0x1999];
            faceEquipId = state[0x199A];
            faceEquipUnk = state[0x199E];
            headOption = *reinterpret_cast<std::uint16_t*>(state + 0x199C);
        }
        else
        {
            return;
        }

        // If a custom suit is active, the original StoreCurrentCharacterSuitAndHeadPartsInfo
        // wrote garbage (custom partsType/selector as arm/face values) into Quark.
        // Repair by overwriting with our last known good preserved values.
        if (IsCustomLiveAppearance(state) || faceEquipId == 0xFF || armType >= 0x40)
        {
            PreservedAppearanceState preserved{};
            if (TryGetPreservedAppearance(playerType, preserved))
            {
                if (playerType == 0 || playerType == 3)
                {
                    state[0x1994] = preserved.armType;
                    state[0x1995] = preserved.faceEquipId;
                    state[0x1998] = preserved.faceEquipUnk;
                    *reinterpret_cast<std::uint16_t*>(state + 0x1996) = preserved.headOption;
                }
                else if (playerType == 1 || playerType == 2)
                {
                    state[0x1999] = preserved.armType;
                    state[0x199A] = preserved.faceEquipId;
                    state[0x199E] = preserved.faceEquipUnk;
                    *reinterpret_cast<std::uint16_t*>(state + 0x199C) = preserved.headOption;
                }

                Log(
                    "[CharSelectPreserve] repaired Quark type=0x%02X arm=%u faceEquip=%u unk=0x%02X head=0x%04X (was arm=%u face=%u)\n",
                    static_cast<unsigned>(playerType),
                    static_cast<unsigned>(preserved.armType),
                    static_cast<unsigned>(preserved.faceEquipId),
                    static_cast<unsigned>(preserved.faceEquipUnk),
                    static_cast<unsigned>(preserved.headOption),
                    static_cast<unsigned>(armType),
                    static_cast<unsigned>(faceEquipId)
                );
            }
            else
            {
                Log(
                    "[CharSelectPreserve] skip custom live, no preserved state type=0x%02X arm=%u faceEquip=%u\n",
                    static_cast<unsigned>(playerType),
                    static_cast<unsigned>(armType),
                    static_cast<unsigned>(faceEquipId)
                );
            }

            return;
        }

        RememberPreservedFullAppearance(
            playerType,
            armType,
            faceEquipId,
            faceEquipUnk,
            headOption
        );

        Log(
            "[CharSelectPreserve] type=0x%02X arm=%u faceEquip=%u unk=0x%02X head=0x%04X faceId=0x%04X\n",
            static_cast<unsigned>(playerType),
            static_cast<unsigned>(armType),
            static_cast<unsigned>(faceEquipId),
            static_cast<unsigned>(faceEquipUnk),
            static_cast<unsigned>(headOption),
            static_cast<unsigned>(faceId)
        );
    }
}

static void __fastcall hkStoreCurrentCharacterSuitAndHeadPartsInfo(void* self)
{
    g_OrigStoreCurrentCharacterSuitAndHeadPartsInfo(self);
    CapturePreservedAppearanceFromQuark();
}

bool Install_CharacterSelectorPreserve_Hook()
{
    if (g_Installed)
    {
        Log("[Hook] CharacterSelectorPreserve: already installed\n");
        return true;
    }

    if (!ResolveApis())
    {
        Log("[Hook] CharacterSelectorPreserve: failed to resolve GetQuarkSystemTable\n");
        return false;
    }

    void* target =
        ResolveGameAddress(gAddr.CharacterSelectorCallbackImpl_StoreCurrentCharacterSuitAndHeadPartsInfo);

    if (!target)
    {
        Log("[Hook] CharacterSelectorPreserve: failed to resolve target\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkStoreCurrentCharacterSuitAndHeadPartsInfo),
        reinterpret_cast<void**>(&g_OrigStoreCurrentCharacterSuitAndHeadPartsInfo)
    );

    Log("[Hook] CharacterSelectorPreserve: %s\n", ok ? "OK" : "FAIL");
    g_Installed = ok;
    return ok;
}

bool Uninstall_CharacterSelectorPreserve_Hook()
{
    if (!g_Installed)
        return true;

    if (void* target =
        ResolveGameAddress(gAddr.CharacterSelectorCallbackImpl_StoreCurrentCharacterSuitAndHeadPartsInfo))
    {
        DisableAndRemoveHook(target);
    }

    g_OrigStoreCurrentCharacterSuitAndHeadPartsInfo = nullptr;
    g_GetQuarkSystemTable = nullptr;
    g_Installed = false;

    Log("[Hook] CharacterSelectorPreserve: removed\n");
    return true;
}
