#include <pch.h>
#include "TextIcons.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using PadPicture_t      = bool(__fastcall*)(float* u, float* v, std::uint8_t kind, std::uint32_t padButton);
    using KeyPicture_t      = bool(__fastcall*)(std::uint32_t keyCode, float* u, float* v);
    using SetIcon_t         = void(__fastcall*)(void* fontManager, std::uint32_t name, std::uint32_t texture,
                                                float padU, float padV, float keyU, float keyV);
    using GetIcon_t         = std::uint32_t(__fastcall*)(void* fontManager, std::uint32_t name, float* u, float* v);
    using PrefetchControl_t = void*(__fastcall*)();
    using CreatePrefetch_t  = bool(__fastcall*)(void* prefetch, std::uint64_t path, int level);
    using GetPrefetch_t     = void**(__fastcall*)(void* prefetch, void** texture, std::uint64_t path);
    using SetTexture_t      = void(__fastcall*)(void* fontManager, std::uint32_t texture, void** loaded,
                                                float cellWidth, float cellHeight, float drawScale, float advanceScale);

    constexpr std::uint32_t kButtonSheet        = 0x01798CBF;   // the texture every game button icon is cut from
    constexpr std::uint32_t kNoIcon             = 0xBF169F98;   // what the game draws for a name its icon table lacks
    constexpr std::size_t   kMaxNameBytes       = 63;           // the game's parser kills the process on a longer icon name
    constexpr int           kPrefetchLevel      = 2;            // what the game passes for its own icon textures
    constexpr int           kTextureRetryFrames = 120;          // a texture whose pack is not loaded yet is asked for again after this
    constexpr float         kMaxSize            = 8.0f;
    constexpr float         kFitSlack           = 1e-5f;        // 0.6 + 0.4 is a hair over 1 in floats

    constexpr const char* kPadSheetPath     = "/Assets/tpp/ui/texture/ButtonIcon/cmn_btn_icon_a_x36_alp.ftex";   // the PC game's own pad sheet; the Button Icons option redraws it in the chosen style
    constexpr float       kPadCellWidth     = 0.125f;   // the game's own numbers for that sheet (Ui_RegisterButtonIconTextures)
    constexpr float       kPadCellHeight    = 0.25f;
    constexpr float       kPadDrawScale     = 2.6f;
    constexpr float       kPadAdvanceScale  = 1.5f;

    constexpr std::size_t kNodeLeft   = 0x00;   // the button map is a std::map keyed by the action's StrCode32
    constexpr std::size_t kNodeParent = 0x08;
    constexpr std::size_t kNodeRight  = 0x10;
    constexpr std::size_t kNodeIsNil  = 0x19;
    constexpr std::size_t kNodeAction = 0x1C;
    constexpr std::size_t kNodePad    = 0x20;
    constexpr std::size_t kNodeKey    = 0x24;

    struct Engine
    {
        PadPicture_t      padPicture = nullptr;
        KeyPicture_t      keyPicture = nullptr;
        SetIcon_t         setIcon = nullptr;
        GetIcon_t         getIcon = nullptr;
        PrefetchControl_t prefetchControl = nullptr;
        CreatePrefetch_t  createPrefetch = nullptr;
        GetPrefetch_t     getPrefetch = nullptr;
        SetTexture_t      setTexture = nullptr;
        void**            fontManager = nullptr;
        void**            buttonMap = nullptr;
    };

    Engine         g;
    bool           g_Ready = false;
    std::once_flag g_ResolveOnce;

    struct Icon
    {
        std::string   name;               // empty for an action from the button map: nobody to warn, and it never replaces an icon
        std::uint32_t hash = 0;
        std::uint32_t texture = kButtonSheet;
        std::uint32_t action = 0;         // nonzero: the pictures come from this action's button and key
        std::uint64_t texturePath = 0;    // nonzero: a texture of the mod's own, loaded on first use
        float padU = 0.0f, padV = 0.0f, keyU = 0.0f, keyV = 0.0f;
        float width = 0.0f, height = 0.0f, size = 1.0f;
        float advance = 0.0f;             // how far the text moves on after the picture, 0: as far as size
        int   wait = 0;                   // frames before the texture is asked for again
        bool  redraw = false;             // an action's icon again, after its button changed
        bool  ownPicture = false;         // one of the DLL's own pictures: a warning names no Lua call
    };

    std::mutex        g_PendingLock;
    std::vector<Icon> g_Pending;
    std::atomic<bool> g_HasPending{ false };

    std::mutex                        g_AskedLock;
    std::unordered_set<std::uint32_t> g_AskedActions;

    // touched only by the writer, in the game's UI depend job
    void*                                                  g_WrittenTo = nullptr;   // the font manager our icons are in
    std::unordered_map<std::uint32_t, Icon>                g_Written;                // name -> what was written, to write again into a new font manager
    std::unordered_set<std::uint32_t>                      g_Textures;               // texture keys this DLL added
    std::unordered_map<std::uint32_t, std::vector<Icon>>   g_WaitingForAction;       // action -> named icons asked before it was in the button map
    std::vector<std::uint32_t>                             g_RedrawnActions;         // actions whose own icon was drawn again in this pass

    template <typename T> T Resolved(std::uintptr_t address)
    {
        return reinterpret_cast<T>(ResolveGameAddress(address));
    }

    bool Ready()
    {
        std::call_once(g_ResolveOnce, []() {
            g.padPicture      = Resolved<PadPicture_t>(gAddr.Ui_GetPadButtonUvShift);
            g.keyPicture      = Resolved<KeyPicture_t>(gAddr.Ui_GetKeyboardIconUV);
            g.setIcon         = Resolved<SetIcon_t>(gAddr.FontManager_SetIconTextureCoord);
            g.getIcon         = Resolved<GetIcon_t>(gAddr.FontManager_GetIconTextureCoord);
            g.prefetchControl = Resolved<PrefetchControl_t>(gAddr.Daemon_GetDaemonPrefetchController);
            g.createPrefetch  = Resolved<CreatePrefetch_t>(gAddr.PrefetchTextureController_CreatePrefetchTexture);
            g.getPrefetch     = Resolved<GetPrefetch_t>(gAddr.PrefetchTextureController_GetPrefetchTexture);
            g.setTexture      = Resolved<SetTexture_t>(gAddr.Ui_SetFontIconTexture);
            g.fontManager     = Resolved<void**>(gAddr.RawDaemon_s_fontManager);
            g.buttonMap       = Resolved<void**>(gAddr.ButtonMapper_Map);
            g_Ready = g.padPicture && g.keyPicture && g.setIcon && g.getIcon && g.prefetchControl && g.createPrefetch
                   && g.getPrefetch && g.setTexture && g.fontManager && g.buttonMap;
            if (!g_Ready)
                LogError("TextIcons: addresses missing on %s, V_UixUtility.SetTextIcon and SetButtonMap actions make no text icons",
                         GetGameBuildName(gGameBuild));
        });
        return g_Ready;
    }

    bool PadPicture(int padButton, float& u, float& v)
    {
        return padButton >= 0 && g.padPicture(&u, &v, 0, static_cast<std::uint32_t>(padButton));   // kind 0: a plain button, as the game asks
    }

    bool KeyPicture(int keyCode, float& u, float& v)
    {
        return keyCode >= 0 && g.keyPicture(static_cast<std::uint32_t>(keyCode), &u, &v);
    }

    template <typename T> T Field(const std::uint8_t* node, std::size_t offset)
    {
        return *reinterpret_cast<const T*>(node + offset);
    }

    bool FindActionSEH(std::uint32_t action, int& padButton, int& keyCode)   // the game's own walk (ButtonNameToButtonNo), without its jump on a miss
    {
        __try
        {
            const auto* map  = *reinterpret_cast<std::uint8_t* const*>(g.buttonMap);
            const auto* head = map ? *reinterpret_cast<std::uint8_t* const*>(map) : nullptr;
            if (!head)
                return false;
            const std::uint8_t* found = head;
            for (const std::uint8_t* node = Field<const std::uint8_t*>(head, kNodeParent); !node[kNodeIsNil];)
            {
                if (Field<std::uint32_t>(node, kNodeAction) < action)
                    node = Field<const std::uint8_t*>(node, kNodeRight);
                else
                {
                    found = node;
                    node = Field<const std::uint8_t*>(node, kNodeLeft);
                }
            }
            if (found == head || Field<std::uint32_t>(found, kNodeAction) != action)
                return false;
            padButton = Field<int>(found, kNodePad);
            keyCode   = Field<int>(found, kNodeKey);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SetActionPadSEH(std::uint32_t action, int padButton)   // the same walk, then the button is written into the action's node
    {
        __try
        {
            auto* map  = *reinterpret_cast<std::uint8_t**>(g.buttonMap);
            auto* head = map ? *reinterpret_cast<std::uint8_t**>(map) : nullptr;
            if (!head)
                return false;
            std::uint8_t* found = head;
            for (std::uint8_t* node = Field<std::uint8_t*>(head, kNodeParent); !node[kNodeIsNil];)
            {
                if (Field<std::uint32_t>(node, kNodeAction) < action)
                    node = Field<std::uint8_t*>(node, kNodeRight);
                else
                {
                    found = node;
                    node = Field<std::uint8_t*>(node, kNodeLeft);
                }
            }
            if (found == head || Field<std::uint32_t>(found, kNodeAction) != action)
                return false;
            *reinterpret_cast<int*>(found + kNodePad) = padButton;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void Warn(const Icon& icon, const char* why)
    {
        if (!icon.name.empty())
            LogWarnOnce(icon.ownPicture ? "TextIcons: %s %s" : "V_UixUtility.SetTextIcon: %s %s", icon.name.c_str(), why);
    }

    bool SameRequest(const Icon& a, const Icon& b)
    {
        return a.hash == b.hash && a.name.empty() == b.name.empty();
    }

    void Keep(std::vector<Icon>& icons, Icon icon)   // a newer request for the same name replaces the older one
    {
        for (Icon& kept : icons)
            if (SameRequest(kept, icon))
            {
                kept = std::move(icon);
                return;
            }
        icons.push_back(std::move(icon));
    }

    void Queue(Icon icon)
    {
        std::lock_guard<std::mutex> lock(g_PendingLock);
        Keep(g_Pending, std::move(icon));
        g_HasPending.store(true, std::memory_order_release);
    }

    void QueueUnlessAsked(const Icon& icon)   // a request already waiting for the same name is the newer one
    {
        std::lock_guard<std::mutex> lock(g_PendingLock);
        for (const Icon& pending : g_Pending)
            if (SameRequest(pending, icon))
                return;
        g_Pending.push_back(icon);
        g_HasPending.store(true, std::memory_order_release);
    }

    bool Write(void* fontManager, Icon& icon)   // false: not possible yet, try again next frame
    {
        const bool fromButtonMap = icon.name.empty();
        if (fromButtonMap)
        {
            auto waiting = g_WaitingForAction.find(icon.action);
            if (waiting != g_WaitingForAction.end())
            {
                for (Icon& named : waiting->second)
                    Queue(std::move(named));
                g_WaitingForAction.erase(waiting);
            }
        }

        float u = 0.0f, v = 0.0f;
        const auto written = g_Written.find(icon.hash);
        const bool overOurOwn = icon.redraw && written != g_Written.end() && written->second.name.empty();
        if (!overOurOwn && g.getIcon(fontManager, icon.hash, &u, &v) != kNoIcon && (fromButtonMap || written == g_Written.end()))
        {
            Warn(icon, "is one of the game's own icons, left as it is");
            return true;
        }

        if (icon.action)
        {
            int padButton = -1, keyCode = -1;
            if (!FindActionSEH(icon.action, padButton, keyCode))
            {
                if (!fromButtonMap)
                {
                    Warn(icon, "waits until its action is added to the button map (UiDaemon.SetButtonMap)");
                    Keep(g_WaitingForAction[icon.action], icon);
                }
                return true;
            }
            const bool pad = PadPicture(padButton, icon.padU, icon.padV);
            const bool key = KeyPicture(keyCode, icon.keyU, icon.keyV);
            if (!pad && !key)
            {
                Warn(icon, "has no picture: neither its action's button nor its key has one");
                return true;
            }
        }

        if (icon.texturePath && g_Textures.count(icon.texture) == 0)   // the game's own recipe for its icon textures (0x140916FF0)
        {
            if (icon.wait > 0)
            {
                --icon.wait;
                return false;
            }
            void* prefetch = g.prefetchControl();
            if (!prefetch)
                return false;
            void* loaded = nullptr;
            if (g.createPrefetch(prefetch, icon.texturePath, kPrefetchLevel))
                g.getPrefetch(prefetch, &loaded, icon.texturePath);
            if (!loaded)
            {
                Warn(icon, "has no picture yet: its texture is not loaded, asking again every few seconds");
                icon.wait = kTextureRetryFrames;
                return false;
            }
            g.setTexture(fontManager, icon.texture, &loaded, icon.width, icon.height, icon.size,
                         icon.advance > 0.0f ? icon.advance : icon.size);   // takes our reference
            g_Textures.insert(icon.texture);
        }

        g.setIcon(fontManager, icon.hash, icon.texture, icon.padU, icon.padV, icon.keyU, icon.keyV);
        const bool redrawn = icon.redraw;
        icon.redraw = false;
        g_Written[icon.hash] = icon;
        if (redrawn)
            g_RedrawnActions.push_back(icon.action);
        return true;
    }

    bool NameIsUsable(const std::string& name)
    {
        if (name.empty() || name.size() > kMaxNameBytes)
        {
            LogWarnOnce("V_UixUtility.SetTextIcon: name must be 1 to %zu characters", kMaxNameBytes);
            return false;
        }
        for (const char c : name)
            if (c <= ' ' || c > '~' || c == '<' || c == '>' || c == '&')   // the tag parser ends a name at > or &
            {
                LogWarnOnce("V_UixUtility.SetTextIcon: name \"%s\" may only hold printable characters other than < > &", name.c_str());
                return false;
            }
        if (name.size() >= 2 && (name[0] == 'R' || name[0] == 'r') && name[1] == '=')   // the tag parser reads <I=G=R=...> as another kind of tag
        {
            LogWarnOnce("V_UixUtility.SetTextIcon: name \"%s\" may not start with R=", name.c_str());
            return false;
        }
        return true;
    }

    bool Prepare(const std::string& name, Icon& icon)
    {
        if (!Ready() || !NameIsUsable(name))
            return false;
        icon.name = name;
        icon.hash = FoxHashes::StrCode32(name);   // the hash the text parser takes of <I=G=name>
        return icon.hash != 0;
    }

    bool InRange(float value, float low, float high)
    {
        return std::isfinite(value) && value >= low && value <= high + kFitSlack;
    }

    std::uint32_t TextureKey(std::uint64_t path, float width, float height, float size)   // one key per cell size: the font manager keeps the cell with the texture
    {
        char key[96];
        std::snprintf(key, sizeof(key), "V_FrameWork text icon %016llX %g %g %g", static_cast<unsigned long long>(path), width, height, size);
        return FoxHashes::StrCode32(key);
    }
}

namespace TextIcons
{
    bool SetFixed(const std::string& name, int padButton, int keyCode)
    {
        Icon icon;
        if (!Prepare(name, icon))
            return false;
        if (!PadPicture(padButton, icon.padU, icon.padV))
        {
            LogWarnOnce("V_UixUtility.SetTextIcon: %s: pad %d has no picture (fox.PAD_ numbers 0 to 15)", name.c_str(), padButton);
            return false;
        }
        if (!KeyPicture(keyCode, icon.keyU, icon.keyV))
        {
            LogWarnOnce("V_UixUtility.SetTextIcon: %s: key %d has no picture", name.c_str(), keyCode);
            return false;
        }
        Queue(std::move(icon));
        return true;
    }

    bool SetForAction(const std::string& name, const std::string& action)
    {
        Icon icon;
        if (!Prepare(name, icon))
            return false;
        icon.action = action.empty() ? 0 : FoxHashes::StrCode32(action);
        if (!icon.action)
        {
            LogWarnOnce("V_UixUtility.SetTextIcon: %s: action is empty", name.c_str());
            return false;
        }
        Queue(std::move(icon));
        return true;
    }

    bool SetPicture(const std::string& name, std::uint64_t texturePath, float u, float v, float width, float height, float size)
    {
        Icon icon;
        if (!Prepare(name, icon))
            return false;
        if (!texturePath)
        {
            LogWarnOnce("V_UixUtility.SetTextIcon: %s: texture must be an .ftex path or a \"0x...\" path id", name.c_str());
            return false;
        }
        if (!InRange(u, 0.0f, 1.0f) || !InRange(v, 0.0f, 1.0f) || !InRange(width, 0.0f, 1.0f - u) || !InRange(height, 0.0f, 1.0f - v)
            || width <= 0.0f || height <= 0.0f)
        {
            LogWarnOnce("V_UixUtility.SetTextIcon: %s: u, v, width and height are parts of the texture from 0 to 1, and the picture must fit inside it",
                        name.c_str());
            return false;
        }
        if (!InRange(size, 0.0f, kMaxSize) || size <= 0.0f)
        {
            LogWarnOnce("V_UixUtility.SetTextIcon: %s: size must be above 0 and at most %g", name.c_str(), kMaxSize);
            return false;
        }
        icon.texturePath = texturePath;
        icon.texture = TextureKey(texturePath, width, height, size);
        icon.padU = icon.keyU = u;
        icon.padV = icon.keyV = v;
        icon.width = width;
        icon.height = height;
        icon.size = size;
        Queue(std::move(icon));
        return true;
    }

    bool SetPadPicture(const std::string& name, int padButton)
    {
        Icon icon;
        if (!Prepare(name, icon))
            return false;
        if (!PadPicture(padButton, icon.padU, icon.padV))
        {
            LogWarnOnce("TextIcons: %s: pad %d has no picture (fox.PAD_ numbers 0 to 15)", name.c_str(), padButton);
            return false;
        }
        icon.texturePath = FoxHashes::PathCode64Ext(kPadSheetPath);
        if (!icon.texturePath)
            return false;
        icon.texture = TextureKey(icon.texturePath, kPadCellWidth, kPadCellHeight, kPadDrawScale);
        icon.keyU = icon.padU;   // a texture of its own, so the keyboard's sheet never stands in for it
        icon.keyV = icon.padV;
        icon.width = kPadCellWidth;
        icon.height = kPadCellHeight;
        icon.size = kPadDrawScale;
        icon.advance = kPadAdvanceScale;
        icon.ownPicture = true;
        Queue(std::move(icon));
        return true;
    }

    bool SetActionPadButton(std::uint32_t action, int padButton)
    {
        Ready();   // the map is written even when an icon address is missing
        if (!action || !g.buttonMap || !SetActionPadSEH(action, padButton))
            return false;
        if (g_Ready)
        {
            Icon icon;
            icon.hash = action;
            icon.action = action;
            icon.redraw = true;
            Queue(std::move(icon));
        }
        return true;
    }

    void AddForNewAction(std::uint32_t action)
    {
        if (!action || !Ready())
            return;
        {
            std::lock_guard<std::mutex> lock(g_AskedLock);
            if (!g_AskedActions.insert(action).second)
                return;
        }
        Icon icon;
        icon.hash = action;
        icon.action = action;
        Queue(std::move(icon));
    }

    bool FindButton(std::uint32_t action, int& padButton, int& keyCode)
    {
        Ready();   // the map is read even when an icon address is missing
        return g.buttonMap && FindActionSEH(action, padButton, keyCode);
    }

    void WritePending()
    {
        if (!g_HasPending.load(std::memory_order_acquire) && (g_Written.empty() || *g.fontManager == g_WrittenTo))
            return;
        void* fontManager = *g.fontManager;
        if (!fontManager)
            return;
        std::vector<Icon> work;
        if (fontManager != g_WrittenTo)   // a new font manager starts empty: everything goes in again
        {
            for (auto& written : g_Written)
                work.push_back(std::move(written.second));
            g_Written.clear();
            g_Textures.clear();
            g_WrittenTo = fontManager;
        }
        {
            std::lock_guard<std::mutex> lock(g_PendingLock);
            for (Icon& icon : g_Pending)
                work.push_back(std::move(icon));
            g_Pending.clear();
            g_HasPending.store(false, std::memory_order_release);
        }
        std::vector<Icon> later;
        for (Icon& icon : work)
            if (!Write(fontManager, icon))
                later.push_back(std::move(icon));
        if (!later.empty())
        {
            std::lock_guard<std::mutex> lock(g_PendingLock);
            for (Icon& icon : later)
            {
                bool askedAgain = false;   // a newer request that came in meanwhile wins over the retry
                for (const Icon& pending : g_Pending)
                    askedAgain = askedAgain || SameRequest(pending, icon);
                if (!askedAgain)
                    g_Pending.push_back(std::move(icon));
            }
            g_HasPending.store(true, std::memory_order_release);
        }
        for (const std::uint32_t action : g_RedrawnActions)   // named icons that show the same action follow its new button
            for (const auto& written : g_Written)
                if (!written.second.name.empty() && written.second.action == action)
                    QueueUnlessAsked(written.second);
        g_RedrawnActions.clear();
    }
}
