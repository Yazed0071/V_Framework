#include "pch.h"
#include "EquipDevelopControllerImpl_IsDisplayableBluePrintIDContion.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "CustomBluePrint.h"
#include "DataBaseControllerImpl_AddDataBase.h"
#include "DevelopArrayGrow.h"
#include "EquipDevelop_AddToEquipDevelopTable.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using IsDisplayableBluePrintId_t =
        bool (__fastcall*)(void* controller, std::uint16_t dataBaseId);
    using GetDevelopConditionCount_t =
        std::uint8_t (__fastcall*)(void* base20, std::uint16_t row);
    using GetDevConditionMsgIdArray_t =
        void (__fastcall*)(void* base20, std::uint16_t row,
                           std::uint8_t capacity, std::uint64_t* msgIds);

    constexpr std::size_t  kRecordStride        = 0x68;
    constexpr std::size_t  kRecordDevelopId     = 0x8;
    constexpr std::size_t  kRecordBluePrintId   = 0x16;
    constexpr std::size_t  kRecordIntimacyPoint = 0x59;
    constexpr std::uint8_t kIntimacyPointMask   = 0x7F;
    constexpr std::uint8_t kConditionLineCap    = 6;

    const std::uint8_t kIsDisplayablePrologue154[] = {
        0xB8, 0xFF, 0xFF, 0x00, 0x00, 0x66, 0x3B, 0xD0
    };
    const std::uint8_t kIsDisplayablePrologue153[] = {
        0xB8, 0xFF, 0xFF, 0x00, 0x00, 0x66, 0x39, 0xC2
    };
    const std::uint8_t kConditionCountPrologue[] = {
        0x0F, 0xB7, 0xD2, 0xB8, 0x02, 0x00, 0x00, 0x00, 0x41, 0xB0, 0x01
    };
    const std::uint8_t kConditionMsgIdPrologue[] = {
        0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20,
        0x41, 0x56, 0x48, 0x83, 0xEC, 0x30
    };

    static_assert(sizeof(kIsDisplayablePrologue154) == sizeof(kIsDisplayablePrologue153),
                  "both IsDisplayableBluePrintIDContion prologues share one length");

    struct BuildTargets
    {
        std::uintptr_t      isDisplayable;
        std::uintptr_t      conditionCount;
        std::uintptr_t      conditionMsgIds;
        const std::uint8_t* isDisplayablePrologue;
    };

    struct HookTarget
    {
        const char*         name;
        std::uintptr_t      address;
        const std::uint8_t* prologue;
        std::size_t         prologueSize;
        void*               detour;
        void**              original;
    };

    constexpr std::size_t kHookCount = 3;

    IsDisplayableBluePrintId_t  g_OrigIsDisplayable   = nullptr;
    GetDevelopConditionCount_t  g_OrigConditionCount  = nullptr;
    GetDevConditionMsgIdArray_t g_OrigConditionMsgIds = nullptr;
    void*                       g_HookedTargets[kHookCount] = {};
    std::atomic<bool>           g_LineCapWarned{ false };

    bool SelectBuildTargets(BuildTargets& out)
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:
            out = { 0x140F6CC70ull, 0x140F67C80ull, 0x140F670C0ull, kIsDisplayablePrologue154 };
            return true;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:
            out = { 0x140F6CCD0ull, 0x140F67CE0ull, 0x140F67120ull, kIsDisplayablePrologue154 };
            return true;
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:
            out = { 0x14951ECA0ull, 0x149506670ull, 0x140F67850ull, kIsDisplayablePrologue153 };
            return true;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:
            out = { 0x149F38AE0ull, 0x149F1F2D0ull, 0x140F67950ull, kIsDisplayablePrologue153 };
            return true;
        default:
            return false;
        }
    }

    bool PrologueMatchesSEH(const void* fn, const std::uint8_t* expected, std::size_t size)
    {
        __try
        {
            return std::memcmp(fn, expected, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    const std::uint8_t* RowRecord(const void* base20, std::uint16_t row)
    {
        return static_cast<const std::uint8_t*>(base20)
            + static_cast<std::size_t>(row) * kRecordStride;
    }

    std::uint16_t CustomBluePrintRowDevelopId(const void* base20, std::uint16_t row)
    {
        if (!base20 || row >= equip::NativeFlowBound())
            return 0;
        __try
        {
            const std::uint8_t* rec = RowRecord(base20, row);
            if (!bluePrint::IsReservedDataBaseId(
                    *reinterpret_cast<const std::uint16_t*>(rec + kRecordBluePrintId)))
                return 0;
            return *reinterpret_cast<const std::uint16_t*>(rec + kRecordDevelopId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool RowHasIntimacyCondition(const void* base20, std::uint16_t row)
    {
        __try
        {
            return (RowRecord(base20, row)[kRecordIntimacyPoint] & kIntimacyPointMask) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint64_t CustomBluePrintNameLangId(std::uint16_t row, std::uint16_t developId)
    {
        const std::int32_t dataBaseId = EquipDevelopAdd::BluePrintIdForFlowIndex(row, developId);
        if (dataBaseId <= 0)
            return 0;
        bluePrintDb::Documentation doc;
        if (!bluePrintDb::TryGetDocumentation(dataBaseId, doc))
            return 0;
        return doc.nameHash;
    }

    bool __fastcall hkIsDisplayableBluePrintIDContion(void* controller, std::uint16_t dataBaseId)
    {
        if (bluePrint::IsReservedDataBaseId(dataBaseId)
            && !MissionCodeGuard::ShouldBypassHooks())
            return true;
        return g_OrigIsDisplayable(controller, dataBaseId);
    }

    std::uint8_t __fastcall hkGetDevelopConditionCount(void* base20, std::uint16_t row)
    {
        const std::uint8_t count = g_OrigConditionCount(base20, row);
        if (CustomBluePrintRowDevelopId(base20, row) == 0
            || MissionCodeGuard::ShouldBypassHooks())
            return count;
        if (count >= kConditionLineCap)
        {
            if (!g_LineCapWarned.exchange(true))
                LogWarn("DevelopBluePrint: develop row %u has %u other requirements, one line not shown in R&D",
                    static_cast<unsigned>(row), static_cast<unsigned>(count));
            return count;
        }
        return static_cast<std::uint8_t>(count + 1);
    }

    void __fastcall hkGetDevConditionMsgIDArray(void* base20, std::uint16_t row,
                                                std::uint8_t capacity, std::uint64_t* msgIds)
    {
        g_OrigConditionMsgIds(base20, row, capacity, msgIds);
        if (!msgIds)
            return;
        const std::uint16_t developId = CustomBluePrintRowDevelopId(base20, row);
        if (developId == 0 || MissionCodeGuard::ShouldBypassHooks())
            return;
        const std::uint64_t nameLangId = CustomBluePrintNameLangId(row, developId);
        if (nameLangId == 0)
            return;
        const std::uint8_t otherLines = g_OrigConditionCount(base20, row);
        const std::uint8_t bluePrintLine = static_cast<std::uint8_t>(
            otherLines - (RowHasIntimacyCondition(base20, row) ? 1 : 0));
        if (bluePrintLine < capacity)
            msgIds[bluePrintLine] = nameLangId;
    }

    void RemoveHooks()
    {
        for (std::size_t i = kHookCount; i-- > 0;)
        {
            if (g_HookedTargets[i])
                DisableAndRemoveHook(g_HookedTargets[i]);
            g_HookedTargets[i] = nullptr;
        }
        g_OrigConditionMsgIds = nullptr;
        g_OrigConditionCount  = nullptr;
        g_OrigIsDisplayable   = nullptr;
    }
}

namespace equip
{
    bool Install_EquipDevelopControllerImpl_IsDisplayableBluePrintIDContion()
    {
        if (g_HookedTargets[0])
            return true;

        BuildTargets targets{};
        if (!SelectBuildTargets(targets))
        {
            LogError("DevelopBluePrint: no addresses for %s, custom blueprints not listed as R&D requirements",
                GetGameBuildName(gGameBuild));
            return false;
        }

        const HookTarget hooks[kHookCount] = {
            { "IsDisplayableBluePrintIDContion", targets.isDisplayable,
              targets.isDisplayablePrologue, sizeof(kIsDisplayablePrologue154),
              reinterpret_cast<void*>(&hkIsDisplayableBluePrintIDContion),
              reinterpret_cast<void**>(&g_OrigIsDisplayable) },
            { "GetDevelopConditionCount", targets.conditionCount,
              kConditionCountPrologue, sizeof(kConditionCountPrologue),
              reinterpret_cast<void*>(&hkGetDevelopConditionCount),
              reinterpret_cast<void**>(&g_OrigConditionCount) },
            { "GetDevConditionMsgIDArray", targets.conditionMsgIds,
              kConditionMsgIdPrologue, sizeof(kConditionMsgIdPrologue),
              reinterpret_cast<void*>(&hkGetDevConditionMsgIDArray),
              reinterpret_cast<void**>(&g_OrigConditionMsgIds) },
        };

        for (std::size_t i = 0; i < kHookCount; ++i)
        {
            void* fn = ResolveGameAddress(hooks[i].address);
            if (!fn || !PrologueMatchesSEH(fn, hooks[i].prologue, hooks[i].prologueSize))
            {
                LogError("DevelopBluePrint: %s prologue mismatch on %s, custom blueprints not listed as R&D requirements",
                    hooks[i].name, GetGameBuildName(gGameBuild));
                RemoveHooks();
                return false;
            }
            if (!CreateAndEnableHook(fn, hooks[i].detour, hooks[i].original))
            {
                LogError("DevelopBluePrint: %s hook failed on %s, custom blueprints not listed as R&D requirements",
                    hooks[i].name, GetGameBuildName(gGameBuild));
                RemoveHooks();
                return false;
            }
            g_HookedTargets[i] = fn;
        }
        return true;
    }

    void Uninstall_EquipDevelopControllerImpl_IsDisplayableBluePrintIDContion()
    {
        RemoveHooks();
    }
}
