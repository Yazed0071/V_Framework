#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace VehicleSortieRows
{
    bool Install();
    void Uninstall();

    void AfterListBuilt(void* itemSelector);     // from the ItemSelectorCallbackImpl::SetupPrefabListElement hook
    void AfterRowDrawn(void* record);            // from the ItemSelectorRecordCallFunc::UpdateRecords hook
    void BeforeRowDecided(void* itemSelector);   // from the hook on the Sortie Prep decide handler
    void ForgetList();

    struct ResourceStepScope
    {
        std::int32_t saleRowId;
        std::uint8_t saleRowType;
        std::uint8_t pageState;   // the Resources page's step before this one: the sale popup opening is a change to its popup step
    };
    ResourceStepScope BeginResourceStep(void* resourcePageState);   // from the shared Resources menu step hook
    void EndResourceStep(const ResourceStepScope& outer);
    struct DispatchRewardLine
    {
        enum class Kind : std::uint8_t
        {
            Vehicle,   // a registered mod vehicle, paid into the Mother Base stock
            Pool,      // a game reward type (GMP, a resource, staff), paid by the game itself
            Custom,    // own icon and name; on receipt Lua gets Dispatch / OnRewardReceived
        };
        Kind          kind = Kind::Custom;
        std::string   vehicle;           // Vehicle
        std::uint8_t  poolType = 0;      // Pool: TppMotherBaseManagementConst.POOL_REWARD_TYPE_*
        std::uint8_t  section = 0;       // Pool staff: the unit they join
        std::int32_t  count = 1;
        std::uint8_t  chance = 100;      // 1-100, rolled once per mission from its seed
        std::string   name;              // Custom: handed to Lua
        std::uint64_t nameLangId = 0;    // Vehicle/Custom: 0 = the vehicle's own name
        std::uint64_t iconFile = 0;      // Vehicle/Custom: 0 = the vehicle's own picture
    };
    constexpr std::size_t kMostRewardLines = 16;   // the reward entry keeps the line in 4 bits
    bool SetDispatchRewards(std::uint32_t rewardRow, const char* missionKey, const std::vector<DispatchRewardLine>& lines);
}
