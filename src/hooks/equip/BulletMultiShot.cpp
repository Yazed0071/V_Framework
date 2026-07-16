#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "AddressSet.h"
#include "BulletLockOn.h"
#include "BulletMultiShot.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    struct MultiShotSpec
    {
        int ammoPerShot = 0;
        int lockAmmoPerShot = 0;
    };

    std::recursive_mutex g_Mutex;
    std::unordered_map<int, MultiShotSpec> g_SpecByBulletId;

    using Fire_t = void(__fastcall*)(void*, std::uint8_t*, std::uint32_t);
    Fire_t g_OrigFire = nullptr;

    int SehKeepAvOnly(unsigned long code)
    {
        return (code == EXCEPTION_ACCESS_VIOLATION
                || code == EXCEPTION_IN_PAGE_ERROR)
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
    }

    bool ReadWorkIdsSEH(std::uint8_t* work, int* outBulletId)
    {
        __try
        {
            if (work[0x3df] == 0)
                return false;
            *outBulletId = work[0x22c];
            return true;
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    bool LookupForWork(std::uint8_t* work, MultiShotSpec* out)
    {
        int bulletId = 0;
        if (!ReadWorkIdsSEH(work, &bulletId))
            return false;
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        auto it = g_SpecByBulletId.find(bulletId);
        if (it == g_SpecByBulletId.end())
            return false;
        *out = it->second;
        return true;
    }

    void ApplyCountSEH(std::uint8_t* work, int rounds)
    {
        __try
        {
            if (work[0x3df] != 0)
                work[0x3df] = static_cast<std::uint8_t>(rounds);
        }
        __except (SehKeepAvOnly(GetExceptionCode()))
        {
        }
    }

    void __fastcall hkFire(void* self, std::uint8_t* work, std::uint32_t idx)
    {
        MultiShotSpec spec{};
        if (LookupForWork(work, &spec))
        {
            int n = spec.ammoPerShot;
            if (spec.lockAmmoPerShot > 0 && equip::LockOn_IsLockedNow())
                n = spec.lockAmmoPerShot;
            if (n > 1)
                ApplyCountSEH(work, n);
        }
        g_OrigFire(self, work, idx);
    }
}

namespace equip
{
    void MultiShot_RegisterBullet(int bulletId, int ammoPerShot,
                                  int lockAmmoPerShot)
    {
        if (bulletId <= 0 || bulletId > 255)
            return;
        MultiShotSpec spec{};
        spec.ammoPerShot = ammoPerShot;
        if (spec.ammoPerShot < 0) spec.ammoPerShot = 0;
        if (spec.ammoPerShot > 255) spec.ammoPerShot = 255;
        spec.lockAmmoPerShot = lockAmmoPerShot;
        if (spec.lockAmmoPerShot < 0) spec.lockAmmoPerShot = 0;
        if (spec.lockAmmoPerShot > 255) spec.lockAmmoPerShot = 255;
        {
            std::lock_guard<std::recursive_mutex> lock(g_Mutex);
            g_SpecByBulletId[bulletId] = spec;
        }
        Log("[MultiShot] bulletId=%d ammoPerShot=%d lockAmmoPerShot=%d "
            "registered\n", bulletId, spec.ammoPerShot, spec.lockAmmoPerShot);
    }

    bool Install_BulletMultiShot_Hooks()
    {
        void* fn = ResolveGameAddress(gAddr.AttackAction_Fire);
        if (!fn)
        {
            Log("[MultiShot] AttackAction_Fire unresolved on this build "
                "- SetBullet ammoPerShot inactive.\n");
            return true;
        }
        const bool ok = CreateAndEnableHook(
            fn, reinterpret_cast<void*>(&hkFire),
            reinterpret_cast<void**>(&g_OrigFire));
        Log("[MultiShot] Fire hook Install -> %s\n", ok ? "OK" : "FAIL");
        return ok;
    }

    void Uninstall_BulletMultiShot_Hooks()
    {
        void* fn = ResolveGameAddress(gAddr.AttackAction_Fire);
        if (fn && g_OrigFire)
            DisableAndRemoveHook(fn);
        g_OrigFire = nullptr;
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        g_SpecByBulletId.clear();
    }
}
