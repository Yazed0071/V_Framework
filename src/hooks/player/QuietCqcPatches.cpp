#include "pch.h"

#include "QuietCqcPatches.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

using AddrSet = ::AddressSetRuntime::AddressSet;

namespace
{
    constexpr int kPatchCount = 4;

    constexpr int kIdxChokeHold         = 0;
    constexpr int kIdxInterrogate       = 1;
    constexpr int kIdxInterrogateBypass = 2;
    constexpr int kIdxHoldupInterrogate = 3;

    struct PatchDef
    {
        const char*               name;
        std::uintptr_t AddrSet::* addr;
        std::size_t               len;
        std::size_t               writeOffset;
        std::size_t               writeLen;
        std::uint8_t              orig[8];
        std::uint8_t              patched[8];
    };

    const PatchDef kPatches[kPatchCount] =
    {
        { "ChokeHold", &AddrSet::QuietChokeHold, 4, 0, 4,
          { 0x80, 0x3C, 0x06, 0x74 },
          { 0x48, 0x39, 0xC6, 0x90 } },

        { "Interrogate", &AddrSet::QuietInterrogate, 5, 0, 5,
          { 0x41, 0x80, 0x3C, 0x0F, 0x74 },
          { 0x4C, 0x39, 0xF9, 0x90, 0x90 } },

        { "InterrogateBypass", &AddrSet::QuietInterrogateBypass, 8, 7, 1,
          { 0x66, 0x83, 0xB8, 0x88, 0x00, 0x00, 0x00, 0x06 },
          { 0x66, 0x83, 0xB8, 0x88, 0x00, 0x00, 0x00, 0x69 } },

        { "HoldupInterrogate", &AddrSet::QuietHoldupInterrogate, 4, 3, 1,
          { 0x80, 0x3C, 0x18, 0x06 },
          { 0x80, 0x3C, 0x18, 0x69 } },
    };

    const int kHoldCqcGroup[]     = { kIdxChokeHold };
    const int kInterrogateGroup[] = { kIdxInterrogate, kIdxInterrogateBypass,
                                      kIdxHoldupInterrogate };

    static bool g_Want[kPatchCount]    = {};
    static bool g_Applied[kPatchCount] = {};
    static bool g_GuardHolding         = false;
    static bool g_FailLogged[kPatchCount] = {};

    static bool WriteBytes(void* target, const std::uint8_t* src, std::size_t len)
    {
        DWORD oldProtect = 0;
        if (!VirtualProtect(target, len, PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;

        std::memcpy(target, src, len);

        DWORD restored = 0;
        VirtualProtect(target, len, oldProtect, &restored);
        FlushInstructionCache(GetCurrentProcess(), target, len);
        return true;
    }

    static bool HasAddress(int index)
    {
        return gAddr.*(kPatches[index].addr) != 0;
    }

    static bool ApplyOne(int index, bool on)
    {
        if (g_Applied[index] == on) return true;

        const PatchDef& p = kPatches[index];

        const std::uintptr_t rva = gAddr.*(p.addr);
        if (!rva)
        {
            if (!g_FailLogged[index])
                LogWarn("QuietCqc: %s address missing for this build, not turned %s", p.name, on ? "on" : "off");
            g_FailLogged[index] = true;
            return false;
        }

        auto* base = static_cast<std::uint8_t*>(ResolveGameAddress(rva));
        if (!base)
        {
            if (!g_FailLogged[index])
                LogWarn("QuietCqc: %s address resolve failed, not turned %s", p.name, on ? "on" : "off");
            g_FailLogged[index] = true;
            return false;
        }

        const std::uint8_t* expect = on ? p.orig : p.patched;
        if (std::memcmp(base, expect, p.len) != 0)
        {
            if (!g_FailLogged[index])
                LogWarn("QuietCqc: %s bytes mismatch at %p, not turned %s",
                    p.name, static_cast<void*>(base), on ? "on" : "off");
            g_FailLogged[index] = true;
            return false;
        }

        const std::uint8_t* src = (on ? p.patched : p.orig) + p.writeOffset;
        if (!WriteBytes(base + p.writeOffset, src, p.writeLen))
        {
            if (!g_FailLogged[index])
                LogWarn("QuietCqc: %s VirtualProtect failed (err=%lu), not turned %s",
                    p.name, GetLastError(), on ? "on" : "off");
            g_FailLogged[index] = true;
            return false;
        }

        g_Applied[index] = on;
        g_FailLogged[index] = false;
        return true;
    }

    static bool AnyWanted()
    {
        for (int i = 0; i < kPatchCount; ++i)
            if (g_Want[i]) return true;
        return false;
    }

    static bool Reconcile()
    {
        const bool blocked = MissionCodeGuard::ShouldBypassHooks();

        bool ok = true;
        for (int i = 0; i < kPatchCount; ++i)
            if (!ApplyOne(i, g_Want[i] && !blocked))
                ok = false;

        if (blocked && !g_GuardHolding && AnyWanted())
            LogWarn("QuietCqc: FOB mission live, patches reverted until it ends");

        g_GuardHolding = blocked;
        return ok;
    }

    static bool SetGroup(const int* group, int count, bool enable,
                         const char* label)
    {
        if (enable && MissionCodeGuard::ShouldBypassHooks())
        {
            LogWarn("QuietCqc: %s refused during an FOB mission", label);
            return false;
        }

        if (enable)
        {
            for (int i = 0; i < count; ++i)
            {
                if (HasAddress(group[i])) continue;
                LogWarn("QuietCqc: %s needs %s, address missing for this build, nothing changed",
                    label, kPatches[group[i]].name);
                return false;
            }
        }

        bool prev[kPatchCount];
        for (int i = 0; i < kPatchCount; ++i)
            prev[i] = g_Want[i];

        for (int i = 0; i < count; ++i)
            g_Want[group[i]] = enable;

        if (Reconcile()) return true;

        for (int i = 0; i < kPatchCount; ++i)
            g_Want[i] = prev[i];
        Reconcile();

        LogWarn("QuietCqc: %s could not be turned %s, rolled back", label, enable ? "on" : "off");
        return false;
    }
}

bool QuietCqc_SetHoldCqc(bool enable)
{
    return SetGroup(kHoldCqcGroup,
        static_cast<int>(sizeof(kHoldCqcGroup) / sizeof(kHoldCqcGroup[0])),
        enable, "the Quiet CQC hold");
}

bool QuietCqc_SetInterrogate(bool enable)
{
    return SetGroup(kInterrogateGroup,
        static_cast<int>(sizeof(kInterrogateGroup) / sizeof(kInterrogateGroup[0])),
        enable, "Quiet interrogation");
}

void QuietCqc_EnforceMissionGuard()
{
    for (int i = 0; i < kPatchCount; ++i)
    {
        if (!g_Want[i] && !g_Applied[i]) continue;
        Reconcile();
        return;
    }
}

bool Install_QuietCqcPatches()
{
    return true;
}

void Uninstall_QuietCqcPatches()
{
    for (int i = 0; i < kPatchCount; ++i)
    {
        g_Want[i] = false;
        ApplyOne(i, false);
    }
    g_GuardHolding = false;
}
