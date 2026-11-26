#include <pch.h>

#include "NoticeSensorImpl_CreateController.h"

#include <atomic>
#include <cstdint>
#include <intrin.h>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "NoticeSensorImpl_Init.h"
#include "log.h"

namespace
{
    using CreateController_t = void*(__fastcall*)(void* sensor, const std::uint32_t* request);

    constexpr std::size_t kSensorControllers = 0x70;   // controllers made so far (sensor = the +0x20 interface)
    constexpr std::size_t kSensorEntries     = 0x74;   // notice entries handed out so far
    constexpr int         kLoggedCalls       = 80;

    const std::uint8_t kCreateControllerEntry[] = { 0x4C, 0x8B, 0xC9, 0x8B, 0x4A, 0x04, 0x81, 0xF9, 0x98, 0x9F, 0x16, 0xBF };

    CreateController_t         g_Orig = nullptr;
    void*                      g_Target = nullptr;
    std::atomic<int>           g_Logged{ 0 };
    std::atomic<std::uint32_t> g_ReportedBuild{ 0 };   // notice table build count + 1 of the last overflow reported

    bool ReadPairSEH(const void* base, std::size_t offset, std::uint32_t& first, std::uint32_t& second)
    {
        __try
        {
            const auto* p = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(base) + offset);
            first = p[0];
            second = p[1];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void* __fastcall hkCreateController(void* sensor, const std::uint32_t* request)
    {
        void* controller = g_Orig(sensor, request);
        std::uint32_t controllers = 0;
        std::uint32_t entries = 0;
        if (!sensor || !ReadPairSEH(sensor, kSensorControllers, controllers, entries))
            return controller;
        const std::uint32_t controllerSlots = GetNoticeSensorControllerSlots();
        const std::uint32_t entrySlots = GetNoticeSensorEntrySlots();
        const std::uint32_t build = GetNoticeSensorBuildCount() + 1;
        if ((controllers > controllerSlots || entries > entrySlots) && g_ReportedBuild.exchange(build) != build)
            LogError("NoticeSensor: mission %u: game objects asked for %u notice entries in %u groups, the notice table holds %u in %u "
                     "(too many soldiers, cameras, UAVs or animals): expect a crash",
                     static_cast<unsigned>(MissionCodeGuard::GetCurrentMissionCode()), entries, controllers, entrySlots, controllerSlots);
#ifdef _DEBUG
        std::uint32_t wanted = 0;
        std::uint32_t id = 0;
        if (request && ReadPairSEH(request, 0, wanted, id) && g_Logged.fetch_add(1, std::memory_order_relaxed) < kLoggedCalls)
        {
            const std::uintptr_t caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - GetExeBase() + EXE_PREFERRED_BASE; // as Ghidra shows it
            LogDebug("NoticeSensor: mission %u, game code at 0x%llX asks for %u notice entries (id 0x%08X) -> controllers %u of %u, "
                     "entries %u of %u",
                static_cast<unsigned>(MissionCodeGuard::GetCurrentMissionCode()), static_cast<unsigned long long>(caller),
                wanted, id, controllers, controllerSlots, entries, entrySlots);
        }
#endif
        return controller;
    }
}

bool Install_NoticeSensorImpl_CreateController_Hook()
{
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.NoticeSensorImpl_CreateController);
    if (!target)
        return false;
    const HookEntry state = CheckHookEntry(target, kCreateControllerEntry, sizeof(kCreateControllerEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogWarn("NoticeSensor: CreateController code differs on this build (%s), notice table overflow check off", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("NoticeSensor: CreateController", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkCreateController), reinterpret_cast<void**>(&g_Orig)))
    {
        LogWarn("NoticeSensor: CreateController hook failed, notice table overflow check off");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_NoticeSensorImpl_CreateController_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
}
