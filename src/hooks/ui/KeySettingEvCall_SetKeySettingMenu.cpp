#include <pch.h>

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "log.h"
#include "KeySettingEvCall_SetKeySettingMenu.h"
#include "OptionListLayoutImage.h"
#include "ButtonIconSheet.h"
#include "ControlTypePageAssign.h"
#include "../sound/CAkParameterNodeBase_SetInitialRTPC.h"

namespace
{
    struct MenuAddresses
    {
        std::uintptr_t setKeySettingMenu;
        std::uintptr_t setListMenu;
        std::uintptr_t getListInfoText;
        std::uintptr_t dispKeySettingParam;
        std::uintptr_t settingParamChanged;
        std::uintptr_t setDecideParam;
        std::uintptr_t setCancelParam;
        std::uintptr_t getConnectNodeName;
        std::uintptr_t connectComponent;
        std::uintptr_t kernelStringFormat;
        std::uintptr_t rowListSite;
        std::uintptr_t rowListResume;
        std::uintptr_t readUilb;
        std::uintptr_t getModelAssetForPause;
        std::uintptr_t visibleKeySetting;
        std::uintptr_t updateKeySetting;
        std::uintptr_t getLayout;
        std::uintptr_t getModel;
        std::uintptr_t getAnimation;
        std::uintptr_t utilGetModel;
        std::uintptr_t utilGetModelNodeText;
        std::uintptr_t setNodeVisible;
        std::uintptr_t startLoopAnimation;
        std::uintptr_t pauseInitAnimation;
    };

    const MenuAddresses* AddressesForBuild()
    {
        static const MenuAddresses addresses = {
            gAddr.KeySettingEvCall_SetKeySettingMenu,
            gAddr.KeySettingEvCall_SetListMenu,
            gAddr.KeySettingEvCall_GetListInfoText,
            gAddr.KeySettingEvCall_DispKeySettingParam,
            gAddr.KeySettingEvCall_SettingParamChanged,
            gAddr.KeySettingEvCall_SetDecideParam,
            gAddr.KeySettingEvCall_SetCancelParam,
            gAddr.KeySettingEvCall_GetConnectNodeName,
            gAddr.Fox_Ui_ConnectComponent,
            gAddr.FoxKernel_StringFormat,
            gAddr.KeySettingEvCall_SetKeySettingMenu_RowListSite,
            gAddr.KeySettingEvCall_SetKeySettingMenu_RowListResume,
            gAddr.FoxUi_ReadUilb,
            gAddr.KeySettingEvCall_GetModelAssetForPause,
            gAddr.KeySettingEvCall_VisibleKeySetting,
            gAddr.KeySettingEvCall_UpdateKeySetting,
            gAddr.Layout_GetLayout,
            gAddr.Layout_GetModel,
            gAddr.Layout_GetAnimation,
            gAddr.UiUtility_GetModel,
            gAddr.UiUtility_GetModelNodeText,
            gAddr.ModelNode_SetVisible,
            gAddr.Ui_StartLoopUiAnimation,
            gAddr.Ui_PauseInitUiAnimation };
        return &addresses;
    }

    using SelfCall_t           = void(__fastcall*)(std::uint8_t* self);
    using RowCall_t            = void(__fastcall*)(std::uint8_t* self, std::uint8_t* row);
    using SetListMenu_t        = const char*(__fastcall*)(std::uint8_t* self, std::uint8_t* row);
    using GetListInfoText_t    = const char*(__fastcall*)(std::uint8_t* self, std::int32_t itemId);
    using GetConnectNodeName_t = std::uint64_t*(__fastcall*)(std::uint8_t* self, std::uint64_t* out, std::uint32_t rowIndex);
    using ConnectComponent_t   = void(__fastcall*)(void* child, void* parent, std::uint64_t nodeName);
    using KernelStringFormat_t = void(__cdecl*)(void* dst, const char* format, ...);
    using SetVisible_t         = void(__fastcall*)(void* component, bool visible);
    using LangIdToKey_t        = std::uint64_t*(__fastcall*)(std::uint64_t* out, const char* langId);
    using GetLangText_t        = const char*(__fastcall*)(std::uint64_t key);
    using ReadUilb_t           = void(__fastcall*)(std::uint8_t* resource, void* package, void* outLayout, void* caarFiles);
    using GetModelAssetForPause_t = bool(__fastcall*)(std::uint8_t* self, void* windowLayout);
    using GetChild_t           = void*(__fastcall*)(void* parent, std::uint32_t name);
    using GetChild64_t         = void*(__fastcall*)(void* parent, std::uint64_t name);
    using SetNodeVisible_t     = void(__fastcall*)(void* node, bool visible);
    using AnimationCall_t      = void(__fastcall*)(void* animation);

    constexpr std::size_t kPageId        = 0xC4;
    constexpr std::size_t kRowCount      = 0xE8;    // page rows plus the OK / CANCEL / DEFAULT row
    constexpr std::size_t kShownValue    = 0xF8;    // the value the menu is about to print
    constexpr std::size_t kParentModel   = 0x140;
    constexpr std::size_t kBars          = 0xAD8;   // 4 bar graphs, 0x48 each, the layout component sits at +8
    constexpr std::size_t kBarStride     = 0x48;
    constexpr std::size_t kBarComponent  = 0x8;
    constexpr int         kBarCount      = 4;
    constexpr std::size_t kRows          = 0xBF8;
    constexpr std::size_t kRowStride     = 0x70;
    constexpr std::size_t kRowOriginal   = 0x4;
    constexpr std::size_t kRowCurrent    = 0x8;
    constexpr std::size_t kRowChoices    = 0xC;
    constexpr std::size_t kRowDefault    = 0x10;
    constexpr std::size_t kRowBar        = 0x48;
    constexpr std::size_t kRowValueText  = 0x50;
    constexpr std::size_t kSetVisibleSlot = 0xA8 / 8;
    constexpr std::int32_t kSoundPage    = 6;
    constexpr std::size_t kState         = 0xCC;
    constexpr std::int32_t kStateClosing = 0xD;
    constexpr std::size_t kListLayout    = 0x138;

    constexpr std::size_t kResourceFile  = 0x30;   // the layout resource's raw file, its image pointer sits at +0x60
    constexpr std::size_t kFileImage     = 0x60;

    constexpr std::size_t kBarLayout     = 0x08;
    constexpr std::size_t kBarModel      = 0x10;
    constexpr std::size_t kBarCursorOn   = 0x18;
    constexpr std::size_t kBarCursorOff  = 0x20;
    constexpr std::size_t kBarValueText  = 0x28;
    constexpr std::size_t kBarGauge      = 0x30;
    constexpr std::size_t kBarGaugeFill  = 0x38;
    constexpr std::size_t kBarLoop       = 0x40;
    constexpr int         kExtraBarCount = 4;

    constexpr std::uint32_t kExtraBarNames[kExtraBarCount] = {
        0x87121C25u,   // UI_sys_pus_opt_lyt_bgrh_5
        0x4C153EE2u,   // UI_sys_pus_opt_lyt_bgrh_6
        0xF7726121u,   // UI_sys_pus_opt_lyt_bgrh_7
        0x381F8B25u,   // UI_sys_pus_opt_lyt_bgrh_8
    };
    constexpr std::uint32_t kBarModelName      = 0xE0C02498u;        // UI_sys_pus_opt_lyt_bgrh
    constexpr std::uint64_t kBarCursorOnName   = 0x329AE75D2598ull;  // hash of the bar's cursor on animation (real name unknown)
    constexpr std::uint64_t kBarCursorOffName  = 0xD4A01E4C4E01ull;  // hash of the bar's cursor off animation (real name unknown)
    constexpr std::uint64_t kBarBodyName       = 0xD11640DB2008ull;  // UI_sys_pus_opt_bgrh_body
    constexpr std::uint64_t kBarValueTextName  = 0xA97229859011ull;  // hash of the bar's value text node (real name unknown)
    constexpr std::uint32_t kBarGaugeName      = 0xBEEB85EAu;        // UI_sys_pus_opt_bgrh
    constexpr std::uint64_t kBarGaugeFillName  = 0x9D0544A508DAull;  // UI_sys_pus_opt_bgrh_scle
    constexpr std::uint64_t kBarLoopName       = 0x69362415DD5Cull;  // hash of the bar's noise loop animation (real name unknown)

    constexpr std::size_t kListLayoutHeaderBytes = 0x30;

    constexpr std::int32_t kItemMaster      = 0x39;
    constexpr std::int32_t kItemMusic       = 0x28;   // the game's own unused BGM volume row
    constexpr std::int32_t kItemVoice       = 0x3A;
    constexpr std::int32_t kItemSoundEffect = 0x3B;
    constexpr std::int32_t kItemAmbience    = 0x3C;
    constexpr std::int32_t kItemUi          = 0x3D;
    constexpr std::int32_t kItemTape        = 0x3E;
    constexpr std::int32_t kItemCustomSoundtrack = 0x1F;
    constexpr std::int32_t kItemAudioGuidance    = 0x27;

    std::int32_t g_SoundPageItems[] = {
        kItemMaster, kItemMusic, kItemVoice, kItemSoundEffect, kItemAmbience, kItemUi,
        kItemTape, kItemCustomSoundtrack, kItemAudioGuidance, 0 };

    constexpr std::int32_t kItemButtonIcons = 0x3F;

    std::int32_t g_ControlsPageItems[] = { 3, 5, 9, 0xA, 7, 4, 2, kItemButtonIcons, 0 };   // the game's own seven controls rows, then ours

    std::int32_t g_ControlTypePageItems[] = { 1, 0, 0 };   // the game's own Control Type row, then ASSIGN BUTTON while it is offered

    constexpr const char* kButtonIconValueText[buttoniconsheet::kChoiceCount] = {
        "V_option_button_icons_auto", "V_option_button_icons_playstation", "V_option_button_icons_playstation3",
        "V_option_button_icons_xbox", "V_option_button_icons_xbox_one" };

    constexpr std::uint8_t kRowListSiteBytes[5] = { 0x48, 0x8D, 0x5C, 0x24, 0x58 };   // lea rbx,[rsp+0x58], the sound page item list
    constexpr std::uint8_t kControlsRowListSiteBytes[5] = { 0x48, 0x8D, 0x5C, 0x24, 0x70 };   // lea rbx,[rsp+0x70], the controls page item list
    constexpr std::uint8_t kControlTypeRowListSiteBytes[5] = { 0x48, 0x8D, 0x5C, 0x24, 0x38 };   // lea rbx,[rsp+0x38], the Control Type page item list

    struct ExpectedBytes
    {
        std::uintptr_t MenuAddresses::* field;
        const char*    name;
        std::uint8_t   bytes[5];
    };

    constexpr ExpectedBytes kExpected[] = {
        { &MenuAddresses::setKeySettingMenu, "AudioVolume: SetKeySettingMenu", { 0x48, 0x89, 0x5C, 0x24, 0x10 } },
        { &MenuAddresses::setListMenu, "AudioVolume: SetListMenu", { 0x40, 0x53, 0x55, 0x48, 0x83 } },
        { &MenuAddresses::getListInfoText, "AudioVolume: GetListInfoText", { 0xFF, 0xCA, 0x83, 0xFA, 0x37 } },
        { &MenuAddresses::dispKeySettingParam, "AudioVolume: DispKeySettingParam", { 0x48, 0x85, 0xD2, 0x0F, 0x84 } },
        { &MenuAddresses::settingParamChanged, "AudioVolume: SettingParamChanged", { 0x48, 0x85, 0xD2, 0x0F, 0x84 } },
        { &MenuAddresses::setDecideParam, "AudioVolume: SetDecideParam", { 0x40, 0x53, 0x57, 0x48, 0x81 } },
        { &MenuAddresses::setCancelParam, "AudioVolume: SetCancelParam", { 0x40, 0x55, 0x48, 0x83, 0xEC } },
        { &MenuAddresses::getConnectNodeName, "AudioVolume: GetConnectNodeName", { 0x48, 0x81, 0xEC, 0x98, 0x00 } },
        { &MenuAddresses::connectComponent, "AudioVolume: ConnectComponent", { 0x48, 0x85, 0xC9, 0x74, 0x3E } },
        { &MenuAddresses::kernelStringFormat, "AudioVolume: KernelStringFormat", { 0x4C, 0x8B, 0xDC, 0x49, 0x89 } },
        { &MenuAddresses::readUilb, "AudioVolume: ReadUilb", { 0x40, 0x55, 0x53, 0x56, 0x57 } },
        { &MenuAddresses::getModelAssetForPause, "AudioVolume: GetModelAssetForPause", { 0x48, 0x8B, 0xC4, 0x55, 0x56 } },
        { &MenuAddresses::visibleKeySetting, "AudioVolume: VisibleKeySetting", { 0x48, 0x89, 0x5C, 0x24, 0x08 } },
        { &MenuAddresses::updateKeySetting, "AudioVolume: UpdateKeySetting", { 0x40, 0x55, 0x53, 0x56, 0x57 } },
        { &MenuAddresses::getLayout, "AudioVolume: GetLayout", { 0x48, 0x8B, 0x81, 0xD8, 0x00 } },
        { &MenuAddresses::getModel, "AudioVolume: GetModel", { 0x48, 0x8B, 0x81, 0xC8, 0x00 } },
        { &MenuAddresses::getAnimation, "AudioVolume: GetAnimation", { 0x48, 0x8B, 0x81, 0xA8, 0x00 } },
        { &MenuAddresses::utilGetModel, "AudioVolume: UtilGetModel", { 0x48, 0x89, 0x54, 0x24, 0x10 } },
        { &MenuAddresses::utilGetModelNodeText, "AudioVolume: UtilGetModelNodeText", { 0x48, 0x89, 0x54, 0x24, 0x10 } },
        { &MenuAddresses::setNodeVisible, "AudioVolume: SetNodeVisible", { 0x84, 0xD2, 0x74, 0x06, 0x66 } },
        { &MenuAddresses::startLoopAnimation, "AudioVolume: StartLoopAnimation", { 0x40, 0x53, 0x48, 0x83, 0xEC } },
        { &MenuAddresses::pauseInitAnimation, "AudioVolume: PauseInitAnimation", { 0x40, 0x53, 0x48, 0x83, 0xEC } },
    };

    struct VolumeRow
    {
        std::int32_t    item;
        AudioVolumeType type;
        const char*     label;
        const char*     info;
    };

    constexpr VolumeRow kVolumeRows[] = {
        { kItemMaster,      VOLUME_MASTER,       "option_master_volume",  "V_option_master_volume_info" },
        { kItemMusic,       VOLUME_MUSIC,        "V_option_bgm_volume",   "V_option_bgm_volume_info" },
        { kItemVoice,       VOLUME_VOICE,        "V_option_voice_volume", "V_option_voice_volume_info" },
        { kItemSoundEffect, VOLUME_SOUND_EFFECT, "V_option_se_volume",    "V_option_se_volume_info" },
        { kItemAmbience,    VOLUME_AMBIENCE,     "V_option_ambience_volume", "V_option_ambience_volume_info" },
        { kItemUi,          VOLUME_UI,           "V_option_ui_volume",    "V_option_ui_volume_info" },
        { kItemTape,        VOLUME_TAPE,         "V_option_tape_volume",  "V_option_tape_volume_info" },
    };

    const MenuAddresses* g_Addr = nullptr;

    SelfCall_t           g_OrigSetKeySettingMenu   = nullptr;
    SetListMenu_t        g_OrigSetListMenu         = nullptr;
    GetListInfoText_t    g_OrigGetListInfoText     = nullptr;
    RowCall_t            g_OrigDispKeySettingParam = nullptr;
    RowCall_t            g_OrigSettingParamChanged = nullptr;
    SelfCall_t           g_OrigSetDecideParam      = nullptr;
    SelfCall_t           g_OrigSetCancelParam      = nullptr;
    GetConnectNodeName_t g_GetConnectNodeName      = nullptr;
    ConnectComponent_t   g_ConnectComponent        = nullptr;
    KernelStringFormat_t g_KernelStringFormat      = nullptr;
    LangIdToKey_t        g_LangIdToKey             = nullptr;
    GetLangText_t        g_GetLangText             = nullptr;

    ReadUilb_t              g_OrigReadUilb              = nullptr;
    GetModelAssetForPause_t g_OrigGetModelAssetForPause = nullptr;
    SelfCall_t              g_OrigVisibleKeySetting     = nullptr;
    SelfCall_t              g_OrigUpdateKeySetting      = nullptr;
    GetChild_t              g_GetLayout                 = nullptr;
    GetChild_t              g_GetModel                  = nullptr;
    GetChild64_t            g_GetAnimation              = nullptr;
    GetChild64_t            g_UtilGetModel              = nullptr;
    GetChild64_t            g_UtilGetModelNodeText      = nullptr;
    SetNodeVisible_t        g_SetNodeVisible            = nullptr;
    AnimationCall_t         g_StartLoopAnimation        = nullptr;
    AnimationCall_t         g_PauseInitAnimation        = nullptr;

    struct RowListPatch
    {
        std::uint8_t* site = nullptr;
        std::uint8_t  original[5] = {};
        std::uint8_t  jump[5] = {};
    };

    void*         g_HookTargets[11]     = {};
    RowListPatch  g_SoundRowList;
    RowListPatch  g_ControlsRowList;
    RowListPatch  g_ControlTypeRowList;
    bool          g_RowListPatched      = false;
    std::atomic<bool> g_MissingTextWarned{ false };

    alignas(16) std::uint8_t g_ExtendedLayout[sizeof(kOptionListLayoutExtended)];
    alignas(16) std::uint8_t g_ExtraBars[kExtraBarCount][kBarStride];
    std::uint8_t*            g_ExtraBarsOwner = nullptr;

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    const VolumeRow* FindVolumeRow(std::int32_t item)
    {
        for (const VolumeRow& r : kVolumeRows)
            if (r.item == item)
                return &r;
        return nullptr;
    }

    bool IsSoundPage(std::uint8_t* self)
    {
        return g_RowListPatched && self && At<std::int32_t>(self, kPageId) == kSoundPage;
    }

    std::uint8_t* RowAt(std::uint8_t* self, int index)
    {
        return self + kRows + static_cast<std::size_t>(index) * kRowStride;
    }

    int PageRowCount(std::uint8_t* self)
    {
        const int count = At<std::int32_t>(self, kRowCount) - 1;
        return count < 0 ? 0 : (count > 16 ? 16 : count);
    }

    bool IsPageRow(std::uint8_t* self, std::uint8_t* row)
    {
        const std::uint8_t* first = self + kRows;
        if (row < first)
            return false;
        const std::size_t offset = static_cast<std::size_t>(row - first);
        return offset % kRowStride == 0 && static_cast<int>(offset / kRowStride) < PageRowCount(self);
    }

    const char* LangText(const char* langId)
    {
        std::uint64_t key = 0;
        const char* text = nullptr;
        __try
        {
            if (g_LangIdToKey(&key, langId) && key)
                text = g_GetLangText(key);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            text = nullptr;
        }
        if (text && text[0])
            return text;
        if (!g_MissingTextWarned.exchange(true))
            LogWarn("AudioVolume: no text for %s, is the V_FrameWork lang pack installed?", langId);
        return langId;
    }

    bool IsVanillaListLayout(const std::uint8_t* image)
    {
        __try
        {
            return std::memcmp(image, kOptionListLayoutVanilla, kListLayoutHeaderBytes) == 0
                && std::memcmp(image, kOptionListLayoutVanilla, sizeof(kOptionListLayoutVanilla)) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hk_ReadUilb(std::uint8_t* resource, void* package, void* outLayout, void* caarFiles)
    {
        std::uint8_t* file = resource ? At<std::uint8_t*>(resource, kResourceFile) : nullptr;
        std::uint8_t* image = file ? At<std::uint8_t*>(file, kFileImage) : nullptr;
        if (!image || !IsVanillaListLayout(image))
        {
            g_OrigReadUilb(resource, package, outLayout, caarFiles);
            return;
        }
        At<std::uint8_t*>(file, kFileImage) = g_ExtendedLayout;
        g_OrigReadUilb(resource, package, outLayout, caarFiles);
        At<std::uint8_t*>(file, kFileImage) = image;
    }

    std::uint8_t* ExtraBar(std::uint8_t* self, int index)
    {
        return g_ExtraBarsOwner == self ? g_ExtraBars[index] : nullptr;
    }

    void FillExtraBars(std::uint8_t* self)
    {
        g_ExtraBarsOwner = nullptr;
        void* list = At<void*>(self, kListLayout);
        if (!list)
            return;
        for (int i = 0; i < kExtraBarCount; ++i)
        {
            std::uint8_t* bar = g_ExtraBars[i];
            std::memset(bar, 0, kBarStride);
            void* layout      = g_GetLayout(list, kExtraBarNames[i]);
            void* model       = layout ? g_GetModel(layout, kBarModelName) : nullptr;
            void* body        = layout ? g_UtilGetModel(layout, kBarBodyName) : nullptr;
            void* text        = body ? g_UtilGetModelNodeText(body, kBarValueTextName) : nullptr;
            void* gaugeLayout = layout ? g_GetLayout(layout, kBarGaugeName) : nullptr;
            void* gauge       = gaugeLayout ? g_GetModel(gaugeLayout, kBarGaugeName) : nullptr;
            if (!model || !text || !gauge)
                return;
            At<void*>(bar, kBarLayout)    = layout;
            At<void*>(bar, kBarModel)     = model;
            At<void*>(bar, kBarCursorOn)  = g_GetAnimation(model, kBarCursorOnName);
            At<void*>(bar, kBarCursorOff) = g_GetAnimation(model, kBarCursorOffName);
            At<void*>(bar, kBarValueText) = text;
            At<void*>(bar, kBarGauge)     = gauge;
            At<void*>(bar, kBarGaugeFill) = g_GetAnimation(gauge, kBarGaugeFillName);
            At<void*>(bar, kBarLoop)      = g_GetAnimation(gauge, kBarLoopName);
            g_SetNodeVisible(text, false);
        }
        g_ExtraBarsOwner = self;
    }

    int BarSlotCount(std::uint8_t* self)
    {
        return kBarCount + (g_ExtraBarsOwner == self ? kExtraBarCount : 0);
    }

    std::uint8_t* BarSlot(std::uint8_t* self, int slot)
    {
        return slot < kBarCount ? self + kBars + static_cast<std::size_t>(slot) * kBarStride
                                : ExtraBar(self, slot - kBarCount);
    }

    void SetBarVisible(std::uint8_t* barBase, bool visible)
    {
        void* component = barBase ? At<void*>(barBase, kBarComponent) : nullptr;
        if (component)
            (*reinterpret_cast<SetVisible_t**>(component))[kSetVisibleSlot](component, visible);
    }

    bool RowWantsBar(std::int32_t item)
    {
        return FindVolumeRow(item) || item == kItemCustomSoundtrack;
    }

    void AttachBars(std::uint8_t* self)
    {
        auto* parent = At<void*>(self, kParentModel);
        const int slots = BarSlotCount(self);
        int bar = 0;
        for (int i = 0; i < PageRowCount(self); ++i)
        {
            std::uint8_t* row = RowAt(self, i);
            std::uint8_t* barBase = bar < slots && RowWantsBar(At<std::int32_t>(row, 0)) ? BarSlot(self, bar) : nullptr;
            void* component = barBase ? At<void*>(barBase, kBarComponent) : nullptr;
            if (!component)
            {
                At<void*>(row, kRowBar) = nullptr;
                continue;
            }
            SetBarVisible(barBase, true);
            std::uint64_t nodeName = 0;
            g_ConnectComponent(component, parent, *g_GetConnectNodeName(self, &nodeName, static_cast<std::uint32_t>(i)));
            At<void*>(row, kRowBar) = barBase;
            ++bar;
        }
        for (; bar < slots; ++bar)
            SetBarVisible(BarSlot(self, bar), false);
    }

    void HideExtraBars(std::uint8_t* self)
    {
        for (int i = 0; i < kExtraBarCount; ++i)
            SetBarVisible(ExtraBar(self, i), false);
    }

    bool __fastcall hk_GetModelAssetForPause(std::uint8_t* self, void* windowLayout)
    {
        const bool ok = g_OrigGetModelAssetForPause(self, windowLayout);
        if (ok && self)
            FillExtraBars(self);
        return ok;
    }

    void __fastcall hk_VisibleKeySetting(std::uint8_t* self)
    {
        g_OrigVisibleKeySetting(self);
        for (int i = 0; i < kExtraBarCount; ++i)
            if (std::uint8_t* bar = ExtraBar(self, i))
                if (void* loop = At<void*>(bar, kBarLoop))
                    g_StartLoopAnimation(loop);
    }

    void __fastcall hk_UpdateKeySetting(std::uint8_t* self)
    {
        if (!self)
        {
            g_OrigUpdateKeySetting(self);
            return;
        }
        const std::int32_t before = At<std::int32_t>(self, kState);
        g_OrigUpdateKeySetting(self);
        if (before != kStateClosing || At<std::int32_t>(self, kState) == kStateClosing)
            return;
        for (int i = 0; i < kExtraBarCount; ++i)   // the window just stopped its own 4 bars' loops
            if (std::uint8_t* bar = ExtraBar(self, i))
                if (void* loop = At<void*>(bar, kBarLoop))
                    g_PauseInitAnimation(loop);
    }

    void __fastcall hk_SetKeySettingMenu(std::uint8_t* self)
    {
        g_ControlTypePageItems[1] = g_ControlTypeRowList.site && controltypepage::RowIsOffered() ? controltypepage::kItemAssignButton : 0;
        g_OrigSetKeySettingMenu(self);
        if (!IsSoundPage(self))
        {
            HideExtraBars(self);
            return;
        }
        AttachBars(self);
        AudioVolume_ApplySaved();
    }

    const char* __fastcall hk_SetListMenu(std::uint8_t* self, std::uint8_t* row)
    {
        if (row && At<std::int32_t>(row, 0) == kItemButtonIcons)
        {
            const int choice = buttoniconsheet::GetChoice();
            At<std::int32_t>(row, kRowOriginal) = choice;
            At<std::int32_t>(row, kRowCurrent)  = choice;
            At<std::int32_t>(row, kRowChoices)  = buttoniconsheet::kChoiceCount;
            At<std::int32_t>(row, kRowDefault)  = buttoniconsheet::kChoiceAuto;
            return LangText("V_option_button_icons");
        }
        if (row && At<std::int32_t>(row, 0) == controltypepage::kItemAssignButton)
            return controltypepage::OpenRow(self, row);
        const VolumeRow* volume = row ? FindVolumeRow(At<std::int32_t>(row, 0)) : nullptr;
        if (!volume)
            return g_OrigSetListMenu(self, row);
        const int index = AudioVolume_Get(volume->type) - kAudioVolumeMin;
        At<std::int32_t>(row, kRowOriginal) = index;
        At<std::int32_t>(row, kRowCurrent)  = index;
        At<std::int32_t>(row, kRowChoices)  = kAudioVolumeMax - kAudioVolumeMin + 1;
        At<std::int32_t>(row, kRowDefault)  = kAudioVolumeDefault - kAudioVolumeMin;
        return LangText(volume->label);
    }

    const char* __fastcall hk_GetListInfoText(std::uint8_t* self, std::int32_t itemId)
    {
        if (itemId == kItemButtonIcons)
            return LangText("V_option_button_icons_info");
        if (itemId == controltypepage::kItemAssignButton)
            return controltypepage::RowHelp(self);
        const VolumeRow* volume = FindVolumeRow(itemId);
        return volume ? LangText(volume->info) : g_OrigGetListInfoText(self, itemId);
    }

    void __fastcall hk_DispKeySettingParam(std::uint8_t* self, std::uint8_t* row)
    {
        if (row && FindVolumeRow(At<std::int32_t>(row, 0)))
            g_KernelStringFormat(row + kRowValueText, "%d", At<std::int32_t>(self, kShownValue) + kAudioVolumeMin);
        else if (row && At<std::int32_t>(row, 0) == kItemButtonIcons)
        {
            std::int32_t shown = At<std::int32_t>(self, kShownValue);
            if (shown < 0 || shown >= buttoniconsheet::kChoiceCount)
                shown = buttoniconsheet::kChoiceAuto;
            g_KernelStringFormat(row + kRowValueText, "%s", LangText(kButtonIconValueText[shown]));
        }
        else if (row && At<std::int32_t>(row, 0) == controltypepage::kItemAssignButton)
            g_KernelStringFormat(row + kRowValueText, "%s", controltypepage::RowValue(self, At<std::int32_t>(self, kShownValue)));
        g_OrigDispKeySettingParam(self, row);
    }

    void __fastcall hk_SettingParamChanged(std::uint8_t* self, std::uint8_t* row)
    {
        g_OrigSettingParamChanged(self, row);
        controltypepage::RowChanged(self, row);
        if (!IsSoundPage(self) || !row || !IsPageRow(self, row))
            return;
        if (const VolumeRow* volume = FindVolumeRow(At<std::int32_t>(row, 0)))
            AudioVolume_Preview(volume->type, At<std::int32_t>(row, kRowCurrent) + kAudioVolumeMin);
    }

    void __fastcall hk_SetDecideParam(std::uint8_t* self)
    {
        g_OrigSetDecideParam(self);
        if (!self)
            return;
        controltypepage::Kept(self);
        if (g_ControlsRowList.site)
            for (int i = 0; i < PageRowCount(self); ++i)
                if (std::uint8_t* row = RowAt(self, i); At<std::int32_t>(row, 0) == kItemButtonIcons)
                    buttoniconsheet::SetChoice(At<std::int32_t>(row, kRowCurrent));
        if (!IsSoundPage(self))
            return;
        for (int i = 0; i < PageRowCount(self); ++i)
        {
            std::uint8_t* row = RowAt(self, i);
            if (const VolumeRow* volume = FindVolumeRow(At<std::int32_t>(row, 0)))
                AudioVolume_Commit(volume->type, At<std::int32_t>(row, kRowCurrent) + kAudioVolumeMin);
        }
    }

    void __fastcall hk_SetCancelParam(std::uint8_t* self)
    {
        g_OrigSetCancelParam(self);
        controltypepage::Restored(self);
        if (IsSoundPage(self))
            AudioVolume_ApplySaved();
    }

    bool WriteSite(std::uint8_t* site, const std::uint8_t* bytes, std::size_t size)
    {
        DWORD old = 0;
        if (!VirtualProtect(site, size, PAGE_EXECUTE_READWRITE, &old))
            return false;
        std::memcpy(site, bytes, size);
        VirtualProtect(site, size, old, &old);
        FlushInstructionCache(GetCurrentProcess(), site, size);
        return true;
    }

    bool PatchRowList(std::uintptr_t siteAddress, const std::uint8_t (&expected)[5], const std::int32_t* itemList,
                      std::uint32_t itemCount, RowListPatch& patch)
    {
        auto* site = static_cast<std::uint8_t*>(ResolveGameAddress(siteAddress));
        const auto resume = reinterpret_cast<std::uintptr_t>(ResolveGameAddress(g_Addr->rowListResume));
        if (!site || !resume || std::memcmp(site, expected, sizeof(expected)) != 0)
            return false;

        std::uint8_t cave[29] = {};
        const auto items    = reinterpret_cast<std::uint64_t>(itemList);
        const auto count    = itemCount;
        const auto resume64 = static_cast<std::uint64_t>(resume);
        cave[0] = 0x48; cave[1] = 0xBB;                                      // mov rbx, items
        std::memcpy(cave + 2, &items, 8);
        cave[10] = 0xBF;                                                     // mov edi, count
        std::memcpy(cave + 11, &count, 4);
        cave[15] = 0xFF; cave[16] = 0x25;                                    // jmp [rip+0] to the 8 bytes right after, back into the page build
        std::memcpy(cave + 21, &resume64, 8);

        auto* thunk = static_cast<std::uint8_t*>(HookArena::AllocateNear(reinterpret_cast<std::uintptr_t>(site), sizeof(cave)));
        if (!thunk)
            return false;
        std::memcpy(thunk, cave, sizeof(cave));
        FlushInstructionCache(GetCurrentProcess(), thunk, sizeof(cave));

        const std::int64_t rel = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(thunk))
                               - static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(site) + 5);
        if (rel < INT32_MIN || rel > INT32_MAX)
            return false;
        std::uint8_t jump[5] = { 0xE9 };
        const auto rel32 = static_cast<std::int32_t>(rel);
        std::memcpy(jump + 1, &rel32, 4);

        std::memcpy(patch.original, site, sizeof(patch.original));
        std::memcpy(patch.jump, jump, sizeof(jump));
        if (!WriteSite(site, jump, sizeof(jump)))
            return false;
        patch.site = site;
        return true;
    }

    void UnpatchRowList(RowListPatch& patch, const char* page)
    {
        if (!patch.site)
            return;
        std::uint8_t live[sizeof(patch.jump)] = {};
        if (!ReadEntryBytesSEH(patch.site, live, sizeof(live)) || std::memcmp(live, patch.jump, sizeof(live)) != 0)
            LogWarn("AudioVolume: %s page row list site no longer holds our jump (another module changed it), left as it is", page);
        else
            WriteSite(patch.site, patch.original, sizeof(patch.original));
        patch.site = nullptr;
    }

    bool Hook(int slot, std::uintptr_t addr, void* detour, void** original)
    {
        void* target = ResolveGameAddress(addr);
        if (!target || !CreateAndEnableHook(target, detour, original))
            return false;
        g_HookTargets[slot] = target;
        return true;
    }
}

bool Install_KeySettingEvCall_SetKeySettingMenu_Hooks()
{
    if (g_RowListPatched)
        return true;

    g_Addr = AddressesForBuild();
    if (!g_Addr)
        return false;
    for (const ExpectedBytes& e : kExpected)
    {
        const void* at = ResolveGameAddress(g_Addr->*(e.field));
        const HookEntry entry = CheckHookEntry(at, e.bytes, sizeof(e.bytes));
        if (entry == HookEntry::Unknown)
        {
            LogError("AudioVolume: options menu code differs (another mod patched it?), volume rows off");
            return false;
        }
        if (entry == HookEntry::ForeignHook)
            LogHookedOnTop(e.name, at);
    }

    g_GetConnectNodeName = reinterpret_cast<GetConnectNodeName_t>(ResolveGameAddress(g_Addr->getConnectNodeName));
    g_ConnectComponent   = reinterpret_cast<ConnectComponent_t>(ResolveGameAddress(g_Addr->connectComponent));
    g_KernelStringFormat = reinterpret_cast<KernelStringFormat_t>(ResolveGameAddress(g_Addr->kernelStringFormat));
    g_LangIdToKey        = reinterpret_cast<LangIdToKey_t>(ResolveGameAddress(gAddr.Ui_LangIdToKey));
    g_GetLangText        = reinterpret_cast<GetLangText_t>(ResolveGameAddress(gAddr.Ui_GetLangText));
    g_GetLayout             = reinterpret_cast<GetChild_t>(ResolveGameAddress(g_Addr->getLayout));
    g_GetModel              = reinterpret_cast<GetChild_t>(ResolveGameAddress(g_Addr->getModel));
    g_GetAnimation          = reinterpret_cast<GetChild64_t>(ResolveGameAddress(g_Addr->getAnimation));
    g_UtilGetModel          = reinterpret_cast<GetChild64_t>(ResolveGameAddress(g_Addr->utilGetModel));
    g_UtilGetModelNodeText  = reinterpret_cast<GetChild64_t>(ResolveGameAddress(g_Addr->utilGetModelNodeText));
    g_SetNodeVisible        = reinterpret_cast<SetNodeVisible_t>(ResolveGameAddress(g_Addr->setNodeVisible));
    g_StartLoopAnimation    = reinterpret_cast<AnimationCall_t>(ResolveGameAddress(g_Addr->startLoopAnimation));
    g_PauseInitAnimation    = reinterpret_cast<AnimationCall_t>(ResolveGameAddress(g_Addr->pauseInitAnimation));
    if (!g_GetConnectNodeName || !g_ConnectComponent || !g_KernelStringFormat || !g_LangIdToKey || !g_GetLangText
        || !g_GetLayout || !g_GetModel || !g_GetAnimation || !g_UtilGetModel || !g_UtilGetModelNodeText
        || !g_SetNodeVisible || !g_StartLoopAnimation || !g_PauseInitAnimation)
    {
        LogError("AudioVolume: options menu addresses missing, volume rows off");
        return false;
    }
    std::memcpy(g_ExtendedLayout, kOptionListLayoutExtended, sizeof(kOptionListLayoutExtended));

    const bool hooked =
        Hook(0, g_Addr->setListMenu,         reinterpret_cast<void*>(&hk_SetListMenu),         reinterpret_cast<void**>(&g_OrigSetListMenu))
     && Hook(1, g_Addr->getListInfoText,     reinterpret_cast<void*>(&hk_GetListInfoText),     reinterpret_cast<void**>(&g_OrigGetListInfoText))
     && Hook(2, g_Addr->dispKeySettingParam, reinterpret_cast<void*>(&hk_DispKeySettingParam), reinterpret_cast<void**>(&g_OrigDispKeySettingParam))
     && Hook(3, g_Addr->settingParamChanged, reinterpret_cast<void*>(&hk_SettingParamChanged), reinterpret_cast<void**>(&g_OrigSettingParamChanged))
     && Hook(4, g_Addr->setDecideParam,      reinterpret_cast<void*>(&hk_SetDecideParam),      reinterpret_cast<void**>(&g_OrigSetDecideParam))
     && Hook(5, g_Addr->setCancelParam,      reinterpret_cast<void*>(&hk_SetCancelParam),      reinterpret_cast<void**>(&g_OrigSetCancelParam))
     && Hook(6, g_Addr->setKeySettingMenu,   reinterpret_cast<void*>(&hk_SetKeySettingMenu),   reinterpret_cast<void**>(&g_OrigSetKeySettingMenu))
     && Hook(7, g_Addr->readUilb,            reinterpret_cast<void*>(&hk_ReadUilb),            reinterpret_cast<void**>(&g_OrigReadUilb))
     && Hook(8, g_Addr->getModelAssetForPause, reinterpret_cast<void*>(&hk_GetModelAssetForPause), reinterpret_cast<void**>(&g_OrigGetModelAssetForPause))
     && Hook(9, g_Addr->visibleKeySetting,   reinterpret_cast<void*>(&hk_VisibleKeySetting),   reinterpret_cast<void**>(&g_OrigVisibleKeySetting))
     && Hook(10, g_Addr->updateKeySetting,   reinterpret_cast<void*>(&hk_UpdateKeySetting),    reinterpret_cast<void**>(&g_OrigUpdateKeySetting));
    if (!hooked)
    {
        Uninstall_KeySettingEvCall_SetKeySettingMenu_Hooks();
        LogError("AudioVolume: options menu hook failed, volume rows off");
        return false;
    }

    if (!PatchRowList(g_Addr->rowListSite, kRowListSiteBytes, g_SoundPageItems,
                      static_cast<std::uint32_t>(sizeof(g_SoundPageItems) / sizeof(g_SoundPageItems[0])), g_SoundRowList))
    {
        Uninstall_KeySettingEvCall_SetKeySettingMenu_Hooks();
        LogError("AudioVolume: sound page code differs (another mod patched it?), volume rows off");
        return false;
    }
    g_RowListPatched = true;

    if (!PatchRowList(gAddr.KeySettingEvCall_SetKeySettingMenu_ControlsRowListSite, kControlsRowListSiteBytes,
                      g_ControlsPageItems,
                      static_cast<std::uint32_t>(sizeof(g_ControlsPageItems) / sizeof(g_ControlsPageItems[0])),
                      g_ControlsRowList))
        LogError("ButtonIcons: controls page code differs on %s (another mod patched it?), no Button Icons row",
                 GetGameBuildName(gGameBuild));
    if (!PatchRowList(gAddr.KeySettingEvCall_SetKeySettingMenu_ControlTypeRowListSite, kControlTypeRowListSiteBytes,
                      g_ControlTypePageItems,
                      static_cast<std::uint32_t>(sizeof(g_ControlTypePageItems) / sizeof(g_ControlTypePageItems[0])),
                      g_ControlTypeRowList))
        LogError("ControlTypePage: Control Type page code differs on %s (another mod patched it?), no ASSIGN BUTTON row",
                 GetGameBuildName(gGameBuild));
    return true;
}

bool Uninstall_KeySettingEvCall_SetKeySettingMenu_Hooks()
{
    UnpatchRowList(g_ControlTypeRowList, "Control Type");
    UnpatchRowList(g_ControlsRowList, "controls");
    UnpatchRowList(g_SoundRowList, "sound");
    g_RowListPatched = false;
    for (void*& target : g_HookTargets)
    {
        if (target)
            DisableAndRemoveHook(target);
        target = nullptr;
    }
    g_ExtraBarsOwner = nullptr;
    return true;
}
