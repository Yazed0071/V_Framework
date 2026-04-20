#include "pch.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "CustomSuitRegistry.h"
#include "PlayerSuitResolverHook.h"

namespace
{
    struct LiveSuitState
    {
        std::uint8_t  partsType = 0xFF;    // +0xF8
        std::uint8_t  selector = 0xFF;     // +0xF9
        std::uint8_t  playerType = 0xFF;   // +0xFB
        std::uint16_t faceId = 0xFFFF;     // +0xFC
        std::uint16_t headOption = 0xFFFF; // +0xFE
    };

    using ResolveSuitToPartsType_t =
        std::uint8_t(__fastcall*)(void* self, std::uint32_t suitCode);

    using GetQuarkSystemTable_t =
        void* (__fastcall*)();

    static ResolveSuitToPartsType_t g_OrigResolveSuitToPartsType = nullptr;
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

    static bool TryReadLiveSuitState(LiveSuitState& out)
    {
        if (!ResolveApis())
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

        out.partsType = state[0xF8];
        out.selector = state[0xF9];
        out.playerType = state[0xFB];
        out.faceId = *reinterpret_cast<std::uint16_t*>(state + 0xFC);
        out.headOption = *reinterpret_cast<std::uint16_t*>(state + 0xFE);
        return true;
    }
}

static std::uint8_t __fastcall hkResolveSuitToPartsType(
    void* self,
    std::uint32_t suitCode)
{
    const std::uint8_t code =
        static_cast<std::uint8_t>(suitCode & 0xFF);

    LiveSuitState live{};
    const bool haveLive = TryReadLiveSuitState(live);
    const std::uint8_t currentPlayerType =
        haveLive ? live.playerType : 0xFF;

    // Pending bridge for mission prep / support-drop broken 0xFF transition.
    // Do NOT fire early on other selectors like 0x0F.
    const std::uint16_t pendingDevelopId = GetPendingCustomSuitDevelopId();
    if (pendingDevelopId != 0 && pendingDevelopId != 0xFFFF)
    {
        if (code == 0xFF)
        {
            const CustomSuitEntry* pendingEntry = nullptr;
            if (TryGetCustomSuitByDevelopIdForPlayerType(
                pendingDevelopId,
                currentPlayerType,
                &pendingEntry) &&
                pendingEntry)
            {
                // Track how many times this pending ID has been resolved.
                // After enough calls (game resolves for multiple slots), clear it.
                static std::uint16_t s_lastPendingId = 0;
                static int s_resolveCount = 0;

                if (pendingDevelopId != s_lastPendingId)
                {
                    s_lastPendingId = pendingDevelopId;
                    s_resolveCount = 0;

                    Log(
                        "[SuitResolver] pending developId=%u playerType=%u -> partsType=0x%02X\n",
                        static_cast<unsigned>(pendingDevelopId),
                        static_cast<unsigned>(currentPlayerType),
                        static_cast<unsigned>(pendingEntry->customPartsType)
                    );
                }

                ++s_resolveCount;

                // After 4 resolutions, clear the pending state.
                // The game typically calls ResolveSuitToPartsType 2-3 times per suit change.
                if (s_resolveCount >= 4)
                {
                    ClearPendingCustomSuitDevelopId();
                    s_resolveCount = 0;
                }

                return pendingEntry->customPartsType;
            }
        }
    }

    // Normal custom selector resolution.
    const CustomSuitEntry* mapped = nullptr;
    if (TryGetCustomSuitBySelectorCode(code, &mapped) &&
        mapped &&
        (currentPlayerType == 0xFF || mapped->playerType == currentPlayerType))
    {
        // Throttled: resolver is called per-frame from several call sites.
        // Only log on selector/partsType change.
        static std::uint16_t s_lastKey = 0xFFFF;
        const std::uint16_t key =
            (static_cast<std::uint16_t>(code) << 8) |
            static_cast<std::uint16_t>(mapped->customPartsType);
        if (key != s_lastKey)
        {
            s_lastKey = key;
            Log(
                "[SuitResolver] custom selector=0x%02X -> partsType=0x%02X\n",
                static_cast<unsigned>(code),
                static_cast<unsigned>(mapped->customPartsType)
            );
        }

        return mapped->customPartsType;
    }

    return g_OrigResolveSuitToPartsType(self, suitCode);
}

bool Install_PlayerSuitResolver_Hook()
{
    if (g_Installed)
    {
        Log("[Hook] PlayerSuitResolver: already installed\n");
        return true;
    }

    if (!ResolveApis())
    {
        Log("[Hook] PlayerSuitResolver: failed to resolve GetQuarkSystemTable\n");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.ResolveSuitToPartsType);
    if (!target)
    {
        Log("[Hook] PlayerSuitResolver: failed to resolve ResolveSuitToPartsType\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkResolveSuitToPartsType),
        reinterpret_cast<void**>(&g_OrigResolveSuitToPartsType)
    );

    Log("[Hook] PlayerSuitResolver: %s\n", ok ? "OK" : "FAIL");
    g_Installed = ok;
    return ok;
}

bool Uninstall_PlayerSuitResolver_Hook()
{
    if (!g_Installed)
        return true;

    if (void* target = ResolveGameAddress(gAddr.ResolveSuitToPartsType))
        DisableAndRemoveHook(target);

    g_OrigResolveSuitToPartsType = nullptr;
    g_GetQuarkSystemTable = nullptr;
    g_Installed = false;

    Log("[Hook] PlayerSuitResolver: removed\n");
    return true;
}