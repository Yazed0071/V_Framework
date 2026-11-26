#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

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
    bool Lookup(std::uint32_t handle, Object& out);

    void Queue(std::uint32_t handle, std::function<void(const Object&)> fn);
    void QueueNoHandle(std::function<void()> fn);

    bool Guard(void (*thunk)(void*), void* context);
    template <typename F> bool Guarded(F& f)
    {
        return Guard([](void* c) { (*static_cast<F*>(c))(); }, &f);
    }

    std::uint32_t CreateTextUnit(int count);
    int TextUnitCount(void* units);   // 0 when CreateTextUnit didn't make them
    void DeleteTextUnit(std::uint32_t units);
    void KeepTextAlive(void* units, const Object& node, std::shared_ptr<std::string> text);
    std::shared_ptr<const void> PinTextUnit(void* units);   // held by a queued call: the units are not freed until it lets go

    void RememberLink(void* child, void* childWindow, void* parentWindow);
    int ConnectProblem(void* child, void* childWindow, void* parent);
    void ForgetLink(void* child);

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

    struct MarkerEntry
    {
        std::uint16_t gameObjectId = 0xFFFF;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        bool          hasMarkerFlag = false;
        std::uint32_t markerFlag = 0;          // low byte is the marker type, the rest is its state
        bool          dying = false;
        bool          important = false;
        bool          questImportant = false;
        bool          viewWorldIcon = false;
        bool          viewMapIcon = false;
        bool          viewWorldGoal = false;
        bool          viewMapGoal = false;
        std::uint8_t  radiusLevel = 0;         // 0 means no area
        float         radius = 0.0f;           // in metres
        float mapX = 0.0f, mapY = 0.0f, mapZ = 0.0f;   // an area's centre, moved off the target so you have to search
    };
    int GetMarkers(MarkerEntry* out, int capacity);

    struct UiMarkerSlot
    {
        int           slot = 0;
        std::uint16_t gameObjectId = 0xFFFF;
        std::uint32_t markerFlag = 0;          // low byte is the marker type (3 = an Intel enemy search area)
        bool          important = false;
        bool          questImportant = false;
        bool          viewWorldIcon = false;
        bool          viewMapIcon = false;
        bool          viewWorldGoal = false;
        bool          viewMapGoal = false;
        std::uint8_t  radiusLevel = 0;
        float         radius = 0.0f;
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };
    int GetUiMarkerSlots(UiMarkerSlot* out, int capacity);   // every UI marker the iDroid map can draw, Marker2 or not

    struct EnemyRoute
    {
        int   group = 0;
        float width = 0.0f;
        int   count = 0;
        float x[30] = {};
        float z[30] = {};
    };
    int GetEnemyRoutes(EnemyRoute* out, int capacity);

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

    bool LoadResidentUiPack(const std::string& packPath, std::uint32_t sizeBytes);
    bool LoadResidentPackById(std::uint64_t packId, std::uint32_t sizeBytes, const char* label);
    void ReleaseResidentPackById(std::uint64_t packId);
    void* GetResidentPackBlock(std::uint64_t packId);
    bool  IsResidentPackLoading(std::uint64_t packId);

    bool IsResidentUiPackLoaded(const std::string& packPath);

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

    enum class UiFunction
    {
        SetButtonIconTextureByName, SetPlatformButtonTexture, IsGamePauseMenu, IsInDemo, GetIconFtexPath, GetWeaponItemNameLangId,
        StopSound, GetSoundTypeId, AnimationPause, AnimationForceLoop, AnimationUnsetForceLoop, AddChildWindow,
        IsDirButtonPress, IsDirButtonLong, CheckIllegalDirButtonInput, UiCommonDataManagerGetInstance, GetEnableDirButton,
        PrefabLayoutNameTable,   // data, not a function: 20 layout names, one per prefab type
        Count,
    };
    void* Function(UiFunction f);

    void** UiUtility();
    void*  UiUtilityMethod(std::size_t retailOffset);

    void RememberButtonMesh(void* node, void* window, std::uint32_t registId);
    void ForgetButtonMesh(void* node);
    void ForgetButtonMeshesOf(std::uint32_t registId);
    void NoteDrawScenePaused(bool paused);

    struct LineVertex { float x, y, z, w; };
    void QueueLineDraw(std::uint32_t lineNode, std::vector<std::uint16_t> indices, std::vector<std::uint32_t> stripLengths, std::vector<LineVertex> vertices);
    void QueueResetLineDraw(std::uint32_t lineNode);

    bool QueueAddChildWindow(const Object& parent, std::uint32_t childHandle, const Object& child);

    void* DisconnectComponentFn();
    void* GetTimeLengthFn();
    void* SetFontGroupFn();
    void* StartSoundFn();
    void* SoundControlStartFn();
    void* SoundControlStopFn();
    void* SoundControl();
    void* FocalLengthZoomRatioFn();
    int   WindowGroup(void* window);
    void* WindowRootLayout(void* window);
}

bool Install_UixUtilityControl_Hook();
bool Uninstall_UixUtilityControl_Hook();
