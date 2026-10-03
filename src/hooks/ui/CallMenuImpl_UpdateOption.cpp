#include "pch.h"
#include "CallMenuImpl_UpdateOption.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "../player/CallMenuCommandSet_UpdateState.h"
#include "../player/CallMenuManagerImpl_UpdateInShowing.h"

namespace
{
    using UpdateOption_t = void(__fastcall*)(std::uint8_t* callMenu);
    using GetLangText_t  = const char*(__fastcall*)(void* ui, std::uint64_t langId);
    using SetText_t      = void(__fastcall*)(void* ui, void* textNode, void* owner, const char* text, std::uint32_t refresh);
    using SetTaggedText_t = int(__fastcall*)(void* ui, void* textNode, void* textUnits, std::uint32_t unitCount, const char* text);
    using IsUseKeyboard_t = bool(__fastcall*)(void* ui);

    constexpr std::size_t   kCallMenuUi          = 0x68;
    constexpr std::size_t   kCallMenuTextOwner   = 0x48;
    constexpr std::size_t   kSelectGuideText     = 0xD0;   // the "Select" guide is drawn twice, text and backing
    constexpr std::size_t   kSelectGuideTextBack = 0xD8;
    constexpr std::size_t   kGuideState          = 0x10B;
    constexpr std::uint8_t  kGuideShowsCancel    = 2;
    constexpr std::size_t   kGetLangTextVtblIndex = 0x750 / 8;
    constexpr std::size_t   kIsUseKeyboardVtblIndex = 0xB0 / 8;  // what makes the menu number its rows
    constexpr std::size_t   kSetTextVtblIndex     = 0x708 / 8;   // plain text, <I=G=...> shows as written
    constexpr std::size_t   kSetTaggedTextVtblIndex = 0x740 / 8; // splits <I=G=...> into button icons, as the iDroid help text does
    constexpr std::size_t   kTextUnitSize         = 0x20;
    constexpr std::size_t   kTextUnitCount        = 0x40;
    constexpr std::size_t   kTextNodeAreaWidth    = 0xE8;    // ModelNodeText width before scale; shown width = area * scale.x * 10
    constexpr std::size_t   kTextNodeScaleX       = 0xF0;
    constexpr float         kGuideShownWidth      = 1000.0f; // wide enough that the page guide never clips and scrolls

    constexpr std::size_t   kRows                 = 0x270;   // the 10 rows the HUD draws
    constexpr std::size_t   kRowSize              = 0xD8;
    constexpr std::size_t   kRowCount             = 10;
    constexpr std::size_t   kRowDrawnName         = 0x60;    // label it last drew; a row is only redrawn when this differs

    constexpr std::uint64_t kLangSelect      = 0xF27C7C29CB5Bull;   // callmenu_ok
    constexpr std::uint64_t kLangCancel      = 0x5F1388C696E5ull;   // callmenu_cancel
    constexpr std::uint64_t kLangIdMask      = 0xFFFFFFFFFFFFull;
    constexpr const char*   kSwitchPagesKey  = "help_key_page";      // "<I=G=PAD_ALL> Switch Pages"
    constexpr const char*   kPadPageIcons      = "<I=G=EVADE><I=G=RELOAD>";    // previous, next
    constexpr const char*   kKeyboardPageIcons = "<I=G=ACTION><I=G=RELOAD>";   // EVADE is Space there, which also picks the option

    UpdateOption_t g_Orig       = nullptr;
    std::atomic<void*> g_Ui{ nullptr };   // the UI utility the HUD uses; one object for the whole game
    void*          g_HookTarget = nullptr;
    char           g_GuideText[256];   // the text nodes may keep the pointer, so it has to outlive the call
    alignas(16) std::uint8_t g_GuideUnits[2][kTextUnitSize * kTextUnitCount];   // the nodes draw from these after the call
    int            g_ShownPage  = 0;
    int            g_ShownPages = 0;
    std::uint8_t   g_ShownState = 0;
    void*          g_ShownNode  = nullptr;   // a new HUD (new mission) starts with the game's own text again

    struct NodeWidth
    {
        std::uint8_t* node = nullptr;
        float         area = 0.0f;
    };
    NodeWidth      g_OriginalWidths[2];

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    template <typename Fn>
    Fn VirtualAt(void* object, std::size_t index)
    {
        return (*reinterpret_cast<Fn**>(object))[index];
    }

    const char* LangText(void* ui, std::uint64_t langId)
    {
        const char* text = VirtualAt<GetLangText_t>(ui, kGetLangTextVtblIndex)(ui, langId);
        return text ? text : "";
    }

    bool UsesKeyboard(void* ui)
    {
        return VirtualAt<IsUseKeyboard_t>(ui, kIsUseKeyboardVtblIndex)(ui);
    }

    std::string SwitchPagesText(void* ui)
    {
        const char* icons = UsesKeyboard(ui) ? kKeyboardPageIcons : kPadPageIcons;
        std::string text = LangText(ui, FoxHashes::StrCode64(kSwitchPagesKey) & kLangIdMask);
        const std::size_t tagStart = text.find("<I=G=");
        const std::size_t tagEnd   = tagStart == std::string::npos ? std::string::npos : text.find('>', tagStart);
        if (tagEnd == std::string::npos)
            return std::string(icons) + " Switch Pages";
        return text.replace(tagStart, tagEnd - tagStart + 1, icons);
    }

    void SetGuideText(std::uint8_t* callMenu, const char* text)
    {
        void* ui    = At<void*>(callMenu, kCallMenuUi);
        void* owner = At<void*>(callMenu, kCallMenuTextOwner);
        auto setText = VirtualAt<SetText_t>(ui, kSetTextVtblIndex);
        setText(ui, At<void*>(callMenu, kSelectGuideText), owner, text, 1);
        setText(ui, At<void*>(callMenu, kSelectGuideTextBack), owner, text, 1);
    }

    void WidenGuideNode(std::uint8_t* node, NodeWidth& original)
    {
        if (!node)
            return;
        if (original.node != node)
            original = { node, At<float>(node, kTextNodeAreaWidth) };

        const float scale = At<float>(node, kTextNodeScaleX);
        if (scale > 0.0f)
            At<float>(node, kTextNodeAreaWidth) = kGuideShownWidth / (scale * 10.0f);
    }

    void RestoreGuideNode(std::uint8_t* node, NodeWidth& original)
    {
        if (node && original.node == node)
            At<float>(node, kTextNodeAreaWidth) = original.area;
        original = NodeWidth{};
    }

    void SetTaggedGuideText(std::uint8_t* callMenu, const char* text)
    {
        void* ui = At<void*>(callMenu, kCallMenuUi);
        auto setTaggedText = VirtualAt<SetTaggedText_t>(ui, kSetTaggedTextVtblIndex);
        const std::size_t nodes[2] = { kSelectGuideText, kSelectGuideTextBack };
        for (int i = 0; i < 2; ++i)
        {
            std::uint8_t* node = At<std::uint8_t*>(callMenu, nodes[i]);
            WidenGuideNode(node, g_OriginalWidths[i]);
            setTaggedText(ui, node, g_GuideUnits[i], kTextUnitCount, text);
        }
    }

    void ShowPageGuide(std::uint8_t* callMenu)
    {
        const int pages = CallMenuItems::PageCount();
        const int page  = CallMenuPaging::CurrentPage();
        const std::uint8_t state = At<std::uint8_t>(callMenu, kGuideState);
        void* node = At<void*>(callMenu, kSelectGuideText);
        const bool stateChanged = state != g_ShownState || node != g_ShownNode;
        g_ShownState = state;
        g_ShownNode  = node;

        if (pages <= 1)
        {
            if (g_ShownPages > 1)
            {
                RestoreGuideNode(At<std::uint8_t*>(callMenu, kSelectGuideText), g_OriginalWidths[0]);
                RestoreGuideNode(At<std::uint8_t*>(callMenu, kSelectGuideTextBack), g_OriginalWidths[1]);
                if (!stateChanged)
                {
                    void* ui = At<void*>(callMenu, kCallMenuUi);
                    SetGuideText(callMenu, LangText(ui, state == kGuideShowsCancel ? kLangCancel : kLangSelect));
                }
            }
            g_ShownPages = pages;
            return;
        }

        void* ui = At<void*>(callMenu, kCallMenuUi);
        const char* select = LangText(ui, state == kGuideShowsCancel ? kLangCancel : kLangSelect);
        std::snprintf(g_GuideText, sizeof(g_GuideText), "%s   %s %d/%d", select, SwitchPagesText(ui).c_str(), page, pages);
        SetTaggedGuideText(callMenu, g_GuideText);

        g_ShownPage  = page;
        g_ShownPages = pages;
    }

    void RedrawEveryRow(std::uint8_t* callMenu)
    {
        for (std::size_t row = 0; row < kRowCount; ++row)
            At<std::uint64_t>(callMenu, kRows + row * kRowSize + kRowDrawnName) ^= kLangIdMask;   // a label set while its row was hidden never got drawn
    }

    void __fastcall hk_UpdateOption(std::uint8_t* callMenu)
    {
        const std::uint8_t stateBefore = callMenu ? At<std::uint8_t>(callMenu, kGuideState) : 0;
        g_Orig(callMenu);

        if (!callMenu || MissionCodeGuard::ShouldBypassHooks())
            return;
        g_Ui.store(At<void*>(callMenu, kCallMenuUi), std::memory_order_release);
        if (At<std::uint8_t>(callMenu, kGuideState) != stateBefore)
            g_ShownState = 0xFF;   // the game just rewrote the guide with its own text
        ShowPageGuide(callMenu);
        RedrawEveryRow(callMenu);
    }
}

bool CallMenuGuide::IsUsingKeyboard()
{
    void* ui = g_Ui.load(std::memory_order_acquire);
    return ui && UsesKeyboard(ui);
}

bool Install_CallMenuImpl_UpdateOption_Hook()
{
    if (g_HookTarget)
        return true;

    if (!gAddr.CallMenuImpl_UpdateOption)
    {
        LogError("CallMenu: CallMenuImpl::UpdateOption address missing for %s, no page guide next to Select",
                 GetGameBuildName(gGameBuild));
        return true;
    }

    void* target = ResolveGameAddress(gAddr.CallMenuImpl_UpdateOption);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_UpdateOption),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("CallMenu: CallMenuImpl::UpdateOption hook failed at %p, no page guide next to Select", target);
        return false;
    }

    g_HookTarget = target;
    return true;
}

void Uninstall_CallMenuImpl_UpdateOption_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
    g_ShownPage  = 0;
    g_ShownPages = 0;
}
