#include <pch.h>
#include "MbTopMenuStickyHide.h"

#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#ifdef _DEBUG
#include "../../lua/LuaApi.h"
#endif

namespace mbTopMenuStickyHide
{
    namespace
    {
        enum class Caller : std::uint8_t
        {
            None,
            Single,
            InitAll,
        };

        using LuaBinding_t     = int(__fastcall*)(void* L);
        using SetVisibleItem_t = void(__fastcall*)(void* self, std::uint8_t id, bool visible);
        using IsVisibleItem_t  = bool(__fastcall*)(void* self, std::uint8_t id);
        using IsFobMode_t      = bool(__fastcall*)();

        constexpr std::uint32_t kMaxItemId = 128;

        LuaBinding_t     g_OrigSetMbTopMenuItemVisible     = nullptr;
        LuaBinding_t     g_OrigInitAllMbTopMenuItemVisible = nullptr;
        SetVisibleItem_t g_OrigSetVisibleItem              = nullptr;
        IsVisibleItem_t  g_OrigIsVisibleItem               = nullptr;
        IsFobMode_t      g_IsFobMode                       = nullptr;

        std::atomic<std::uint64_t> g_Hidden[kMaxItemId / 64] = {};
        thread_local Caller t_Caller = Caller::None;
#ifdef _DEBUG
        thread_local const char* t_Label = nullptr;   // TEMP probe: the label the Lua call named
        constexpr std::uint8_t kProbeItem = 12;        // Customize > BUDDY > BATTLE GEAR
#endif

        class CallerScope
        {
        public:
            explicit CallerScope(Caller caller)
                : m_saved(t_Caller)
            {
                t_Caller = caller;
            }

            ~CallerScope()
            {
                t_Caller = m_saved;
            }

            CallerScope(const CallerScope&) = delete;
            CallerScope& operator=(const CallerScope&) = delete;

        private:
            Caller m_saved;
        };

        bool AnyHidden()
        {
            for (const auto& word : g_Hidden)
            {
                if (word.load(std::memory_order_relaxed))
                    return true;
            }
            return false;
        }

        bool IsHidden(std::uint8_t id)
        {
            if (id >= kMaxItemId)
                return false;
            return (g_Hidden[id >> 6].load(std::memory_order_relaxed) >> (id & 63)) & 1u;
        }

        void Mark(std::uint8_t id, bool hidden)
        {
            if (id >= kMaxItemId)
                return;
            const std::uint64_t bit = 1ull << (id & 63);
            if (hidden)
                g_Hidden[id >> 6].fetch_or(bit, std::memory_order_relaxed);
            else
                g_Hidden[id >> 6].fetch_and(~bit, std::memory_order_relaxed);
        }

        bool InFobSEH()
        {
            __try
            {
                return g_IsFobMode();
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return true;
            }
        }

        int __fastcall hkSetMbTopMenuItemVisible(void* L)
        {
            CallerScope scope(Caller::Single);
#ifdef _DEBUG
            auto* state = static_cast<lua_State*>(L);
            t_Label = ResolveLuaApi() && LuaIsString(state, 1) ? GetLuaString(state, 1) : nullptr;
#endif
            return g_OrigSetMbTopMenuItemVisible(L);
        }

        int __fastcall hkInitAllMbTopMenuItemVisible(void* L)
        {
            CallerScope scope(Caller::InitAll);
            return g_OrigInitAllMbTopMenuItemVisible(L);
        }

        void __fastcall hkSetVisibleItem(void* self, std::uint8_t id, bool visible)
        {
            if (t_Caller == Caller::Single || (t_Caller == Caller::InitAll && visible))
                Mark(id, !visible);
#ifdef _DEBUG
            if (t_Caller == Caller::Single && (!visible || id == kProbeItem))
                LogDebug("MbTopMenu: Lua SetMbTopMenuItemVisible(%s, %s) -> item %u %s", t_Label ? t_Label : "?", visible ? "true" : "false", id,
                         visible ? "shown" : "kept hidden from now on");
#endif
            g_OrigSetVisibleItem(self, id, visible);
        }

        bool __fastcall hkIsVisibleItem(void* self, std::uint8_t id)
        {
            if (IsHidden(id) && !MissionCodeGuard::ShouldBypassHooks() && !InFobSEH())
                return false;
            return g_OrigIsVisibleItem(self, id);
        }
    }

    void ClearForNewMission()
    {
        if (!AnyHidden())
            return;
        for (auto& word : g_Hidden)
            word.store(0, std::memory_order_relaxed);
    }

    bool Install(HMODULE)
    {
        if (!gAddr.TppUiCommand_SetMbTopMenuItemVisible
            || !gAddr.TppUiCommand_InitAllMbTopMenuItemVisible
            || !gAddr.MotherBaseDeviceTopMenuTableImpl_SetVisibleItem
            || !gAddr.MotherBaseDeviceTopMenuTableImpl_IsVisibleItem
            || !gAddr.Barrier_IsFobMode)
        {
            LogError("MbTopMenu: no address for this build, SetMbTopMenuItemVisible stays vanilla");
            return true;
        }

        g_IsFobMode = reinterpret_cast<IsFobMode_t>(ResolveGameAddress(gAddr.Barrier_IsFobMode));

        bool ok = CreateAndEnableHook(
            ResolveGameAddress(gAddr.MotherBaseDeviceTopMenuTableImpl_SetVisibleItem),
            &hkSetVisibleItem,
            reinterpret_cast<void**>(&g_OrigSetVisibleItem));

        ok = CreateAndEnableHook(
                 ResolveGameAddress(gAddr.MotherBaseDeviceTopMenuTableImpl_IsVisibleItem),
                 &hkIsVisibleItem,
                 reinterpret_cast<void**>(&g_OrigIsVisibleItem)) && ok;

        ok = CreateAndEnableHook(
                 ResolveGameAddress(gAddr.TppUiCommand_SetMbTopMenuItemVisible),
                 &hkSetMbTopMenuItemVisible,
                 reinterpret_cast<void**>(&g_OrigSetMbTopMenuItemVisible)) && ok;

        ok = CreateAndEnableHook(
                 ResolveGameAddress(gAddr.TppUiCommand_InitAllMbTopMenuItemVisible),
                 &hkInitAllMbTopMenuItemVisible,
                 reinterpret_cast<void**>(&g_OrigInitAllMbTopMenuItemVisible)) && ok;

        if (!ok)
        {
            LogError("MbTopMenu: hooks failed, unlocked iDroid items can come back after SetMbTopMenuItemVisible(false)");
        }
        return ok;
    }
}
