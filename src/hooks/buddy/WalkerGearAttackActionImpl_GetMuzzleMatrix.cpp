#include <pch.h>

#include "WalkerGearAttackActionImpl_GetMuzzleMatrix.h"

#include <Windows.h>
#include <cmath>
#include <cstdint>

#include "AddressSet.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using GetMuzzleMatrix_t = bool(__fastcall*)(void* attack, float* matrix, std::uint32_t walker, std::int32_t arm);

    constexpr std::size_t kWorks      = 0x90;
    constexpr std::size_t kFirstWork  = 0x98;
    constexpr std::size_t kWorkSize   = 0x2D0;
    constexpr std::size_t kShotCount  = 0x15C;   // the missile pod's tube counter
    constexpr int         kMatrixSide = 0;       // rows of four floats: side, up, forward, position
    constexpr int         kMatrixUp   = 4;
    constexpr int         kMatrixAim  = 8;       // State_HoldOnArmLoop only fires while this row's yaw is near the aim point's
    constexpr int         kMatrixMove = 12;

    const std::uint8_t kGetMuzzleMatrixEntry[] = { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48,
                                                   0x8D, 0xAC, 0x24, 0x78, 0xFE, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x88, 0x02, 0x00, 0x00 };

    GetMuzzleMatrix_t g_Orig = nullptr;
    void*             g_Target = nullptr;

    std::int32_t MissileShotSEH(const void* attack, std::uint32_t walker)
    {
        __try
        {
            const auto* base = static_cast<const std::uint8_t*>(attack);
            const auto* works = *reinterpret_cast<const std::uint8_t* const*>(base + kWorks);
            const std::uint32_t first = *reinterpret_cast<const std::uint32_t*>(base + kFirstWork);
            return *reinterpret_cast<const std::int32_t*>(works + static_cast<std::size_t>(walker - first) * kWorkSize + kShotCount);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void AimRows(float* matrix, const float* aim)   // forward = the barrel, up kept upright, side = up x forward
    {
        const float lift = aim[1];
        float up[3] = { -aim[0] * lift, 1.0f - aim[1] * lift, -aim[2] * lift };
        const float upLength = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
        if (upLength < 1e-3f)   // barrel pointing straight up or down: no upright frame, keep the walker's
            return;
        for (float& u : up)
            u /= upLength;
        for (int i = 0; i < 3; ++i)
        {
            matrix[kMatrixAim + i] = aim[i];
            matrix[kMatrixUp + i] = up[i];
        }
        matrix[kMatrixSide] = up[1] * aim[2] - up[2] * aim[1];
        matrix[kMatrixSide + 1] = up[2] * aim[0] - up[0] * aim[2];
        matrix[kMatrixSide + 2] = up[0] * aim[1] - up[1] * aim[0];
    }

    bool __fastcall hkGetMuzzleMatrix(void* attack, float* matrix, std::uint32_t walker, std::int32_t arm)
    {
        const bool found = g_Orig(attack, matrix, walker, arm);
        if (!found || !matrix || !battleGearWeapons::IsBattleGearAttack(attack))
            return found;
        const bool ridden = battleGearWeapons::Ridden();
        if (!ridden)
            battleGearWeapons::NoteAiAttack();
        float muzzle[3];
        if (battleGearWeapons::MuzzleFor(arm, arm == 0 ? MissileShotSEH(attack, walker) : 0, muzzle))
        {
            matrix[kMatrixMove] = muzzle[0];
            matrix[kMatrixMove + 1] = muzzle[1];
            matrix[kMatrixMove + 2] = muzzle[2];
        }
        float aim[3];
        if (ridden && battleGearWeapons::BarrelFor(arm, aim))   // the turret turns all the way round; the walker's own gun cannot
            AimRows(matrix, aim);
        return found;
    }
}

bool Install_WalkerGearAttackActionImpl_GetMuzzleMatrix_Hook()
{
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.WalkerGearAttackActionImpl_GetMuzzleMatrix);
    if (!target)
    {
        LogError("BattleGear: walker muzzle address missing on this build, Battle Gear shots leave the D-Walker guns");
        return false;
    }
    const HookEntry state = CheckHookEntry(target, kGetMuzzleMatrixEntry, sizeof(kGetMuzzleMatrixEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("BattleGear: walker muzzle code differs on this build (%s), Battle Gear shots leave the D-Walker guns", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: walker GetMuzzleMatrix", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkGetMuzzleMatrix), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BattleGear: walker muzzle hook failed, Battle Gear shots leave the D-Walker guns");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_WalkerGearAttackActionImpl_GetMuzzleMatrix_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
}
