#pragma once


bool Install_MbCommonPopupHook();


bool Uninstall_MbCommonPopupHook();


namespace MbCommonPopup
{
    void* GetCapturedPopupWindow();

    bool ShowPopup(const char* text, unsigned int popupType);
}
