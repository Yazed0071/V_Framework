#include <pch.h>
#include "CcRootAdjustAction_Exec.h"

#include <Windows.h>
#include <intrin.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using Exec_t = void(__fastcall*)(void* rootAdjust, void* animControl);

    constexpr std::uint32_t kMaxSoldiers = 0x200;
    constexpr int           kMaxPins     = 16;
    constexpr std::uint64_t kStaleMs     = 150;   // the ride stopped refreshing him, e.g. he was unrealized and the slot may go to someone else

    constexpr std::size_t   kModuleInAction = 0x80;   // GetRootAdjustModule hands out the RootAdjustActionInterface part
    constexpr std::size_t   kPluginNameId   = 0x10;
    constexpr std::uint32_t kRootAdjustId   = 0x7F1402F4;
    constexpr std::size_t   kExecSlot       = 3;
    constexpr std::size_t   kParamLayers    = 0x40;   // one param per anim layer, set by the motion state that plays there
    constexpr std::size_t   kParams         = 0x48;
    constexpr std::size_t   kValid          = 0x70;   // two bytes, the action does nothing while both are 0
    constexpr std::size_t   kCharaControl   = 0x68;
    constexpr std::size_t   kCharaFlags     = 0x188;   // bit 2: his moves collide with the world
    constexpr long          kCharaCollision = 2;

    constexpr std::size_t kParamSize   = 0x20;
    constexpr std::size_t kParamMode   = 0x0C;   // 0 = put him on the seat matrix as it is, and gravity off
    constexpr std::size_t kParamBlend  = 0x10;   // 0 = no blend in
    constexpr std::size_t kParamExtra  = 0x19;
    constexpr std::size_t kParamWeight = 0x1C;

    struct Learned
    {
        std::atomic<bool>          ok{ false };
        alignas(16) std::uint8_t   param[kParamSize];
    };

    struct Pin
    {
        std::atomic<std::uint32_t> owner{ 0 };   // soldier index + 1
        std::atomic<std::uint8_t*> plugin{ nullptr };
        std::atomic<std::uint64_t> seenMs{ 0 };
        std::atomic<std::uint8_t>  phase{ 0 };
    };

    struct Lend
    {
        void**        slot     = nullptr;
        void*         previous = nullptr;
        void*         lent     = nullptr;
        std::uint8_t* valid    = nullptr;
    };

    Exec_t            g_OrigExec   = nullptr;
    void*             g_ExecTarget = nullptr;
    bool              g_Installed  = false;
    Pin               g_Pins[kMaxPins];
    Learned           g_Learned[kMaxSoldiers];
    std::atomic<int>  g_ActivePins{ 0 };
    std::atomic<bool> g_WarnedFull{ false };
    std::atomic<bool> g_WarnedPlugin{ false };

    alignas(16) std::uint8_t g_PlainHold[kParamSize] = {};   // mode 0, no blend: used when no heli clip param was seen

    Pin* PinOf(std::uint32_t soldierIndex)
    {
        for (Pin& pin : g_Pins)
            if (pin.owner.load(std::memory_order_acquire) == soldierIndex + 1)
                return &pin;
        return nullptr;
    }

    Pin* PinOfPlugin(std::uint8_t* plugin)
    {
        for (Pin& pin : g_Pins)
        {
            if (pin.plugin.load(std::memory_order_acquire) != plugin || pin.owner.load(std::memory_order_acquire) == 0)
                continue;
            if (GetTickCount64() - pin.seenMs.load(std::memory_order_relaxed) > kStaleMs)
                continue;
            return &pin;
        }
        return nullptr;
    }

    bool IsOurs(const void* param, const Learned& learned)
    {
        return param == learned.param || param == g_PlainHold;
    }

    bool IsRootAdjustSeh(std::uint8_t* plugin, std::uint32_t& nameId, void*& execSlot)
    {
        __try
        {
            void** vtbl = *reinterpret_cast<void***>(plugin);
            nameId   = *reinterpret_cast<std::uint32_t*>(plugin + kPluginNameId);
            execSlot = vtbl[kExecSlot];
            return nameId == kRootAdjustId && execSlot == g_ExecTarget;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void LearnSeh(Learned& learned, const std::uint8_t* heliParam)
    {
        __try
        {
            std::memcpy(learned.param, heliParam, kParamSize);
            *reinterpret_cast<std::int32_t*>(learned.param + kParamMode)  = 0;
            *reinterpret_cast<std::int32_t*>(learned.param + kParamBlend) = 0;
            learned.param[kParamExtra] = 0;
            learned.ok.store(true, std::memory_order_release);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void LendSeh(Pin& pin, std::uint8_t* plugin, Lend& lend)
    {
        __try
        {
            const std::uint32_t soldier = pin.owner.load(std::memory_order_acquire) - 1;
            if (soldier >= kMaxSoldiers)
                return;
            Learned& learned = g_Learned[soldier];
            const std::uint16_t layers = *reinterpret_cast<std::uint16_t*>(plugin + kParamLayers);
            void** params = *reinterpret_cast<void***>(plugin + kParams);
            int pick = 0;   // the layer GetAnimParam reads
            void* current = nullptr;
            if (layers && params)
            {
                for (int layer = layers - 1; layer > 0 && !current; --layer)
                {
                    current = params[layer];
                    pick = layer;
                }
                if (!current)
                {
                    current = params[0];
                    pick = 0;
                }
            }
            const auto phase = static_cast<RootAdjustPinPhase>(pin.phase.load(std::memory_order_relaxed));
            if (phase == RootAdjustPinPhase::Learn)
            {
                if (current && !IsOurs(current, learned))
                    LearnSeh(learned, static_cast<const std::uint8_t*>(current));
                return;
            }
            if (!layers || !params)
                return;
            void* hold = learned.ok.load(std::memory_order_acquire) ? static_cast<void*>(learned.param) : static_cast<void*>(g_PlainHold);
            if (current != hold)
            {
                params[pick] = hold;   // only for this call, whatever the motion state left there comes back after it
                lend.slot     = &params[pick];
                lend.previous = current;
                lend.lent     = hold;
            }
            if (plugin[kValid] == 0 && plugin[kValid + 1] == 0)
            {
                plugin[kValid + 1] = 1;
                lend.valid = plugin + kValid + 1;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void ReturnSeh(const Lend& lend)
    {
        __try
        {
            if (lend.slot && *lend.slot == lend.lent)
                *lend.slot = lend.previous;
            if (lend.valid)
                *lend.valid = 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void NoCollisionSeh(std::uint8_t* plugin)
    {
        __try
        {
            auto* chara = *reinterpret_cast<std::uint8_t**>(plugin + kCharaControl);
            if (chara && (*reinterpret_cast<volatile long*>(chara + kCharaFlags) & kCharaCollision))
                _InterlockedAnd(reinterpret_cast<volatile long*>(chara + kCharaFlags), ~kCharaCollision);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkExec(void* rootAdjust, void* animControl)
    {
        Pin* pin = g_ActivePins.load(std::memory_order_relaxed) > 0 ? PinOfPlugin(static_cast<std::uint8_t*>(rootAdjust)) : nullptr;
        if (!pin)
        {
            g_OrigExec(rootAdjust, animControl);
            return;
        }
        Lend lend;
        LendSeh(*pin, static_cast<std::uint8_t*>(rootAdjust), lend);
        g_OrigExec(rootAdjust, animControl);
        if (lend.slot || lend.valid)
            ReturnSeh(lend);
        if (static_cast<RootAdjustPinPhase>(pin->phase.load(std::memory_order_relaxed)) == RootAdjustPinPhase::Hold)
            NoCollisionSeh(static_cast<std::uint8_t*>(rootAdjust));
    }
}

void RootAdjustPin_Update(std::uint32_t soldierIndex, void* rootAdjustModule, RootAdjustPinPhase phase)
{
    if (!g_Installed || soldierIndex >= kMaxSoldiers || !rootAdjustModule)
        return;
    auto* plugin = static_cast<std::uint8_t*>(rootAdjustModule) - kModuleInAction;
    std::uint32_t nameId = 0;
    void* execSlot = nullptr;
    if (!IsRootAdjustSeh(plugin, nameId, execSlot))
    {
        if (!g_WarnedPlugin.exchange(true))
            LogWarn("RequestToGetOnHelicopterAndAimTarget: soldier %u has no usable seat lock (name id 0x%08X, exec %p), he can fall when he aims", soldierIndex, nameId, execSlot);
        return;
    }
    Pin* pin = PinOf(soldierIndex);
    if (!pin)
    {
        for (Pin& candidate : g_Pins)
        {
            std::uint32_t free = 0;
            if (!candidate.owner.compare_exchange_strong(free, soldierIndex + 1, std::memory_order_acq_rel))
                continue;
            g_ActivePins.fetch_add(1, std::memory_order_relaxed);
            pin = &candidate;
            break;
        }
        if (!pin)
        {
            if (!g_WarnedFull.exchange(true))
                LogWarn("RequestToGetOnHelicopterAndAimTarget: more than %d soldiers on helis at once, soldier %u can fall when he aims", kMaxPins, soldierIndex);
            return;
        }
    }
    pin->phase.store(static_cast<std::uint8_t>(phase), std::memory_order_relaxed);
    pin->seenMs.store(GetTickCount64(), std::memory_order_relaxed);
    pin->plugin.store(plugin, std::memory_order_release);
}

void RootAdjustPin_Release(std::uint32_t soldierIndex)
{
    if (soldierIndex >= kMaxSoldiers)
        return;
    Pin* pin = PinOf(soldierIndex);
    if (!pin)
        return;
    pin->plugin.store(nullptr, std::memory_order_release);
    std::uint32_t owner = soldierIndex + 1;
    if (pin->owner.compare_exchange_strong(owner, 0, std::memory_order_acq_rel))
        g_ActivePins.fetch_sub(1, std::memory_order_relaxed);
}

void RootAdjustPin_Forget(std::uint32_t soldierIndex)
{
    if (soldierIndex >= kMaxSoldiers)
        return;
    RootAdjustPin_Release(soldierIndex);
    g_Learned[soldierIndex].ok.store(false, std::memory_order_release);
}

void RootAdjustPin_Clear()
{
    for (Pin& pin : g_Pins)
    {
        pin.plugin.store(nullptr, std::memory_order_release);
        pin.owner.store(0, std::memory_order_release);
    }
    g_ActivePins.store(0, std::memory_order_relaxed);
    for (Learned& learned : g_Learned)
        learned.ok.store(false, std::memory_order_release);
}

bool Install_CcRootAdjustAction_Exec_Hook()
{
    if (g_Installed)
        return true;

    RootAdjustPin_Clear();
    const float fullWeight = 1.0f;
    std::memcpy(g_PlainHold + kParamWeight, &fullWeight, sizeof(fullWeight));

    void* target = gAddr.CcRootAdjustAction_Exec ? ResolveGameAddress(gAddr.CcRootAdjustAction_Exec) : nullptr;
    if (!target)
    {
        LogError("CcRootAdjustAction_Exec: address missing for this build, soldiers boarded by script fall when they aim");
        return false;
    }
    g_ExecTarget = target;
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkExec), reinterpret_cast<void**>(&g_OrigExec)))
    {
        LogError("CcRootAdjustAction_Exec: hook failed, soldiers boarded by script fall when they aim");
        g_ExecTarget = nullptr;
        return false;
    }
    g_Installed = true;
    return true;
}

bool Uninstall_CcRootAdjustAction_Exec_Hook()
{
    if (!g_Installed)
        return true;
    g_Installed = false;
    RootAdjustPin_Clear();
    DisableAndRemoveHook(g_ExecTarget);
    g_OrigExec   = nullptr;
    g_ExecTarget = nullptr;
    return true;
}
