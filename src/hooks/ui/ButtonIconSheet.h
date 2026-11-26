#pragma once

namespace buttoniconsheet
{
    constexpr int kChoiceAuto       = 0;
    constexpr int kChoiceDualShock4 = 1;
    constexpr int kChoiceDualShock3 = 2;
    constexpr int kChoiceXbox       = 3;   // the Xbox 360 sheet
    constexpr int kChoiceXboxOne    = 4;
    constexpr int kChoiceCount      = 5;

    void FollowPad();
    int  GetChoice();
    void SetChoice(int choice);
}

bool Install_ButtonIconSheet_Hook();
void Uninstall_ButtonIconSheet_Hook();
