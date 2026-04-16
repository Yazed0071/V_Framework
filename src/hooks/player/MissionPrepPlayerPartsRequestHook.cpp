#include "pch.h"

#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "CustomSuitRegistry.h"
#include "MissionPrepPlayerPartsRequestHook.h"

namespace
{
    using RequestToChangePlayerPartsInMissionPreparationMode_t = void(__fastcall*)(
        void* self,
        int param_2,
        const void* param_3,
        std::uint8_t param_4
        );

    using GetQuarkSystemTable_t = void* (__fastcall*)();

    static RequestToChangePlayerPartsInMissionPreparationMode_t
        g_OrigRequestToChangePlayerPartsInMissionPreparationMode = nullptr;

    static GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;
    static bool g_Installed = false;

    static constexpr std::size_t kBlobSize = 0xE8;

    struct LivePlayerAppearance
    {
        std::uint8_t  f8_partsType = 0xFF;
        std::uint8_t  f9_camoType = 0xFF;
        std::uint8_t  fb_playerType = 0xFF;
        std::uint16_t fc_faceId = 0xFFFF;
        std::uint16_t fe_headOption = 0xFFFF;
        std::uint8_t  ba0_extra = 0xFF;
    };

    static bool IsCustomLiveState(const LivePlayerAppearance& s)
    {
        return
            (s.f8_partsType >= 0x40 && s.f8_partsType <= 0x7F) ||
            (s.f9_camoType >= 0x80 && s.f9_camoType <= 0xFE);
    }

    static bool IsCustomLiveAppearance(const LivePlayerAppearance& s)
    {
        return
            (s.f8_partsType >= 0x40 && s.f8_partsType <= 0x7F) ||
            (s.f9_camoType >= 0x80 && s.f9_camoType <= 0xFE);
    }

    static bool ResolveApis()
    {
        if (!g_GetQuarkSystemTable)
        {
            g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable)
                );
        }

        return g_GetQuarkSystemTable != nullptr;
    }

    static bool TryReadLivePlayerAppearance(LivePlayerAppearance& out)
    {
        if (!ResolveApis())
            return false;

        auto* quarkSystemTable = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
        if (!quarkSystemTable)
            return false;

        auto* q98 = *reinterpret_cast<std::uint8_t**>(quarkSystemTable + 0x98);
        if (!q98)
            return false;

        auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
        if (!state)
            return false;

        out.f8_partsType = state[0xF8];
        out.f9_camoType = state[0xF9];
        out.fb_playerType = state[0xFB];
        out.fc_faceId = *reinterpret_cast<std::uint16_t*>(state + 0xFC);
        out.fe_headOption = *reinterpret_cast<std::uint16_t*>(state + 0xFE);
        out.ba0_extra = state[0xBA0];
        return true;
    }

    static std::uint8_t ReadU8(const std::uint8_t* blob, std::size_t offset)
    {
        return blob[offset];
    }

    static std::uint32_t ReadU32(const std::uint8_t* blob, std::size_t offset)
    {
        std::uint32_t value = 0;
        std::memcpy(&value, blob + offset, sizeof(value));
        return value;
    }

    static std::uint64_t ReadU64(const std::uint8_t* blob, std::size_t offset)
    {
        std::uint64_t value = 0;
        std::memcpy(&value, blob + offset, sizeof(value));
        return value;
    }

    static void LogBlobSummary(
        const char* phase,
        int param_2,
        std::uint8_t param_4,
        const std::uint8_t* blob,
        const LivePlayerAppearance* live)
    {
        const std::uint8_t b00 = ReadU8(blob, 0x00);
        const std::uint8_t b01 = ReadU8(blob, 0x01);
        const std::uint8_t b02 = ReadU8(blob, 0x02);
        const std::uint8_t b03 = ReadU8(blob, 0x03);

        const std::uint32_t flags = ReadU32(blob, 0xBC);
        const std::uint8_t requestedPlayerType = ReadU8(blob, 0xC0);
        const std::uint64_t requestedIdentity = ReadU64(blob, 0xC8);

        const std::uint64_t q00 = ReadU64(blob, 0x00);
        const std::uint64_t q08 = ReadU64(blob, 0x08);
        const std::uint64_t q10 = ReadU64(blob, 0x10);
        const std::uint64_t q18 = ReadU64(blob, 0x18);

        if (live)
        {
            Log(
                "[MissionPrepPartsReq] %s idx=%d apply=%u "
                "blob{00=%02X 01=%02X 02=%02X 03=%02X flags=%08X type=%02X id=%016llX "
                "q00=%016llX q08=%016llX q10=%016llX q18=%016llX} "
                "live{f8=%02X f9=%02X fb=%02X fc=%04X fe=%04X ba0=%02X}\n",
                phase,
                param_2,
                static_cast<unsigned>(param_4),
                static_cast<unsigned>(b00),
                static_cast<unsigned>(b01),
                static_cast<unsigned>(b02),
                static_cast<unsigned>(b03),
                static_cast<unsigned>(flags),
                static_cast<unsigned>(requestedPlayerType),
                static_cast<unsigned long long>(requestedIdentity),
                static_cast<unsigned long long>(q00),
                static_cast<unsigned long long>(q08),
                static_cast<unsigned long long>(q10),
                static_cast<unsigned long long>(q18),
                static_cast<unsigned>(live->f8_partsType),
                static_cast<unsigned>(live->f9_camoType),
                static_cast<unsigned>(live->fb_playerType),
                static_cast<unsigned>(live->fc_faceId),
                static_cast<unsigned>(live->fe_headOption),
                static_cast<unsigned>(live->ba0_extra)
            );
        }
        else
        {
            Log(
                "[MissionPrepPartsReq] %s idx=%d apply=%u "
                "blob{00=%02X 01=%02X 02=%02X 03=%02X flags=%08X type=%02X id=%016llX "
                "q00=%016llX q08=%016llX q10=%016llX q18=%016llX}\n",
                phase,
                param_2,
                static_cast<unsigned>(param_4),
                static_cast<unsigned>(b00),
                static_cast<unsigned>(b01),
                static_cast<unsigned>(b02),
                static_cast<unsigned>(b03),
                static_cast<unsigned>(flags),
                static_cast<unsigned>(requestedPlayerType),
                static_cast<unsigned long long>(requestedIdentity),
                static_cast<unsigned long long>(q00),
                static_cast<unsigned long long>(q08),
                static_cast<unsigned long long>(q10),
                static_cast<unsigned long long>(q18)
            );
        }
    }
}

static void __fastcall hkRequestToChangePlayerPartsInMissionPreparationMode(
    void* self,
    int param_2,
    const void* param_3,
    std::uint8_t param_4)
{
    if (!param_3)
    {
        g_OrigRequestToChangePlayerPartsInMissionPreparationMode(self, param_2, param_3, param_4);
        return;
    }

    std::uint8_t blobCopy[kBlobSize]{};
    std::memcpy(blobCopy, param_3, kBlobSize);
    auto* blob = blobCopy;

    LivePlayerAppearance before{};
    const bool haveBefore = TryReadLivePlayerAppearance(before);

    LogBlobSummary("in ", param_2, param_4, blob, haveBefore ? &before : nullptr);

    const bool looksLikeBrokenCustomSuit =
        param_4 == 1 &&
        blob[0x00] == 0x00 &&
        blob[0x01] == 0xFF &&
        blob[0x02] == 0x00;

    if (looksLikeBrokenCustomSuit)
    {
        const std::uint16_t pendingDevelopId = GetPendingCustomSuitDevelopId();

        // Cache the last meaningful non-custom head option before the custom swap wipes it.
        if (haveBefore &&
            !IsCustomLiveAppearance(before) &&
            before.fe_headOption != 0 &&
            before.fe_headOption != 0xFFFF)
        {
            RememberPreservedHeadOption(before.fb_playerType, before.fe_headOption);
        }

        const CustomSuitEntry* entry = nullptr;
        if (pendingDevelopId != 0 &&
            TryGetCustomSuitByDevelopIdForPlayerType(
                pendingDevelopId,
                haveBefore ? before.fb_playerType : 0xFF,
                &entry) &&
            entry)
        {
            PreservedAppearanceState preserved{};
            const bool havePreserved =
                TryGetPreservedAppearance(entry->playerType, preserved);

            std::uint8_t resolvedHead = 0x00;
            if (entry->IsFaceEnabled())
            {
                if (havePreserved &&
                    preserved.headOption != 0 &&
                    preserved.headOption != 0xFFFF)
                {
                    resolvedHead =
                        static_cast<std::uint8_t>(preserved.headOption & 0xFF);
                }
                else if (haveBefore &&
                    before.fe_headOption != 0 &&
                    before.fe_headOption != 0xFFFF)
                {
                    resolvedHead =
                        static_cast<std::uint8_t>(before.fe_headOption & 0xFF);
                }
                else
                {
                    resolvedHead = blob[0x03];
                }
            }

            blob[0x00] = entry->customPartsType;
            blob[0x01] = entry->customSelectorCode;
            blob[0x02] = haveBefore ? before.ba0_extra : 0x01;
            blob[0x03] = resolvedHead;

            *reinterpret_cast<std::uint32_t*>(blob + 0xBC) = 0x81;
            blob[0xC0] = entry->playerType;

            Log(
                "[MissionPrepPartsReq] rewrite developId=%u -> parts=%02X selector=%02X aux=%02X head=%02X type=%02X\n",
                static_cast<unsigned>(pendingDevelopId),
                static_cast<unsigned>(blob[0x00]),
                static_cast<unsigned>(blob[0x01]),
                static_cast<unsigned>(blob[0x02]),
                static_cast<unsigned>(blob[0x03]),
                static_cast<unsigned>(blob[0xC0])
            );

            SetActiveCustomSuit(
                pendingDevelopId,
                entry->playerType,
                entry->customPartsType,
                entry->customSelectorCode,
                haveBefore ? before.fc_faceId : 0xFFFF,
                static_cast<std::uint16_t>(resolvedHead)
            );

            ClearPendingCustomSuitDevelopId();
        }
        else
        {
            Log("[MissionPrepPartsReq] broken custom blob but no pending custom developId\n");
        }
    }

    g_OrigRequestToChangePlayerPartsInMissionPreparationMode(self, param_2, blob, param_4);

    LivePlayerAppearance after{};
    const bool haveAfter = TryReadLivePlayerAppearance(after);
    LogBlobSummary("out", param_2, param_4, blob, haveAfter ? &after : nullptr);

    if (haveAfter && !IsCustomLiveState(after))
        ClearActiveCustomSuit();
}

bool Install_MissionPrepPlayerPartsRequest_Hook()
{
    if (g_Installed)
    {
        Log("[Hook] MissionPrepPartsReq: already installed\n");
        return true;
    }

    if (!ResolveApis())
    {
        Log("[Hook] MissionPrepPartsReq: failed to resolve GetQuarkSystemTable\n");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.MissionPrep_RequestToChangePlayerPartsInMissionPreparationMode);
    if (!target)
    {
        Log("[Hook] MissionPrepPartsReq: failed to resolve target\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkRequestToChangePlayerPartsInMissionPreparationMode),
        reinterpret_cast<void**>(&g_OrigRequestToChangePlayerPartsInMissionPreparationMode)
    );

    Log("[Hook] MissionPrepPartsReq: %s\n", ok ? "OK" : "FAIL");
    g_Installed = ok;
    return ok;
}

bool Uninstall_MissionPrepPlayerPartsRequest_Hook()
{
    if (!g_Installed)
        return true;

    if (void* target = ResolveGameAddress(gAddr.MissionPrep_RequestToChangePlayerPartsInMissionPreparationMode))
        DisableAndRemoveHook(target);

    g_OrigRequestToChangePlayerPartsInMissionPreparationMode = nullptr;
    g_GetQuarkSystemTable = nullptr;
    g_Installed = false;

    Log("[Hook] MissionPrepPartsReq: removed\n");
    return true;
}