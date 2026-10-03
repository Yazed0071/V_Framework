#include "pch.h"
#include "Bullet3Impl_ActivateBulletAtEmptyWorkPatch.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"

namespace
{
    static bool   g_Enabled     = false;
    // AttackActionImpl::ShootOneBullet
    static bool   g_MaskApplied = false;
    static void*  g_MaskSite     = nullptr;
    // Bullet3Impl::ActivateBulletAtEmptyWork
    static bool   g_DmgApplied  = false;
    static void*  g_DmgSite      = nullptr;

    static bool WriteBytes(void* target, const std::uint8_t* src, std::size_t size)
    {
        DWORD oldProtect = 0;
        if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            LogError("FriendlyFire: VirtualProtect failed (err %lu), patch not written", GetLastError());
            return false;
        }
        std::memcpy(target, src, size);
        DWORD restored = 0;
        VirtualProtect(target, size, oldProtect, &restored);
        FlushInstructionCache(GetCurrentProcess(), target, size);
        return true;
    }
}

static void PatchSite(std::uintptr_t addr, const char* addrName,
                      const std::uint8_t* orig, const std::uint8_t* patch, std::size_t size,
                      bool enable, bool& applied, void*& savedSite, const char* name)
{
    if (!addr)
    {
        if (enable && !applied)
            LogError("FriendlyFire: %s address missing on %s, %s stays vanilla",
                     addrName, GetGameBuildName(gGameBuild), name);
        return;
    }
    void* site = ResolveGameAddress(addr);
    if (!site)
    {
        LogError("FriendlyFire: %s address not resolved, %s stays vanilla", addrName, name);
        return;
    }
    auto* p = static_cast<std::uint8_t*>(site);
    if (enable && !applied)
    {
        if (std::memcmp(p, orig, size) != 0)
        {
            LogError("FriendlyFire: %s site bytes unexpected, %s stays vanilla", addrName, name);
            return;
        }
        if (WriteBytes(site, patch, size))
        {
            savedSite = site;
            applied   = true;
        }
    }
    else if (!enable && applied)
    {
        if (WriteBytes(savedSite, orig, size))
        {
            applied = false;
            savedSite = nullptr;
        }
    }
}

void Set_FriendlyFire(bool enable)
{
    if (enable && MissionCodeGuard::ShouldBypassHooks())
        enable = false;

    g_Enabled = enable;

    static const std::uint8_t kMaskOrig[3]  = { 0xFF, 0x50, 0x58 }; // CALL qword [RAX+0x58]
    static const std::uint8_t kMaskPatch[3] = { 0x31, 0xC0, 0x90 }; // XOR EAX,EAX ; NOP -> mask = 0
    static const std::uint8_t kDmgOrig[2]   = { 0x75, 0x25 };       // JNZ +0x25
    static const std::uint8_t kDmgPatch[2]  = { 0xEB, 0x25 };       // JMP +0x25

    PatchSite(gAddr.Soldier_ShootOneBullet_GroupMaskCall,
              "Soldier_ShootOneBullet_GroupMaskCall",
              kMaskOrig, kMaskPatch, sizeof(kMaskOrig), enable, g_MaskApplied, g_MaskSite, "collision-mask");

    PatchSite(gAddr.Soldier_ActivateBulletAtEmptyWork_SameArmyJnz,
              "Soldier_ActivateBulletAtEmptyWork_SameArmyJnz",
              kDmgOrig, kDmgPatch, sizeof(kDmgOrig), enable, g_DmgApplied, g_DmgSite, "same-army-damage");
}

bool Get_FriendlyFire()
{
    return g_Enabled;
}
