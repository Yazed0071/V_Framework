#include "pch.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "CustomSuitRegistry.h"
#include "PlayerFaceEquipRepairHook.h"

namespace
{
    using UpdatePartsStatus_t = void(__fastcall*)(void* self);
    using GetQuarkSystemTable_t = void* (__fastcall*)();

    static UpdatePartsStatus_t g_OrigUpdatePartsStatus = nullptr;
    static GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;
    static bool g_Installed = false;

    struct LiveSuitState
    {
        std::uint8_t  partsType = 0xFF;    // +0xF8
        std::uint8_t  selector = 0xFF;     // +0xF9
        std::uint8_t  playerType = 0xFF;   // +0xFB
        std::uint16_t faceId = 0xFFFF;     // +0xFC
        std::uint16_t headOption = 0xFFFF; // +0xFE
    };

    struct QuarkStoredAppearance
    {
        bool valid = false;
        std::uint8_t armType = 0;
        std::uint8_t faceEquipId = 0;
        std::uint8_t faceEquipUnk = 0;
        std::uint16_t headOption = 0;
    };

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

        return *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
    }

    static bool TryReadLiveSuitState(LiveSuitState& out)
    {
        auto* state = GetPlayerQuarkState();
        if (!state)
            return false;

        out.partsType = state[0xF8];
        out.selector = state[0xF9];
        out.playerType = state[0xFB];
        out.faceId = *reinterpret_cast<std::uint16_t*>(state + 0xFC);
        out.headOption = *reinterpret_cast<std::uint16_t*>(state + 0xFE);
        return true;
    }

    static bool TryReadQuarkStoredAppearance(
        std::uint8_t playerType,
        QuarkStoredAppearance& out)
    {
        out = {};

        auto* state = GetPlayerQuarkState();
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

    static bool TryResolveDesiredHeadOption(
        const CustomSuitEntry* entry,
        const ActiveCustomSuitState& active,
        std::uint16_t& outHeadOption)
    {
        outHeadOption = 0;

        if (!entry)
            return false;

        QuarkStoredAppearance quark{};
        if (TryReadQuarkStoredAppearance(entry->playerType, quark) &&
            quark.valid &&
            quark.headOption != 0 &&
            quark.headOption != 0xFFFF)
        {
            outHeadOption = quark.headOption;
            return true;
        }

        PreservedAppearanceState preserved{};
        if (TryGetPreservedAppearance(entry->playerType, preserved) &&
            preserved.headOption != 0 &&
            preserved.headOption != 0xFFFF)
        {
            outHeadOption = preserved.headOption;
            return true;
        }

        if (active.headOption != 0 && active.headOption != 0xFFFF)
        {
            outHeadOption = active.headOption;
            return true;
        }

        return false;
    }
}

static void __fastcall hkUpdatePartsStatus(void* self)
{
    g_OrigUpdatePartsStatus(self);

    LiveSuitState live{};
    if (!TryReadLiveSuitState(live))
        return;

    ActiveCustomSuitState active{};
    if (!TryGetActiveCustomSuit(active) || !active.valid)
        return;

    if (live.partsType != active.partsType || live.playerType != active.playerType)
        return;

    const CustomSuitEntry* entry = nullptr;
    if (!TryGetCustomSuitByPartsType(live.partsType, &entry) || !entry || !entry->IsFaceEnabled())
        return;

    if (live.headOption != 0 && live.headOption != 0xFFFF)
        return;

    std::uint16_t desiredHeadOption = 0;
    if (!TryResolveDesiredHeadOption(entry, active, desiredHeadOption) ||
        desiredHeadOption == 0 ||
        desiredHeadOption == 0xFFFF)
    {
        return;
    }

    auto* state = GetPlayerQuarkState();
    if (!state)
        return;

    const std::uint16_t previousHeadOption =
        *reinterpret_cast<std::uint16_t*>(state + 0xFE);

    if (previousHeadOption == desiredHeadOption)
        return;

    *reinterpret_cast<std::uint16_t*>(state + 0xFE) = desiredHeadOption;

    Log(
        "[HeadOptionRepair] parts=0x%02X type=0x%02X headOption 0x%04X -> 0x%04X\n",
        static_cast<unsigned>(live.partsType),
        static_cast<unsigned>(live.playerType),
        static_cast<unsigned>(previousHeadOption),
        static_cast<unsigned>(desiredHeadOption)
    );
}

bool Install_PlayerHeadOptionRepair_Hook()
{
    if (g_Installed)
    {
        Log("[Hook] PlayerHeadOptionRepair: already installed\n");
        return true;
    }

    if (!ResolveApis())
    {
        Log("[Hook] PlayerHeadOptionRepair: failed to resolve GetQuarkSystemTable\n");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.UpdatePartsStatus);
    if (!target)
    {
        Log("[Hook] PlayerHeadOptionRepair: failed to resolve UpdatePartsStatus\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkUpdatePartsStatus),
        reinterpret_cast<void**>(&g_OrigUpdatePartsStatus)
    );

    Log("[Hook] PlayerHeadOptionRepair: %s\n", ok ? "OK" : "FAIL");
    g_Installed = ok;
    return ok;
}

bool Uninstall_PlayerHeadOptionRepair_Hook()
{
    if (!g_Installed)
        return true;

    if (void* target = ResolveGameAddress(gAddr.UpdatePartsStatus))
        DisableAndRemoveHook(target);

    g_OrigUpdatePartsStatus = nullptr;
    g_GetQuarkSystemTable = nullptr;
    g_Installed = false;

    Log("[Hook] PlayerHeadOptionRepair: removed\n");
    return true;
}