#pragma once

namespace outfit
{


    bool Install_OutfitEquippedState_Hooks();
    void Uninstall_OutfitEquippedState_Hooks();


    void* GetCachedEquipDevelopController();


    bool IsFlowIndexDevelopedByOrig(unsigned short flowIndex);
}
