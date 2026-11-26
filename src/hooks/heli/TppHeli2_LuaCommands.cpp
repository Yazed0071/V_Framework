#include <pch.h>
#include "TppHeli2_LuaCommands.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "FoxHashes.h"
#include "HeliKey.h"
#include "HeliSoundController.h"
#include "MissionCodeGuard.h"
#include "SupportHeliHookInstall.h"
#include "SupportHeliWeapon.h"
#include "../fox/gm/GameObjectPosition.h"
#include "../fox/gm/GameObjectReinitialize.h"

namespace
{
    using LuaCommands_t = std::uint64_t(__fastcall*)(void* heli, void* args, std::uint32_t heliIndex, std::uint32_t command);
    using PushCommand_t = void(__fastcall*)(void* sink, std::uint32_t heliIndex, const std::uint8_t* command);

    constexpr std::size_t   kHeliCommandSink    = 0x40;
    constexpr std::size_t   kHeliCustomize      = 0xF0;
    constexpr std::size_t   kHeliType           = 0x130;
    constexpr std::size_t   kCustomizeRecords   = 0x8;
    constexpr std::size_t   kRecordStride       = 0x24;
    constexpr std::size_t   kRecordSubWeapon    = 0x4;
    constexpr std::size_t   kRecordAmmo         = 0x14;
    constexpr std::int32_t  kFirstMissileLevel  = 4;
    constexpr std::int32_t  kLastSubWeaponLevel = 6;
    constexpr std::int32_t  kVanillaSalvo       = 2;

    constexpr std::size_t   kCommandSize          = 0xC0;
    constexpr std::size_t   kCommandMissileTarget = 0x00;
    constexpr std::size_t   kCommandMissileSalvo  = 0x0C;
    constexpr std::size_t   kCommandMissileMode   = 0x10;
    constexpr std::size_t   kCommandMissileAim    = 0x14;
    constexpr std::size_t   kCommandRocketTarget  = 0x40;
    constexpr std::size_t   kCommandRocketSpeed   = 0x80;
    constexpr std::size_t   kCommandScaleA        = 0x88;
    constexpr std::size_t   kCommandScaleB        = 0xAC;
    constexpr std::size_t   kCommandScaleC        = 0xB4;
    constexpr std::size_t   kCommandKind          = 0xB9;
    constexpr std::size_t   kCommandFlags         = 0xBA;
    constexpr std::uint8_t  kKindRocket           = 0x30;
    constexpr std::uint8_t  kKindMissile          = 0x04;
    constexpr std::int32_t  kMissileMode          = 1;
    constexpr std::int32_t  kAimAtObject          = 1;
    constexpr std::int32_t  kAimAtPosition        = 0;
    constexpr std::uint32_t kRocketSpeedBits      = 0x425E38E4;   // 55.56 m/s, what the AI's rocket command carries

    constexpr std::uint8_t kLuaCommandsEntry[] =
    {
        0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x48, 0x20, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41,
    };

    LuaCommands_t g_OrigLuaCommands = nullptr;
    void*         g_Target          = nullptr;

    thread_local SupportHeliFireRequest t_Pending;
    thread_local bool                   t_HasPending = false;
    thread_local bool                   t_Handled    = false;

    struct SubWeaponState
    {
        std::int32_t level = 0;
        std::int32_t ammo  = 0;
    };

    bool ReadSubWeaponSEH(const std::uint8_t* heli, std::uint32_t heliIndex, SubWeaponState& out)
    {
        __try
        {
            const std::uint8_t* customize = *reinterpret_cast<std::uint8_t* const*>(heli + kHeliCustomize);
            if (!customize)
                return false;
            const std::uint8_t* records = *reinterpret_cast<std::uint8_t* const*>(customize + kCustomizeRecords);
            if (!records)
                return false;
            const std::uint8_t* record = records + heliIndex * kRecordStride;
            out.level = *reinterpret_cast<const std::int32_t*>(record + kRecordSubWeapon);
            out.ammo  = *reinterpret_cast<const std::int32_t*>(record + kRecordAmmo);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint16_t HeliTypeSEH(const std::uint8_t* heli)
    {
        __try
        {
            return *reinterpret_cast<const std::uint16_t*>(heli + kHeliType);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void* CommandSinkSEH(const std::uint8_t* heli, PushCommand_t& push)
    {
        __try
        {
            void* sink = *reinterpret_cast<void* const*>(heli + kHeliCommandSink);
            push = sink ? reinterpret_cast<PushCommand_t>((*static_cast<void***>(sink))[0]) : nullptr;
            return sink;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    template <typename T>
    void Put(std::uint8_t* command, std::size_t offset, T value)
    {
        std::memcpy(command + offset, &value, sizeof(value));
    }

    void SetCommandScales(std::uint8_t* command)
    {
        Put(command, kCommandScaleA, 1.0f);
        Put(command, kCommandScaleB, 1.0f);
        Put(command, kCommandScaleC, 1.0f);
    }

    bool BuildRocketCommand(const SupportHeliFireRequest& request, std::uint8_t* command)
    {
        float target[3] = {};
        if (request.hasPosition)
            std::memcpy(target, request.position, sizeof(target));
        else if (!GameObjectPosition::Read(request.targetId, target))
            return false;
        std::memcpy(command + kCommandRocketTarget, target, sizeof(target));
        Put(command, kCommandRocketSpeed, kRocketSpeedBits);
        SetCommandScales(command);
        command[kCommandKind]  = kKindRocket;
        command[kCommandFlags] = 1;
        return true;
    }

    void BuildMissileCommand(const SupportHeliFireRequest& request, std::int32_t salvo, std::uint8_t* command)
    {
        if (request.hasTarget)
        {
            Put(command, kCommandMissileTarget, static_cast<std::uint16_t>(request.targetId));
            Put(command, kCommandMissileAim, kAimAtObject);
        }
        else
        {
            std::memcpy(command + kCommandMissileTarget, request.position, sizeof(request.position));
            Put(command, kCommandMissileAim, kAimAtPosition);
        }
        Put(command, kCommandMissileSalvo, salvo);
        Put(command, kCommandMissileMode, kMissileMode);
        SetCommandScales(command);
        command[kCommandKind] = kKindMissile;
    }

    void FireAtTarget(std::uint8_t* heli, std::uint32_t heliIndex, const SupportHeliFireRequest& request)
    {
        if (HeliTypeSEH(heli) != HeliKey::kFirstType)
        {
            LogWarn("SetActionCommandFireToTarget: only the support heli (TppHeli2) can fire, ignored");
            return;
        }
        SubWeaponState state{};
        if (!ReadSubWeaponSEH(heli, heliIndex, state) || state.level < 1 || state.level > kLastSubWeaponLevel)
        {
            LogWarn("SetActionCommandFireToTarget: the support heli has no rockets or missiles fitted, ignored");
            return;
        }
        if (state.ammo <= 0)
        {
            return;
        }

        alignas(16) std::uint8_t command[kCommandSize] = {};
        if (state.level >= kFirstMissileLevel)
        {
            SupportHeliWeapon::Weapon weapon{};
            const bool custom = SupportHeliWeapon::GetSortieWeapon(weapon) && weapon.shotCount > 0;
            BuildMissileCommand(request, custom ? weapon.shotCount : kVanillaSalvo, command);
        }
        else if (!BuildRocketCommand(request, command))
        {
            LogWarn("SetActionCommandFireToTarget: target %u has no position, ignored", request.targetId);
            return;
        }

        PushCommand_t push = nullptr;
        void* sink = CommandSinkSEH(heli, push);
        if (!sink || !push)
            return;
        std::uint32_t line = state.level >= kFirstMissileLevel ? SupportHeliWeapon::kMissileAttackLine : SupportHeliWeapon::kRocketAttackLine;
        std::uint32_t lineType = SupportHeliWeapon::kPilotRadio;
        if (SupportHeliWeapon::SortieAttackLine(line, lineType))
            Play_PilotCallVoice(line, heliIndex, lineType, 0);
        push(sink, heliIndex, command);
    }

    std::uint64_t __fastcall hkLuaCommands(void* heli, void* args, std::uint32_t heliIndex, std::uint32_t command)
    {
        static const std::uint32_t kFireToTarget = FoxHashes::StrCode32("SetActionCommandFireToTarget");
        if (GameObjectReinitialize::Claims(command))
        {
            GameObjectReinitialize::Report(GameObjectReinitialize::Helis(static_cast<std::uint8_t*>(heli), args, heliIndex,
                                                                         reinterpret_cast<GameObjectReinitialize::LuaCommand_t>(g_OrigLuaCommands)));
            return 0;
        }
        if (!t_HasPending || command != kFireToTarget)
            return g_OrigLuaCommands(heli, args, heliIndex, command);

        t_HasPending = false;
        t_Handled    = true;
        if (!MissionCodeGuard::ShouldBypassHooks())
            FireAtTarget(static_cast<std::uint8_t*>(heli), heliIndex, t_Pending);
        return 0;
    }
}

void SupportHeliFire_SetPending(const SupportHeliFireRequest& request)
{
    t_Pending    = request;
    t_HasPending = true;
    t_Handled    = false;
}

bool SupportHeliFire_TakeHandled()
{
    const bool handled = t_Handled;
    t_HasPending = false;
    t_Handled    = false;
    return handled;
}

bool Install_TppHeli2_LuaCommands_Hook()
{
    if (g_Target)
        return true;
    g_Target = InstallSupportHeliHook(
        "Pequod SendCommand", gAddr.TppHeli2_LuaCommands,
        kLuaCommandsEntry, nullptr, sizeof(kLuaCommandsEntry),
        reinterpret_cast<void*>(&hkLuaCommands), reinterpret_cast<void**>(&g_OrigLuaCommands),
        "SetActionCommandFireToTarget does nothing");
    return g_Target != nullptr;
}

void Uninstall_TppHeli2_LuaCommands_Hook()
{
    UninstallSupportHeliHook(g_Target);
}
