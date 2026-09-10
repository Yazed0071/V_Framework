#include "pch.h"
#include "ShellImpl_ActivateShellAtEmptyWork.h"

#include <Windows.h>
#include <cstdint>
#include <mutex>
#include <set>

#include "AddressSet.h"
#include "EquipPartParams.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    constexpr std::size_t kDescEquipId  = 0x2e;
    constexpr std::size_t kShellIfaceA  = 0x140;
    constexpr std::size_t kShellIfaceB  = 0x08;
    constexpr unsigned int kScanLast    = 191;
    constexpr unsigned int kNoShellIndex = 0x3F;

    using ActivateShell_t = unsigned int(__fastcall*)(void*, void*);
    using Lookup_t        = unsigned int(__fastcall*)(void*, unsigned int);

    ActivateShell_t g_Orig = nullptr;
    void*           g_Target = nullptr;
    std::mutex      g_Mutex;
    std::set<unsigned int> g_Logged;

    void* ReadPtrSEH(const void* base, std::size_t offset)
    {
        __try
        {
            return *reinterpret_cast<void* const*>(
                static_cast<const std::uint8_t*>(base) + offset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    int ReadU16SEH(const void* base, std::size_t offset)
    {
        __try
        {
            return *reinterpret_cast<const std::uint16_t*>(
                static_cast<const std::uint8_t*>(base) + offset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    bool WriteU16SEH(void* base, std::size_t offset, std::uint16_t value)
    {
        __try
        {
            *reinterpret_cast<std::uint16_t*>(
                static_cast<std::uint8_t*>(base) + offset) = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    unsigned int CallLookupSEH(void* iface, unsigned int id)
    {
        __try
        {
            void** vtbl = *reinterpret_cast<void***>(iface);
            if (!vtbl)
                return 0;
            auto fn = reinterpret_cast<Lookup_t>(vtbl[1]);
            if (!fn)
                return 0;
            return fn(iface, id) & 0xffffu;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void* ResolveIface(void* self)
    {
        void* a = ReadPtrSEH(self, kShellIfaceA);
        return a ? ReadPtrSEH(a, kShellIfaceB) : nullptr;
    }

    unsigned int FirstResolvingId(void* iface, unsigned int failedId)
    {
        for (unsigned int id = 1; id <= kScanLast; ++id)
        {
            if (id == failedId)
                continue;
            if (CallLookupSEH(iface, id) != 0)
                return id;
        }
        return 0;
    }

    unsigned int __fastcall hkActivateShellAtEmptyWork(void* self, void* desc)
    {
        if (!self || !desc || !g_Orig)
            return g_Orig ? g_Orig(self, desc) : kNoShellIndex;

        const int rawId = ReadU16SEH(desc, kDescEquipId);
        void* iface = ResolveIface(self);
        if (rawId <= 0 || !iface)
            return g_Orig(self, desc);

        const unsigned int id = static_cast<unsigned int>(rawId);
        if (CallLookupSEH(iface, id) != 0)
            return g_Orig(self, desc);

        unsigned int replacement = 0;
        int matchedBullet = 0;
        const int bulletId = EquipParam_GetMagazineBulletId(static_cast<int>(id));
        const int twin = EquipParam_FindVanillaMagazineByBullet(bulletId,
                                                                static_cast<int>(id));
        if (twin > 0 && CallLookupSEH(iface, static_cast<unsigned int>(twin)) != 0)
        {
            replacement = static_cast<unsigned int>(twin);
            matchedBullet = bulletId;
        }
        else
        {
            replacement = FirstResolvingId(iface, id);
        }

        bool first = false;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            first = g_Logged.insert(id).second;
        }

        if (replacement == 0)
        {
            if (first)
                Log("[ShellGuard] magazine id %u has no row in the engine's shell table and no "
                    "vanilla magazine carries its bullet %d, so nothing sane can stand in - the "
                    "shot is dropped instead of faulting. Give the magazine a vanilla bulletId, "
                    "or a bulletId a vanilla magazine also uses\n",
                    id, bulletId);
            return kNoShellIndex;
        }

        if (first)
        {
            if (matchedBullet > 0)
                Log("[ShellGuard] magazine id %u is past the engine's %d-entry shell table, so its "
                    "projectile had no parameters - borrowing vanilla magazine %u, the one that "
                    "fires the same bullet %d, so the shot flies and detonates like that round\n",
                    id, kScanLast, replacement, matchedBullet);
            else
                Log("[ShellGuard] magazine id %u is past the engine's shell table and no vanilla "
                    "magazine fires its bullet %d - falling back to magazine %u, whose projectile "
                    "parameters are arbitrary; set a vanilla bulletId to get the right ones\n",
                    id, bulletId, replacement);
        }

        if (!WriteU16SEH(desc, kDescEquipId, static_cast<std::uint16_t>(replacement)))
            return kNoShellIndex;
        const unsigned int r = g_Orig(self, desc);
        WriteU16SEH(desc, kDescEquipId, static_cast<std::uint16_t>(id));
        return r;
    }
}

bool ShellActivateGuard_Install()
{
    if (!gAddr.Shell_ActivateShellAtEmptyWork)
        return true;

    void* target = ResolveGameAddress(gAddr.Shell_ActivateShellAtEmptyWork);
    if (!target)
        return true;

    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkActivateShellAtEmptyWork),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        Log("[ShellGuard] ERROR: ActivateShellAtEmptyWork hook failed at %p - firing a launcher "
            "whose shell id has no equip row still faults inside the engine\n", target);
        return false;
    }
    g_Target = target;
    return true;
}

void ShellActivateGuard_Uninstall()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
}
