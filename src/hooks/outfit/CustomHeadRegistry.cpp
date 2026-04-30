#include "pch.h"

#include "CustomHeadRegistry.h"

#include <array>
#include <cstring>
#include <mutex>

#include <HookUtils.h>

#include "log.h"
#include "AddressSet.h"
#include "../../core/V_FrameWorkState.h"

namespace outfit
{
    namespace
    {
        std::array<CustomHeadEntry, kMaxCustomHeads> g_Heads{};
        std::uint8_t                                  g_NextSlot = kCustomHeadSlotBase;
        std::mutex                                    g_Mutex;

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
                if (row == 0 || row == 0xFFFF) return -1;   // sentinel
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
        std::uint16_t TppEnemyFaceId,
        std::uint64_t langNameHash,
        std::uint64_t iconFtexCode)
    {
        if (!name || !name[0])
        {
            Log("[CustomHead] RegisterHeadOption: missing name\n");
            return 0;
        }
        if (TppEnemyFaceId == 0)
        {
            TppEnemyFaceId = kDefaultTppEnemyFaceId;
        }

        std::lock_guard<std::mutex> lock(g_Mutex);

        if (const std::int32_t existing = FindByNameUnlocked(name); existing >= 0)
        {
            CustomHeadEntry& e = g_Heads[existing];
            e.TppEnemyFaceId = TppEnemyFaceId;
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
        if (!V_FrameWorkState::ResolveOrCreateDevelopId(name, 0, developId)
            || developId <= 0 || developId > 0xFFFF)
        {
            Log("[CustomHead] RegisterHeadOption: developId allocation "
                "failed (name=%s, raw=%d)\n", name, developId);
            return 0;
        }


        std::int32_t flowIndex = 0;
        if (!V_FrameWorkState::ResolveOrCreateFlowIndex(name, 0, flowIndex)
            || flowIndex <= 0 || flowIndex > 0xFFFF)
        {
            Log("[CustomHead] RegisterHeadOption: flowIndex allocation "
                "failed (name=%s, raw=%d) — develop-gate will be skipped "
                "and the head will appear in the menu regardless of R&D "
                "state\n", name, flowIndex);


        }


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

        CustomHeadEntry& e = g_Heads[idx];
        e.used           = true;
        e.equipId        = static_cast<std::uint16_t>(rowIndex);
        e.developId      = static_cast<std::uint16_t>(developId);
        e.flowIndex      = static_cast<std::uint16_t>(flowIndex);
        e.slotByte       = g_NextSlot++;
        e.TppEnemyFaceId = TppEnemyFaceId;
        e.langNameHash   = langNameHash;
        e.iconFtexCode   = iconFtexCode;

        const std::size_t nameLen = std::strlen(name);
        const std::size_t copyLen = nameLen < (sizeof(e.name) - 1)
            ? nameLen
            : (sizeof(e.name) - 1);
        std::memcpy(e.name, name, copyLen);
        e.name[copyLen] = '\0';

        Log("[CustomHead] registered '%s' equipId=%u (rowIndex=0x%X) "
            "developId=%u flowIndex=%u slot=0x%02X TppEnemyFaceId=0x%X\n",
            e.name,
            static_cast<unsigned>(e.equipId),
            static_cast<unsigned>(e.equipId),
            static_cast<unsigned>(e.developId),
            static_cast<unsigned>(e.flowIndex),
            static_cast<unsigned>(e.slotByte),
            static_cast<unsigned>(e.TppEnemyFaceId));

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
}
