#include "pch.h"

#include "CustomHeadRegistry.h"

#include <array>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include <HookUtils.h>

#include "log.h"
#include "AddressSet.h"
#include "../../core/V_FrameWorkState.h"
#include "EquipDevelopControllerImpl_GetSuitDevelopInfoIndex.h"
#include "../equip/EquipDevelop_AddToEquipDevelopTable.h"
#include "OutfitRegistry.h"
#include "../../core/FoxHashes.h"

namespace outfit
{
    namespace
    {
        std::array<CustomHeadEntry, kMaxCustomHeads> g_Heads{};
        std::mutex                                    g_Mutex;


        struct PendingSummaryDisplay
        {
            std::uint64_t nameHash = 0;
            std::uint64_t iconHash = 0;
        };
        std::unordered_map<std::uint16_t, PendingSummaryDisplay>
            g_PendingSummaryDisplay;

        using GetQuarkSystemTable_t = void* (__fastcall*)();
        static GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;

        std::int32_t FindByNameUnlocked(const char* name)
        {
            for (std::size_t i = 0; i < g_Heads.size(); ++i)
            {
                if (g_Heads[i].used && std::strcmp(g_Heads[i].name, name) == 0)
                    return static_cast<std::int32_t>(i);
            }
            return -1;
        }

        std::int32_t AllocateUnlocked()
        {
            for (std::size_t i = 0; i < g_Heads.size(); ++i)
            {
                if (!g_Heads[i].used)
                    return static_cast<std::int32_t>(i);
            }
            return -1;
        }

        std::uint8_t AllocateSlotUnlocked()
        {
            for (std::uint32_t s = kCustomHeadSlotBase; s < 0x100u; ++s)
            {
                bool taken = false;
                for (const CustomHeadEntry& e : g_Heads)
                {
                    if (e.used && e.slotByte == static_cast<std::uint8_t>(s))
                    {
                        taken = true;
                        break;
                    }
                }
                if (!taken) return static_cast<std::uint8_t>(s);
            }
            return 0;
        }


        static void* WalkToEquipDevelopController()
        {
            if (!g_GetQuarkSystemTable)
            {
                g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
                    ResolveGameAddress(gAddr.GetQuarkSystemTable));
                if (!g_GetQuarkSystemTable) return nullptr;
            }

            __try
            {
                auto* qst = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
                if (!qst) return nullptr;
                auto* app = *reinterpret_cast<std::uint8_t**>(qst + 0x98);
                if (!app) return nullptr;
                auto* base = *reinterpret_cast<std::uint8_t**>(app + 0x110);
                if (!base) return nullptr;
                return *reinterpret_cast<void**>(base + 0xac8);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return nullptr;
            }
        }


        static std::int32_t TranslateDevelopIdToRowIndex(std::uint32_t developId)
        {
            void* edc = WalkToEquipDevelopController();
            if (!edc) return -1;

            __try
            {
                using GetIdx_t = std::uint16_t (__fastcall*)(void*, std::uint16_t);
                void** vt = *reinterpret_cast<void***>(edc);
                if (!vt) return -1;
                auto getIdx = reinterpret_cast<GetIdx_t>(vt[0xE0 / 8]);
                if (!getIdx) return -1;
                const std::uint16_t row = getIdx(edc, static_cast<std::uint16_t>(developId));
                if (row == 0 || row == 0xFFFF) return -1;
                return static_cast<std::int32_t>(row);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return -1;
            }
        }
    }

    std::uint16_t RegisterHeadOption(
        const char* name,
        const std::uint16_t* TppEnemyFaceIdsPerPt,
        std::uint64_t langNameHash,
        std::uint64_t iconFtexCode,
        std::uint16_t explicitDevelopId,
        bool showInDevelopMenu)
    {
        static_assert(kPlayerTypeMax == 4,
                      "head registration log assumes 4 player types");

        if (!name || !name[0])
        {
            Log("[CustomHead] RegisterHeadOption: missing name\n");
            return 0;
        }

        std::uint16_t faceIds[kPlayerTypeMax];
        for (std::size_t i = 0; i < kPlayerTypeMax; ++i)
        {
            const std::uint16_t v = TppEnemyFaceIdsPerPt ? TppEnemyFaceIdsPerPt[i] : 0;
            faceIds[i] = (v != 0) ? v : kDefaultTppEnemyFaceId;
        }

        std::lock_guard<std::mutex> lock(g_Mutex);

        if (const std::int32_t existing = FindByNameUnlocked(name); existing >= 0)
        {
            CustomHeadEntry& e = g_Heads[existing];
            for (std::size_t i = 0; i < kPlayerTypeMax; ++i)
                e.TppEnemyFaceId[i] = faceIds[i];
            if (langNameHash != 0)  e.langNameHash = langNameHash;
            if (iconFtexCode != 0)  e.iconFtexCode = iconFtexCode;
            return e.equipId;
        }

        const std::int32_t idx = AllocateUnlocked();
        if (idx < 0)
        {
            Log("[CustomHead] RegisterHeadOption: registry full "
                "(max=%zu, name=%s)\n", kMaxCustomHeads, name);
            return 0;
        }


        std::int32_t developId = 0;
        if (explicitDevelopId != 0)
        {
            developId = explicitDevelopId;
        }
        else if (!V_FrameWorkState::ResolveOrCreateDevelopId(name, 0, developId)
            || developId <= 0 || developId > 0xFFFF)
        {
            Log("[CustomHead] RegisterHeadOption: developId allocation "
                "failed (name=%s, raw=%d)\n", name, developId);
            return 0;
        }


        const std::int32_t flowIndex = 0;


        const std::int32_t rowIndex =
            TranslateDevelopIdToRowIndex(static_cast<std::uint32_t>(developId));
        if (rowIndex < 0)
        {
            Log("[CustomHead] RegisterHeadOption: orig translator returned "
                "no row for developId=%d (name=%s). Either V_TppEquip."
                "AddToEquipDevelopTable wasn't called for this name, or "
                "the EquipDevelopController isn't reachable yet. Skipping "
                "registration.\n", developId, name);
            return 0;
        }
        if (rowIndex > 0xFFFF)
        {
            Log("[CustomHead] RegisterHeadOption: row index %d exceeds "
                "uint16 (name=%s); refusing\n", rowIndex, name);
            return 0;
        }

        const std::uint8_t slotByte = AllocateSlotUnlocked();
        if (slotByte < kCustomHeadSlotBase)
        {
            Log("[CustomHead] RegisterHeadOption: head slot space full "
                "(name=%s) — refusing rather than wrapping/aliasing a "
                "vanilla slot\n", name);
            return 0;
        }

        CustomHeadEntry& e = g_Heads[idx];
        e.used           = true;
        e.equipId        = static_cast<std::uint16_t>(rowIndex);
        e.developId      = static_cast<std::uint16_t>(developId);
        e.flowIndex      = static_cast<std::uint16_t>(flowIndex);
        e.slotByte       = slotByte;
        for (std::size_t i = 0; i < kPlayerTypeMax; ++i)
            e.TppEnemyFaceId[i] = faceIds[i];
        e.langNameHash   = langNameHash;
        e.iconFtexCode   = iconFtexCode;

        const std::size_t nameLen = std::strlen(name);
        const std::size_t copyLen = nameLen < (sizeof(e.name) - 1)
            ? nameLen
            : (sizeof(e.name) - 1);
        std::memcpy(e.name, name, copyLen);
        e.name[copyLen] = '\0';


        if (const auto it = g_PendingSummaryDisplay.find(e.developId);
            it != g_PendingSummaryDisplay.end())
        {
            if (it->second.nameHash != 0) e.langNameHash = it->second.nameHash;
            if (it->second.iconHash != 0) e.iconFtexCode = it->second.iconHash;
            g_PendingSummaryDisplay.erase(it);
#ifdef _DEBUG
            Log("[CustomHead] drained pending summary display for "
                "developId=%u (nameHash=0x%016llX iconHash=0x%016llX)\n",
                static_cast<unsigned>(e.developId),
                static_cast<unsigned long long>(e.langNameHash),
                static_cast<unsigned long long>(e.iconFtexCode));
#endif
        }

#ifdef _DEBUG
        Log("[CustomHead] registered '%s' equipId=%u (rowIndex=0x%X) "
            "developId=%u flowIndex=%u slot=0x%02X "
            "TppEnemyFaceId[S/M/F/A]=0x%X/0x%X/0x%X/0x%X\n",
            e.name,
            static_cast<unsigned>(e.equipId),
            static_cast<unsigned>(e.equipId),
            static_cast<unsigned>(e.developId),
            static_cast<unsigned>(e.flowIndex),
            static_cast<unsigned>(e.slotByte),
            static_cast<unsigned>(e.TppEnemyFaceId[0]),
            static_cast<unsigned>(e.TppEnemyFaceId[1]),
            static_cast<unsigned>(e.TppEnemyFaceId[2]),
            static_cast<unsigned>(e.TppEnemyFaceId[3]));
#endif

        if (!showInDevelopMenu)
        {
            outfit::SetDevelopHidden(e.equipId);
#ifdef _DEBUG
            Log("[CustomHead] '%s' showInDevelopMenu=false: flagged record index=%u "
                "(0x%X) for IsEquipVisile hide -> hidden from R&D Develop\n",
                e.name, static_cast<unsigned>(e.equipId),
                static_cast<unsigned>(e.equipId));
#endif
        }
        else
        {
#ifdef _DEBUG
            Log("[CustomHead] '%s' showInDevelopMenu=true: lists in the R&D Develop "
                "browser (index=%u)\n",
                e.name, static_cast<unsigned>(e.equipId));
#endif
        }

        if (const int filled = outfit::ResolvePendingHeadName(
                FoxHashes::StrCode64(e.name), e.equipId);
            filled > 0)
        {
#ifdef _DEBUG
            Log("[CustomHead] back-filled deferred head '%s' (equipId=%u) into "
                "%d outfit head slot(s)\n",
                e.name, static_cast<unsigned>(e.equipId), filled);
#endif
        }

        return e.equipId;
    }

    const CustomHeadEntry* TryGetCustomHeadByName(const char* name)
    {
        if (!name || !name[0]) return nullptr;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const std::int32_t idx = FindByNameUnlocked(name);
        return (idx >= 0) ? &g_Heads[idx] : nullptr;
    }

    const CustomHeadEntry* TryGetCustomHeadByEquipId(std::uint16_t equipId)
    {
        if (equipId == 0) return nullptr;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const CustomHeadEntry& e : g_Heads)
        {
            if (e.used && e.equipId == equipId)
                return &e;
        }
        return nullptr;
    }

    const CustomHeadEntry* TryGetCustomHeadBySlot(std::uint8_t slotByte)
    {
        if (slotByte < kCustomHeadSlotBase) return nullptr;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const CustomHeadEntry& e : g_Heads)
        {
            if (e.used && e.slotByte == slotByte)
                return &e;
        }
        return nullptr;
    }

    void SetCustomHeadSummaryDisplay(std::uint16_t developId,
                                     std::uint64_t nameHash,
                                     std::uint64_t iconHash)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);

        for (CustomHeadEntry& e : g_Heads)
        {
            if (!e.used || e.developId != developId) continue;
            if (nameHash != 0) e.langNameHash = nameHash;
            if (iconHash != 0) e.iconFtexCode = iconHash;
#ifdef _DEBUG
            Log("[CustomHead] summary display set developId=%u "
                "nameHash=0x%016llX iconHash=0x%016llX\n",
                static_cast<unsigned>(developId),
                static_cast<unsigned long long>(e.langNameHash),
                static_cast<unsigned long long>(e.iconFtexCode));
#endif
            return;
        }


        PendingSummaryDisplay& p = g_PendingSummaryDisplay[developId];
        if (nameHash != 0) p.nameHash = nameHash;
        if (iconHash != 0) p.iconHash = iconHash;
#ifdef _DEBUG
        Log("[CustomHead] summary display stashed (pending) developId=%u "
            "nameHash=0x%016llX iconHash=0x%016llX\n",
            static_cast<unsigned>(developId),
            static_cast<unsigned long long>(p.nameHash),
            static_cast<unsigned long long>(p.iconHash));
#endif
    }

    std::uint16_t GetCurrentWornHeadEquipId()
    {
        if (!g_GetQuarkSystemTable)
        {
            g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable));
            if (!g_GetQuarkSystemTable) return 0;
        }

        std::uint8_t slotByte = 0;
        __try
        {
            auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
            if (!qt) return 0;
            auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
            if (!q98) return 0;
            auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
            if (!state) return 0;


            slotByte = state[0xFA];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }

        if (const CustomHeadEntry* head = TryGetCustomHeadBySlot(slotByte))
            return head->equipId;
        return 0;
    }

    bool GetCurrentEquippedSuitBytes(std::uint8_t* outPartsType,
                                     std::uint8_t* outSelector)
    {
        if (outPartsType) *outPartsType = 0;
        if (outSelector)  *outSelector  = 0;

        if (!g_GetQuarkSystemTable)
        {
            g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable));
            if (!g_GetQuarkSystemTable) return false;
        }

        std::uint8_t pt  = 0;
        std::uint8_t sel = 0;
        __try
        {
            auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
            if (!qt) return false;
            auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
            if (!q98) return false;
            auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
            if (!state) return false;

            pt  = state[0xF8];
            sel = state[0xF9];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        if (outPartsType) *outPartsType = pt;
        if (outSelector)  *outSelector  = sel;
        return true;
    }
}
