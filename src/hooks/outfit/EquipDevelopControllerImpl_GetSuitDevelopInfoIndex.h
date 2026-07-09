#pragma once

namespace outfit
{


    bool Install_OutfitEquippedState_Hooks();
    void Uninstall_OutfitEquippedState_Hooks();


    void* GetCachedEquipDevelopController();


    bool IsFlowIndexDevelopedByOrig(unsigned short flowIndex);


    void MarkDeveloped(unsigned short flowIndex);


    void SuppressDeveloped(unsigned short flowIndex);


    void ClearDevelopedInController(void* controller, unsigned short index);


    void SetDevelopHidden(unsigned short index, bool hidden = true);
    bool IsDevelopHidden(unsigned short index);
}
