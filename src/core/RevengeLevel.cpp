#include <pch.h>

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "RevengeLevel.h"
#include "log.h"

namespace
{
    struct VarHandle   // ScriptDeclVarHandle
    {
        std::uint8_t* base   = nullptr;
        std::uint16_t count  = 0;
        std::uint16_t offset = 0;
        std::uint8_t  type   = 0;
        std::uint8_t  pad[3] = {};
    };
    using GetQuarkSystemTable_t = std::uint8_t* (__fastcall*)();
    using GetVarHandle_t        = VarHandle* (__fastcall*)(void* scriptSystem, VarHandle* out, std::uint32_t group, std::uint32_t name);

    constexpr std::size_t   kQuarkTableApplication = 0x98;
    constexpr std::size_t   kApplicationScripts    = 0x18;         // ScriptSystem, next to the ScriptVars PlayerAlertPhase reads
    constexpr std::size_t   kGetVarHandleSlot      = 0;            // ScriptSystemImpl::GetScriptDeclVarHandle
    constexpr std::uint32_t kGvars                 = 0xE1EDE3DE;   // "gvars"
    constexpr std::uint32_t kRevengeLevels         = 0xC80D4E07;   // "rev_revengeLv"
    constexpr std::uint16_t kRevengeTypes          = 16;           // TppRevenge.REVENGE_TYPE.MAX
    constexpr std::uint8_t  kTypeUint8             = 5;
    constexpr std::uint16_t kStealth               = 0;            // TppRevenge.REVENGE_TYPE.STEALTH
    constexpr std::uint8_t  kChecksGvars[]         = { 0x41, 0x81, 0xF8, 0xDE, 0xE3, 0xED, 0xE1 };   // cmp r8d, "gvars"
    constexpr std::size_t   kFingerprintBytes      = 0x20;

    std::atomic<bool> g_Broken{ false };

    bool ChecksGvarsGroup(const std::uint8_t* function)   // the slot we call really is GetScriptDeclVarHandle
    {
        for (std::size_t i = 0; i + sizeof(kChecksGvars) <= kFingerprintBytes; ++i)
        {
            std::size_t matched = 0;
            while (matched < sizeof(kChecksGvars) && function[i + matched] == kChecksGvars[matched])
                ++matched;
            if (matched == sizeof(kChecksGvars))
                return true;
        }
        return false;
    }

    int ReadStealth(GetQuarkSystemTable_t quarkTable, bool& wrongShape)
    {
        __try
        {
            std::uint8_t* table = quarkTable();
            if (!table)
                return -1;
            std::uint8_t* application = *reinterpret_cast<std::uint8_t**>(table + kQuarkTableApplication);
            if (!application)
                return -1;
            void* scripts = *reinterpret_cast<void**>(application + kApplicationScripts);
            if (!scripts)
                return -1;
            const auto getVarHandle = (*reinterpret_cast<GetVarHandle_t* const*>(scripts))[kGetVarHandleSlot];
            if (!ChecksGvarsGroup(reinterpret_cast<const std::uint8_t*>(getVarHandle)))
            {
                wrongShape = true;
                return -1;
            }
            VarHandle handle;
            getVarHandle(scripts, &handle, kGvars, kRevengeLevels);
            if (!handle.base)
                return -1;   // gvars aren't declared yet
            if (handle.count != kRevengeTypes || (handle.type & 7) != kTypeUint8)
            {
                wrongShape = true;
                return -1;
            }
            return handle.base[handle.offset + kStealth];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }
}

int RevengeLevel::Stealth()
{
    if (g_Broken.load(std::memory_order_relaxed))
        return -1;
    const auto quarkTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.Fox_GetQuarkSystemTable));
    if (!quarkTable)
        return -1;
    bool wrongShape = false;
    const int level = ReadStealth(quarkTable, wrongShape);
    if (wrongShape && !g_Broken.exchange(true))
        LogError("RevengeLevel: gvars.rev_revengeLv isn't where it is on 1.0.15.4a, the STEALTH revenge level reads as unknown");
    return level;
}
