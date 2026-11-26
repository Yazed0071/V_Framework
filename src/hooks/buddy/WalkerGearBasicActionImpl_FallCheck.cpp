#include <pch.h>

#include "WalkerGearBasicActionImpl_FallCheck.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "BattleGearLegs.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "WalkerGearBasicActionImpl_CorrectVelocity.h"
#include "log.h"

namespace
{
    using FallCheck_t    = bool(__fastcall*)(std::uint8_t* action, std::uint8_t* work, std::uint32_t index);
    using GetComm_t      = const std::uint8_t*(__fastcall*)(void* system, std::uint16_t commId);
    using GetTransform_t = const std::uint8_t*(__fastcall*)(void* animation, std::uint32_t index);

    constexpr std::size_t   kSystem            = 0x70;
    constexpr std::size_t   kAnimation         = 0x80;
    constexpr std::size_t   kWorkCommId        = 0xF0;
    constexpr std::size_t   kGetCommSlot       = 0xF8 / sizeof(void*);
    constexpr std::size_t   kGetTransformSlot  = 0x388 / sizeof(void*);
    constexpr std::size_t   kTransformPosition = 0x20;
    constexpr std::size_t   kControlHeight     = 0x160;       // metres the walker's position rides over the ground
    constexpr std::uint32_t kFallGroundFlags   = 0x4030700;   // what the game's own test casts its 1.5 m ray against

    const std::uint8_t kFallCheckEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00 };

    FallCheck_t g_Orig = nullptr;
    void*       g_Target = nullptr;
#ifdef _DEBUG
    ULONGLONG   g_NextLog = 0;   // TEMP probe: the drops the Battle Gear walked off, and the ones it fell
#endif

    bool BattleGearPlaceSEH(const std::uint8_t* action, const std::uint8_t* work, std::uint32_t index, float* place, float* height)
    {
        __try
        {
            void* system = *reinterpret_cast<void* const*>(action + kSystem);
            void* animation = *reinterpret_cast<void* const*>(action + kAnimation);
            if (!system || !animation)
                return false;
            const auto getComm = reinterpret_cast<GetComm_t>((*static_cast<void* const* const*>(system))[kGetCommSlot]);
            const auto getTransform = reinterpret_cast<GetTransform_t>((*static_cast<void* const* const*>(animation))[kGetTransformSlot]);
            if (!battleGearWeapons::IsBattleGearComm(getComm(system, *reinterpret_cast<const std::uint16_t*>(work + kWorkCommId))))
                return false;
            const std::uint8_t* transform = getTransform(animation, index);
            if (!transform)
                return false;
            const float* p = reinterpret_cast<const float*>(transform + kTransformPosition);
            for (int k = 0; k < 3; ++k)
                place[k] = p[k];
            *height = *reinterpret_cast<const float*>(transform + kControlHeight);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool __fastcall hkFallCheck(std::uint8_t* action, std::uint8_t* work, std::uint32_t index)
    {
        const bool falls = g_Orig(action, work, index);
        if (!falls || !battleGearWeapons::Ridden())
            return falls;
        float place[3];
        float height = 0.0f;
        float ground = 0.0f;
        if (!BattleGearPlaceSEH(action, work, index, place, &height))
            return falls;
        const bool reached = BattleGearGroundUnder(place, height + kBattleGearDeepestDrop, &ground);
#ifdef _DEBUG
        const ULONGLONG now = GetTickCount64();
        if (now >= g_NextLog && (!reached || place[1] - ground - height > 0.3f))   // the game's own test always says fall: the
        {                                                                          // ridden body rides higher than it looks
            g_NextLog = now + 500;
            if (reached)
                LogDebug("BattleGear drop: the ground lies %.2f m under the walker's feet, walked off without the drop clip",
                         place[1] - ground - height);
            else
                LogDebug("BattleGear drop: no ground within %.1f m under the walker's feet, the game's fall takes it", kBattleGearDeepestDrop);
        }
#endif
        return !reached;
    }
}

bool BattleGearFallCheckHooked()
{
    return g_Target != nullptr;
}

bool BattleGearGroundUnder(const float* position, float below, float* ground)
{
    const float bottom[3] = { position[0], position[1] - below, position[2] };
    float hit[3];
    if (!battleGearLegs::FirstHit(position, bottom, kFallGroundFlags, hit))
        return false;
    *ground = hit[1];
    return true;
}

bool Install_WalkerGearBasicActionImpl_FallCheck_Hook()
{
    if (g_Target)
        return true;
    void* target = gAddr.WalkerGearBasicActionImpl_FallCheck ? ResolveGameAddress(gAddr.WalkerGearBasicActionImpl_FallCheck) : nullptr;
    if (!target)
    {
        LogError("BattleGear: walker fall test address missing on this build, the Battle Gear stops at every drop over 0.7 m");
        return false;
    }
    const HookEntry state = CheckHookEntry(target, kFallCheckEntry, sizeof(kFallCheckEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("BattleGear: walker fall test code differs on this build (%s), the Battle Gear stops at every drop over 0.7 m", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: walker FallCheck", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkFallCheck), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BattleGear: walker fall test hook failed, the Battle Gear stops at every drop over 0.7 m");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_WalkerGearBasicActionImpl_FallCheck_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
}
