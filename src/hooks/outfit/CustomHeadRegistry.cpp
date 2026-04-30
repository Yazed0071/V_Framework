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

        // Walks the same Quark chain that retail FUN_1460B9FA0 uses to
        // reach the EquipDevelopController:
        //   QuarkSystemTable* qst = fox::GetQuarkSystemTable();
        //   ApplicationSystem* app = *(qst + 0x98);
        //   void* base = *(app + 0x110);
        //   EDC*  edc  = *(base + 0xac8);
        // Returns nullptr if any link is null (game not booted yet, etc.).
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

        // Calls EDC::vtable[0xE0] which translates a developId (p00 of an
        // EquipDevelopConstSetting row) into the row index that the orig's
        // lookup function FUN_14951AE20 expects. Same vtable slot retail's
        // GetBalaclavaDevelopInfoIndex (FUN_140954f70 in mgsvtpp.exe.c at
        // line 1278021) uses to find row 0x210 (BALACLAVA's row) from
        // developId 43000.
        //
        // NOTE on vtable slot: the NAMED build (1.0.0.1) puts this method
        // at vtable[0x70]; RETAIL (1.0.15.x) puts it at vtable[0xE0]. The
        // difference is a vtable layout shift between builds — the actual
        // method semantics (linear scan of developId table) are unchanged.
        // Calling vtable[0x70] in retail dispatches to a DIFFERENT method
        // that returns junk values (commonly 0x6B0 for our developId range)
        // — verified 2026-04-30 by debugging two-head registration where
        // both heads collapsed to the same rowIndex 0x6B0.
        //
        // Returns -1 if the EDC isn't reachable yet OR vtable[0xE0] returns
        // a sentinel "not found" value (typically 0xFFFF or 0).
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

        // Pull the developId from the shared V_FrameWorkState key registry.
        // If V_TppEquip.AddToEquipDevelopTable was called previously with
        // the same `name`, that call's allocated developId is returned.
        std::int32_t developId = 0;
        if (!V_FrameWorkState::ResolveOrCreateDevelopId(name, 0, developId)
            || developId <= 0 || developId > 0xFFFF)
        {
            Log("[CustomHead] RegisterHeadOption: developId allocation "
                "failed (name=%s, raw=%d)\n", name, developId);
            return 0;
        }

        // Also pull the flowIndex (p50). AddToEquipDevelopTable allocates
        // both developId AND flowIndex for the same key in
        // V_FrameWorkState, so this just retrieves what's already there.
        // The flowIndex is needed by the develop-gate at panel-injection
        // time so an unresearched head stays hidden from the submenu.
        std::int32_t flowIndex = 0;
        if (!V_FrameWorkState::ResolveOrCreateFlowIndex(name, 0, flowIndex)
            || flowIndex <= 0 || flowIndex > 0xFFFF)
        {
            Log("[CustomHead] RegisterHeadOption: flowIndex allocation "
                "failed (name=%s, raw=%d) — develop-gate will be skipped "
                "and the head will appear in the menu regardless of R&D "
                "state\n", name, flowIndex);
            // Fall through with flowIndex=0; the gate at injection time
            // treats 0 as "ungated" rather than blocking the registration
            // — better to show the head than fail registration outright.
        }

        // The orig's iDroid HEAD OPTION lookup (FUN_14951AE20) indexes
        // the EquipDevelopConstSetting table by row index, NOT by
        // developId. We use the orig's own developId→row-index translator
        // (EDC::vtable[0x70]) — same one GetBalaclavaDevelopInfoIndex
        // uses to find row 0x210 from developId 43000. Works because
        // V_TppEquip.AddToEquipDevelopTable's RegCstDev hook properly
        // appends the row into the orig's data structures, so the orig
        // recognizes our developId and gives us back its actual row
        // index.
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
