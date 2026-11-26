#include <pch.h>

#include "EquipCrossLaserBox.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "UiPadLock.h"
#include "log.h"
#include "../equip/LaserSightSwitch.h"

namespace
{
    using UiAnimation_t         = void(__fastcall*)(void* animation);
    using UiInstantAnimation_t  = void(__fastcall*)(void* animation, bool instant);
    using GetLangText_t         = const char*(__fastcall*)(std::uint64_t langId);
    using SetText_t             = void(__fastcall*)(void* textNode, void* textUnits, const char* text, bool fit);
    using SetTextureName_t      = std::uint8_t(__fastcall*)(void* mesh, std::uint64_t pathCode, std::uint64_t slot, int level);
    using GetModel_t            = void*(__fastcall*)(void* layout, std::uint32_t name);
    using ConnectComponent_t    = void(__fastcall*)(void* component, void* parent, std::uint64_t nodeName);
    using GetPadButtonUvShift_t = bool(__fastcall*)(float* u, float* v, bool keyboard, int padButton);
    using SetShaderParameter_t  = void(__fastcall*)(void* mesh, std::uint32_t name, const float* value);
    using SetNodeVisible_t      = void(__fastcall*)(void* node, bool visible);
    using CallEquipSE_t         = void(__fastcall*)(void* cross);
    using SetPanelInfo_t        = void(__fastcall*)(void* cross, void* card, bool);
    using LayoutSetVisible_t    = void(__fastcall*)(void* layout, bool visible);
    using LayoutIsVisible_t     = bool(__fastcall*)(void* layout);
    using KeyHelp_t             = void(__fastcall*)(void* owner, void* mesh, std::uint32_t action, float u, float v);

    struct Engine
    {
        UiAnimation_t         pauseInitAnimation = nullptr;
        UiAnimation_t         startAnimation = nullptr;
        UiInstantAnimation_t  startInstantAnimation = nullptr;
        GetLangText_t         getLangText = nullptr;
        SetText_t             setText = nullptr;
        SetTextureName_t      setTextureName = nullptr;
        GetModel_t            getModel = nullptr;
        ConnectComponent_t    connect = nullptr;
        GetPadButtonUvShift_t padButtonUv = nullptr;
        SetShaderParameter_t  setShaderParameter = nullptr;
        SetNodeVisible_t      setNodeVisible = nullptr;
        CallEquipSE_t         callEquipSE = nullptr;
        SetPanelInfo_t        setPanelInfo = nullptr;
    };

    Engine            g;
    std::atomic<int>  g_Resolved{ 0 };   // 0 not tried, 1 ready, -1 missing on this build

    constexpr std::size_t kCrossHudInfo       = 0xE0;
    constexpr std::size_t kCrossKeyHelp       = 0xF0;
    constexpr std::size_t kCrossLmenu         = 0x110;
    constexpr std::size_t kCrossOptionLayout  = 0x128;
    constexpr std::size_t kCrossPrimaryOpen   = 0x2CF;
    constexpr std::size_t kCrossPrimaryClose  = 0x2D0;
    constexpr std::size_t kCrossSecondaryOpen = 0x2D1;
    constexpr std::size_t kCrossSecondaryClose = 0x2D2;
    constexpr std::size_t kCrossPreview       = 0x2DE;
    constexpr std::size_t kCrossTextUnits     = 0x380;
    constexpr std::size_t kCrossOptionPanels  = 0x1D98;
    constexpr std::size_t kPanelStride        = 0x88;

    constexpr std::size_t kCrossSecondPrimaryCard = 0xE98;
    constexpr std::size_t kCrossPrimaryCard       = 0x1618;
    constexpr std::size_t kCardCannotUse          = 0x1F;
    constexpr std::size_t kLmenuSecondPrimary     = 0x389;
    constexpr std::size_t kCrossPrimaryDetail     = 0x2D7;   // set while the strip's weapon card is the detailed one
    constexpr std::size_t kCrossSecondaryDetail   = 0x2D9;
    constexpr std::size_t kCardSlot               = 0x00;
    constexpr std::size_t kCardMode               = 0x08;
    constexpr std::uint32_t kCardModeRefresh      = 0xE;     // what the game writes before redrawing the card after a light / suppressor switch
    constexpr std::uint32_t kCardSlotPrimary      = 0;
    constexpr std::uint32_t kCardSlotSecondary    = 3;

    constexpr std::size_t kPanelSuppressor   = 0x05;
    constexpr std::size_t kPanelLaser        = 0x06;   // spare byte the game clears on every open, marks our box
    constexpr std::size_t kPanelLight        = 0x07;
    constexpr std::size_t kPanelLayout       = 0x10;
    constexpr std::size_t kPanelShowAnim     = 0x18;
    constexpr std::size_t kPanelOnInstant    = 0x28;
    constexpr std::size_t kPanelOffInstant   = 0x30;
    constexpr std::size_t kPanelOnAnim       = 0x38;
    constexpr std::size_t kPanelOffAnim      = 0x40;
    constexpr std::size_t kPanelPicture      = 0x58;
    constexpr std::size_t kPanelKeyIcon      = 0x60;
    constexpr std::size_t kPanelTitle        = 0x70;
    constexpr std::size_t kPanelOnOffText    = 0x80;
    constexpr std::size_t kPanelResetByte    = 0x04;   // the game zeroes it every time it lays a box out

    constexpr std::size_t kHudWeaponIds       = 0x04;
    constexpr std::size_t kHudOptionWords     = 0xC0;
    constexpr std::size_t kHudSecondSecondary = 0x2C6;

    constexpr std::size_t kLayoutSetVisible = 0xA8;
    constexpr std::size_t kLayoutIsVisible  = 0xD0;
    constexpr std::size_t kKeyHelpSlot      = 0x8C8;

    constexpr std::uint32_t kOptionLayoutModel = 0x90A8D67C;
    constexpr std::uint64_t kAnchorLeft        = 0xB1F58287F3E3;   // x -10, where SUPPRESSOR sits
    constexpr std::uint64_t kAnchorMiddle      = 0xA75F26FF9111;   // x +10, where LIGHT sits
    constexpr std::uint64_t kAnchorRight       = 0xE512B4F7;       // x +30, only the low 32 bits reach the node lookup

    constexpr std::uint64_t kLaserTitle     = 0x30E11995382E;   // name_parts_Laser
    constexpr std::uint64_t kOnText         = 0x05E57DEB1F11;   // the ON caption the light box uses (real name unknown)
    constexpr std::uint64_t kOffText        = 0xAAB86D0FEE1A;   // the OFF caption the light box uses (real name unknown)
    constexpr std::uint64_t kBaseTextureSlot = 0x3BBF9889;
    constexpr int           kTextureLevel   = 2;
    constexpr int           kPadB           = 2;   // fox.PAD_B, the same number picks its icon cell
    constexpr std::uint32_t kKeyIconU       = 0x3E2EF6C2;
    constexpr std::uint32_t kKeyIconV       = 0xA1B43647;

    template <typename T> T Resolved(std::uintptr_t address)
    {
        return reinterpret_cast<T>(ResolveGameAddress(address));
    }

    template <typename T> T At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    void* VirtualAt(void* object, std::size_t slot)
    {
        return (*reinterpret_cast<void***>(object))[slot / sizeof(void*)];
    }

    std::uint8_t* LaserPanel(std::uint8_t* cross)
    {
        return cross + kCrossOptionPanels;   // panel id 0, built and hidden by the game, never used by it
    }

    std::uint16_t* OptionWord(std::uint8_t* hud, std::uint32_t weaponId)
    {
        for (int slot = 0; slot < 2; ++slot)
            if (*reinterpret_cast<std::uint32_t*>(hud + kHudWeaponIds + 4 * slot) == weaponId)
                return reinterpret_cast<std::uint16_t*>(hud + kHudOptionWords + 2 * slot);
        return reinterpret_cast<std::uint16_t*>(hud + kHudOptionWords + 4);   // anything else is the pistol, same as the game picks
    }

    void ApplyLook(std::uint8_t* cross, std::uint8_t* panel, bool on)
    {
        if (on)
        {
            g.pauseInitAnimation(At<void*>(panel, kPanelOffAnim));
            g.startAnimation(At<void*>(panel, kPanelOnAnim));
            g.startInstantAnimation(At<void*>(panel, kPanelOnInstant), true);
        }
        else
        {
            g.pauseInitAnimation(At<void*>(panel, kPanelOnAnim));
            g.startAnimation(At<void*>(panel, kPanelOffAnim));
            g.startInstantAnimation(At<void*>(panel, kPanelOffInstant), true);
        }
        if (const char* caption = g.getLangText(on ? kOnText : kOffText))
            g.setText(At<void*>(panel, kPanelOnOffText), cross + kCrossTextUnits, caption, true);
    }

    void PlaceRightOfVanillaBoxes(std::uint8_t* cross, void* layout)
    {
        const bool lightShown = LaserPanel(cross)[2 * kPanelStride + kPanelLight] != 0;
        const bool suppressorShown = LaserPanel(cross)[1 * kPanelStride + kPanelSuppressor] != 0;
        const std::uint64_t anchor = lightShown ? kAnchorRight : (suppressorShown ? kAnchorMiddle : kAnchorLeft);
        void* model = g.getModel(At<void*>(cross, kCrossOptionLayout), kOptionLayoutModel);
        if (model)
            g.connect(layout, model, anchor);
    }

    void SetKeyIcon(std::uint8_t* cross, void* mesh)
    {
        if (!mesh)
            return;
        float u = 0.0f;
        float v = 0.0f;
        if (!g.padButtonUv(&u, &v, false, kPadB))
        {
            g.setNodeVisible(mesh, false);
            return;
        }
        alignas(16) const float uValue[4] = { u, 0.0f, 0.0f, 0.0f };
        alignas(16) const float vValue[4] = { v, 0.0f, 0.0f, 0.0f };
        g.setShaderParameter(mesh, kKeyIconU, uValue);
        g.setShaderParameter(mesh, kKeyIconV, vValue);
        g.setNodeVisible(mesh, true);
        void* keyHelp = At<void*>(cross, kCrossKeyHelp);
        if (keyHelp)
            reinterpret_cast<KeyHelp_t>(VirtualAt(keyHelp, kKeyHelpSlot))(keyHelp, mesh, LaserSightSwitch::kMenuAction, u, v);
    }

    bool ShowSEH(std::uint8_t* cross, std::uint32_t weaponId, std::uint64_t picture)
    {
        __try
        {
            if (cross[kCrossPreview])
                return false;
            auto* hud = At<std::uint8_t*>(cross, kCrossHudInfo);
            if (!hud || (*OptionWord(hud, weaponId) & LaserSightSwitch::kHudHasLaser) == 0)
                return false;
            std::uint8_t* panel = LaserPanel(cross);
            void* layout = At<void*>(panel, kPanelLayout);
            if (!layout)
                return false;

            PlaceRightOfVanillaBoxes(cross, layout);
            g.startAnimation(At<void*>(panel, kPanelShowAnim));
            g.startInstantAnimation(At<void*>(panel, kPanelOffInstant), true);
            if (picture)
                g.setTextureName(At<void*>(panel, kPanelPicture), picture, kBaseTextureSlot, kTextureLevel);
            panel[kPanelLaser] = 1;
            if (const char* title = g.getLangText(kLaserTitle))
                g.setText(At<void*>(panel, kPanelTitle), cross + kCrossTextUnits, title, true);
            SetKeyIcon(cross, At<void*>(panel, kPanelKeyIcon));
            reinterpret_cast<LayoutSetVisible_t>(VirtualAt(layout, kLayoutSetVisible))(layout, true);
            panel[kPanelResetByte] = 0;
            ApplyLook(cross, panel, (*OptionWord(hud, weaponId) & LaserSightSwitch::kHudLaserOn) != 0);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    int FlipWordSEH(std::uint8_t* cross, std::uint32_t weaponId)
    {
        __try
        {
            auto* hud = At<std::uint8_t*>(cross, kCrossHudInfo);
            if (!hud)
                return -1;
            std::uint16_t* word = OptionWord(hud, weaponId);
            if ((*word & LaserSightSwitch::kHudHasLaser) == 0)
                return -1;
            const bool on = (*word & LaserSightSwitch::kHudLaserOn) == 0;
            *word = on ? static_cast<std::uint16_t>(*word | LaserSightSwitch::kHudLaserOn)
                       : static_cast<std::uint16_t>(*word & ~LaserSightSwitch::kHudLaserOn);
            return on ? 1 : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    bool ApplyToggleSEH(std::uint8_t* cross, bool on)
    {
        __try
        {
            std::uint8_t* panel = LaserPanel(cross);
            void* layout = At<void*>(panel, kPanelLayout);
            if (layout && reinterpret_cast<LayoutIsVisible_t>(VirtualAt(layout, kLayoutIsVisible))(layout))
                ApplyLook(cross, panel, on);
            g.callEquipSE(cross);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint8_t* PrepareCardSEH(std::uint8_t* cross, bool secondary)
    {
        __try
        {
            std::size_t card = kCrossPrimaryCard;
            std::uint32_t slot = kCardSlotPrimary;
            if (secondary)
            {
                if (!cross[kCrossSecondaryDetail])
                    return nullptr;
                slot = kCardSlotSecondary;
            }
            else
            {
                if (!cross[kCrossPrimaryDetail])
                    return nullptr;
                auto* lmenu = At<std::uint8_t*>(cross, kCrossLmenu);
                if (lmenu && lmenu[kLmenuSecondPrimary])
                    card = kCrossSecondPrimaryCard;
            }
            *reinterpret_cast<std::uint32_t*>(cross + card + kCardSlot) = slot;
            *reinterpret_cast<std::uint32_t*>(cross + card + kCardMode) = kCardModeRefresh;
            return cross + card;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void Toggle(std::uint8_t* cross, std::uint32_t weaponId, bool secondary)
    {
        const int on = FlipWordSEH(cross, weaponId);
        if (on < 0)
            return;
        LaserSightSwitch::SetOn(LaserSightSwitch::EquipIdOfEngineId(weaponId), on == 1);
        ApplyToggleSEH(cross, on == 1);
        if (std::uint8_t* card = PrepareCardSEH(cross, secondary))
            g.setPanelInfo(cross, card, false);   // outside SEH: it re-enters the logo hook that holds a lock
    }

    bool MenuKeyPressed()
    {
        bool pressed = false;
        return uipad::Press(LaserSightSwitch::kMenuAction, pressed) && pressed;
    }

    bool PrimaryCardUsableSEH(std::uint8_t* cross)
    {
        __try
        {
            if (!cross[kCrossPrimaryOpen] || cross[kCrossPrimaryClose] || cross[kCrossPreview])
                return false;
            if (LaserPanel(cross)[kPanelLaser] != 1)
                return false;
            auto* lmenu = At<std::uint8_t*>(cross, kCrossLmenu);
            const std::size_t card = (lmenu && lmenu[kLmenuSecondPrimary]) ? kCrossSecondPrimaryCard : kCrossPrimaryCard;
            return cross[card + kCardCannotUse] == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SecondaryCardUsableSEH(std::uint8_t* cross)
    {
        __try
        {
            if (!cross[kCrossSecondaryOpen] || cross[kCrossSecondaryClose] || cross[kCrossPreview])
                return false;
            if (LaserPanel(cross)[kPanelLaser] != 1)
                return false;
            auto* hud = At<std::uint8_t*>(cross, kCrossHudInfo);
            if (!hud || (hud[kHudSecondSecondary] & 1))
                return false;
            return cross[kCrossPrimaryCard + kCardCannotUse] == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t FirstSecondaryWeaponSEH(std::uint8_t* cross)
    {
        __try
        {
            auto* hud = At<std::uint8_t*>(cross, kCrossHudInfo);
            if (!hud || (hud[kHudSecondSecondary] & 1))   // the second pistol has no option keys, and the game shows the first pistol's options for it
                return 0;
            return *reinterpret_cast<std::uint32_t*>(hud + kHudWeaponIds + 8);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }
}

namespace EquipCrossLaserBox
{
    bool Resolve()
    {
        const int state = g_Resolved.load(std::memory_order_acquire);
        if (state != 0)
            return state > 0;

        Engine e;
        e.pauseInitAnimation    = Resolved<UiAnimation_t>(gAddr.Ui_PauseInitUiAnimation);
        e.startAnimation        = Resolved<UiAnimation_t>(gAddr.Ui_StartUiAnimation);
        e.startInstantAnimation = Resolved<UiInstantAnimation_t>(gAddr.Ui_StartInstantUiAnimation);
        e.getLangText           = Resolved<GetLangText_t>(gAddr.Ui_GetLangText);
        e.setText               = Resolved<SetText_t>(gAddr.Ui_SetTextForModelNodeText);
        e.setTextureName        = Resolved<SetTextureName_t>(gAddr.SetTextureName);
        e.getModel              = Resolved<GetModel_t>(gAddr.Layout_GetModel);
        e.connect               = Resolved<ConnectComponent_t>(gAddr.Fox_Ui_ConnectComponent);
        e.padButtonUv           = Resolved<GetPadButtonUvShift_t>(gAddr.Ui_GetPadButtonUvShift);
        e.setShaderParameter    = Resolved<SetShaderParameter_t>(gAddr.ModelNodeMesh_SetShaderParameter);
        e.setNodeVisible        = Resolved<SetNodeVisible_t>(gAddr.ModelNode_SetVisible);
        e.callEquipSE           = Resolved<CallEquipSE_t>(gAddr.EquipCrossEvCall_CallEquipSE);
        e.setPanelInfo          = Resolved<SetPanelInfo_t>(gAddr.EquipCrossEvCall_SetPanelInfo);

        const bool ready = e.pauseInitAnimation && e.startAnimation && e.startInstantAnimation && e.getLangText
                        && e.setText && e.setTextureName && e.getModel && e.connect && e.padButtonUv
                        && e.setShaderParameter && e.setNodeVisible && e.callEquipSE && e.setPanelInfo;
        if (ready)
            g = e;
        g_Resolved.store(ready ? 1 : -1, std::memory_order_release);
        return ready;
    }

    void Show(std::uint8_t* cross, std::uint32_t weaponId)
    {
        if (!cross || weaponId == 0 || !Resolve())
            return;
        const std::uint64_t picture = LaserSightSwitch::BoxPictureForWeapon(LaserSightSwitch::EquipIdOfEngineId(weaponId));
        ShowSEH(cross, weaponId, picture);
    }

    void PollPrimaryKey(std::uint8_t* cross, std::uint32_t weaponId)
    {
        if (!cross || weaponId == 0 || !Resolve() || !PrimaryCardUsableSEH(cross) || !MenuKeyPressed())
            return;
        Toggle(cross, weaponId, false);
    }

    void PollSecondaryKey(std::uint8_t* cross)
    {
        if (!cross || !Resolve() || !SecondaryCardUsableSEH(cross) || !MenuKeyPressed())
            return;
        const std::uint32_t weaponId = FirstSecondaryWeaponSEH(cross);
        if (weaponId)
            Toggle(cross, weaponId, true);
    }

    std::uint32_t FirstSecondaryWeapon(std::uint8_t* cross)
    {
        return cross ? FirstSecondaryWeaponSEH(cross) : 0;
    }
}
