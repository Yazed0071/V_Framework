#include "pch.h"
#include "PartIdWiden.h"

#include <Windows.h>
#include <atomic>
#include <cstring>
#include <limits>

#include "AddressSet.h"
#include "HookUtils.h"
#include "NativeCallPatch.h"
#include "log.h"

namespace
{
    struct WidenSite
    {
        std::uint64_t addr;
        const char*   slot;
        std::uint8_t  expect[5];
        std::uint8_t  patch[5];
        int           len;
    };

    static const WidenSite* SitesForBuild()
    {
        static WidenSite sites[] = {
            { 0, "receiver", { 0x44,0x0F,0xB6,0x45,0x00 }, { 0x44,0x0F,0xB7,0x45,0x00 }, 5 },
            { 0, "barrel", { 0x0F,0xB6,0x55,0x01,0x00 }, { 0x0F,0xB7,0x55,0x02,0x00 }, 4 },
            { 0, "magazine", { 0x0F,0xB6,0x45,0x02,0x00 }, { 0x0F,0xB7,0x45,0x04,0x00 }, 4 },
            { 0, "stock", { 0x0F,0xB6,0x45,0x03,0x00 }, { 0x0F,0xB7,0x45,0x06,0x00 }, 4 },
            { 0, "muzzleOption", { 0x0F,0xB6,0x45,0x05,0x00 }, { 0x0F,0xB7,0x45,0x0A,0x00 }, 4 },
            { 0, "sight", { 0x0F,0xB6,0x45,0x06,0x00 }, { 0x0F,0xB7,0x45,0x0C,0x00 }, 4 },
            { 0, "sight2", { 0x0F,0xB6,0x45,0x07,0x00 }, { 0x0F,0xB7,0x45,0x0E,0x00 }, 4 },
            { 0, "laserFlash1", { 0x0F,0xB6,0x45,0x08,0x00 }, { 0x0F,0xB7,0x45,0x10,0x00 }, 4 },
            { 0, "laserFlash2", { 0x0F,0xB6,0x45,0x09,0x00 }, { 0x0F,0xB7,0x45,0x12,0x00 }, 4 },
            { 0, "underBarrel", { 0x0F,0xB6,0x45,0x0A,0x00 }, { 0x0F,0xB7,0x45,0x14,0x00 }, 4 },
            { 0, "underBarrelCmp", { 0x80,0x7D,0x0A,0x06,0x00 }, { 0x83,0x7D,0x14,0x06,0x00 }, 4 },
        };
        sites[0].addr = gAddr.GunPartsDescWide_receiver;
        sites[1].addr = gAddr.GunPartsDescWide_barrel;
        sites[2].addr = gAddr.GunPartsDescWide_magazine;
        sites[3].addr = gAddr.GunPartsDescWide_stock;
        sites[4].addr = gAddr.GunPartsDescWide_muzzleOption;
        sites[5].addr = gAddr.GunPartsDescWide_sight;
        sites[6].addr = gAddr.GunPartsDescWide_sight2;
        sites[7].addr = gAddr.GunPartsDescWide_laserFlash1;
        sites[8].addr = gAddr.GunPartsDescWide_laserFlash2;
        sites[9].addr = gAddr.GunPartsDescWide_underBarrel;
        sites[10].addr = gAddr.GunPartsDescWide_underBarrelCmp;
        for (const auto& site : sites)
            if (!site.addr)
                return nullptr;
        return sites;
    }

    static constexpr int kWidenSiteCount = 11;
    static constexpr int kInstallAttempts = 50;
    static constexpr DWORD kInstallRetryMs = 4;

    static const int kDescWordToRowSlot[11] = { 0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 8 };

    static std::atomic<bool> g_Planned{ false };
    static std::atomic<bool> g_PlanRefused{ false };
    static std::atomic<bool> g_Active{ false };
    static std::atomic<bool> g_Poisoned{ false };
    static SRWLOCK g_UseLock = SRWLOCK_INIT;
    static SRWLOCK g_PlanLock = SRWLOCK_INIT;
    static thread_local unsigned int g_ReadDepth = 0;
    static NativeCodePatch g_Patches[kWidenSiteCount]{};
    static bool g_Prepared = false;

    struct ExclusiveGuard
    {
        bool held;

        ExclusiveGuard() : held(g_ReadDepth == 0
            && TryAcquireSRWLockExclusive(&g_UseLock) != FALSE) {}

        ~ExclusiveGuard()
        {
            if (held)
                ReleaseSRWLockExclusive(&g_UseLock);
        }
    };

    static bool ReadSiteBytes(const void* address, std::uint8_t* out, int len)
    {
        __try
        {
            std::memcpy(out, address, static_cast<size_t>(len));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool PreparePatches(const void*& quietBegin, const void*& quietEnd)
    {
        const WidenSite* sites = SitesForBuild();
        const auto target = reinterpret_cast<DWORD64>(
            ResolveGameAddress(gAddr.EquipSystem_SetUpGunInfoFromGunPartsDesc));
        if (!sites || !target)
            return false;
        DWORD64 imageBase = 0;
        const auto function = RtlLookupFunctionEntry(target, &imageBase, nullptr);
        if (!function || function->BeginAddress >= function->EndAddress
            || imageBase > (std::numeric_limits<DWORD64>::max)() - function->EndAddress)
            return false;
        const auto begin = imageBase + function->BeginAddress;
        const auto end = imageBase + function->EndAddress;
        if (target < begin || target >= end)
            return false;
        for (int i = 0; i < kWidenSiteCount; ++i)
        {
            void* address = ResolveGameAddress(sites[i].addr);
            const auto at = reinterpret_cast<DWORD64>(address);
            if (at < begin || at >= end || static_cast<DWORD64>(sites[i].len) > end - at)
                return false;
            if (!g_Prepared)
            {
                auto& patch = g_Patches[i];
                patch.target = address;
                patch.size = static_cast<unsigned char>(sites[i].len);
                std::memcpy(patch.expected, sites[i].expect, patch.size);
                std::memcpy(patch.replacement, sites[i].patch, patch.size);
            }
            else if (g_Patches[i].target != address)
                return false;
        }
        g_Prepared = true;
        quietBegin = reinterpret_cast<const void*>(begin);
        quietEnd = reinterpret_cast<const void*>(end);
        return true;
    }

    static bool PlanLocked()
    {
        if (g_Planned.load(std::memory_order_acquire))
            return true;
        if (g_PlanRefused.load(std::memory_order_acquire))
            return false;
        const void* begin = nullptr;
        const void* end = nullptr;
        if (!PreparePatches(begin, end))
        {
            g_PlanRefused.store(true, std::memory_order_release);
            LogError("PartIdWiden: patch sites unverified for %s, part ids capped at 255",
                GetGameBuildName(gGameBuild));
            return false;
        }
        const WidenSite* sites = SitesForBuild();
        for (int i = 0; i < kWidenSiteCount; ++i)
        {
            const auto& patch = g_Patches[i];
            std::uint8_t current[6]{};
            const std::uint8_t* wanted = patch.installed ? patch.replacement : patch.expected;
            if (!ReadSiteBytes(patch.target, current, patch.size)
                || std::memcmp(current, wanted, patch.size) != 0)
            {
                g_PlanRefused.store(true, std::memory_order_release);
                LogError("PartIdWiden: site %s holds %02X %02X %02X %02X %02X, patched by another mod, part ids capped at 255",
                    sites[i].slot, current[0], current[1], current[2], current[3], current[4]);
                return false;
            }
        }
        g_Planned.store(true, std::memory_order_release);
        return true;
    }

    static bool Operate(bool install, DWORD& error)
    {
        error = ERROR_SUCCESS;
        ExclusiveGuard gate;
        if (!gate.held)
        {
            error = ERROR_BUSY;
            return false;
        }
        if (g_Poisoned.load(std::memory_order_acquire))
        {
            error = ERROR_PARTIAL_COPY;
            return false;
        }
        if (g_Active.load(std::memory_order_acquire) == install)
            return true;
        const void* begin = nullptr;
        const void* end = nullptr;
        if (!PreparePatches(begin, end))
        {
            error = ERROR_INVALID_ADDRESS;
            return false;
        }
        if (install)
        {
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                    | GET_MODULE_HANDLE_EX_FLAG_PIN,
                    reinterpret_cast<LPCWSTR>(&PartIdWiden_Install), &module))
            {
                error = ERROR_MOD_NOT_FOUND;
                return false;
            }
        }
        const bool okay = install
            ? InstallNativeCodePatches(g_Patches, kWidenSiteCount, &g_Active, begin, end)
            : RemoveNativeCodePatches(g_Patches, kWidenSiteCount, &g_Active, begin, end);
        if (!okay)
        {
            error = GetLastError();
            if (error == ERROR_PARTIAL_COPY)
                g_Poisoned.store(true, std::memory_order_release);
        }
        return okay;
    }

    static void DescribeChangedSite(char* out, size_t cap)
    {
        out[0] = '\0';
        const WidenSite* sites = SitesForBuild();
        if (!sites)
            return;
        for (int i = 0; i < kWidenSiteCount; ++i)
        {
            const auto& patch = g_Patches[i];
            std::uint8_t current[6]{};
            if (!patch.target || !ReadSiteBytes(patch.target, current, patch.size)
                || std::memcmp(current, patch.expected, patch.size) == 0)
                continue;
            if (std::memcmp(current, patch.replacement, patch.size) == 0)
                sprintf_s(out, cap, " at site %s, already widened by other code", sites[i].slot);
            else
                sprintf_s(out, cap, " at site %s, now %02X %02X %02X %02X", sites[i].slot,
                    current[0], current[1], current[2], current[3]);
            return;
        }
    }

    static bool OperateWithRetry(bool install)
    {
        DWORD error = ERROR_SUCCESS;
        for (int attempt = 0; attempt < kInstallAttempts; ++attempt)
        {
            if (Operate(install, error))
                return true;
            if (!NativePatchErrorIsTransient(error))
                break;
            Sleep(kInstallRetryMs);
        }
        char where[96] = "";
        if (error == ERROR_INVALID_DATA)
            DescribeChangedSite(where, sizeof(where));
        LogError("PartIdWiden: %s failed: %s (%lu)%s, %s",
            install ? "install" : "remove", NativePatchErrorName(error), error, where,
            install ? "part ids above 255 use byte aliases" : "wide reads stay patched");
        return false;
    }
}

PartIdWidenReadGuard::PartIdWidenReadGuard() noexcept
{
    if (g_ReadDepth == 0)
        AcquireSRWLockShared(&g_UseLock);
    ++g_ReadDepth;
    usable = !g_Poisoned.load(std::memory_order_acquire);
}

PartIdWidenReadGuard::~PartIdWidenReadGuard() noexcept
{
    if (--g_ReadDepth == 0)
        ReleaseSRWLockShared(&g_UseLock);
}

PartIdWidenReadGuard::operator bool() const noexcept
{
    return usable;
}

bool PartIdWiden_Plan()
{
    AcquireSRWLockExclusive(&g_PlanLock);
    const bool planned = PlanLocked();
    ReleaseSRWLockExclusive(&g_PlanLock);
    return planned;
}

bool PartIdWiden_IsPlanned()
{
    return g_Planned.load(std::memory_order_acquire)
        && !g_Poisoned.load(std::memory_order_acquire);
}

bool PartIdWiden_IsActive()
{
    return g_Active.load(std::memory_order_acquire)
        && !g_Poisoned.load(std::memory_order_acquire);
}

int PartIdWiden_MaxPartId()
{
    return (PartIdWiden_IsPlanned() || PartIdWiden_IsActive()) ? 0xFFFF : 0xFF;
}

bool PartIdWiden_BuildWideDesc(const unsigned char* narrowDesc,
                               const int* wideIds12,
                               std::uint8_t* out24)
{
    if (!out24)
        return false;
    std::memset(out24, 0, 24);
    for (int word = 0; word < 11; ++word)
    {
        const int slot = kDescWordToRowSlot[word];
        int id = 0;
        if (wideIds12 && wideIds12[slot] > 0)
            id = wideIds12[slot];
        else if (narrowDesc)
            id = narrowDesc[word];
        if (id < 0)
            id = 0;
        if (id > 0xFFFF)
            id = 0xFFFF;
        out24[word * 2 + 0] = static_cast<std::uint8_t>(id & 0xFF);
        out24[word * 2 + 1] = static_cast<std::uint8_t>((id >> 8) & 0xFF);
    }
    return true;
}

bool PartIdWiden_Install()
{
    if (!PartIdWiden_Plan())
        return false;
    return OperateWithRetry(true);
}

bool PartIdWiden_Uninstall()
{
    if (!g_Active.load(std::memory_order_acquire))
        return true;
    return OperateWithRetry(false);
}
