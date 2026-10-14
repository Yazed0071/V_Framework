#include "pch.h"
#include "OutfitPalette.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "OutfitRegistry.h"
#include "CustomHeadRegistry.h"
#include "BlockControllerImpl_LoadPartsNew.h"
#include "ItemSelectorCallbackImpl_AddListSuit.h"
#include "AddressSet.h"
#include "FoxHashes.h"
#include "FoxPathInternal.h"
#include "HookUtils.h"
#include "log.h"
#include "../ui/UixUtilityControl.h"
#include "../ui/UixUtilityDev.h"
#include "../ui/UiPadLock.h"

namespace
{
    using namespace UixUtilityControl;

    constexpr const char* kOpenButton     = "UI_DETAIL";
    constexpr bool        kHideGameUi     = true;
    constexpr double      kPackPreviewDelay = 0.35;
    constexpr float       kRepeatFirst    = 0.35f;
    constexpr float       kRepeatStep     = 0.08f;
    constexpr double      kEquipTimeout   = 15.0;
    constexpr double      kEquipAskTimeout = 1.0;
    constexpr const char* kDrawPriority   = "DEV_COND_POPUP";
    constexpr float       kHintX          = -32.4f;             // right after the list's own key help bar
    constexpr float       kHintY          = -28.3f;
    constexpr double      kReleaseGrace   = 0.25;
    constexpr double      kOutAnimMax     = 1.0;
    constexpr double      kGameUiFadeIn   = 0.25;
    constexpr const char* kHoldId         = "V_OutfitPalette";
    constexpr const char* kSelectButton   = "UI_DECIDE";
    constexpr const char* kSelectMouse    = "UI_MOUSE_DECIDE";
    constexpr const char* kBackButton     = "UI_CANCEL";
    constexpr const char* kBackMouse      = "UI_MOUSE_CANCEL";

    constexpr std::uint32_t kSoundCursor  = 0x6DDE924A;   // Wwise hash of the menu cursor-move sound (real name unknown)
    constexpr std::uint32_t kSoundOpen    = 0xA75C55A0;   // sfx_s_terminal_winopen
    constexpr std::uint32_t kSoundSelect  = 0x8B215BD8;   // sfx_s_terminal_enter
    constexpr std::uint32_t kSoundBack    = 0x7757C41C;   // sfx_s_terminal_cancel

    constexpr const char* kHintText = "<I=G=UI_DETAIL> Colours";
    constexpr const char* kKeyHelp[] = {
        "<I=G=UI_DECIDE> Select",
        "<I=G=UI_CANCEL> Back",
    };
    constexpr int         kKeyHelpCount = 2;
    constexpr int         kKeyUnits     = 8;                    // one unit per icon and per run of text
    constexpr const char* kChannelIcon[]   = { "UI_ID_C_Sel_CP1", "UI_ID_C_Sel_CP2" };
    constexpr const char* kChannelButtons[] = { "<I=G=UI_LB>", "<I=G=UI_RB>" };
    constexpr std::uint32_t kSelButtonText = 0x8800B657;        // UI_ID_C_Button_txt

    constexpr const char* kPaletteLayout = "/Assets/mgo/ui/LayoutAsset/id_customize_new/id_customize_common_layout.uilb";
    constexpr const char* kHintLayout    = "/Assets/mgo/ui/LayoutAsset/id_customize_new/id_customize_control_record.uilb";
    constexpr const char* kPaletteWindow = "V_OutfitPalette";
    constexpr const char* kHintWindow    = "V_OutfitPaletteHint";
    constexpr const char* kPaletteRootId = "VOutfitPalette";
    constexpr const char* kHintRootId    = "VOutfitPaletteHint";
    constexpr int         kColumns       = 5;
    constexpr int         kRows          = 8;
    constexpr int         kCells         = kColumns * kRows;
    constexpr const char* kBaseTexture   = "Base_Texture";
    constexpr std::uint32_t kPaletteGroup = 0xFF4ECD39;         // hash of the node holding the swatch rows and colour name (real name unknown)
    constexpr std::uint32_t kScrollThumb  = 0xC813CDCB;         // hash of the slider's thumb mesh (real name unknown)
    constexpr float         kThumbX       = 23.81f;
    constexpr float         kThumbTopY    = 1.90f;
    constexpr float         kTrackLength  = 34.9f;
    constexpr float         kThumbZ       = 0.02f;
    constexpr std::uint32_t kHiddenGroups[] = {
        0x8FAEE230,   // hash of the weight / loadout panel (real name unknown)
        0x4CB486FF,   // hash of the gear part selectors (real name unknown)
        0x7F73893B,   // hash of the class, mobility and XP panel (real name unknown)
        0x7DB42F8E,   // hash of the class panel (real name unknown)
    };
    constexpr const char* kHiddenLayouts[] = {
        "id_customize_sel_record0000", "id_customize_sel_record0001", "id_customize_sel_record0002",
        "id_customize_sel_record0003", "id_customize_sel_record0004", "id_customize_sel_record0005",
        "id_customize_sel_record0006", "id_customize_sel_record0007", "id_customize_sel_record0008",
    };
    constexpr std::uint32_t kCellNoise    = 0x084BD4C4;   // hash of the layer with the cursor's noise glow (real name unknown)
    constexpr std::uint32_t kCellGlow     = 0x1C04DFB7;   // UI_ID_C_Pallet_Cursor
    constexpr std::uint32_t kBlendMaskTex = 0x2D45F6E6;   // Blend_MaskTex
    constexpr float         kCellGlowSize = 42.5f;              // the glow bars end just inside the white cursor frame
    constexpr float         kCellGlowX    = 3.5f;
    constexpr float         kCellGlowY    = 0.0f;
    constexpr float         kCellGlowZ    = 0.2f;

    constexpr std::uint64_t kId48 = 0xFFFFFFFFFFFFull;

    struct alignas(16) V4 { float v[4]; };

    template <typename R, typename... A>
    R Call(std::size_t devOffset, A... args)
    {
        using F = R(__fastcall*)(void**, A...);
        return reinterpret_cast<F>(Method(devOffset))(Utility(), args...);
    }

    std::uint32_t Name32(const char* name) { return static_cast<std::uint32_t>(FoxHashes::StrCode64(name)); }

    bool IsNode(Kind k) { return k == Kind::NodeCommon || k == Kind::NodeText || k == Kind::NodeMesh || k == Kind::NodeLine; }

    std::size_t Overload(std::size_t nodeOffset, const Object& o) { return IsNode(o.kind) ? nodeOffset : nodeOffset + sizeof(void*); }   // the component overload sits in the next vtable slot

    bool Get(std::uint32_t handle, Object& o) { return handle != 0 && Lookup(handle, o); }

    std::uint32_t Child(std::uint32_t parentHandle, Kind kind, std::size_t offset, std::uint32_t name)
    {
        Object parent;
        if (!Get(parentHandle, parent))
            return 0;
        void* child = Call<void*>(offset, parent.ptr, name);
        return HandleFor(kind, child, parent.window, IsNode(kind) ? parent.ptr : nullptr);
    }

    std::uint32_t LayoutOf(std::uint32_t parent, const char* name)
    {
        Object p;
        if (!Get(parent, p))
            return 0;
        return Child(parent, Kind::Layout, p.kind == Kind::Window ? dev::GetLayoutOfWindow : dev::GetLayoutOfLayout, Name32(name));
    }

    std::uint32_t ModelOf(std::uint32_t layout, const char* name) { return Child(layout, Kind::Model, dev::GetModel, Name32(name)); }
    std::uint32_t TextOf(std::uint32_t model, const char* name)   { return Child(model, Kind::NodeText, dev::GetModelNodeText, Name32(name)); }
    std::uint32_t MeshOf(std::uint32_t model, const char* name)   { return Child(model, Kind::NodeMesh, dev::GetModelNodeMesh, Name32(name)); }

    void Anim(std::uint32_t model, const char* name, bool loop = false)
    {
        Object m;
        if (!Get(model, m))
            return;
        if (void* anim = Call<void*>(dev::GetAnimationOfModel, m.ptr, Name32(name)))
            Call<void>(loop ? dev::StartLoopAnimation : dev::StartAnimation, anim);
    }

    std::uint32_t AnimHandle(std::uint32_t model, const char* name)
    {
        return Child(model, Kind::Animation, dev::GetAnimationOfModel, Name32(name));
    }

    bool AnimRunning(std::uint32_t anim)
    {
        Object a;
        return Get(anim, a) && Call<bool>(dev::IsAnimationRunning, a.ptr);
    }

    void SetVisible(std::uint32_t handle, bool visible)
    {
        Object o;
        if (Get(handle, o))
            Call<void>(Overload(dev::SetVisible, o), o.ptr, visible);
    }

    bool IsVisible(void* ptr, Kind kind)
    {
        Object o{ kind, ptr };
        return Call<bool>(Overload(dev::IsVisible, o), ptr);
    }

    void SetAlpha(std::uint32_t handle, float alpha)
    {
        Object o;
        if (Get(handle, o))
            Call<void>(Overload(dev::SetAlpha, o), o.ptr, alpha);
    }

    float GetAlpha(std::uint32_t handle)
    {
        Object o;
        return Get(handle, o) ? Call<float>(Overload(dev::GetAlpha, o), o.ptr) : 0.0f;
    }

    void SetTranslation(std::uint32_t handle, float x, float y, float z = 0.0f)
    {
        Object o;
        if (!Get(handle, o))
            return;
        V4 v{ { x, y, z, 0.0f } };
        Call<void>(Overload(dev::SetTranslation, o), o.ptr, &v);
    }

    void SetScale(std::uint32_t handle, float x, float y)
    {
        Object o;
        if (!Get(handle, o))
            return;
        V4 v{ { x, y, 1.0f, 0.0f } };
        Call<void>(Overload(dev::SetScaleVector, o), o.ptr, &v);
    }

    void SetTexture(std::uint32_t mesh, const std::string& path)
    {
        Object m;
        if (!Get(mesh, m))
            return;
        const std::string normal = FoxHashes::NormalizeAssetPath(path);
        Call<void>(dev::SetTextureNameLevel, m.ptr, normal.c_str(), static_cast<std::uint64_t>(Name32(kBaseTexture)), 2);
    }

    void SetText(std::uint32_t node, std::uint32_t units, const std::string& text)
    {
        Object n, u;
        if (!Get(node, n) || !Get(units, u))
            return;
        auto keep = std::make_shared<std::string>(text);
        Call<void>(dev::SetTextForModelNodeText, n.ptr, u.ptr, keep->c_str(), true);
        KeepTextAlive(u.ptr, n, keep);
    }

    void SetTaggedText(std::uint32_t node, std::uint32_t units, const char* text)
    {
        Object n, u;
        if (!Get(node, n) || !Get(units, u))
            return;
        auto keep = std::make_shared<std::string>(text);
        Call<int>(dev::CreateScrollBoxText, n.ptr, u.ptr, static_cast<std::uint32_t>(kKeyUnits), keep->c_str());
        KeepTextAlive(u.ptr, n, keep);
    }

    bool PressedFree(const char* name)
    {
        uipad::OwnQuery own;
        return Call<bool>(dev::IsButtonPressByName, Name32(name));
    }

    bool Pressed(const char* name)  { bool on = false; return uipad::Press(Name32(name), on) && on; }
    bool Released(const char* name) { bool on = false; return uipad::Release(Name32(name), on) && on; }
    bool Holding(const char* name)  { bool on = false; return uipad::Pressing(Name32(name), on) && on; }
    bool Moved(const char* name)
    {
        bool on = false;
        return Pressed(name) || (uipad::Repeat(Name32(name), kRepeatFirst, kRepeatStep, on) && on);
    }

    bool MouseDown(const char* name)     { uipad::OwnQuery own; return Call<bool>(dev::IsMouseButtonDownByName, Name32(name)); }
    bool MouseUp(const char* name)       { uipad::OwnQuery own; return Call<bool>(dev::IsMouseButtonUpByName, Name32(name)); }
    bool MousePressing(const char* name) { uipad::OwnQuery own; return Call<bool>(dev::IsMouseButtonPressingByName, Name32(name)); }

    bool UsingKeyboard() { return Call<bool>(dev::IsUseKeyboard); }

    float MouseWheel() { return Call<float>(dev::GetMouseWheel); }

    void PlaySound(std::uint32_t eventId)
    {
        void* control = SoundControl();
        void* start = SoundControlStartFn();
        if (control && start)
            reinterpret_cast<bool(__fastcall*)(void*, std::uint32_t, bool)>(start)(control, eventId, false);
    }

    double Now()
    {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    struct Target
    {
        const outfit::OutfitEntry* entry = nullptr;
        const outfit::CustomHeadEntry* head = nullptr;
        std::string headKey;
        std::shared_ptr<const outfit::HeadColorSet> headColors;
        std::uint8_t playerType = 0;
        std::uint8_t variant = 0;

        const char* Key() const { return head ? headKey.c_str() : entry->key; }

        const outfit::OutfitColorList* Colors(std::size_t channel) const
        {
            if (head)
                return headColors && channel < outfit::kColorChannelCount ? &headColors->colors[channel] : nullptr;
            return entry->GetColors(playerType, variant, channel);
        }

        bool Same(const Target& o) const
        {
            return head ? (o.head && headKey == o.headKey) : (entry && entry == o.entry && variant == o.variant);
        }
    };

    bool HeadTarget(const outfit::CustomHeadEntry* head, Target& t)
    {
        const std::uint8_t livePlayer = outfit::ReadLivePlayerType();
        if (!head || livePlayer >= outfit::kPlayerTypeMax)
            return false;
        t = Target{};
        t.head = head;
        t.headKey = head->name;
        t.playerType = livePlayer;
        t.headColors = outfit::GetCustomHeadColors(head->name, livePlayer);
        return t.headColors != nullptr;
    }

    bool WornHeadTarget(Target& t)
    {
        return HeadTarget(outfit::TryGetCustomHeadBySlot(outfit::GetRealizedLocalHeadSlot()), t);
    }

    bool WornTarget(Target& t)
    {
        const std::uint8_t liveParts = outfit::ReadLivePartsType();
        if (liveParts < outfit::kCustomPartsTypeStart || liveParts > outfit::kCustomPartsTypeEnd)
            return false;
        const outfit::OutfitEntry* entry = nullptr;
        if (!outfit::TryGetOutfitByPartsType(liveParts, &entry) || !entry || !entry->bound)
            return false;
        const std::uint8_t livePlayer = outfit::ReadLivePlayerType();
        t.entry = entry;
        t.playerType = livePlayer < outfit::kPlayerTypeMax ? livePlayer : entry->FirstSupportedPlayerType();
        t.variant = entry->HasVariants() ? outfit::GetActiveVariant(entry->partsType) : std::uint8_t{ 0 };
        return true;
    }

    std::uint8_t PlayerTypeFor(const outfit::OutfitEntry* entry)
    {
        const std::uint8_t livePlayer = outfit::ReadLivePlayerType();
        return livePlayer < outfit::kPlayerTypeMax ? livePlayer : entry->FirstSupportedPlayerType();
    }

    bool ChooseColor(const Target& t, std::size_t channel, std::uint32_t index, bool save)
    {
        const outfit::OutfitColorList* list = t.Colors(channel);
        if (!list || index >= list->size())
            return false;
        const outfit::OutfitColor& color = (*list)[index];
        if (save)
            outfit::SetSavedColor(t.Key(), t.playerType, channel, index);
        else
            outfit::SetPreviewColor(t.Key(), t.playerType, channel, index);
        if (t.head)
        {
            outfit::RequestColorReapply(false);
            return true;
        }

        const outfit::OutfitColor* pack = outfit::GetServablePackColor(*t.entry, t.playerType, t.variant);
        if (color.fpkCode != 0 && (!pack || pack->fpkCode != color.fpkCode)
            && outfit::GetPackColor(*t.entry, t.playerType, t.variant) == &color)
        {
            LogWarn("OutfitPalette: '%s' colour %u: its package is not installed", t.entry->key, index + 1);
            return false;
        }
        const std::uint64_t wantPack = pack ? pack->fpkCode : 0;
        if (wantPack != outfit::GetServedColorFpk())
        {
            outfit::RequestPartsRestream();
            outfit::RequestColorReapply(true);
        }
        else
        {
            outfit::RequestColorReapply(false);
        }
        return true;
    }

    struct KeyHelp
    {
        std::uint32_t model = 0, text = 0, units = 0;
        const char* label = "";
    };

    struct Cell
    {
        std::uint32_t model = 0, icon = 0, noise = 0, glow = 0;
    };

    struct Selector
    {
        std::uint32_t layout = 0, model = 0, text = 0, units = 0;
        const char* label = "";
    };

    struct PaletteUi
    {
        std::uint32_t window = 0, root = 0, model = 0;
        std::uint32_t nameText = 0, nameShadow = 0, nameUnits = 0, shadowUnits = 0;
        std::uint32_t thumb = 0;
        Cell cells[kCells];
        Selector channels[2];
        Selector buttons[2];
        KeyHelp keys[kKeyHelpCount];
    };

    struct HintUi
    {
        std::uint32_t window = 0, root = 0;
        KeyHelp key;
    };

    struct Channel
    {
        std::size_t channel = 0;
        const outfit::OutfitColorList* colors = nullptr;
        int saved = 1;
        int cursor = 1;
        int top = 0;
        bool appliedPack = false;
        double previewAt = -1.0;
    };

    constexpr float kStickOn     = 0.5f;
    constexpr float kStickOff    = 0.3f;
    constexpr float kStickSwitch = 0.15f;   // how much the other axis has to win by

    struct StickState
    {
        int dx = 0, dy = 0;
        double next = 0.0;
        bool waitNeutral = true;
    };

    void StickMoved(StickState& s, double now, int& dx, int& dy)
    {
        float x = 0.0f, y = 0.0f;
        int sx = 0, sy = 0;
        if (uipad::StickAxis(0, x, y))   // 0 is the left stick, +y is down
        {
            const float ax = std::fabs(x), ay = std::fabs(y);
            const float big = ax > ay ? ax : ay;
            if (big >= ((s.dx || s.dy) ? kStickOff : kStickOn))
            {
                bool useX = ax >= ay;
                if (s.dx && ay < ax + kStickSwitch)
                    useX = true;
                else if (s.dy && ax < ay + kStickSwitch)
                    useX = false;
                if (useX)
                    sx = x > 0.0f ? 1 : -1;
                else
                    sy = y > 0.0f ? 1 : -1;
            }
        }
        if (s.waitNeutral)
        {
            s.dx = sx;
            s.dy = sy;
            s.waitNeutral = sx != 0 || sy != 0;
            return;
        }
        if (sx != s.dx || sy != s.dy)
        {
            s.dx = sx;
            s.dy = sy;
            s.next = now + kRepeatFirst;
            dx = sx;
            dy = sy;
        }
        else if ((sx || sy) && now >= s.next)
        {
            s.next = now + kRepeatStep;
            dx = sx;
            dy = sy;
        }
    }

    struct Open
    {
        Target target;
        std::vector<Channel> channels;
        std::size_t active = 0;
        int drawnActive = -1, drawnTop = -1, drawnCursor = -1;
        std::int8_t cellHot[kCells];   // -1 until the swatch is first drawn
        StickState stick;
        int sliderShown = -1;   // -1 not set yet
        bool closing = false;
        bool byMouse = false;
        const char* button = nullptr;
        const char* mouse = nullptr;
        double closedAt = 0.0, releasedAt = -1.0;
        std::vector<std::uint32_t> outAnims;
        bool rootHidden = false;
    };

    struct HiddenLayout
    {
        std::uint32_t layout = 0;
        float alpha = 1.0f;
    };

    std::mutex g_Lock;
    bool g_Installed = false;
    bool g_WindowsAsked = false;
    std::unique_ptr<PaletteUi> g_Ui;
    std::unique_ptr<HintUi> g_Hint;
    bool g_HintShown = false;
    int g_UsingKeyboard = -1;          // -1 until the key help is first laid out
    std::unique_ptr<Open> g_Open;
    Target g_Pending;
    bool g_PendingSet = false;
    double g_PendingUntil = 0.0;
    bool g_PendingByList = false;
    bool g_PendingAsked = false;
    double g_PendingAskedUntil = 0.0;
    std::vector<HiddenLayout> g_HiddenGameUi;
    bool g_GameUiHidden = false;
    std::vector<HiddenLayout> g_FadingIn;
    double g_FadeStart = 0.0;
    bool g_HoldingPad = false;
    std::atomic<bool> g_OwnsMouse{ false };
    bool g_MouseFilterSet = false;
    std::atomic<bool> g_PaletteReady{ false };

    void AskWindows()
    {
        if (g_WindowsAsked)
            return;
        g_WindowsAsked = true;
        const auto make = [](const char* window, const char* layout, const char* id) {
            if (Call<void*>(dev::FindWindow, FoxHashes::StrCode64(window) & kId48))
                return;
            LayoutRecord r;
            r.path = layout;
            r.id = FoxHashes::StrCode64(id);
            r.drawPriority = FoxHashes::StrCode64(kDrawPriority);
            CreateUiWindow(FoxHashes::StrCode64(window), { r }, 0, 0, 0);
        };
        make(kPaletteWindow, kPaletteLayout, kPaletteRootId);
        make(kHintWindow, kHintLayout, kHintRootId);
    }

    std::uint32_t FindWindowHandle(const char* name)
    {
        void* window = Call<void*>(dev::FindWindow, FoxHashes::StrCode64(name) & kId48);
        return HandleFor(Kind::Window, window, window);
    }

    void PostShowAndStart(std::uint32_t window)
    {
        Object w;
        if (Get(window, w))
            Call<void>(dev::PostShowAndStartMessage, w.ptr);
    }

    void SetupKeyHelp(KeyHelp& k, std::uint32_t model, const char* label)
    {
        k.model = model;
        k.label = label;
        k.text = TextOf(model, "UI_keyhelp_txt");
        k.units = CreateTextUnit(kKeyUnits);
        SetTaggedText(k.text, k.units, label);
        Anim(model, "mb_cmn_keyhelp_on");
    }

    void ShowKeyHelp(const KeyHelp& k)
    {
        Anim(k.model, "mb_cmn_keyhelp_on");
        Anim(k.model, "mb_cmn_keyhelp_chg");
    }

    void FitGlow(const Cell& cell)
    {
        Object glow;
        if (!Get(cell.glow, glow))
            return;
        V4 off{ { 0.0f, 0.0f, 0.0f, 0.0f } };
        Call<void>(dev::SetShaderParameter, glow.ptr, std::uint64_t{ kBlendMaskTex }, &off);
        SetScale(cell.glow, kCellGlowSize, kCellGlowSize);
        SetTranslation(cell.glow, kCellGlowX, kCellGlowY, kCellGlowZ);
    }

    void LogGlowScale(const Cell& cell, const char* when)
    {
        static int s_logged = 0;
        Object glow;
        if (s_logged >= 3 || !Get(cell.glow, glow))
            return;
        ++s_logged;
        V4 out{};
        Call<V4*>(Overload(dev::GetScale, glow), &out, glow.ptr);
        LogInfo("OutfitPalette: cursor glow scale %s = %.2f x %.2f (fitted %.1f)", when, out.v[0], out.v[1], kCellGlowSize);
    }

    std::unique_ptr<PaletteUi> FindPalette()
    {
        const std::uint32_t window = FindWindowHandle(kPaletteWindow);
        const std::uint32_t root = window ? LayoutOf(window, kPaletteRootId) : 0;
        const std::uint32_t model = root ? ModelOf(root, "UI_ID_Customize_Common_layout") : 0;
        if (!model)
            return nullptr;
        PostShowAndStart(window);
        Object r;
        if (Get(root, r))
            Call<void>(dev::DisableShaderResetLayout, r.ptr);

        auto p = std::make_unique<PaletteUi>();
        p->window = window;
        p->root = root;
        p->model = model;
        for (std::uint32_t group : kHiddenGroups)
            SetVisible(Child(model, Kind::NodeCommon, dev::GetModelNodeCommon, group), false);
        for (const char* name : kHiddenLayouts)
            SetVisible(LayoutOf(root, name), false);
        p->nameText = TextOf(model, "UI_ID_C_Pallet_txt");
        p->nameShadow = TextOf(model, "UI_ID_C_Pallet_txt_sdw");
        p->nameUnits = CreateTextUnit(1);
        p->shadowUnits = CreateTextUnit(1);
        p->thumb = Child(model, Kind::NodeMesh, dev::GetModelNodeMesh, kScrollThumb);

        char name[64];
        for (int i = 0; i < kCells; ++i)
        {
            std::snprintf(name, sizeof(name), "id_customize_pallet_record%04d", i + 1);
            const std::uint32_t cellModel = ModelOf(LayoutOf(root, name), "UI_ID_C_Pallet_record");
            p->cells[i].model = cellModel;
            p->cells[i].icon = cellModel ? MeshOf(cellModel, "UI_ID_C_PalletIcon") : 0;
            p->cells[i].noise = cellModel ? Child(cellModel, Kind::NodeCommon, dev::GetModelNodeCommon, kCellNoise) : 0;
            p->cells[i].glow = cellModel ? Child(cellModel, Kind::NodeMesh, dev::GetModelNodeMesh, kCellGlow) : 0;
            FitGlow(p->cells[i]);
        }
        for (int i = 0; i < 2; ++i)
        {
            std::snprintf(name, sizeof(name), "id_customize_sel_swatch%d", i + 2);
            Selector& s = p->channels[i];
            s.layout = LayoutOf(root, name);
            s.model = ModelOf(s.layout, "UI_ID_C_SelectorIcon_record");
            s.text = s.model ? TextOf(s.model, "UI_ID_C_SelectiorIcon_txt") : 0;
            SetVisible(s.text, false);

            std::snprintf(name, sizeof(name), "id_customize_sel_swatch%d", i == 0 ? 1 : 4);
            Selector& b = p->buttons[i];
            b.layout = LayoutOf(root, name);
            b.model = ModelOf(b.layout, "UI_ID_C_SelectorIcon_record");
            b.text = b.model ? Child(b.model, Kind::NodeText, dev::GetModelNodeText, kSelButtonText) : 0;
            b.units = CreateTextUnit(kKeyUnits);
            b.label = kChannelButtons[i];
            SetTaggedText(b.text, b.units, b.label);
        }
        for (int i = 0; i < kKeyHelpCount; ++i)
        {
            std::snprintf(name, sizeof(name), "id_customize_control_record%04d", i);
            if (const std::uint32_t helpModel = ModelOf(LayoutOf(root, name), "UI_ID_Customize_Control"))
                SetupKeyHelp(p->keys[i], helpModel, kKeyHelp[i]);
        }
        for (int i = kKeyHelpCount; i < 4; ++i)
        {
            std::snprintf(name, sizeof(name), "id_customize_control_record%04d", i);
            Anim(ModelOf(LayoutOf(root, name), "UI_ID_Customize_Control"), "mb_cmn_keyhelp_off");
        }
        SetVisible(root, false);
        return p;
    }

    std::unique_ptr<HintUi> FindHint()
    {
        const std::uint32_t window = FindWindowHandle(kHintWindow);
        const std::uint32_t root = window ? LayoutOf(window, kHintRootId) : 0;
        const std::uint32_t model = root ? ModelOf(root, "UI_ID_Customize_Control") : 0;
        if (!model)
            return nullptr;
        PostShowAndStart(window);
        auto h = std::make_unique<HintUi>();
        h->window = window;
        h->root = root;
        SetupKeyHelp(h->key, model, kHintText);
        Anim(model, "UI_ID_C_helpColor_TPP");
        SetTranslation(root, kHintX, kHintY);
        SetVisible(root, false);
        return h;
    }

    void RefreshKeyHelp()
    {
        const int keyboard = UsingKeyboard() ? 1 : 0;
        if (keyboard == g_UsingKeyboard)
            return;
        g_UsingKeyboard = keyboard;
        for (KeyHelp& k : g_Ui->keys)
            SetTaggedText(k.text, k.units, k.label);
        for (Selector& b : g_Ui->buttons)
            SetTaggedText(b.text, b.units, b.label);
        SetTaggedText(g_Hint->key.text, g_Hint->key.units, g_Hint->key.label);
    }

    void ShowHint(bool show)
    {
        if (!g_Hint || g_HintShown == show)
            return;
        g_HintShown = show;
        SetVisible(g_Hint->root, show);
        if (show)
            ShowKeyHelp(g_Hint->key);
    }

    void DeleteTextUnits()
    {
        if (g_Ui)
        {
            DeleteTextUnit(g_Ui->nameUnits);
            DeleteTextUnit(g_Ui->shadowUnits);
            for (Selector& s : g_Ui->channels)
                DeleteTextUnit(s.units);
            for (Selector& b : g_Ui->buttons)
                DeleteTextUnit(b.units);
            for (KeyHelp& k : g_Ui->keys)
                DeleteTextUnit(k.units);
        }
        if (g_Hint)
            DeleteTextUnit(g_Hint->key.units);
    }

    void FinishFadeIn()
    {
        for (const HiddenLayout& h : g_FadingIn)
            SetAlpha(h.layout, h.alpha);
        g_FadingIn.clear();
    }

    void HideGameUi()
    {
        if (!kHideGameUi || g_GameUiHidden)
            return;
        FinishFadeIn();
        g_GameUiHidden = true;
        g_HiddenGameUi.clear();
        Object paletteWindow, hintWindow;
        Get(g_Ui->window, paletteWindow);
        if (g_Hint)
            Get(g_Hint->window, hintWindow);
        for (const WindowEntry& window : GetWindows())
        {
            if (!window.window || window.window == paletteWindow.ptr || window.window == hintWindow.ptr)
                continue;
            for (const LayoutEntry& layout : window.layouts)
            {
                if (layout.depth != 0 || !layout.layout || !IsVisible(layout.layout, Kind::Layout))
                    continue;
                const std::uint32_t handle = HandleFor(Kind::Layout, layout.layout, window.window);
                g_HiddenGameUi.push_back({ handle, GetAlpha(handle) });
                Call<void>(dev::DisableShaderResetLayout, layout.layout);
                SetVisible(handle, false);
            }
        }
    }

    void ShowGameUi(double now)
    {
        if (!g_GameUiHidden)
            return;
        g_GameUiHidden = false;
        for (const HiddenLayout& h : g_HiddenGameUi)
        {
            SetAlpha(h.layout, 0.0f);
            SetVisible(h.layout, true);
        }
        g_FadingIn.swap(g_HiddenGameUi);
        g_HiddenGameUi.clear();
        g_FadeStart = now;
    }

    void UpdateFadeIn(double now)
    {
        if (g_FadingIn.empty())
            return;
        const double t = (now - g_FadeStart) / kGameUiFadeIn;
        if (t >= 1.0)
        {
            FinishFadeIn();
            return;
        }
        for (const HiddenLayout& h : g_FadingIn)
            SetAlpha(h.layout, h.alpha * static_cast<float>(t));
    }

    bool PaletteOwnsMouse(std::uint32_t name)
    {
        static const std::uint32_t decide = Name32(kSelectMouse);
        static const std::uint32_t cancel = Name32(kBackMouse);
        return g_OwnsMouse.load(std::memory_order_acquire) && (name == decide || name == cancel);
    }

    void HoldPad(bool hold)
    {
        if (hold != g_HoldingPad)
        {
            if (hold)
            {
                Call<void>(dev::SetDisablePad, Name32(kHoldId));
                g_HoldingPad = Call<bool>(dev::IsDisablePad, Name32(kHoldId));
            }
            else
            {
                Call<void>(dev::ResetDisablePad, Name32(kHoldId));
                g_HoldingPad = false;
            }
        }
        g_OwnsMouse.store(g_HoldingPad, std::memory_order_release);
    }

    Channel& ActiveChannel() { return g_Open->channels[g_Open->active]; }

    void DrawCell(const Channel& channel, int i, bool full)
    {
        const Cell& cell = g_Ui->cells[i - 1];
        const int index = channel.top * kColumns + i;
        if (!cell.model)
            return;
        if (index > static_cast<int>(channel.colors->size()))
        {
            Anim(cell.model, "UI_ID_C_Pallet_Off");
            return;
        }
        const outfit::OutfitColor& color = (*channel.colors)[static_cast<std::size_t>(index) - 1];
        if (full)
        {
            Anim(cell.model, "UI_ID_C_Pallet_On");
            Anim(cell.model, "UI_ID_C_Pallet_Full");
            Anim(cell.model, "UI_ID_C_Pallet_New_Off");
            if (!color.icon.empty())
            {
                Anim(cell.model, "UI_ID_C_Pallet_Pallet");
                SetTexture(cell.icon, color.icon);
            }
            else
            {
                Anim(cell.model, "UI_ID_C_Pallet_None");
            }
            Anim(cell.model, index == channel.saved ? "UI_ID_C_Pallet_EQ_On" : "UI_ID_C_Pallet_EQ_Off");
        }
        const std::int8_t hot = index == channel.cursor ? 1 : 0;
        std::int8_t& shown = g_Open->cellHot[i - 1];
        if (shown == hot)
            return;
        shown = hot;
        SetAlpha(cell.noise, hot ? 1.0f : 0.0f);
        if (hot)
        {
            Anim(cell.model, "UI_ID_C_Pallet_Focus");
            Anim(cell.model, "UI_ID_C_Pallet_Cursor_Noise", true);
            LogGlowScale(cell, "before refit");
            FitGlow(cell);
        }
        else
        {
            Anim(cell.model, "UI_ID_C_Pallet_FocusOut");
        }
    }

    void UpdateSlider(const Channel& channel)
    {
        Open& o = *g_Open;
        const int rows = (static_cast<int>(channel.colors->size()) - 1) / kColumns + 1;
        const int shown = rows > kRows ? 1 : 0;
        if (shown != o.sliderShown)
        {
            o.sliderShown = shown;
            Anim(g_Ui->model, shown ? "UI_ID_C_Slider_on" : "UI_ID_C_Slider_off");
        }
        if (!shown)
            return;
        SetScale(g_Ui->thumb, 1.0f, static_cast<float>(kRows) / static_cast<float>(rows));
        SetTranslation(g_Ui->thumb, kThumbX, kThumbTopY - kTrackLength * static_cast<float>(channel.top) / static_cast<float>(rows), kThumbZ);
    }

    void Render()
    {
        Open& o = *g_Open;
        const Channel& channel = ActiveChannel();
        const int count = static_cast<int>(channel.colors->size());
        const bool first = o.drawnActive < 0;
        const bool tabs = o.drawnActive != static_cast<int>(o.active);
        const bool full = tabs || o.drawnTop != channel.top;
        if (full)
        {
            for (int i = 1; i <= kCells; ++i)
                DrawCell(channel, i, true);
            UpdateSlider(channel);
        }
        else
        {
            const int before = o.drawnCursor - channel.top * kColumns;
            if (o.drawnCursor > 0 && before >= 1 && before <= kCells)
                DrawCell(channel, before, false);
            DrawCell(channel, channel.cursor - channel.top * kColumns, false);
        }
        o.drawnActive = static_cast<int>(o.active);
        o.drawnTop = channel.top;
        o.drawnCursor = channel.cursor;

        char counter[32];
        std::snprintf(counter, sizeof(counter), "%d / %d", channel.cursor, count);
        SetText(g_Ui->nameText, g_Ui->nameUnits, counter);
        SetText(g_Ui->nameShadow, g_Ui->shadowUnits, counter);

        for (std::size_t i = 0; i < 2; ++i)
        {
            const Selector& s = g_Ui->channels[i];
            if (!s.layout)
                continue;
            const bool used = i < o.channels.size();
            SetVisible(s.layout, used);
            if (!used)
                continue;
            if (!tabs)
                continue;
            if (first)
            {
                Anim(s.model, kChannelIcon[o.channels[i].channel]);
                Anim(s.model, "UI_ID_C_Selector_New_Off");
            }
            if (i == o.active)
            {
                Anim(s.model, "UI_ID_C_SelectorIcon_Focus");
                Anim(s.model, "UI_ID_C_SelectorIcon_Noise", true);
            }
            else
            {
                Anim(s.model, "UI_ID_C_SelectorIcon_FocusOut");
            }
        }
        const bool switchable = o.channels.size() > 1;
        for (const Selector& b : g_Ui->buttons)
        {
            if (!b.layout)
                continue;
            SetVisible(b.layout, switchable);
            if (!switchable || !first)
                continue;
            Anim(b.model, "UI_ID_C_Sel_ButtonTxts");
            Anim(b.model, "UI_ID_C_Selector_New_Off");
            Anim(b.model, "UI_ID_C_SelectorIcon_FocusOut");
        }
    }

    void ApplyPreview(Channel& channel)
    {
        const outfit::OutfitColor& color = (*channel.colors)[static_cast<std::size_t>(channel.cursor) - 1];
        channel.previewAt = -1.0;
        channel.appliedPack = color.fpkCode != 0;
        ChooseColor(g_Open->target, channel.channel, static_cast<std::uint32_t>(channel.cursor - 1), false);
    }

    void Preview(Channel& channel, double now)
    {
        const outfit::OutfitColor& color = (*channel.colors)[static_cast<std::size_t>(channel.cursor) - 1];
        if (color.fpkCode != 0 || channel.appliedPack)
            channel.previewAt = now + kPackPreviewDelay;
        else
            ApplyPreview(channel);
    }

    void OpenPalette(const Target& target)
    {
        auto o = std::make_unique<Open>();
        o->target = target;
        std::memset(o->cellHot, -1, sizeof(o->cellHot));
        for (std::size_t ch = 0; ch < outfit::kColorChannelCount; ++ch)
        {
            const outfit::OutfitColorList* list = target.Colors(ch);
            if (!list || list->empty())
                continue;
            std::uint32_t saved = outfit::GetSavedColor(target.Key(), target.playerType, ch);
            if (saved >= list->size())
                saved = 0;
            Channel c;
            c.channel = ch;
            c.colors = list;
            c.saved = static_cast<int>(saved) + 1;
            c.cursor = c.saved;
            c.appliedPack = (*list)[saved].fpkCode != 0;
            const int row = static_cast<int>(saved) / kColumns;
            c.top = row > kRows - 1 ? row - (kRows - 1) : 0;
            o->channels.push_back(c);
        }
        if (o->channels.empty())
            return;
        HoldPad(true);
        if (!g_HoldingPad)
        {
            LogWarn("OutfitPalette: the game's menus could not be held (4 pad requests already), palette not opened");
            return;
        }
        LogInfo("OutfitPalette: opened for %s (%zu channel(s), saved %d)", target.Key(), o->channels.size(),
            o->channels[0].saved);
        g_Open = std::move(o);
        PlaySound(kSoundOpen);
        ShowHint(false);
        HideGameUi();
        SetVisible(g_Ui->root, true);
        SetAlpha(Child(g_Ui->model, Kind::NodeCommon, dev::GetModelNodeCommon, kPaletteGroup), 1.0f);
        Anim(g_Ui->model, "UI_ID_C_setin");
        Anim(g_Ui->model, "UI_ID_C_Pallet_Setin");
        for (const KeyHelp& k : g_Ui->keys)
            ShowKeyHelp(k);
        Render();
    }

    void SelectColour()
    {
        Open& o = *g_Open;
        Channel& c = ActiveChannel();
        const outfit::OutfitColor& color = (*c.colors)[static_cast<std::size_t>(c.cursor) - 1];
        const bool ok = ChooseColor(o.target, c.channel, static_cast<std::uint32_t>(c.cursor - 1), true);
        c.saved = c.cursor;
        c.previewAt = -1.0;
        c.appliedPack = color.fpkCode != 0;
        PlaySound(kSoundSelect);
        LogInfo("OutfitPalette: kept %s colour %d%s", c.channel == 0 ? "primary" : "secondary", c.cursor,
            ok ? "" : " (not applied)");
        o.drawnTop = -1;   // -1 forces a full redraw
        Render();
    }

    void ClosePalette(bool keep, const char* button, const char* mouse, bool byMouse, double now)
    {
        Open& o = *g_Open;
        for (Channel& c : o.channels)
        {
            const int chosen = keep ? c.cursor : c.saved;
            const bool ok = ChooseColor(o.target, c.channel, static_cast<std::uint32_t>(chosen - 1), true);
            LogInfo("OutfitPalette: %s %s colour %d%s", keep ? "kept" : "put back", c.channel == 0 ? "primary" : "secondary",
                chosen, ok ? "" : " (not applied)");
        }
        PlaySound(keep ? kSoundSelect : kSoundBack);
        o.outAnims.clear();
        const auto out = [&](std::uint32_t model, const char* name) {
            if (const std::uint32_t anim = AnimHandle(model, name))
            {
                Object a;
                if (Get(anim, a))
                    Call<void>(dev::StartAnimation, a.ptr);
                o.outAnims.push_back(anim);
            }
        };
        out(g_Ui->model, "UI_ID_C_setout");
        out(g_Ui->model, "UI_ID_C_Pallet_Setout");
        for (KeyHelp& k : g_Ui->keys)
            out(k.model, "mb_cmn_keyhelp_setout");
        o.closing = true;
        o.button = button;
        o.mouse = mouse;
        o.byMouse = byMouse;
        o.closedAt = now;
        o.releasedAt = -1.0;
        o.rootHidden = false;
    }

    bool MoveCursor(Channel& channel, int dx, int dy)
    {
        const int count = static_cast<int>(channel.colors->size());
        const int index = channel.cursor - 1;
        int col = index % kColumns;
        int row = index / kColumns;
        const int rows = (count - 1) / kColumns + 1;
        if (dx != 0)
        {
            const int inRow = count - row * kColumns < kColumns ? count - row * kColumns : kColumns;
            col = ((col + dx) % inRow + inRow) % inRow;
        }
        if (dy != 0)
            row = ((row + dy) % rows + rows) % rows;
        int moved = row * kColumns + col + 1;
        if (moved > count)
            moved = count;
        if (moved == channel.cursor)
            return false;
        channel.cursor = moved;
        row = (moved - 1) / kColumns;
        if (row < channel.top)
            channel.top = row;
        else if (row > channel.top + kRows - 1)
            channel.top = row - (kRows - 1);
        return true;
    }

    void UpdateOpen(double now)
    {
        Open& o = *g_Open;
        if (o.closing)
        {
            if (!o.rootHidden)
            {
                bool running = false;
                for (std::uint32_t anim : o.outAnims)
                    running = running || AnimRunning(anim);
                if (!running || now - o.closedAt > kOutAnimMax)
                {
                    SetVisible(g_Ui->root, false);
                    o.rootHidden = true;
                }
            }
            if (o.releasedAt < 0.0)
            {
                const bool released = o.byMouse ? MouseUp(o.mouse) : Released(o.button);
                const bool held = o.byMouse ? MousePressing(o.mouse) : Holding(o.button);
                if (released || (!held && now - o.closedAt > 0.5) || now - o.closedAt > 5.0)
                    o.releasedAt = now;
                return;
            }
            if (now - o.releasedAt < kReleaseGrace || !o.rootHidden)
                return;
            ShowGameUi(now);
            HoldPad(false);
            g_Open.reset();
            return;
        }

        RefreshKeyHelp();
        if (Pressed(kSelectButton) || MouseDown(kSelectMouse))
            return SelectColour();
        if (Pressed(kBackButton))
            return ClosePalette(false, kBackButton, kBackMouse, false, now);
        if (MouseDown(kBackMouse))
            return ClosePalette(false, kBackButton, kBackMouse, true, now);

        bool changed = false;
        Channel& channel = ActiveChannel();
        int sdx = 0, sdy = 0;
        StickMoved(o.stick, now, sdx, sdy);
        if (o.channels.size() > 1 && (Pressed("UI_LB") || Pressed("UI_RB")))
        {
            o.active = (o.active + 1) % o.channels.size();
            PlaySound(kSoundCursor);
            changed = true;
        }
        else
        {
            int dx = 0, dy = 0;
            if (Moved("UI_LEFT")) dx = -1; else if (Moved("UI_RIGHT")) dx = 1;
            if (Moved("UI_UP")) dy = -1; else if (Moved("UI_DOWN")) dy = 1;
            if (dx == 0 && dy == 0 && !(Holding("UI_LEFT") || Holding("UI_RIGHT") || Holding("UI_UP") || Holding("UI_DOWN")))
            {
                dx = sdx;
                dy = sdy;
            }
            if (dx == 0 && dy == 0)
            {
                const float wheel = MouseWheel();
                dy = wheel > 0.0f ? -1 : (wheel < 0.0f ? 1 : 0);
            }
            if ((dx != 0 || dy != 0) && MoveCursor(channel, dx, dy))
            {
                PlaySound(kSoundCursor);
                Preview(channel, now);
                changed = true;
            }
        }
        for (Channel& c : o.channels)
            if (c.previewAt >= 0.0 && now >= c.previewAt)
                ApplyPreview(c);
        if (changed)
            Render();
    }

    void UpdateList(double now)
    {
        outfit::SortieListCursor cursor;
        outfit::SortieHeadCursor headCursor;
        const bool onSuits = outfit::GetSortieListCursor(cursor);
        const bool onHeads = !onSuits && outfit::GetSortieHeadCursor(headCursor);
        const bool onList = onSuits || onHeads;
        Target target;
        bool onColours = false;
        bool cursorWorn = false;
        if (onSuits && cursor.entry)
        {
            target.entry = cursor.entry;
            target.playerType = PlayerTypeFor(cursor.entry);
            target.variant = cursor.variant;
            onColours = cursor.entry->HasColors(target.playerType, cursor.variant);
            cursorWorn = cursor.worn;
        }
        else if (onHeads)
        {
            onColours = HeadTarget(headCursor.head, target);
            cursorWorn = headCursor.worn;
        }

        if (g_PendingSet)
        {
            const int result = g_PendingByList ? outfit::TakeSortieListEquipResult() : 0;
            if (result == 1)
                g_PendingAsked = false;
            Target worn;
            const bool wornNow = g_Pending.head ? WornHeadTarget(worn) : WornTarget(worn);
            if ((!g_PendingByList || !g_PendingAsked) && (g_PendingByList || !onList)
                && wornNow && worn.Same(g_Pending))
            {
                g_PendingSet = false;
                OpenPalette(worn);
                if (!g_Open)
                    HoldPad(false);
                return;
            }
            const bool lost = result == -1 || (g_PendingAsked && now > g_PendingAskedUntil);
            if (lost || now > g_PendingUntil)
            {
                if (g_PendingByList)
                {
                    outfit::CancelSortieListEquip();
                    HoldPad(false);
                }
                g_PendingSet = false;
            }
        }

        ShowHint(onColours && !g_PendingSet);
        if (!onColours)
            return;
        RefreshKeyHelp();
        if (g_PendingSet || !PressedFree(kOpenButton))
            return;
        if (cursorWorn)
        {
            OpenPalette(target);
        }
        else if (onHeads ? outfit::RequestSortieHeadEquip() : outfit::RequestSortieListEquip())
        {
            LogInfo("OutfitPalette: equipping %s before opening", target.Key());
            HoldPad(true);
            g_Pending = target;
            g_PendingSet = true;
            g_PendingUntil = now + kEquipTimeout;
            g_PendingByList = true;
            g_PendingAsked = true;
            g_PendingAskedUntil = now + kEquipAskTimeout;
        }
        else if (onSuits && outfit::DecideSortieListCursor())
        {
            LogInfo("OutfitPalette: equipping %s before opening (list decide)", target.Key());
            g_Pending = target;
            g_PendingSet = true;
            g_PendingUntil = now + kEquipTimeout;
            g_PendingByList = false;
            g_PendingAsked = false;
        }
    }

    struct FoxPathArray
    {
        std::uint32_t count;
        std::uint32_t capacity;
        std::uint64_t* data;
    };
    using GetPackagePaths_t = void (__fastcall*)(FoxPathArray* list, std::uint16_t missionCode);
    using PathPushBack_t    = void* (__fastcall*)(FoxPathArray* list, const std::uint64_t* path);
    using PathFromText_t    = void* (__fastcall*)(std::uint64_t* path, const char* text);
    using PathDtor_t        = void (__fastcall*)(std::uint64_t* path);

    constexpr std::uint8_t kGetPackagePathsCode[] = { 0x4C, 0x8B, 0xDC, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00 };   // mov r11,rsp / push rdi / sub rsp,0x80

    GetPackagePaths_t g_OrigGetPackagePaths = nullptr;
    void* g_GetPackagePathsTarget = nullptr;

    enum PackAppend { PackAlreadyThere, PackAdded, PackMissing, PackFaulted };

    PackAppend AppendPackSEH(FoxPathArray* list, const char* text)
    {
        auto fromText = reinterpret_cast<PathFromText_t>(ResolveGameAddress(gAddr.FoxPath_FromCString));
        auto pushBack = reinterpret_cast<PathPushBack_t>(ResolveGameAddress(gAddr.FoxPathArray_PushBack));
        auto dtor = reinterpret_cast<PathDtor_t>(ResolveGameAddress(gAddr.Fox_Path_Dtor));
        if (!fromText || !pushBack || !dtor)
            return PackFaulted;
        __try
        {
            std::uint64_t path = 0;
            fromText(&path, text);
            if (path == 0)
                return PackFaulted;
            for (std::uint32_t i = 0; i < list->count; ++i)
                if (list->data[i] == path)
                {
                    dtor(&path);
                    return PackAlreadyThere;
                }
            if (!fox::detail::PathExistsByCode(path))
            {
                dtor(&path);
                return PackMissing;
            }
            pushBack(list, &path);
            dtor(&path);
            return PackAdded;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return PackFaulted;
        }
    }

    void __fastcall hkGetPackagePaths(FoxPathArray* list, std::uint16_t missionCode)
    {
        g_OrigGetPackagePaths(list, missionCode);
        if (!list || list->count == 0 || missionCode <= 5 || missionCode == 0xFFFF || missionCode / 10000 == 5)   // skip the boot missions (<= 5), no mission (0xFFFF) and FOB (5xxxx)
            return;
        static bool s_missingLogged = false;
        static bool s_faultLogged = false;
        switch (AppendPackSEH(list, outfit::kOutfitPalettePack))
        {
        case PackAdded:
            LogInfo("OutfitPalette: UI pack added to mission %u", static_cast<unsigned>(missionCode));
            break;
        case PackMissing:
            if (!s_missingLogged)
            {
                s_missingLogged = true;
                LogError("OutfitPalette: %s is not installed, the colour palette is off", outfit::kOutfitPalettePack);
            }
            break;
        case PackFaulted:
            if (!s_faultLogged)
            {
                s_faultLogged = true;
                LogWarn("OutfitPalette: the UI pack could not be added to mission %u", static_cast<unsigned>(missionCode));
            }
            break;
        default:
            break;
        }
    }

    bool CodeMatchesSEH(const void* code, const std::uint8_t* expect, std::size_t size)
    {
        __try { return std::memcmp(code, expect, size) == 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void ResetLocked()
    {
        HoldPad(false);
        if (g_GameUiHidden)
            for (const HiddenLayout& h : g_HiddenGameUi)
                SetVisible(h.layout, true);
        FinishFadeIn();
        DeleteTextUnits();
        g_HiddenGameUi.clear();
        g_GameUiHidden = false;
        g_Open.reset();
        g_Ui.reset();
        g_Hint.reset();
        g_HintShown = false;
        g_UsingKeyboard = -1;
        g_Pending = Target{};
        g_PendingSet = false;
        g_PendingByList = false;
        outfit::ForgetSortieListPanels();
        g_PendingAsked = false;
        outfit::CancelSortieListEquip();
        g_WindowsAsked = false;
        g_PaletteReady.store(false, std::memory_order_release);
    }

    void Tick()
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        const double now = Now();
        if (!g_MouseFilterSet)
            g_MouseFilterSet = uipad::SetGameMouseFilter(&PaletteOwnsMouse);
        UpdateFadeIn(now);
        if (!g_Ui || !g_Hint)
        {
            outfit::SortieListCursor cursor;
            outfit::SortieHeadCursor headCursor;
            if (!g_WindowsAsked && (outfit::GetSortieListCursor(cursor) || outfit::GetSortieHeadCursor(headCursor)))
                AskWindows();
            if (!g_WindowsAsked)
                return;
            if (!g_Ui)
                g_Ui = FindPalette();
            if (!g_Hint)
                g_Hint = FindHint();
            if (!g_Ui || !g_Hint)
                return;
            g_PaletteReady.store(true, std::memory_order_release);
        }
        if (g_Open)
            UpdateOpen(now);
        else
            UpdateList(now);
    }
}

namespace outfit
{
    bool Install_OutfitPalettePack()
    {
        if (g_GetPackagePathsTarget)
            return true;
        void* target = ResolveGameAddress(gAddr.Mission_GetPackagePaths);
        if (!target || !gAddr.FoxPathArray_PushBack || !gAddr.FoxPath_FromCString || !gAddr.Fox_Path_Dtor)
        {
            LogWarn("OutfitPalette: the mission pack list is not ported to this game version, the colour palette is off");
            return false;
        }
        if (!CodeMatchesSEH(target, kGetPackagePathsCode, sizeof(kGetPackagePathsCode)))
        {
            LogWarn("OutfitPalette: GetPackagePaths at %p is not the known code, the colour palette is off", target);
            return false;
        }
        if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkGetPackagePaths), reinterpret_cast<void**>(&g_OrigGetPackagePaths)))
            return false;
        g_GetPackagePathsTarget = target;
        return true;
    }

    void Uninstall_OutfitPalettePack()
    {
        if (!g_GetPackagePathsTarget)
            return;
        DisableAndRemoveHook(g_GetPackagePathsTarget);
        g_GetPackagePathsTarget = nullptr;
    }

    bool IsOutfitPaletteReady()
    {
        return g_GetPackagePathsTarget != nullptr && g_PaletteReady.load(std::memory_order_acquire);
    }

    bool Install_OutfitPalette()
    {
        if (g_Installed)
            return true;
        SetFrameTick(&Tick);
        g_Installed = true;
        return true;
    }

    void Uninstall_OutfitPalette()
    {
        if (!g_Installed)
            return;
        SetFrameTick(nullptr);
        Uninstall_OutfitPalettePack();
        uipad::SetGameButtonFilter(nullptr);
        uipad::SetGameMouseFilter(nullptr);
        std::lock_guard<std::mutex> lock(g_Lock);
        ResetLocked();
        g_Installed = false;
    }

    void ResetOutfitPaletteForMission()
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        ResetLocked();
    }
}
