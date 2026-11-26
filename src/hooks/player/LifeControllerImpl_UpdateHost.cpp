#include <pch.h>
#include "LifeControllerImpl_UpdateHost.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "NativeCallPatch.h"
#include "log.h"

namespace
{
    constexpr float kVanillaDelay              = 4.0f; // seconds after a hit before life starts coming back
    constexpr float kVanillaDoubledDamageDelay = 6.0f; // same delay when the game doubles damage taken

    const std::uint8_t kDelayLoad[]              = { 0xF3, 0x0F, 0x10, 0x3D };       // movss xmm7,[rip+disp]
    const std::uint8_t kDoubledDamageDelayLoad[] = { 0xF3, 0x44, 0x0F, 0x10, 0x2D }; // movss xmm13,[rip+disp]

    NativeCodePatch g_Patches[2]{};
    float*          g_Delays = nullptr; // [0] normal, [1] doubled damage; UpdateHost now loads these

    void DelayLoadAddrs(std::uintptr_t& delay, std::uintptr_t& doubledDamageDelay)
    {
        delay = gAddr.LifeControllerImpl_UpdateHost_DelayLoad;
        doubledDamageDelay = gAddr.LifeControllerImpl_UpdateHost_DoubledDamageDelayLoad;
        return;
    }

    bool ReadDelayLoadSEH(const std::uint8_t* at, const std::uint8_t* opcode, std::size_t opcodeSize,
                          float expected, std::int32_t& disp)
    {
        __try
        {
            if (std::memcmp(at, opcode, opcodeSize) != 0)
                return false;
            std::memcpy(&disp, at + opcodeSize, sizeof(disp));
            return *reinterpret_cast<const float*>(at + opcodeSize + sizeof(disp) + disp) == expected;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool PrepareDispPatch(NativeCodePatch& patch, std::uint8_t* load, std::size_t opcodeSize,
                          std::int32_t disp, const float* slot)
    {
        const std::uintptr_t next = reinterpret_cast<std::uintptr_t>(load) + opcodeSize + sizeof(disp);
        const std::int64_t rel = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(slot))
                               - static_cast<std::int64_t>(next);
        if (rel < INT32_MIN || rel > INT32_MAX)
            return false;

        const std::int32_t newDisp = static_cast<std::int32_t>(rel);
        patch = NativeCodePatch{};
        patch.target = load + opcodeSize;
        patch.size = sizeof(disp);
        std::memcpy(patch.expected, &disp, sizeof(disp));
        std::memcpy(patch.replacement, &newDisp, sizeof(newDisp));
        return true;
    }
}

bool Install_LifeControllerImpl_UpdateHost()
{
    std::uintptr_t delayAddr = 0;
    std::uintptr_t doubledAddr = 0;
    DelayLoadAddrs(delayAddr, doubledAddr);
    if (delayAddr == 0)
        return true;

    auto* delay   = static_cast<std::uint8_t*>(ResolveGameAddress(delayAddr));
    auto* doubled = static_cast<std::uint8_t*>(ResolveGameAddress(doubledAddr));
    std::int32_t delayDisp = 0;
    std::int32_t doubledDisp = 0;
    if (!delay || !doubled
        || !ReadDelayLoadSEH(delay, kDelayLoad, sizeof(kDelayLoad), kVanillaDelay, delayDisp)
        || !ReadDelayLoadSEH(doubled, kDoubledDamageDelayLoad, sizeof(kDoubledDamageDelayLoad),
                             kVanillaDoubledDamageDelay, doubledDisp))
    {
        LogError("LifeRecovery: LifeControllerImpl::UpdateHost does not match %s, SetLifeRecoveryCooldownTimer is unavailable",
            GetGameBuildName(gGameBuild));
        return false;
    }

    g_Delays = static_cast<float*>(HookArena::AllocateNear(reinterpret_cast<std::uintptr_t>(delay), 2 * sizeof(float)));
    if (!g_Delays
        || !PrepareDispPatch(g_Patches[0], delay, sizeof(kDelayLoad), delayDisp, &g_Delays[0])
        || !PrepareDispPatch(g_Patches[1], doubled, sizeof(kDoubledDamageDelayLoad), doubledDisp, &g_Delays[1]))
    {
        LogError("LifeRecovery: no data page near LifeControllerImpl::UpdateHost, SetLifeRecoveryCooldownTimer is unavailable");
        return false;
    }
    g_Delays[0] = kVanillaDelay;
    g_Delays[1] = kVanillaDoubledDamageDelay;

    for (int attempt = 0; attempt < 50; ++attempt)
    {
        if (InstallNativeCodePatches(g_Patches, 2))
            return true;
        if (!NativePatchErrorIsTransient(GetLastError()))
            break;
        Sleep(4);
    }
    LogError("LifeRecovery: LifeControllerImpl::UpdateHost patch refused (%s), SetLifeRecoveryCooldownTimer is unavailable",
        NativePatchErrorName(GetLastError()));
    return false;
}

void Uninstall_LifeControllerImpl_UpdateHost()
{
    if (g_Patches[0].installed || g_Patches[1].installed)
        RemoveNativeCodePatches(g_Patches, 2);
}

bool LifeRecoveryCooldown_Set(float seconds)
{
    if (!g_Patches[0].installed || !g_Patches[1].installed)
        return false;
    g_Delays[0] = seconds;
    g_Delays[1] = seconds * (kVanillaDoubledDamageDelay / kVanillaDelay);
    return true;
}

bool LifeRecoveryCooldown_Get(float& seconds)
{
    if (!g_Patches[0].installed || !g_Patches[1].installed)
        return false;
    seconds = g_Delays[0];
    return true;
}

void LifeRecoveryCooldown_Reset()
{
    if (!g_Patches[0].installed || !g_Patches[1].installed)
        return;
    g_Delays[0] = kVanillaDelay;
    g_Delays[1] = kVanillaDoubledDamageDelay;
}
