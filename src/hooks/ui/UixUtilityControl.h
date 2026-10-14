#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Runtime behind V_UixUtility: Lua gets the dev team's fox::uix::UixUtility API with opaque handles.
// Setters run once on the UI thread of the handle's window group, getters read right away.
namespace UixUtilityControl
{
    enum class Kind : std::uint8_t
    {
        None, Window, Layout, Model, NodeCommon, NodeText, NodeMesh, NodeLine, Animation, TextUnit, TriggerPool,
    };

    struct Object
    {
        Kind  kind = Kind::None;
        void* ptr = nullptr;
        void* window = nullptr;   // the window the object belongs to; null for text units
        void* owner = nullptr;    // for nodes: the model they came from
    };

    bool IsSupportedOnThisBuild();

    void** Utility();
    void*  Method(std::size_t devOffset);
    void** MethodSlot(std::size_t devOffset);

    void SetFrameTick(void (*tick)());

    std::uint32_t HandleFor(Kind kind, void* ptr, void* window, void* owner = nullptr);   // 0 for a null ptr; same object, same handle
    bool Lookup(std::uint32_t handle, Object& out);                  // false once the object's window is gone

    // runs once on the UI thread before the next layout pass of the handle's window group
    void Queue(std::uint32_t handle, std::function<void(const Object&)> fn);
    void QueueNoHandle(std::function<void()> fn);

    // SEH around a game call made from the Lua thread
    bool Guard(void (*thunk)(void*), void* context);
    template <typename F> bool Guarded(F& f)
    {
        return Guard([](void* c) { (*static_cast<F*>(c))(); }, &f);
    }

    // text units point into the strings they show, so V_FrameWork keeps each string alive with its units
    std::uint32_t CreateTextUnit(int count);
    int TextUnitCount(void* units);   // 0 when CreateTextUnit didn't make them
    void DeleteTextUnit(std::uint32_t units);
    void KeepTextAlive(void* units, const Object& node, std::shared_ptr<std::string> text);   // null text keeps the one it has
    std::shared_ptr<const void> PinTextUnit(void* units);   // held by a queued call: the units are not freed until it lets go

    // links made with ConnectComponent are undone before either window is deleted (the game never does)
    void RememberLink(void* child, void* childWindow, void* parentWindow);
    // why child must not be connected under parent: 0 fine, 1 itself, 2 a window's root layout, 3 a loop (parent sits
    // under child), 4 unreadable. Run on the UI thread, right before the connect.
    int ConnectProblem(void* child, void* childWindow, void* parent);
    void ForgetLink(void* child);

    // screen positions run -1..1 (x right, y up); with a component they are in its HUD units, ready for SetTranslation
    struct ScreenPoint
    {
        float x = 0.0f, y = 0.0f;
        bool  onScreen = false;
    };

    struct MarkerPosition
    {
        bool  enabled = false;   // marker on: marked by the player or a buddy, or enabled by script
        float world[3] = {};
        ScreenPoint screen;      // with a component: in its HUD units
    };
    int GetMarkerPosition(std::uint16_t objectId, const Object* component, MarkerPosition& out);   // 1 found, 0 no marker, -1 unknown

    // every game object whose marker is on right now (marked, or enabled by a mission script); -1 when not ported
    struct MarkerEntry
    {
        std::uint16_t gameObjectId = 0xFFFF;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        bool          hasMarkerFlag = false;   // false when the HUD has no marker slot for it this frame
        std::uint32_t markerFlag = 0;          // UiMarkerCommonData::GetMarkerFlag: low byte = marker type, higher bits = state
        bool          dying = false;           // the head marker shows the dying colour (V_FrameWork's dying-state hook)
        bool          important = false;       // UiMarkerCommonData::IsMarkerImportant: a mission target (the yellow flashing circle)
        bool          questImportant = false;  // IsMarkerQuestImportant: a side-op target (the blue flashing circle)
        bool          viewWorldIcon = false;   // IsMarkerViewWorldIcon: shown in the world as a head marker
        bool          viewMapIcon = false;     // IsMarkerViewMapIcon: shown on the iDroid map
        bool          viewWorldGoal = false;   // IsMarkerViewWorldGoal: shown in the world as a mission goal (TppMarker.Enable view "all")
        bool          viewMapGoal = false;     // IsMarkerViewMapGoal: shown on the iDroid map as a mission goal
        std::uint8_t  radiusLevel = 0;         // the area size TppMarker.Enable gave it (0-9), 0 = no area
        float         radius = 0.0f;           // that area's radius in metres
        float mapX = 0.0f, mapY = 0.0f, mapZ = 0.0f;   // where the HUD and iDroid draw it: an area's centre, moved off the target by randomLevel
    };
    int GetMarkers(MarkerEntry* out, int capacity);

    // the predicted routes TppUiCommand.ShowEnemyRoutePoints keeps (5 groups, up to 30 points each, x/z only)
    struct EnemyRoute
    {
        int   group = 0;
        float width = 0.0f;
        int   count = 0;
        float x[30] = {};
        float z[30] = {};
    };
    int GetEnemyRoutes(EnemyRoute* out, int capacity);

    // where a node is drawn after every parent node, model and layout above it
    bool NodeToScreen(const Object& node, const Object* component, ScreenPoint& out);

    struct LayoutRecord
    {
        std::string   path;
        std::uint64_t id = 0;
        std::uint64_t drawPriority = 0;
        int           fontType = 0;
    };
    void CreateUiWindow(std::uint64_t name, const std::vector<LayoutRecord>& layouts, std::uint32_t group, std::uint32_t pauseMask, std::uint16_t priority);
    void CreateGraphWindow(std::uint64_t name, const std::string& graph, std::uint32_t group, std::uint32_t pauseMask, std::uint16_t priority);
    void DestroyUiWindow(const Object& window);

    // keeps a mod's UI pack (.fpk + .fpkd; its .pftxs comes along) loaded for the rest of the game in a block group of
    // its own, so windows made from it live through mission loads and loading screens. sizeBytes = block memory for the
    // pack's fpk + fpkd contents (textures are not counted). Loads once the game's own resident UI block is up; asking
    // again for the same pack does nothing. false on a build it is not ported to.
    bool LoadResidentUiPack(const std::string& packPath, std::uint32_t sizeBytes);

    // true once a pack asked for with LoadResidentUiPack is loaded and active, so CreateWindow can use its files
    bool IsResidentUiPackLoaded(const std::string& packPath);

    // every live window with all its layouts (depth 0 = the window's root layout) and their models, read right away
    struct ModelEntry
    {
        void*         model = nullptr;
        std::uint32_t id = 0;
    };
    struct LayoutEntry
    {
        void*         layout = nullptr;
        std::uint32_t id = 0;
        std::uint32_t parentId = 0;
        int           depth = 0;
        std::vector<ModelEntry> models;
    };
    struct WindowEntry
    {
        void*         window = nullptr;
        void*         parent = nullptr;
        std::uint32_t id = 0;
        int           group = -1;
        std::vector<LayoutEntry> layouts;
    };
    std::vector<WindowEntry> GetWindows();

    // tpp::ui::utility and fox::ui functions the dev team called directly
    enum class UiFunction
    {
        SetButtonIconTextureByName, SetPlatformButtonTexture, IsGamePauseMenu, IsInDemo, GetIconFtexPath, GetWeaponItemNameLangId,
        StopSound, GetSoundTypeId, AnimationPause, AnimationForceLoop, AnimationUnsetForceLoop, AddChildWindow, PostTriggerMessage,
        Count,
    };
    void* Function(UiFunction f);   // null when not ported to this build yet

    // TPP's own UI helper, tpp::ui::utility::UiUtility; offsets are its retail EN/JP 1.0.15.4 vtable slots
    void** UiUtility();
    void*  UiUtilityMethod(std::size_t retailOffset);

    // the game's button-icon table (RegistButtonMeshParam) keeps raw node pointers: V_FrameWork erases a node before its window is deleted
    void RememberButtonMesh(void* node, void* window, std::uint32_t registId);
    void ForgetButtonMesh(void* node);
    void ForgetButtonMeshesOf(std::uint32_t registId);
    void NoteDrawScenePaused(bool paused);   // so an unloading DLL can give the 3D world back

    // the line node keeps pointers into the index and vertex arrays, so V_FrameWork owns them until the node lets go
    struct LineVertex { float x, y, z, w; };
    void QueueLineDraw(std::uint32_t lineNode, std::vector<std::uint16_t> indices, std::vector<LineVertex> vertices);
    void QueueResetLineDraw(std::uint32_t lineNode);

    // only windows made by V_UixUtility.CreateWindow can be adopted: a vanilla child would die with our parent
    bool QueueAddChildWindow(const Object& parent, std::uint32_t childHandle, const Object& child);

    // engine functions the dev team called directly (not UixUtility methods)
    void* DisconnectComponentFn();
    void* GetTimeLengthFn();
    void* SetFontGroupFn();
    void* StartSoundFn();
    void* SoundControlStartFn();
    void* SoundControlStopFn();
    void* SoundControl();
    void* FocalLengthZoomRatioFn();   // null when not ported to this build yet
    int   WindowGroup(void* window);
}

bool Install_UixUtilityControl_Hook();
bool Uninstall_UixUtilityControl_Hook();
