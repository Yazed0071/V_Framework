#include <pch.h>

// Release builds leave the Battle Gear out: these answer for it the way it answers while not installed

#include "BattleGearBuddy.h"
#include "BattleGearCamera.h"
#include "BattleGearCustomizePage.h"
#include "BattleGearEffects.h"
#include "BattleGearSound.h"
#include "BattleGearWeapons.h"

namespace battleGear
{
    bool Install() { return false; }
    void Uninstall() {}
    bool IsHostedType(std::int32_t) { return false; }
    std::uint64_t MainPackPath() { return 0; }
    void EnsureLuaWraps(lua_State*) {}
    void Pump() {}
}

namespace battleGearCamera
{
    void Update(lua_State*) {}
}

namespace battleGearCustomizePage
{
    void AfterSetupItemList(std::uintptr_t) {}
}

namespace battleGearEffects
{
    void Drop() {}
}

namespace battleGearSound
{
    void BeforeSidearmShot(void*, std::uint32_t, int, SidearmShot&) {}
    void AfterSidearmShot(const SidearmShot&) {}
}

namespace battleGearWeapons
{
    bool LoadoutApplies() { return false; }
    void ClearScriptPicks() {}
}
