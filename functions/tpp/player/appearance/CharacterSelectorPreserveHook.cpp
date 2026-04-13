#include "pch.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "tpp/player/appearance/CharacterSelectorPreserveHook.h"
#include "tpp/player/appearance/CustomSuitRegistry.h"

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