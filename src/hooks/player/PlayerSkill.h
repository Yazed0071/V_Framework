#pragma once

#include <cstdint>

namespace PlayerSkill
{
    constexpr int kSkillCount  = 17;   // SkillControllerImpl skill ids 0-16
    constexpr int kAcceleramin = 10;   // its length belongs to the reflex system, not to a skill timer

    bool IsUsable(int skill);
    bool Start(int skill, int level, float seconds, int usedItem);   // applied at the next HUD update of the player; usedItem = the item whose Use started it, 0 = none
    bool Stop(int skill);
    bool IsActive(int skill);
    bool GetTime(int skill, float& seconds);
    bool ItemGauge(int equipId, int& current, int& max);   // the time bar of the skill that item started, in seconds
    void OnUpdateEquipHud(void* uiController);         // from the UiControllerImpl::UpdateEquipHud hook, outside FOB
    void DropRequests();
}
