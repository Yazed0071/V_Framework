#include <pch.h>

#include "LaserSightSwitch.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <unordered_set>

#include "AddressSet.h"
#include "EquipAliasLease.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "LaserSight_UpdatePrim.h"
#include "LuaApi.h"
#include "V_FrameWorkState.h"
#include "log.h"
#include "../ui/TextIcons.h"

namespace
{
    constexpr const char*   kSaveSpace          = "LaserOff";
    constexpr std::uint32_t kFirstCustomizedId  = 0x367;   // the six customized-weapon slots, 2 per player
    constexpr std::uint32_t kLastCustomizedId   = 0x36C;
    constexpr int           kHudSlots           = 3;
    constexpr std::size_t   kHudWeaponIds       = 0x04;
    constexpr std::size_t   kHudOptionWords     = 0xC0;
    constexpr ULONGLONG     kAimToggleDebounceMs = 200;
    constexpr ULONGLONG     kAimToggleSwapGuardMs = 600;

    constexpr double kPadB  = 2;    // fox.PAD_B
    constexpr double kPadLeft = 14;   // fox.PAD_L
    constexpr double kKeyY  = 92;   // fox.KEY_Y
    constexpr double kKeyZ  = 93;   // fox.KEY_Z
    constexpr ULONGLONG kKeyRetryMs      = 250;
    constexpr ULONGLONG kKeyWarnAfterMs  = 300000;

    std::mutex                       g_Mutex;
    std::unordered_set<std::int32_t> g_Off;
    std::unordered_set<std::int32_t> g_KeptThisSession;
    std::atomic<bool>                g_AnyOff{ false };

    std::mutex                       g_IconMutex;
    std::map<int, std::uint64_t>     g_OptionIcons;
    std::map<int, std::uint64_t>     g_VanillaIcons;

    std::atomic<ULONGLONG> g_LastAimToggle{ 0 };

    std::atomic<bool> g_KeysDone{ false };
    std::atomic<bool> g_KeysBusy{ false };
    std::atomic<ULONGLONG> g_KeyFirstTry{ 0 };
    std::atomic<ULONGLONG> g_KeyLastTry{ 0 };
    std::atomic<bool>      g_KeyWarned{ false };
    const char*            g_KeyFailure = "not tried";

    std::string SaveName(std::int32_t equipId)
    {
        return std::to_string(equipId);
    }

    void NoteLoadedOff(const char* name, std::int32_t value)
    {
        const std::int32_t equipId = std::atoi(name);
        if (value == 0 || equipId <= 0)
            return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_Off.insert(equipId);
        g_AnyOff.store(true, std::memory_order_release);
    }

    std::uint32_t CustomizedBaseIdSEH(std::uint32_t slotId)
    {
        const auto* table = static_cast<const std::uint16_t*>(
            ResolveGameAddress(gAddr.EquipSystem_ChimeraPartsSetWork));
        if (!table)
            return slotId;
        __try
        {
            const std::uint16_t baseId = table[slotId - kFirstCustomizedId];
            return baseId ? baseId : slotId;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return slotId;
        }
    }

    const char* VanillaIconPath(int optionId)
    {
        switch (optionId)
        {
        case 2:  return "/Assets/tpp/ui/texture/WeaponPartsIcon/laser/ui_wpp_ls01_main1_def_alp.ftex";   // LT_10104
        case 6:  return "/Assets/tpp/ui/texture/WeaponPartsIcon/laser/ui_wpp_ls01_main0_def_alp.ftex";   // LS_10415
        case 7:  return "/Assets/tpp/ui/texture/WeaponPartsIcon/laser/ui_wpp_ls03_main0_def_alp.ftex";   // LS_20004
        case 9:  return "/Assets/tpp/ui/texture/WeaponPartsIcon/laser/ui_wpp_ls02_main0_def_alp.ftex";   // LS_40034
        default: return "/Assets/tpp/ui/texture/WeaponPartsIcon/laser/ui_wpp_ls00_main0_def_alp.ftex";   // LS_30104, and any part without a picture of its own
        }
    }

    std::uint64_t VanillaIcon(int optionId)
    {
        const int key = (optionId == 2 || optionId == 6 || optionId == 7 || optionId == 9) ? optionId : 0;
        std::lock_guard<std::mutex> lock(g_IconMutex);
        const auto it = g_VanillaIcons.find(key);
        if (it != g_VanillaIcons.end())
            return it->second;
        const std::uint64_t pathCode = FoxHashes::PathCode64Ext(VanillaIconPath(key));
        if (pathCode)
            g_VanillaIcons[key] = pathCode;
        return pathCode;
    }

    void RefreshHudWordsSEH(void* hudInfo, std::int32_t equipId, bool on)
    {
        __try
        {
            auto* base = static_cast<std::uint8_t*>(hudInfo);
            for (int slot = 0; slot < kHudSlots; ++slot)
            {
                const auto weaponId = *reinterpret_cast<std::uint32_t*>(base + kHudWeaponIds + 4 * slot);
                if (weaponId == 0 || LaserSightSwitch::EquipIdOfEngineId(weaponId) != equipId)   // customized slots 0x367-0x36C map to their base weapon
                    continue;
                auto* word = reinterpret_cast<std::uint16_t*>(base + kHudOptionWords + 2 * slot);
                if ((*word & LaserSightSwitch::kHudHasLaser) == 0)
                    continue;
                *word = on ? static_cast<std::uint16_t>(*word | LaserSightSwitch::kHudLaserOn)
                           : static_cast<std::uint16_t>(*word & ~LaserSightSwitch::kHudLaserOn);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool SetButtonMap(lua_State* L, int daemon, const char* action, double padButton, double keyCode)
    {
        g_lua_getfield(L, daemon, const_cast<char*>("SetButtonMap"));
        if (g_lua_type(L, -1) != LUA_TFUNCTION)
            return false;
        g_lua_pushstring(L, const_cast<char*>(action));
        g_lua_pushnumber(L, padButton);
        g_lua_pushnumber(L, keyCode);
        return g_lua_pcall(L, 3, 0, 0) == 0;
    }

    bool RegisterKeys(lua_State* L)
    {
        const int top = g_lua_gettop(L);
        bool ok = false;
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("UiDaemon"));
        if (g_lua_type(L, -1) == LUA_TNIL)
            g_KeyFailure = "no UiDaemon in this Lua state";
        else
        {
            g_lua_getfield(L, -1, const_cast<char*>("GetInstance"));
            if (g_lua_type(L, -1) != LUA_TFUNCTION)
                g_KeyFailure = "UiDaemon.GetInstance missing";
            else if (g_lua_pcall(L, 0, 1, 0) != 0 || g_lua_type(L, -1) == LUA_TNIL)
                g_KeyFailure = "UiDaemon.GetInstance gave nothing";
            else
            {
                const int daemon = g_lua_gettop(L);
                ok = SetButtonMap(L, daemon, "UI_EQUIP_OP_3", kPadB, kKeyY)
                  && SetButtonMap(L, daemon, "UI_LASER_SWITCH", kPadLeft, kKeyZ);
                if (!ok)
                    g_KeyFailure = "SetButtonMap refused";
            }
        }
        g_lua_settop(L, top);
        return ok;
    }
}

namespace LaserSightSwitch
{
    void LoadSaved()
    {
        V_FrameWorkState::ForEachPersistedConstant(kSaveSpace, &NoteLoadedOff);
    }

    bool AnyOff()
    {
        return g_AnyOff.load(std::memory_order_acquire);
    }

    bool IsOff(std::int32_t equipId)
    {
        if (equipId <= 0 || !AnyOff())
            return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_Off.count(equipId) != 0;
    }

    void SetOn(std::int32_t equipId, bool on)
    {
        if (equipId <= 0)
            return;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (on)
                g_Off.erase(equipId);
            else
                g_Off.insert(equipId);
            g_AnyOff.store(!g_Off.empty(), std::memory_order_release);
            g_KeptThisSession.insert(equipId);
        }
        V_FrameWorkState::SetPersistedConstant(kSaveSpace, SaveName(equipId).c_str(), on ? 0 : 1);
    }

    void KeepSaved(std::int32_t equipId)
    {
        if (!IsOff(equipId))
            return;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (!g_KeptThisSession.insert(equipId).second)
                return;
        }
        V_FrameWorkState::SetPersistedConstant(kSaveSpace, SaveName(equipId).c_str(), 1);   // same value, only marks the weapon as still in use
    }

    std::int32_t EquipIdOfEngineId(std::uint32_t engineEquipId)
    {
        if (engineEquipId >= kFirstCustomizedId && engineEquipId <= kLastCustomizedId)
            engineEquipId = CustomizedBaseIdSEH(engineEquipId);
        return equip::FromEngine(static_cast<std::int32_t>(engineEquipId));
    }

    void SetOptionIcon(int optionId, const std::string& ftexPath)
    {
        if (optionId <= 0 || ftexPath.empty())
            return;
        std::string path = ftexPath;
        if (path.size() < 5 || _stricmp(path.c_str() + path.size() - 5, ".ftex") != 0)
            path += ".ftex";
        const std::uint64_t pathCode = FoxHashes::PathCode64Ext(path);
        if (!pathCode)
        {
            LogWarn("SetOption optionId=%d: iconFtexPath could not be hashed, the LASER box shows the stock picture", optionId);
            return;
        }
        std::lock_guard<std::mutex> lock(g_IconMutex);
        g_OptionIcons[optionId] = pathCode;
    }

    std::uint64_t BoxPictureForWeapon(std::int32_t equipId)
    {
        const int optionId = LaserColor_WeaponOption(equipId);
        {
            std::lock_guard<std::mutex> lock(g_IconMutex);
            const auto it = g_OptionIcons.find(optionId);
            if (it != g_OptionIcons.end())
                return it->second;
        }
        return VanillaIcon(optionId);
    }

    void NoteAimKeyToggle()
    {
        g_LastAimToggle.store(GetTickCount64(), std::memory_order_release);
    }

    bool AimKeyToggledJustNow()
    {
        const ULONGLONG last = g_LastAimToggle.load(std::memory_order_acquire);
        return last != 0 && GetTickCount64() - last < kAimToggleSwapGuardMs;
    }

    bool AimKeyDebounced()
    {
        const ULONGLONG last = g_LastAimToggle.load(std::memory_order_acquire);
        return last != 0 && GetTickCount64() - last < kAimToggleDebounceMs;
    }

    void RefreshHudWords(void* hudInfo, std::int32_t equipId, bool on)
    {
        if (hudInfo && equipId > 0)
            RefreshHudWordsSEH(hudInfo, equipId, on);
    }

    void EnsureKeysRegistered(lua_State* L)
    {
        if (g_KeysDone.load(std::memory_order_acquire) || !L)
            return;
        const ULONGLONG now = GetTickCount64();
        if (now - g_KeyLastTry.load(std::memory_order_acquire) < kKeyRetryMs || !ResolveLuaApi())
            return;
        if (g_KeysBusy.exchange(true))
            return;
        g_KeyLastTry.store(now, std::memory_order_release);
        ULONGLONG first = 0;
        g_KeyFirstTry.compare_exchange_strong(first, now);   // the game's UI daemon only shows up partway through boot, so keep asking
        if (RegisterKeys(L))
        {
            g_KeysDone.store(true, std::memory_order_release);
            TextIcons::AddForNewAction(kMenuAction);   // also when the button map hook came too late to see them: the strip's key and the iDroid page draw these
            TextIcons::AddForNewAction(kAimAction);
        }
        else if (now - g_KeyFirstTry.load(std::memory_order_acquire) > kKeyWarnAfterMs && !g_KeyWarned.exchange(true))
            LogWarn("LaserSight: laser keys still not registered (%s), Y / B in the menu and Z / D-pad Left while aiming do nothing yet", g_KeyFailure);
        g_KeysBusy.store(false, std::memory_order_release);
    }

    void* CheckedEntry(std::uintptr_t address, const std::uint8_t* prologue, std::size_t size, const char* function)
    {
        if (address == 0)
        {
            LogError("LaserSight: %s address missing for this build, laser on/off switch off", function);
            return nullptr;
        }
        void* fn = ResolveGameAddress(address);
        const HookEntry entry = CheckHookEntry(fn, prologue, size);
        if (entry == HookEntry::Unknown)
        {
            char found[160] = {};
            DescribeFunctionEntry(fn, found, sizeof(found));
            LogError("LaserSight: %s at 0x%llX is not the expected code (%s), laser on/off switch off",
                     function, static_cast<unsigned long long>(address), found);
            return nullptr;
        }
        if (entry == HookEntry::ForeignHook)
        {
            char what[96] = {};
            snprintf(what, sizeof(what), "LaserSight: %s", function);
            LogHookedOnTop(what, fn);
        }
        return fn;
    }
}
