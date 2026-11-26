#pragma once

#include <cstdint>

struct lua_State;

namespace battleGear
{
    constexpr std::uint8_t kBuddyType     = 5;   // BuddyType.BATTLE_GEAR
    constexpr std::uint8_t kHostBuddyType = 4;   // BuddyType.WALKER_GEAR

    bool Install();
    void Uninstall();

    bool IsHostedType(std::int32_t buddyType);
    const void* HostActiveFlag();
    bool IsBattleGearOut();
    bool SortieUnlocked();

    class BlockSetUp   // while one lives, the buddy block is setting the Battle Gear up: the save still names the buddy that left
    {
    public:
        explicit BlockSetUp(const void* blockController);
        ~BlockSetUp();
        BlockSetUp(const BlockSetUp&) = delete;
        BlockSetUp& operator=(const BlockSetUp&) = delete;
    };

    bool SettingBlockUp();   // a BlockSetUp lives on this thread

    // out in the field: the Battle Gear leaves and drops in again with the main gun, roof gun and head picked now, the way the
    // game re-drops the D-Walker after an equipment change
    bool Redeploy();

    void NoteBuddyBlock(const void* blockController);
    bool IsBattleGearLoaded();   // the buddy the block holds: the ACC shows buddies the save does not name
    bool HoldsAnotherBuddy();

    std::uint64_t MainPackPath();

    void EnsureLuaWraps(lua_State* L);
    void Pump();
}
