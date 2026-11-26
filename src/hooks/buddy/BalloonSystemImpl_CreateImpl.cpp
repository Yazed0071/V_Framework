#include <pch.h>

#include "BalloonSystemImpl_CreateImpl.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using CreateImpl_t = void*(__fastcall*)(void* system, void* handle, const std::uint8_t* desc);

    // BalloonDesc: world matrix, damage callback and its argument, then
    constexpr std::size_t   kDescPosition = 0x30;
    constexpr std::size_t   kDescOwner    = 0x50;   // u16 game object id; the walkers' own balloons carry none
    constexpr std::size_t   kDescScale    = 0x52;   // row of the balloon scale table: 0 and 1 plain, 2 double, 3 half
    constexpr std::size_t   kDescLevel    = 0x53;   // 0 one balloon, 1 a small second one, 2 four small ones round it
    constexpr std::size_t   kDescKind     = 0x54;
    constexpr std::size_t   kDescSize     = 0x58;
    constexpr std::uint16_t kNoOwner      = 0xFFFF;
    constexpr std::uint8_t  kDoubleSize   = 2;
    constexpr std::uint8_t  kFourRound    = 2;
    constexpr float         kAtRoofGun    = 4.0f;   // metres: the walker's balloon point stands over the turret
    constexpr float         kAtRailMuzzle = 8.0f;

    const std::uint8_t kCreateImplEntry[] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
                                              0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xA0, 0x48, 0x81, 0xEC, 0x60, 0x01, 0x00, 0x00 };

    CreateImpl_t g_Orig = nullptr;
    void*        g_Target = nullptr;

    bool WalkerBalloonAtSEH(const std::uint8_t* desc, const float* point, float reach, std::uint8_t* heavy)
    {
        __try
        {
            if (*reinterpret_cast<const std::uint16_t*>(desc + kDescOwner) != kNoOwner || desc[kDescScale] || desc[kDescLevel]
                || desc[kDescKind])
                return false;
            const float* at = reinterpret_cast<const float*>(desc + kDescPosition);
            float gap = 0.0f;
            for (int i = 0; i < 3; ++i)
                gap += (at[i] - point[i]) * (at[i] - point[i]);
            if (gap > reach * reach)
                return false;
            std::memcpy(heavy, desc, kDescSize);
            heavy[kDescScale] = kDoubleSize;
            heavy[kDescLevel] = kFourRound;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void* __fastcall hkCreateImpl(void* system, void* handle, const std::uint8_t* desc)
    {
        alignas(16) std::uint8_t heavy[0x60];
        alignas(16) float gun[16];
        float point[3] = {};
        float reach = kAtRoofGun;
        bool known = false;
        if (desc && battleGearWeapons::OnField())
        {
            known = battleGearWeapons::RoofGun(gun, nullptr);
            if (known)
            {
                point[0] = gun[12];
                point[1] = gun[13];
                point[2] = gun[14];
            }
            else
            {
                known = battleGearWeapons::RailGunMuzzle(point);
                reach = kAtRailMuzzle;
            }
        }
        if (known && WalkerBalloonAtSEH(desc, point, reach, heavy))
        {
#ifdef _DEBUG
            LogDebug("BattleGear: Fulton balloon made double size with four small ones");   // TEMP probe
#endif
            return g_Orig(system, handle, heavy);
        }
        return g_Orig(system, handle, desc);
    }
}

bool Install_BalloonSystemImpl_CreateImpl_Hook()
{
    if (g_Target)
        return true;
    void* target = gAddr.BalloonSystemImpl_CreateImpl ? ResolveGameAddress(gAddr.BalloonSystemImpl_CreateImpl) : nullptr;
    if (!target)
    {
        LogError("BattleGear: balloon system address missing on this build, the Battle Gear flies off on the soldiers' balloon");
        return false;
    }
    const HookEntry state = CheckHookEntry(target, kCreateImplEntry, sizeof(kCreateImplEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("BattleGear: balloon system code differs on this build (%s), the Battle Gear flies off on the soldiers' balloon", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: BalloonSystemImpl CreateImpl", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkCreateImpl), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BattleGear: balloon system hook failed, the Battle Gear flies off on the soldiers' balloon");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_BalloonSystemImpl_CreateImpl_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
}
