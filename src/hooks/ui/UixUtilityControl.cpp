#include "pch.h"
#include "UixUtilityControl.h"
#include "UiPadLock.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HeadMarkMarkerEvCall_SetIconSubType.h"
#include "HookUtils.h"
#include "../../lua/FrameCallbacks.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using namespace UixUtilityControl;

    struct UiAddrs
    {
        std::uintptr_t updateLayouts;
        std::uintptr_t getWindowManager;
        std::uintptr_t destroyWindow;
        std::uintptr_t createWindow;
        std::uintptr_t getTimeLength;
        std::uintptr_t setFontGroup;
        std::uintptr_t startSound;
        std::uintptr_t soundControlStop;
        std::uintptr_t soundControl;
        std::uintptr_t lockShared;
        std::uintptr_t unlock;
        std::uintptr_t blockGroupsLock;
        std::uintptr_t blockGroupHead;
        std::uintptr_t blockGroupNodes;
        std::uintptr_t blockGroupValues;
        std::uintptr_t disconnectComponent;
        std::uintptr_t deleteDestroyedWindows;
        std::uintptr_t objCreateCommon;
        std::uintptr_t entityPtrSet;
        // GetMarkers / GetMarkerPosition / GetNodeScreenPosition; zero = not ported to this build yet
        std::uintptr_t cameraViewMatrix;
        std::uintptr_t projectionMatrix;
        std::uintptr_t focalLengthZoomRatio;
        std::uintptr_t isOutsideScreen;
        std::uintptr_t hudCommonDataManager;
        std::uintptr_t getViewport;
        std::uintptr_t zoomRaise;
        std::uintptr_t screenMargin;
        std::uintptr_t getUiUtility;
        // UiFunction, in enum order; zero = not ported to this build yet
        std::uintptr_t functions[static_cast<int>(UiFunction::Count)];
    };

    UiAddrs AddrsForBuild()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
            return {
                0x141DF5A40ull, // fox::ui::WindowManager::UpdateLayouts
                0x141DEF6D0ull, // fox::ui::WindowDaemon::GetWindowManager
                0x141DABE90ull, // fox::ui::WindowFunction::DestroyWindow
                0x141DABD50ull, // fox::ui::WindowFunction::CreateWindow
                0x141DBF3E0ull, // fox::ui::Animation::GetTimeLength
                0x141DB6800ull, // fox::ui::ModelNodeText::SetFontGroup
                0x14091B8E0ull, // tpp::ui::utility::StartSound
                0x141DCA2D0ull, // fox::ui::SoundControl::Stop
                0x142C8FAD8ull, // the SoundControl the UI graphs play through
                0x1400241F0ull, // fox::LwRWLockSection::LockShared
                0x1400243F0ull, // fox::LwRWLockSection::Unlock
                0x142B64238ull, // g_blockGroupsLock
                0x1429E3CA0ull, // first entry of the block group map
                0x1429E3CB8ull, // block group map nodes
                0x1429E3CC0ull, // block group map values
                0x141DACF80ull, // fox::ui::LayoutUtility::DisconnectComponent
                0x141DF55F0ull, // fox::ui::WindowManager::DeleteDestroyedWindows
                0x141DA9980ull, // fox::ui::ObjControl::CreateCommon
                0x1400B31F0ull, // fox::impl::EntityPtrBase::Set
                0x1409122B0ull, // tpp::ui::utility::GetGameCameraViewMatrix
                0x140913C00ull, // tpp::ui::utility::GetProjectionMatrix
                0x140912100ull, // tpp::ui::utility::GetFocalLengthZoomRatio
                0x140916A90ull, // tpp::ui::utility::IsOutsideScreen
                0x140866910ull, // tpp::ui::hud::CommonDataManager::GetInstance
                0x140914F80ull, // tpp::ui::utility::GetViewport
                0x1420A8068ull, // how far head markers rise per unit of zoom
                0x1421079D4ull, // the screen margin head markers use
                0x14093A480ull, // tpp::ui::utility::GetUiUtility
                {
                    0x140917730ull, // tpp::ui::utility::SetButtonIconTextureByName
                    0x140917AC0ull, // tpp::ui::utility::SetPlatformButtonTexture
                    0x140915DE0ull, // tpp::ui::utility::IsGamePauseMenu
                    0x140915E40ull, // tpp::ui::utility::IsInDemo
                    0x140912330ull, // tpp::ui::utility::GetIconFtexPath
                    0x140915130ull, // tpp::ui::utility::GetWeaponItemNameLangId
                    0x14091BA10ull, // tpp::ui::utility::StopSound
                    0x140913FC0ull, // tpp::ui::utility::GetSoundTypeId
                    0x141DBF830ull, // fox::ui::Animation::Pause
                    0x141DBF390ull, // fox::ui::Animation::ForceLoop
                    0x141DBFE80ull, // fox::ui::Animation::UnsetForceLoop
                    0x141DABC40ull, // fox::ui::WindowFunction::AddChildWindow
                    0x141DAC340ull, // fox::ui::WindowFunction::PostTriggerMessage
                },
            };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
            return {
                0x141DF5A10ull, // fox::ui::WindowManager::UpdateLayouts
                0x141DEF6C0ull, // fox::ui::WindowDaemon::GetWindowManager
                0x141DABD70ull, // fox::ui::WindowFunction::DestroyWindow
                0x141DABC30ull, // fox::ui::WindowFunction::CreateWindow
                0x141DBF440ull, // fox::ui::Animation::GetTimeLength
                0x141DB6870ull, // fox::ui::ModelNodeText::SetFontGroup
                0x14091B7C0ull, // tpp::ui::utility::StartSound
                0x141DCA330ull, // fox::ui::SoundControl::Stop
                0x142C8FAD8ull, // the SoundControl the UI graphs play through
                0x140024240ull, // fox::LwRWLockSection::LockShared
                0x140024440ull, // fox::LwRWLockSection::Unlock
                0x142B64238ull, // g_blockGroupsLock
                0x1429E3CA0ull, // first entry of the block group map
                0x1429E3CB8ull, // block group map nodes
                0x1429E3CC0ull, // block group map values
                0x141DACFC0ull, // fox::ui::LayoutUtility::DisconnectComponent
                0x141DF55C0ull, // fox::ui::WindowManager::DeleteDestroyedWindows
                0x141DA9860ull, // fox::ui::ObjControl::CreateCommon
                0x1400B3510ull, // fox::impl::EntityPtrBase::Set
                0x140912190ull, // tpp::ui::utility::GetGameCameraViewMatrix
                0x140913AE0ull, // tpp::ui::utility::GetProjectionMatrix
                0x140911FE0ull, // tpp::ui::utility::GetFocalLengthZoomRatio
                0x140916970ull, // tpp::ui::utility::IsOutsideScreen
                0x1408669B0ull, // tpp::ui::hud::CommonDataManager::GetInstance
                0x140914E60ull, // tpp::ui::utility::GetViewport
                0x1420A8068ull, // how far head markers rise per unit of zoom
                0x1421079D4ull, // the screen margin head markers use
                0x14093A360ull, // tpp::ui::utility::GetUiUtility
                {
                    0x140917610ull, // tpp::ui::utility::SetButtonIconTextureByName
                    0x1409179A0ull, // tpp::ui::utility::SetPlatformButtonTexture
                    0x140915CC0ull, // tpp::ui::utility::IsGamePauseMenu
                    0x140915D20ull, // tpp::ui::utility::IsInDemo
                    0x140912210ull, // tpp::ui::utility::GetIconFtexPath
                    0x140915010ull, // tpp::ui::utility::GetWeaponItemNameLangId
                    0x14091B8F0ull, // tpp::ui::utility::StopSound
                    0x140913EA0ull, // tpp::ui::utility::GetSoundTypeId
                    0x141DBF890ull, // fox::ui::Animation::Pause
                    0x141DBF3F0ull, // fox::ui::Animation::ForceLoop
                    0x141DBFEE0ull, // fox::ui::Animation::UnsetForceLoop
                    0x141DABB20ull, // fox::ui::WindowFunction::AddChildWindow
                    0x141DAC220ull, // fox::ui::WindowFunction::PostTriggerMessage
                },
            };
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:
            return {
                0x14D964950ull, // fox::ui::WindowManager::UpdateLayouts
                0x141DEF490ull, // fox::ui::WindowDaemon::GetWindowManager
                0x141DAB9F0ull, // fox::ui::WindowFunction::DestroyWindow
                0x141DAB8B0ull, // fox::ui::WindowFunction::CreateWindow
                0x141DBEF00ull, // fox::ui::Animation::GetTimeLength
                0x141DB6240ull, // fox::ui::ModelNodeText::SetFontGroup
                0x14091AFA0ull, // tpp::ui::utility::StartSound
                0x141DC9EF0ull, // fox::ui::SoundControl::Stop
                0x142C8FAD0ull, // the SoundControl the UI graphs play through
                0x140023F80ull, // fox::LwRWLockSection::LockShared
                0x140024180ull, // fox::LwRWLockSection::Unlock
                0x142B64238ull, // g_blockGroupsLock
                0x1429E3CA0ull, // first entry of the block group map
                0x1429E3CB8ull, // block group map nodes
                0x1429E3CC0ull, // block group map values
                0x141DACAD0ull, // fox::ui::LayoutUtility::DisconnectComponent
                0x14D962BA0ull, // fox::ui::WindowManager::DeleteDestroyedWindows
                0x141DA9590ull, // fox::ui::ObjControl::CreateCommon
                0x1400B3120ull, // fox::impl::EntityPtrBase::Set
            };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:
            return {
                0x14DFF9880ull, // fox::ui::WindowManager::UpdateLayouts
                0x141DEF490ull, // fox::ui::WindowDaemon::GetWindowManager
                0x141DAB8A0ull, // fox::ui::WindowFunction::DestroyWindow
                0x141DAB760ull, // fox::ui::WindowFunction::CreateWindow
                0x141DBEF30ull, // fox::ui::Animation::GetTimeLength
                0x141DB62A0ull, // fox::ui::ModelNodeText::SetFontGroup
                0x14091A9D0ull, // tpp::ui::utility::StartSound
                0x141DC9F30ull, // fox::ui::SoundControl::Stop
                0x142C8FAD0ull, // the SoundControl the UI graphs play through
                0x140023F30ull, // fox::LwRWLockSection::LockShared
                0x140024130ull, // fox::LwRWLockSection::Unlock
                0x142B64238ull, // g_blockGroupsLock
                0x1429E3CA0ull, // first entry of the block group map
                0x1429E3CB8ull, // block group map nodes
                0x1429E3CC0ull, // block group map values
                0x141DACAE0ull, // fox::ui::LayoutUtility::DisconnectComponent
                0x14DFF5EA0ull, // fox::ui::WindowManager::DeleteDestroyedWindows
                0x141DA9440ull, // fox::ui::ObjControl::CreateCommon
                0x1400B3250ull, // fox::impl::EntityPtrBase::Set
            };
        default:
            return {};
        }
    }

    // retail added these UixUtility slots to the dev build's vtable (const overloads and a few new methods)
    constexpr std::size_t kRetailOnlySlots[] = {
        0x0E8, 0x178, 0x238, 0x260, 0x278, 0x288, 0x4A8, 0x4B0, 0x4B8, 0x5D8, 0x6D8, 0x6E0, 0x6E8, 0x748, 0x888,
    };
    constexpr std::size_t kDevFindWindow     = 0x590;
    constexpr std::size_t kDevDeleteTextUnit = 0x698;
    constexpr std::size_t kDevCreateTextUnit = 0x690;
    constexpr std::size_t kDevGetOrthogonalProjectionScale = 0x270;
    constexpr std::size_t kDevGetTraversalTranslation      = 0x490;
    constexpr std::size_t kDevSetLineDraw                  = 0x768;
    constexpr std::size_t kDevResetLineDraw                = 0x770;

    using UpdateLayouts_t   = void(__fastcall*)(void* manager, unsigned group);
    using DeleteDestroyed_t = void(__fastcall*)(void* manager);
    using GetWindowManager_t = void* (__fastcall*)();
    using WindowCall_t      = void(__fastcall*)(void* window);
    using CreateWindow_t    = void* (__fastcall*)(void* factory, const void* info, unsigned group, unsigned pauseMask);
    using WindowGetPtr_t    = void* (__fastcall*)(void* window);
    using LockCall_t        = void(__fastcall*)(void* lock);
    using PackageGetFile_t  = void* (__fastcall*)(void* package, std::uint64_t pathId);
    using GetUixUtility_t   = void** (__fastcall*)();
    using ObjCreateCommon_t = void** (__fastcall*)(void** out, void* file, std::uint32_t group, std::uint64_t id, std::uint16_t priority,
                                                   void* layoutInfo, void* parentComponent, void* parentNode, void* parentWindow, std::uint32_t pauseMask);
    using EntityPtrSet_t    = void(__fastcall*)(void** pointer, void* entity);
    using CreateTextUnit_t  = void(__fastcall*)(void** util, void** out, int count);
    using DeleteTextUnit_t  = void(__fastcall*)(void** util, void* units);
    using DisconnectComponent_t = void(__fastcall*)(void* child);
    using GetPtr_t          = void* (__fastcall*)();
    using GetFloat_t        = float(__fastcall*)();
    using IsOutsideScreen_t = bool(__fastcall*)(float* out, const float* position, const void* view, const void* projection, float marginX, float marginY);
    using GetOrthoScale_t   = float(__fastcall*)(void** util, void* model);
    using GetTraversal_t    = float* (__fastcall*)(void** util, float* out, void* component, void* node);
    using SetLineDraw_t     = void(__fastcall*)(void** util, void* node, const void* draw, const void* vertices);
    using ResetLineDraw_t   = void(__fastcall*)(void** util, void* node);
    using AddChildWindow_t  = void(__fastcall*)(void* parent, void* child);

    constexpr std::uintptr_t MANAGER_WINDOWS      = 0x00;
    constexpr std::uintptr_t MANAGER_WINDOW_COUNT = 0x30;
    constexpr std::uintptr_t WINDOW_NAME          = 0x10;
    constexpr std::uintptr_t WINDOW_FLAGS         = 0x18; // bit 0 = destroyed
    constexpr std::uintptr_t WINDOW_GROUP         = 0x38;
    constexpr std::uintptr_t WINDOW_PARENT        = 0x40;
    constexpr std::size_t    WINDOW_VT_GET_ROOT_LAYOUT = 0xB0 / sizeof(void*);

    constexpr std::uintptr_t GROUP_BLOCK_COUNT   = 0x1C;
    constexpr std::uintptr_t GROUP_BLOCKS        = 0x48;
    constexpr std::uintptr_t BLOCK_SIZE          = 0x178;
    constexpr std::uintptr_t BLOCK_STATE         = 0x80; // 2 loaded, 3 active
    constexpr std::uintptr_t BLOCK_PACKAGES      = 0x90;
    constexpr std::uintptr_t BLOCK_PACKAGE_COUNT = 0x150;
    constexpr std::uintptr_t GROUP_NODE_SIZE     = 0x18;
    constexpr std::uintptr_t GROUP_NODE_NEXT     = 0x0C;
    constexpr std::size_t    PACKAGE_VT_GET_FILE = 0x38 / sizeof(void*);

    constexpr std::uintptr_t FILE_PATH_ID  = 0x30;
    constexpr std::uintptr_t FILE_STATE    = 0x40;   // 2 = loaded
    constexpr std::uint32_t  PATH_EXT_UIGB = 0x1AB2; // path id >> 51

    constexpr std::uintptr_t COMPONENT_PARENT      = 0x38; // the game never clears it on delete
    constexpr std::uintptr_t COMPONENT_PARENT_NODE = 0x40;
    constexpr std::uintptr_t LAYOUT_MODEL_COUNT    = 0xC0;
    constexpr std::uintptr_t LAYOUT_MODELS         = 0xC8;
    constexpr std::uintptr_t LAYOUT_CHILD_COUNT    = 0xD0;
    constexpr std::uintptr_t LAYOUT_CHILDREN       = 0xD8;
    constexpr std::uintptr_t COMPONENT_ID          = 0x20; // layouts and models alike
    constexpr std::size_t    WINDOW_VT_GET_ID      = 0xC0 / sizeof(void*);
    constexpr int            kMaxComponents        = 256;
    constexpr int            kMaxLayoutDepth       = 8;
    constexpr std::uintptr_t TEXT_UNITS            = 0x138;

    // every marked or markable game object keeps a tpp::gk::Marker2 record its owner rewrites each frame
    constexpr std::uintptr_t CDM_MARKER_DATA   = 0xF08;   // hud::CommonDataManager -> UiMarkerCommonData
    constexpr std::uintptr_t MARKER_DATA_SYSTEM = 0x19E0; // -> tpp::gk::Marker2System
    constexpr std::uintptr_t MARKER_RECORDS    = 0x88;
    constexpr int            kMarkerRecords    = 0x3B6;
    constexpr std::uintptr_t MARKER_STRIDE     = 0x30;
    constexpr std::uintptr_t MARKER_POSITION   = 0x10;    // the head for soldiers, as the head marker uses it
    constexpr std::uintptr_t MARKER_OBJECT_ID  = 0x1C;
    constexpr std::uintptr_t MARKER_FLAGS      = 0x1F;    // bit 0 = in use, bit 1 = marker on (marked, or enabled by script)

    // the HUD's own 128 marker slots inside UiMarkerCommonData, refilled every frame from the records above
    constexpr int            kUiSlots          = 0x80;
    constexpr std::uintptr_t UI_SLOT_BASE      = 2 * 0x30;
    constexpr std::uintptr_t UI_SLOT_STRIDE    = 0x30;
    constexpr std::uintptr_t UI_SLOT_BITS      = 0x00;    // u16: bit 4 IsMarkerImportant, bit 7 IsMarkerQuestImportant
    constexpr std::uintptr_t UI_SLOT_VIEW      = 0x02;    // bit 0 IsMarkerViewMapIcon, 1 IsMarkerViewWorldIcon, 2 IsMarkerViewMapGoal, 3 IsMarkerViewWorldGoal
    constexpr std::uintptr_t UI_SLOT_RADIUS    = 0x03;    // GetMarkerRadiusLevel
    constexpr std::uintptr_t UI_SLOT_KIND      = 0x08;    // UiMarkerCommonDataImpl::GetMarkerFlag: low byte = marker type, high bits = state
    constexpr std::uintptr_t UI_SLOT_OBJECT_ID = 0x18;
    constexpr std::uintptr_t UI_SLOT_POSITION  = 0x20;    // GetMarkerPos
    constexpr std::uintptr_t UI_RADIUS_METRES  = 0x30;    // GetRadiusMeter: float per radius level
    constexpr int            kRadiusLevels     = 10;

    // TppUiCommand.ShowEnemyRoutePoints: menu::UiCommonDataManager +0x78 holds 5 route groups of 0x108 bytes
    // (+0 u8 shown, +1 u8 point count, +4 float width, +0x14 float x[30], +0x8C float z[30])
    constexpr std::uintptr_t UICDM_MAP_DATA    = 0x78;
    constexpr std::uintptr_t ROUTE_GROUPS      = 2 * 0x108;
    constexpr std::uintptr_t ROUTE_STRIDE      = 0x108;
    constexpr std::uintptr_t ROUTE_COUNT       = 0x01;
    constexpr std::uintptr_t ROUTE_WIDTH       = 0x04;
    constexpr std::uintptr_t ROUTE_X           = 0x14;
    constexpr std::uintptr_t ROUTE_Z           = 0x8C;
    constexpr int            kRouteGroups      = 5;
    constexpr int            kRoutePoints      = 30;
    constexpr std::uintptr_t VIEWPORT_WIDTH    = 0x5D8;
    constexpr std::uintptr_t VIEWPORT_HEIGHT   = 0x5DC;
    constexpr std::uintptr_t VIEWPORT_ASPECT   = 0x5E0;

    constexpr std::uint64_t ID48_MASK = 0xFFFFFFFFFFFFull;
    constexpr int kMaxArray = 4096;
    constexpr int kMaxTextUnits = 256;   // CreateTextUnit cap
    constexpr int kCommandLifeFrames = 120;

    UpdateLayouts_t   g_OrigUpdateLayouts = nullptr;
    DeleteDestroyed_t g_OrigDeleteDestroyed = nullptr;

    UiAddrs g_Addrs{};
    std::atomic<bool> g_Installed{ false };

    template <typename T>
    T Fn(std::uintptr_t address)
    {
        return reinterpret_cast<T>(ResolveGameAddress(address));
    }

    template <typename T>
    T Field(void* base, std::uintptr_t offset)
    {
        return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
    }

    template <typename T>
    T* FieldPtr(void* base, std::uintptr_t offset)
    {
        return reinterpret_cast<T*>(static_cast<char*>(base) + offset);
    }

    std::size_t RetailOffset(std::size_t devOffset)
    {
        std::size_t offset = devOffset;
        for (const std::size_t inserted : kRetailOnlySlots)
            if (inserted <= offset)
                offset += sizeof(void*);
        return offset;
    }

    void* RootLayout(void* window)
    {
        void** vtbl = *reinterpret_cast<void***>(window);
        return reinterpret_cast<WindowGetPtr_t>(vtbl[WINDOW_VT_GET_ROOT_LAYOUT])(window);
    }

    void* TopWindow(void* window)
    {
        for (int i = 0; i < 8; ++i)
        {
            void* parent = Field<void*>(window, WINDOW_PARENT);
            if (!parent)
                break;
            window = parent;
        }
        return window;
    }

    void* Manager()
    {
        auto get = Fn<GetWindowManager_t>(g_Addrs.getWindowManager);
        return get ? get() : nullptr;
    }

    enum class WindowState { Gone, Dying, Alive };

    WindowState StateOfSEH(void* manager, void* window)
    {
        if (!window || !manager)
            return WindowState::Gone;
        __try
        {
            void** windows = Field<void**>(manager, MANAGER_WINDOWS);
            const int count = Field<int>(manager, MANAGER_WINDOW_COUNT);
            for (int i = 0; windows && i < count && i < kMaxArray; ++i)
                if (windows[i] == window)
                    return (Field<std::uint32_t>(window, WINDOW_FLAGS) & 1) ? WindowState::Dying : WindowState::Alive;
            return WindowState::Gone;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return WindowState::Gone; }
    }

    void* FindRegisteredWindowSEH(void* manager, std::uint32_t id)
    {
        if (!manager || !id)
            return nullptr;
        __try
        {
            void** windows = Field<void**>(manager, MANAGER_WINDOWS);
            const int count = Field<int>(manager, MANAGER_WINDOW_COUNT);
            for (int i = 0; windows && i < count && i < kMaxArray; ++i)
                if (windows[i] && !(Field<std::uint32_t>(windows[i], WINDOW_FLAGS) & 1) && Field<std::uint32_t>(windows[i], WINDOW_NAME) == id)
                    return windows[i];
            return nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    int GroupSEH(void* window)
    {
        __try { return Field<int>(TopWindow(window), WINDOW_GROUP); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }

    void CallWindowSEH(WindowCall_t fn, void* window)
    {
        if (!fn || !window)
            return;
        __try { fn(window); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // ---- handles ----

    struct Entry
    {
        Object object;
        bool   dead = false;
    };

    std::recursive_mutex g_Lock;
    std::unordered_map<std::uint32_t, Entry> g_Handles;
    std::unordered_map<std::uint64_t, std::uint32_t> g_HandleOf;   // (ptr, kind) -> handle, so a getter called every frame does not grow the table
    std::uint32_t g_NextHandle = 1;

    std::uint64_t HandleKey(void* ptr, Kind kind)
    {
        return (reinterpret_cast<std::uint64_t>(ptr) << 4) ^ static_cast<std::uint64_t>(kind);
    }

    // a handle of a gone window is dropped: Lua then sees it like any unknown handle, and the table does not grow with
    // every window, layout and node ever fetched
    void KillHandlesOf(const std::unordered_set<void*>& windows)
    {
        for (auto it = g_Handles.begin(); it != g_Handles.end(); )
        {
            const Entry& e = it->second;
            if (e.dead || (e.object.window && windows.count(e.object.window)))
            {
                if (!e.dead)
                    g_HandleOf.erase(HandleKey(e.object.ptr, e.object.kind));
                it = g_Handles.erase(it);
            }
            else
                ++it;
        }
    }

    // ---- queued calls ----

    // a faulting queued call can repeat every frame: the first 10 are logged, then every 1000th
    void NoteQueuedFault()
    {
        static unsigned s_faults = 0;
        ++s_faults;
        if (s_faults <= 10 || s_faults % 1000 == 0)
            LogWarn("V_UixUtility: a queued call faulted and was skipped (%u so far)", s_faults);
    }

    struct Command
    {
        std::uint32_t handle = 0;
        std::function<void(const Object&)> run;
        std::function<void()> plain;
        int age = 0;
    };
    std::vector<Command> g_Commands;
    std::atomic<bool> g_HasCommands{ false };

    struct GuardContext
    {
        const std::function<void(const Object&)>* run;
        const Object* object;
    };

    bool RunSEH(const std::function<void(const Object&)>& run, const Object& object)
    {
        GuardContext context{ &run, &object };
        return Guard([](void* c) {
            auto* g = static_cast<GuardContext*>(c);
            (*g->run)(*g->object);
        }, &context);
    }

    bool RunPlainSEH(const std::function<void()>& run)
    {
        return Guard([](void* c) { (*static_cast<const std::function<void()>*>(c))(); }, const_cast<std::function<void()>*>(&run));
    }

    void RunCommands(void* manager, unsigned group, bool bypass)
    {
        if (g_Commands.empty())
            return;
        if (bypass)
        {
            g_Commands.clear();
            g_HasCommands.store(false, std::memory_order_release);
            return;
        }
        std::vector<Command> keep;
        for (Command& c : g_Commands)
        {
            if (!c.handle)
            {
                if (!RunPlainSEH(c.plain))
                    NoteQueuedFault();
                continue;
            }
            const auto it = g_Handles.find(c.handle);
            if (it == g_Handles.end() || it->second.dead)
                continue;
            const Object& object = it->second.object;
            if (object.window)
            {
                if (StateOfSEH(manager, object.window) != WindowState::Alive)
                    continue;
                if (GroupSEH(object.window) != static_cast<int>(group))
                {
                    if (++c.age < kCommandLifeFrames * 2)
                        keep.push_back(std::move(c));
                    continue;
                }
            }
            if (!RunSEH(c.run, object))
                NoteQueuedFault();
        }
        g_Commands.swap(keep);
        g_HasCommands.store(!g_Commands.empty(), std::memory_order_release);
    }

    // ---- links ----

    struct Link
    {
        void* child;
        void* childWindow;
        void* parentWindow;
    };
    std::vector<Link> g_Links;

    void DisconnectSEH(void* child)
    {
        auto disconnect = Fn<DisconnectComponent_t>(g_Addrs.disconnectComponent);
        if (!disconnect || !child)
            return;
        __try { disconnect(child); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // the parent is freed already: only forget it on the child's side
    void ForgetParentSEH(void* child)
    {
        __try
        {
            *FieldPtr<void*>(child, COMPONENT_PARENT) = nullptr;
            *FieldPtr<void*>(child, COMPONENT_PARENT_NODE) = nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // runs before the game deletes destroyed windows: a link into a window that is about to go would dangle
    void UnlinkDyingWindows(void* manager)
    {
        for (auto it = g_Links.begin(); it != g_Links.end();)
        {
            const WindowState childState = StateOfSEH(manager, it->childWindow);
            const WindowState parentState = StateOfSEH(manager, it->parentWindow);
            if (childState == WindowState::Alive && parentState == WindowState::Alive)
            {
                ++it;
                continue;
            }
            if (childState != WindowState::Gone)
            {
                if (parentState == WindowState::Gone)
                    ForgetParentSEH(it->child);
                else
                    DisconnectSEH(it->child);
            }
            it = g_Links.erase(it);
        }
    }

    // ---- markers and screen positions (GetMarkers, GetMarkerPosition, GetNodeScreenPosition) ----

    bool IsScreenPorted()
    {
        return g_Addrs.cameraViewMatrix && g_Addrs.projectionMatrix && g_Addrs.focalLengthZoomRatio && g_Addrs.isOutsideScreen
            && g_Addrs.hudCommonDataManager && g_Addrs.getViewport && g_Addrs.zoomRaise && g_Addrs.screenMargin;
    }

    struct alignas(16) Vec4 { float v[4]; };

    // what a screen position needs: the camera, the screen and the marker records
    struct ScreenFrame
    {
        const void* view = nullptr;
        const void* projection = nullptr;
        float  margin = 0.0f;
        float  raise = 0.0f;       // the zoom lift the head markers get
        float  heightScale = 0.0f; // HUD y units per HUD x unit
        char*  records = nullptr;
    };

    char* MarkerDataSEH()
    {
        __try
        {
            void* manager = Fn<GetPtr_t>(g_Addrs.hudCommonDataManager)();
            return manager ? Field<char*>(manager, CDM_MARKER_DATA) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    char* MarkerRecordsSEH()
    {
        __try
        {
            char* markerData = MarkerDataSEH();
            void* system = markerData ? Field<void*>(markerData, MARKER_DATA_SYSTEM) : nullptr;
            return system ? Field<char*>(system, MARKER_RECORDS) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    int CollectEnemyRoutesSEH(UixUtilityControl::EnemyRoute* out, int capacity)
    {
        int n = 0;
        __try
        {
            using GetInstance_t = void* (*)();
            const auto getInstance = reinterpret_cast<GetInstance_t>(ResolveGameAddress(gAddr.Ui_UiCommonDataManagerGetInstance));
            void* manager = getInstance ? getInstance() : nullptr;
            const char* data = manager ? Field<const char*>(manager, UICDM_MAP_DATA) : nullptr;
            if (!data)
                return -1;
            for (int g = 0; g < kRouteGroups && n < capacity; ++g)
            {
                const char* r = data + ROUTE_GROUPS + g * ROUTE_STRIDE;
                if (*reinterpret_cast<const std::uint8_t*>(r) == 0)
                    continue;
                int count = *reinterpret_cast<const std::uint8_t*>(r + ROUTE_COUNT);
                if (count > kRoutePoints)
                    count = kRoutePoints;
                UixUtilityControl::EnemyRoute& e = out[n++];
                e.group = g;
                e.width = *reinterpret_cast<const float*>(r + ROUTE_WIDTH);
                e.count = count;
                for (int i = 0; i < count; ++i)
                {
                    e.x[i] = *reinterpret_cast<const float*>(r + ROUTE_X + i * 4);
                    e.z[i] = *reinterpret_cast<const float*>(r + ROUTE_Z + i * 4);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return n;
    }

    void AddMarkerFlagsSEH(UixUtilityControl::MarkerEntry* out, int count)
    {
        __try
        {
            const char* data = MarkerDataSEH();
            if (!data)
                return;
            for (int slot = 0; slot < kUiSlots; ++slot)
            {
                const char* s = data + UI_SLOT_BASE + slot * UI_SLOT_STRIDE;
                const std::uint16_t id = *reinterpret_cast<const std::uint16_t*>(s + UI_SLOT_OBJECT_ID);
                if (id == 0xFFFF)
                    continue;
                const std::uint32_t flag = *reinterpret_cast<const std::uint32_t*>(s + UI_SLOT_KIND);
                const std::uint16_t bits = *reinterpret_cast<const std::uint16_t*>(s + UI_SLOT_BITS);
                const std::uint8_t view = *reinterpret_cast<const std::uint8_t*>(s + UI_SLOT_VIEW);
                const std::uint8_t level = *reinterpret_cast<const std::uint8_t*>(s + UI_SLOT_RADIUS);
                const float* position = reinterpret_cast<const float*>(s + UI_SLOT_POSITION);
                for (int i = 0; i < count; ++i)
                    if (out[i].gameObjectId == id && !out[i].hasMarkerFlag)
                    {
                        out[i].hasMarkerFlag = true;
                        out[i].markerFlag = flag;
                        out[i].important = ((bits >> 4) & 1) != 0;
                        out[i].questImportant = ((bits >> 7) & 1) != 0;
                        out[i].viewWorldIcon = ((view >> 1) & 1) != 0;
                        out[i].viewMapIcon = (view & 1) != 0;
                        out[i].viewWorldGoal = ((view >> 3) & 1) != 0;
                        out[i].viewMapGoal = ((view >> 2) & 1) != 0;
                        out[i].radiusLevel = level;
                        out[i].radius = level < kRadiusLevels ? *reinterpret_cast<const float*>(data + UI_RADIUS_METRES + level * 4) : 0.0f;
                        out[i].mapX = position[0];
                        out[i].mapY = position[1];
                        out[i].mapZ = position[2];
                    }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    bool ReadScreenFrameSEH(ScreenFrame& f, bool needRecords)
    {
        __try
        {
            f.records = MarkerRecordsSEH();
            void* viewport = Fn<GetPtr_t>(g_Addrs.getViewport)();
            if ((needRecords && !f.records) || !viewport)
                return false;
            const int width = Field<int>(viewport, VIEWPORT_WIDTH);
            const int height = Field<int>(viewport, VIEWPORT_HEIGHT);
            const float aspect = Field<float>(viewport, VIEWPORT_ASPECT);
            if (width <= 0 || height <= 0 || aspect <= 0.0f)
                return false;
            f.heightScale = static_cast<float>(height) / (static_cast<float>(width) * aspect);
            f.view = Fn<GetPtr_t>(g_Addrs.cameraViewMatrix)();
            f.projection = Fn<GetPtr_t>(g_Addrs.projectionMatrix)();
            f.raise = Fn<GetFloat_t>(g_Addrs.focalLengthZoomRatio)() * *static_cast<const float*>(ResolveGameAddress(g_Addrs.zoomRaise));
            f.margin = *static_cast<const float*>(ResolveGameAddress(g_Addrs.screenMargin));
            return f.view && f.projection;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // the record the object's owner writes its marker position into; -1 when it has none
    bool RecordHolds(const char* records, int i, std::uint16_t objectId)
    {
        const char* r = records + i * MARKER_STRIDE;
        return (*reinterpret_cast<const std::uint8_t*>(r + MARKER_FLAGS) & 1)
            && *reinterpret_cast<const std::uint16_t*>(r + MARKER_OBJECT_ID) == objectId;
    }

    int FindMarkerRecordSEH(const char* records, std::uint16_t objectId, int hint)
    {
        __try
        {
            if (hint >= 0 && hint < kMarkerRecords && RecordHolds(records, hint, objectId))
                return hint;
            for (int i = 0; i < kMarkerRecords; ++i)
                if (RecordHolds(records, i, objectId))
                    return i;
            return -1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }

    int CollectMarkersSEH(const char* records, UixUtilityControl::MarkerEntry* out, int capacity)
    {
        int n = 0;
        __try
        {
            for (int i = 0; i < kMarkerRecords && n < capacity; ++i)
            {
                const char* r = records + i * MARKER_STRIDE;
                const std::uint8_t flags = *reinterpret_cast<const std::uint8_t*>(r + MARKER_FLAGS);
                if ((flags & 3) != 3)
                    continue;
                UixUtilityControl::MarkerEntry& e = out[n++];
                e.gameObjectId = *reinterpret_cast<const std::uint16_t*>(r + MARKER_OBJECT_ID);
                e.x = *reinterpret_cast<const float*>(r + MARKER_POSITION);
                e.y = *reinterpret_cast<const float*>(r + MARKER_POSITION + 4);
                e.z = *reinterpret_cast<const float*>(r + MARKER_POSITION + 8);
                e.hasMarkerFlag = false;
                e.markerFlag = 0;
                e.dying = false;
                e.important = false;
                e.questImportant = false;
                e.viewWorldIcon = false;
                e.viewMapIcon = false;
                e.viewWorldGoal = false;
                e.viewMapGoal = false;
                e.radiusLevel = 0;
                e.radius = 0.0f;
                e.mapX = e.mapY = e.mapZ = 0.0f;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return n;
    }

    void* FirstModelSEH(void* layout)
    {
        __try
        {
            void** models = Field<void**>(layout, LAYOUT_MODELS);
            return models && Field<int>(layout, LAYOUT_MODEL_COUNT) > 0 ? models[0] : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    // tpp::ui::utility::IsOutsideScreen: -1..1 screen position, x right and y up
    bool ProjectSEH(const ScreenFrame& f, const float* world, float* screen, bool& onScreen)
    {
        __try
        {
            Vec4 position{ { world[0], world[1], world[2], 0.0f } };
            onScreen = !Fn<IsOutsideScreen_t>(g_Addrs.isOutsideScreen)(screen, position.v, f.view, f.projection, f.margin, f.margin);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // HUD units per screen half-width for the component's model camera
    float ComponentScaleSEH(const Object& o)
    {
        __try
        {
            void* model = o.kind == Kind::Model ? o.ptr : FirstModelSEH(o.ptr);
            return model ? reinterpret_cast<GetOrthoScale_t>(Method(kDevGetOrthogonalProjectionScale))(Utility(), model) : 0.0f;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0.0f; }
    }

    // into the component's own units, as HeadMarkMarkerEvCall::SetMarkerPosRot does; lift = the head markers' zoom raise
    bool ToComponentUnits(const ScreenFrame& f, const Object& o, float* screen, bool lift)
    {
        const float scale = ComponentScaleSEH(o);
        if (scale <= 0.0f)
            return false;
        screen[0] *= scale;
        screen[1] = screen[1] * scale * f.heightScale - (lift ? f.raise : 0.0f);
        return true;
    }

    bool MarkerWorldSEH(const char* record, float* world, bool& enabled)
    {
        __try
        {
            std::memcpy(world, record + MARKER_POSITION, sizeof(float) * 3);
            enabled = (*reinterpret_cast<const std::uint8_t*>(record + MARKER_FLAGS) & 2) != 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // fox::ui::Model::GetComponentInfo: the node's translation after the layouts, model and parent nodes above it
    bool NodeHudSEH(void* model, void* node, float* hud)
    {
        __try
        {
            Vec4 out{ { 0.0f, 0.0f, 0.0f, 0.0f } };
            reinterpret_cast<GetTraversal_t>(Method(kDevGetTraversalTranslation))(Utility(), out.v, model, node);
            hud[0] = out.v[0];
            hud[1] = out.v[1];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // ---- line draws ----

    struct LineDrawRecord   // fox::ui::LineDraw
    {
        std::uint32_t        vertexCount;
        std::uint32_t        pad;
        const std::uint16_t* indices;
    };
    static_assert(sizeof(LineDrawRecord) == 0x10, "LineDraw is 0x10 bytes in the engine");

    struct LineBuffers
    {
        LineDrawRecord draw{};
        std::vector<std::uint16_t> indices;
        std::unique_ptr<Vec4[]> vertices;
        void* window = nullptr;
    };
    std::unordered_map<void*, std::shared_ptr<LineBuffers>> g_LineBuffers;   // line node -> what it points at

    // the renderer may still read a replaced buffer this frame, so it waits two passes before it is freed
    struct RetiredLines
    {
        std::shared_ptr<LineBuffers> buffers;
        int passes = 0;
    };
    std::vector<RetiredLines> g_RetiredLines;

    void RetireLineBuffersOf(void* node)
    {
        const auto it = g_LineBuffers.find(node);
        if (it == g_LineBuffers.end())
            return;
        g_RetiredLines.push_back({ it->second, 0 });
        g_LineBuffers.erase(it);
    }

    void AgeRetiredLines(const std::unordered_set<void*>& dyingWindows)
    {
        for (auto it = g_LineBuffers.begin(); it != g_LineBuffers.end();)
        {
            if (!dyingWindows.count(it->second->window))
            {
                ++it;
                continue;
            }
            g_RetiredLines.push_back({ it->second, 0 });
            it = g_LineBuffers.erase(it);
        }
        for (auto it = g_RetiredLines.begin(); it != g_RetiredLines.end();)
            it = ++it->passes >= 2 ? g_RetiredLines.erase(it) : it + 1;
    }

    // ---- button icons the game refreshes when pad / keyboard changes ----

    constexpr std::size_t kUiEraseButtonMeshParam = 0x8F0;
    constexpr std::size_t kUiPauseDrawScene       = 0x228;

    struct ButtonMesh
    {
        void* window = nullptr;
        std::uint32_t registId = 0;
    };
    std::unordered_map<void*, ButtonMesh> g_ButtonMeshes;   // node -> its window
    bool g_DrawScenePaused = false;

    void EraseButtonMeshSEH(void* node)
    {
        __try
        {
            void** util = UixUtilityControl::UiUtility();
            if (util)
                reinterpret_cast<bool(__fastcall*)(void**, void*)>(UixUtilityControl::UiUtilityMethod(kUiEraseButtonMeshParam))(util, node);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void EraseButtonMeshesOf(const std::unordered_set<void*>& dyingWindows)
    {
        for (auto it = g_ButtonMeshes.begin(); it != g_ButtonMeshes.end();)
        {
            if (!dyingWindows.count(it->second.window))
            {
                ++it;
                continue;
            }
            EraseButtonMeshSEH(it->first);
            it = g_ButtonMeshes.erase(it);
        }
    }

    // ---- text units ----

    struct TextUnitInfo
    {
        std::shared_ptr<std::string> text;
        std::vector<std::pair<void*, void*>> nodes;   // node, its window
        std::shared_ptr<const void> queued = std::make_shared<int>(0);   // one extra owner per queued call that still uses the units
        int count = 0;
        bool deleteWanted = false;
    };
    std::unordered_map<void*, TextUnitInfo> g_TextUnits;

    bool NodeUsesUnitsSEH(void* node, void* units)
    {
        __try { return Field<void*>(node, TEXT_UNITS) == units; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void DeleteUnitsSEH(void* units)
    {
        auto del = reinterpret_cast<DeleteTextUnit_t>(Method(kDevDeleteTextUnit));
        void** util = Utility();
        if (!del || !util)
            return;
        __try { del(util, units); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // units are freed only once no node of a live window still shows them
    void DeleteUnusedTextUnits(void* manager)
    {
        for (auto it = g_TextUnits.begin(); it != g_TextUnits.end();)
        {
            TextUnitInfo& info = it->second;
            if (!info.deleteWanted || info.queued.use_count() > 1)
            {
                ++it;
                continue;
            }
            bool inUse = false;
            for (const auto& use : info.nodes)
                if (StateOfSEH(manager, use.second) != WindowState::Gone && NodeUsesUnitsSEH(use.first, it->first))
                    inUse = true;
            if (inUse)
            {
                ++it;
                continue;
            }
            DeleteUnitsSEH(it->first);
            it = g_TextUnits.erase(it);
        }
    }

    // ---- windows made by V_UixUtility ----

    struct CreateCommand
    {
        std::uint64_t name = 0;
        std::vector<LayoutRecord> layouts;
        std::string graph;
        std::uint32_t group = 0;
        std::uint32_t pauseMask = 0;
        std::uint16_t priority = 0;
    };
    std::vector<CreateCommand> g_LayoutCreates;   // run in the layout pass
    std::vector<CreateCommand> g_GraphCreates;    // run where the UI daemon is idle
    std::unordered_map<void*, void*> g_CreatedWindows;   // window -> block its .uilb came from
    std::unordered_map<void*, void*> g_DestroyingWindows; // same, for ours whose DestroyWindow is queued but not run yet

    struct GraphWindow
    {
        std::unique_ptr<void*> control;   // the ObjControl; releasing it destroys its window
        void* block = nullptr;
        std::uint32_t id = 0;
        bool  release = false;
    };
    std::vector<GraphWindow> g_GraphWindows;
    std::atomic<bool> g_HasDaemonWork{ false };

    void RefreshDaemonWork()
    {
        g_HasDaemonWork.store(!g_GraphCreates.empty() || !g_GraphWindows.empty() || !g_Links.empty() || !g_TextUnits.empty() || !g_Handles.empty()
            || !g_LineBuffers.empty() || !g_RetiredLines.empty(), std::memory_order_release);
    }

    void __fastcall CallbackDtor(void*, unsigned) {}
    void __fastcall CallbackSetup(void*, const void*) {}
    void __fastcall CallbackNoop(void*) {}

    void* g_CallbackVtbl[] = {
        reinterpret_cast<void*>(&CallbackDtor),
        reinterpret_cast<void*>(&CallbackSetup),
        reinterpret_cast<void*>(&CallbackNoop), // Init
        reinterpret_cast<void*>(&CallbackNoop), // Start
        reinterpret_cast<void*>(&CallbackNoop), // Update
        reinterpret_cast<void*>(&CallbackNoop), // Stop
        reinterpret_cast<void*>(&CallbackNoop), // Finalize
    };
    struct NullCallback { void** vtbl; void* quark; };
    NullCallback g_NullCallback{ g_CallbackVtbl, nullptr };

    void* __fastcall FactoryCreate(void*) { return &g_NullCallback; }
    void* g_FactoryVtbl[] = {
        reinterpret_cast<void*>(&CallbackDtor),
        reinterpret_cast<void*>(&FactoryCreate),
    };
    struct NullFactory { void** vtbl; };
    NullFactory g_NullFactory{ g_FactoryVtbl };

#pragma pack(push, 1)
    struct EngineLayoutRecord
    {
        const char*   path;
        std::uint64_t id;
        std::uint64_t drawPriority;
        std::int32_t  fontType;
        std::uint8_t  flag;
        std::uint8_t  pad[3];
    };
    struct EngineWindowInfo
    {
        std::uint64_t             name;
        void*                     package;
        const EngineLayoutRecord* layouts;
        void*                     children;
        void*                     replacements;
        std::uint16_t             priority;
        std::uint8_t              layoutCount;
        std::uint8_t              childCount;
        std::uint8_t              replacementCount;
        std::uint8_t              pad[3];
    };
#pragma pack(pop)
    static_assert(sizeof(EngineLayoutRecord) == 0x20, "LayoutRecord is 0x20 bytes in the engine");
    static_assert(sizeof(EngineWindowInfo) == 0x30, "WindowInfo is 0x30 bytes in the engine");

    void* PackageHoldingRaw(std::uint64_t pathId, void*& outBlock, void*& outFile)
    {
        const std::uint32_t* head = reinterpret_cast<const std::uint32_t*>(ResolveGameAddress(g_Addrs.blockGroupHead));
        char** nodes = reinterpret_cast<char**>(ResolveGameAddress(g_Addrs.blockGroupNodes));
        void*** values = reinterpret_cast<void***>(ResolveGameAddress(g_Addrs.blockGroupValues));
        int guard = 0;
        for (std::uint32_t i = *head; i != 0xFFFFFFFFu && guard < 4096; i = *reinterpret_cast<std::uint32_t*>(*nodes + i * GROUP_NODE_SIZE + GROUP_NODE_NEXT), ++guard)
        {
            char* group = static_cast<char*>((*values)[i]);
            if (!group)
                continue;
            const std::uint32_t blockCount = Field<std::uint32_t>(group, GROUP_BLOCK_COUNT);
            char* blocks = Field<char*>(group, GROUP_BLOCKS);
            for (std::uint32_t b = 0; blocks && b < blockCount && b < 256; ++b)
            {
                char* block = blocks + b * BLOCK_SIZE;
                const int state = Field<int>(block, BLOCK_STATE);
                if (state != 2 && state != 3)
                    continue;
                void** packages = Field<void**>(block, BLOCK_PACKAGES);
                const std::uint32_t packageCount = Field<std::uint32_t>(block, BLOCK_PACKAGE_COUNT);
                for (std::uint32_t k = 0; packages && k < packageCount && k < 256; ++k)
                {
                    void* package = packages[k];
                    if (!package)
                        continue;
                    void** vtbl = *reinterpret_cast<void***>(package);
                    if (void* file = reinterpret_cast<PackageGetFile_t>(vtbl[PACKAGE_VT_GET_FILE])(package, pathId))
                    {
                        outBlock = block;
                        outFile = file;
                        return package;
                    }
                }
            }
        }
        return nullptr;
    }

    void* PackageHoldingSEH(std::uint64_t pathId, void*& outBlock, void*& outFile)
    {
        __try { return PackageHoldingRaw(pathId, outBlock, outFile); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    // the package whose block holds the file, the kind of package vanilla HUD code passes when it creates a window
    void* PackageHolding(std::uint64_t pathId, void*& outBlock, void*& outFile)
    {
        auto lockShared = Fn<LockCall_t>(g_Addrs.lockShared);
        auto unlock = Fn<LockCall_t>(g_Addrs.unlock);
        void* lock = ResolveGameAddress(g_Addrs.blockGroupsLock);
        if (!lockShared || !unlock || !lock || !pathId)
            return nullptr;
        lockShared(lock);
        void* package = PackageHoldingSEH(pathId, outBlock, outFile);
        unlock(lock);
        return package;
    }

    bool BlockStillLoadedSEH(void* block)
    {
        __try
        {
            const int state = Field<int>(block, BLOCK_STATE);
            return state == 2 || state == 3;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    int LayoutCountSEH(void* window)
    {
        __try
        {
            void* root = RootLayout(window);
            return root ? Field<int>(root, LAYOUT_CHILD_COUNT) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }

    void* CreateWindowSEH(CreateWindow_t create, const EngineWindowInfo* info, std::uint32_t group, std::uint32_t pauseMask)
    {
        __try { return create(&g_NullFactory, info, group, pauseMask); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    void* OurWindowNamed(void* manager, std::uint32_t id)
    {
        void* window = FindRegisteredWindowSEH(manager, id);
        return window && (g_CreatedWindows.count(window) || g_DestroyingWindows.count(window)) ? window : nullptr;
    }

    bool IdTakenByGame(void* manager, std::uint32_t id)
    {
        void* window = FindRegisteredWindowSEH(manager, id);
        if (!window || g_CreatedWindows.count(window) || g_DestroyingWindows.count(window))
            return false;
        for (const GraphWindow& g : g_GraphWindows)
            if (g.id == id)
                return false;
        return true;
    }

    void ReleaseGraphSEH(void** slot)
    {
        auto set = Fn<EntityPtrSet_t>(g_Addrs.entityPtrSet);
        if (!set || !slot || !*slot)
            return;
        __try { set(slot, nullptr); }
        __except (EXCEPTION_EXECUTE_HANDLER) { *slot = nullptr; }
    }

    void ReleaseGraphNamed(std::uint32_t id)
    {
        for (GraphWindow& g : g_GraphWindows)
            if (g.id == id)
                g.release = true;
    }

    void RunLayoutCreate(void* manager, const CreateCommand& c)
    {
        auto create = Fn<CreateWindow_t>(g_Addrs.createWindow);
        if (!create || c.layouts.empty())
            return;
        const std::uint32_t id = static_cast<std::uint32_t>(c.name);
        if (IdTakenByGame(manager, id))
        {
            LogWarnOnce("V_UixUtility.CreateWindow: the game already has a window 0x%08X, pick another name", id);
            return;
        }
        if (void* old = OurWindowNamed(manager, id))
        {
            CallWindowSEH(Fn<WindowCall_t>(g_Addrs.destroyWindow), old);
            g_CreatedWindows.erase(old);
            g_DestroyingWindows.erase(old);
        }
        ReleaseGraphNamed(id);

        void* block = nullptr;
        void* file = nullptr;
        void* package = PackageHolding(FoxHashes::PathCode64Ext(c.layouts[0].path), block, file);
        if (!package)
        {
            LogWarnOnce("V_UixUtility.CreateWindow: %s is not in a loaded pack, window not created", c.layouts[0].path.c_str());
            return;
        }

        std::vector<EngineLayoutRecord> records(c.layouts.size());
        for (std::size_t i = 0; i < c.layouts.size(); ++i)
        {
            records[i].path = c.layouts[i].path.c_str();
            records[i].id = c.layouts[i].id;
            records[i].drawPriority = c.layouts[i].drawPriority;
            records[i].fontType = c.layouts[i].fontType;
            records[i].flag = 1;
        }
        EngineWindowInfo info{};
        info.name = c.name;
        info.priority = c.priority;
        info.package = package;
        info.layouts = records.data();
        info.layoutCount = static_cast<std::uint8_t>(records.size());
        void* window = CreateWindowSEH(create, &info, c.group, c.pauseMask);
        if (!window)
        {
            LogWarnOnce("V_UixUtility.CreateWindow: window 0x%08X was not created", id);
            return;
        }
        g_CreatedWindows[window] = block;
        if (LayoutCountSEH(window) == 0)
            LogWarnOnce("V_UixUtility.CreateWindow: window 0x%08X is empty, %s did not load", id, c.layouts[0].path.c_str());
        RefreshDaemonWork();
    }

    bool FileIsLoadedGraphSEH(void* file)
    {
        __try
        {
            return Field<std::uint8_t>(file, FILE_STATE) == 2
                && ((Field<std::uint64_t>(file, FILE_PATH_ID) >> 51) & 0x1FFF) == PATH_EXT_UIGB;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void* CreateGraphSEH(ObjCreateCommon_t create, void** slot, void* file, const CreateCommand& c)
    {
        __try
        {
            create(slot, file, c.group, c.name, c.priority, nullptr, nullptr, nullptr, nullptr, c.pauseMask);
            return *slot;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    void RunGraphCreate(void* manager, const CreateCommand& c)
    {
        auto create = Fn<ObjCreateCommon_t>(g_Addrs.objCreateCommon);
        if (!create || !g_Addrs.entityPtrSet)
            return;
        const std::uint32_t id = static_cast<std::uint32_t>(c.name);
        if (IdTakenByGame(manager, id))
        {
            LogWarnOnce("V_UixUtility.CreateWindow: the game already has a window 0x%08X, pick another name", id);
            return;
        }
        for (auto it = g_GraphWindows.begin(); it != g_GraphWindows.end();)
        {
            if (it->id != id)
            {
                ++it;
                continue;
            }
            ReleaseGraphSEH(it->control.get());
            it = g_GraphWindows.erase(it);
        }
        if (void* old = OurWindowNamed(manager, id))
        {
            CallWindowSEH(Fn<WindowCall_t>(g_Addrs.destroyWindow), old);
            g_CreatedWindows.erase(old);
            g_DestroyingWindows.erase(old);
        }

        void* block = nullptr;
        void* file = nullptr;
        if (!PackageHolding(FoxHashes::PathCode64Ext(c.graph), block, file) || !file)
        {
            LogWarnOnce("V_UixUtility.CreateWindow: %s is not in a loaded pack, window not created", c.graph.c_str());
            return;
        }
        if (!FileIsLoadedGraphSEH(file))
        {
            LogWarnOnce("V_UixUtility.CreateWindow: %s is not a loaded .uigb, window not created", c.graph.c_str());
            return;
        }
        GraphWindow g;
        g.control = std::make_unique<void*>(nullptr);
        g.block = block;
        g.id = id;
        if (!CreateGraphSEH(create, g.control.get(), file, c))
        {
            LogWarnOnce("V_UixUtility.CreateWindow: window 0x%08X was not created from %s", id, c.graph.c_str());
            return;
        }
        g_GraphWindows.push_back(std::move(g));
    }

    // a window made from a pack points into the pack's memory, so it goes before the pack does
    void DestroyWindowsOfUnloadedBlocks()
    {
        for (auto it = g_CreatedWindows.begin(); it != g_CreatedWindows.end();)
        {
            if (it->second && BlockStillLoadedSEH(it->second))
            {
                ++it;
                continue;
            }
            CallWindowSEH(Fn<WindowCall_t>(g_Addrs.destroyWindow), it->first);
            it = g_CreatedWindows.erase(it);
        }
        for (auto it = g_DestroyingWindows.begin(); it != g_DestroyingWindows.end();)
        {
            if (it->second && BlockStillLoadedSEH(it->second))
            {
                ++it;
                continue;
            }
            CallWindowSEH(Fn<WindowCall_t>(g_Addrs.destroyWindow), it->first);
            it = g_DestroyingWindows.erase(it);
        }
    }

    // runs where the UI daemon is idle: ObjControls join and leave the daemon's update list here
    void RunGraphWork(void* manager, bool bypass)
    {
        std::vector<CreateCommand> creates;
        creates.swap(g_GraphCreates);
        for (auto it = g_GraphWindows.begin(); it != g_GraphWindows.end();)
        {
            if (!bypass && !it->release && it->block && BlockStillLoadedSEH(it->block))
            {
                ++it;
                continue;
            }
            ReleaseGraphSEH(it->control.get());
            it = g_GraphWindows.erase(it);
        }
        if (!bypass)
            for (const CreateCommand& c : creates)
                RunGraphCreate(manager, c);
    }

    int DyingWindowsSEH(void* manager, void** out, int max)
    {
        int n = 0;
        __try
        {
            void** windows = Field<void**>(manager, MANAGER_WINDOWS);
            const int count = Field<int>(manager, MANAGER_WINDOW_COUNT);
            for (int i = 0; windows && i < count && i < kMaxArray && n < max; ++i)
                if (windows[i] && (Field<std::uint32_t>(windows[i], WINDOW_FLAGS) & 1))
                    out[n++] = windows[i];
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return n;
    }

    void CollectDyingWindows(void* manager, std::unordered_set<void*>& out)
    {
        static std::vector<void*> s_dying(kMaxArray);   // under g_Lock: one caller at a time
        const int n = DyingWindowsSEH(manager, s_dying.data(), static_cast<int>(s_dying.size()));
        out.insert(s_dying.begin(), s_dying.begin() + n);
    }

    // ---- GetWindows: read-only walk of the live windows ----

    int LiveWindowsSEH(void* manager, void** out, int max)
    {
        int n = 0;
        __try
        {
            void** windows = Field<void**>(manager, MANAGER_WINDOWS);
            const int count = Field<int>(manager, MANAGER_WINDOW_COUNT);
            for (int i = 0; windows && i < count && i < kMaxArray && n < max; ++i)
                if (windows[i] && !(Field<std::uint32_t>(windows[i], WINDOW_FLAGS) & 1))
                    out[n++] = windows[i];
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return n;
    }

    // the id the game's own FindWindow compares
    bool WindowFieldsSEH(void* window, std::uint32_t& id, int& group, void*& parent, void*& root)
    {
        __try
        {
            void** vtbl = *reinterpret_cast<void***>(window);
            id = reinterpret_cast<std::uint32_t(__fastcall*)(void*)>(vtbl[WINDOW_VT_GET_ID])(window);
            group = Field<int>(window, WINDOW_GROUP);
            parent = Field<void*>(window, WINDOW_PARENT);
            root = RootLayout(window);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool LayoutFieldsSEH(void* layout, std::uint32_t& id, void** models, int& modelCount, void** children, int& childCount)
    {
        modelCount = 0;
        childCount = 0;
        __try
        {
            id = Field<std::uint32_t>(layout, COMPONENT_ID);
            void** modelArray = Field<void**>(layout, LAYOUT_MODELS);
            const int modelTotal = Field<int>(layout, LAYOUT_MODEL_COUNT);
            for (int i = 0; modelArray && i < modelTotal && i < kMaxComponents; ++i)
                if (modelArray[i])
                    models[modelCount++] = modelArray[i];
            void** childArray = Field<void**>(layout, LAYOUT_CHILDREN);
            const int childTotal = Field<int>(layout, LAYOUT_CHILD_COUNT);
            for (int i = 0; childArray && i < childTotal && i < kMaxComponents; ++i)
                if (childArray[i])
                    children[childCount++] = childArray[i];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    std::uint32_t ComponentIdSEH(void* component)
    {
        __try { return Field<std::uint32_t>(component, COMPONENT_ID); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    // the layout, then every layout under it, in one flat list
    void ReadLayouts(void* layout, std::uint32_t parentId, int depth, std::vector<LayoutEntry>& out)
    {
        LayoutEntry entry;
        entry.layout = layout;
        entry.parentId = parentId;
        entry.depth = depth;
        std::vector<void*> models(kMaxComponents);
        std::vector<void*> children(kMaxComponents);
        int modelCount = 0;
        int childCount = 0;
        if (!LayoutFieldsSEH(layout, entry.id, models.data(), modelCount, children.data(), childCount))
            return;
        for (int i = 0; i < modelCount; ++i)
            entry.models.push_back({ models[i], ComponentIdSEH(models[i]) });
        const std::uint32_t id = entry.id;
        out.push_back(std::move(entry));
        if (depth >= kMaxLayoutDepth)
            return;
        for (int i = 0; i < childCount; ++i)
            ReadLayouts(children[i], id, depth + 1, out);
    }

    // ---- resident UI packs ----------------------------------------------------------------------------
    // The game keeps its own resident UI pack in tpp::ui::ResidentBlockController (UiCommonDataManager +0x840, made by
    // TppUiBootInit.lua). A mod pack gets a block group of its own, built the same way that controller builds its group
    // (FUN_140929B40 / ResidentBlockController::LoadBlock / ::Update), and is never unloaded.

    using BlockMemoryDescCtor_t = void* (__fastcall*)(void* desc);
    using SharedStringMake_t    = void* (__fastcall*)(void* out, const char* text);
    using SharedStringCopy_t    = void* (__fastcall*)(void* dst, const void* src);
    using SharedStringDtor_t    = void  (__fastcall*)(void* str);
    using BlockGroupCreate_t    = void* (__fastcall*)(void* desc);
    using SharedPtrSet_t        = void  (__fastcall*)(void** slot, void* object);
    using BlockLoad_t           = int*  (__fastcall*)(void* block, int* error, const std::uint64_t* pathIds, std::uint32_t count);
    using BlockActivate_t       = int*  (__fastcall*)(void* block, int* error);
    using KernelStringCtor_t    = void* (__fastcall*)(void* out, const char* text);
    using PathInitWithString_t  = void  (__fastcall*)(void* path, const void* kernelString);
    using StdFree_t             = void  (__fastcall*)(void* memory, std::uint64_t unused);

    enum ResidentFnIndex { RF_DESC_CTOR, RF_STRING_MAKE, RF_STRING_COPY, RF_STRING_DTOR, RF_GROUP_CREATE, RF_SHARED_SET, RF_BLOCK_LOAD, RF_BLOCK_ACTIVATE,
                           RF_KSTRING_CTOR, RF_PATH_INIT, RF_STD_FREE, RF_COUNT };

    struct ResidentFn
    {
        std::uintptr_t address;
        std::uint8_t   prologue[18];
        std::size_t    length;
    };

    // EN 1.0.15.4 and 1.0.15.4a share these; the first bytes come from the EN 1.0.15.4 dump and are checked before any call
    const ResidentFn kResidentFnsEn[RF_COUNT] =
    {
        { 0x14006C910ull, { 0x48, 0x89, 0x4C, 0x24, 0x08, 0x53, 0x48, 0x83, 0xEC, 0x30 }, 10 },               // fox::BlockMemoryDesc ctor
        { 0x140061AC0ull, { 0x48, 0x89, 0x4C, 0x24, 0x08, 0x53, 0x48, 0x83, 0xEC, 0x30 }, 10 },               // fox::SharedString(const char*)
        { 0x14001D4C0ull, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xC2, 0x48, 0x8B, 0xD9 }, 12 },   // fox::SharedString copy
        { 0x14001D3B0ull, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9 }, 9 },                     // fox::SharedString dtor
        { 0x14006D3E0ull, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x60 }, 6 },                                       // fox::BlockGroup::Create
        { 0x140016090ull, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA }, 13 }, // SharedPtrBase::Set
        { 0x14006E040ull, { 0x48, 0x8B, 0xC4, 0x57, 0x41, 0x54 }, 6 },                                       // fox::Block::Load(ids, count)
        { 0x14006CFB0ull, { 0x48, 0x89, 0x7C, 0x24, 0x18, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xB9, 0x80, 0x00, 0x00, 0x00, 0x02 }, 18 }, // fox::Block::Activate
        { 0x140016490ull, { 0x48, 0x89, 0x4C, 0x24, 0x08, 0x53, 0x48, 0x83, 0xEC, 0x30 }, 10 },               // fox::KernelString(const char*)
        { 0x1400858D0ull, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x83, 0x7A, 0x18, 0x10 }, 15 }, // fox::Path::CInitWithString
        { 0x1400041C0ull, { 0xE9, 0x1B, 0x0A, 0x00, 0x00 }, 5 },                                              // jmp ffstd::StdFree
    };

    constexpr std::uintptr_t UICDM_RESIDENT_CONTROLLER = 0x840;   // tpp::ui::ResidentBlockController*
    constexpr std::uintptr_t RESIDENT_CONTROLLER_GROUP = 0x30;    // its BlockGroup*
    constexpr std::uintptr_t DESC_BLOCK_SIZE  = 0x08;
    constexpr std::uintptr_t DESC_BLOCK_COUNT = 0x0C;
    constexpr std::uintptr_t DESC_FROM_HEAP   = 0x10;   // 0: the group's memory comes from the heap, not from a loading block
    constexpr std::uintptr_t DESC_MEMORY_TAG  = 0x14;   // the game's resident UI group uses 0x70
    constexpr std::uintptr_t DESC_OWNER       = 0x18;   // never read by the block code, null is fine
    constexpr int RESIDENT_MAX_LOADS = 3;
    constexpr std::size_t   kMaxResidentPacks = 16;
    constexpr std::uint64_t kMaxResidentBytes = 64ull << 20;
    constexpr ULONGLONG RESIDENT_RETRY_MS = 2000;

    struct ResidentPack
    {
        std::string   path;
        std::uint64_t pathId = 0;     // plain path hash, for the log
        std::uint64_t packId = 0;     // the id Block::Load needs (fox::Path of the pack), made on the UI thread before the first load
        std::uint32_t size = 0;
        void*         group = nullptr;   // a SharedPtr slot: our reference keeps the group alive for the rest of the game
        char*         block = nullptr;
        int           loads = 0;
        ULONGLONG     nextLoad = 0;
        int           lastState = -1;
        bool          done = false;
    };

    std::mutex g_ResidentLock;
    std::vector<std::unique_ptr<ResidentPack>> g_ResidentPacks;   // entries never move: the group slots must stay put
    std::atomic<bool> g_ResidentWork{ false };
    int g_ResidentFnsChecked = 0;   // 0 not yet, 1 good, -1 a prologue did not match

    const ResidentFn* ResidentFns()
    {
        if (gGameBuild == ::AddressSetRuntime::GameBuild::En_1_0_15_4 || gGameBuild == ::AddressSetRuntime::GameBuild::En_1_0_15_4a)
            return kResidentFnsEn;
        return nullptr;
    }

    bool PrologueMatchesSEH(const void* fn, const std::uint8_t* bytes, std::size_t length)
    {
        __try { return fn && std::memcmp(fn, bytes, length) == 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // another module's detour (MinHook writes jmp rel32 or jmp [rip]): IHHook hooks fox::Block::Load to log every load
    bool StartsWithJumpSEH(const void* fn)
    {
        __try
        {
            const auto* b = static_cast<const std::uint8_t*>(fn);
            return fn && (b[0] == 0xE9 || (b[0] == 0xFF && b[1] == 0x25));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool ResidentFnsReady()
    {
        if (g_ResidentFnsChecked != 0)
            return g_ResidentFnsChecked > 0;
        const ResidentFn* fns = ResidentFns();
        g_ResidentFnsChecked = fns ? 1 : -1;
        int hooked = 0;
        for (int i = 0; fns && i < RF_COUNT; ++i)
        {
            void* fn = ResolveGameAddress(fns[i].address);
            if (PrologueMatchesSEH(fn, fns[i].prologue, fns[i].length))
                continue;
            if (StartsWithJumpSEH(fn))
            {
                LogInfo("LoadResidentUiPack: game function %d at 0x%llX is hooked by another module, calls go through its hook",
                    i, static_cast<unsigned long long>(fns[i].address));
                ++hooked;
                continue;
            }
            LogError("LoadResidentUiPack: game function %d at 0x%llX does not match this build, resident UI packs stay off",
                i, static_cast<unsigned long long>(fns[i].address));
            g_ResidentFnsChecked = -1;
        }
        if (g_ResidentFnsChecked > 0 && hooked > RF_COUNT / 2)   // most of them unreadable: not proof enough of the build
        {
            LogError("LoadResidentUiPack: %d of %d game functions are hooked by other modules, resident UI packs stay off", hooked, RF_COUNT);
            g_ResidentFnsChecked = -1;
        }
        return g_ResidentFnsChecked > 0;
    }

    template <typename T>
    T ResidentFn_(int index)
    {
        return reinterpret_cast<T>(ResolveGameAddress(ResidentFns()[index].address));
    }

    // A pack's id is not a plain path hash: fox::Path::CInitWithString registers .fpk/.fpkd paths in the engine's path table
    // and returns that id (a plain hash loads only the .fpkd half). Built the way ResidentBlockController's Lua call builds it;
    // the Path is never released, the pack stays loaded for the rest of the game anyway.
    constexpr std::uintptr_t KSTRING_CAPACITY = 0x18;   // > 15: the text lives on the heap, pointer at +0
    bool PackPathIdSEH(const char* path, std::uint64_t& out)
    {
        alignas(16) std::uint8_t text[0x20] = {};
        std::uint64_t pathId = 0;
        __try
        {
            ResidentFn_<KernelStringCtor_t>(RF_KSTRING_CTOR)(text, path);
            ResidentFn_<PathInitWithString_t>(RF_PATH_INIT)(&pathId, text);
            if (*reinterpret_cast<std::uint64_t*>(text + KSTRING_CAPACITY) > 0xF)
                ResidentFn_<StdFree_t>(RF_STD_FREE)(*reinterpret_cast<void**>(text), 0);
            out = pathId;
            return pathId != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // ---- other games' asset roots ----------------------------------------------------------------------
    // TPP's path system knows five asset roots: fox, fox_export, tpp, sh and mgo (the name table at 0x1429E3990). A path
    // under any other root, like MG Survive's /Assets/ssd/..., gets a user flag in its id (bit 50, ff_path_code_set_userflag)
    // when a pack registers its files, while .uilb/.uif files and V_UixUtility look files up by the plain path hash: the
    // files load but are never found. Treating these roots as known gives their files plain ids. No TPP file sits under them.

    using FindKnownProject_t = bool (__fastcall*)(const char* project);
    FindKnownProject_t g_OrigFindKnownProject = nullptr;
    void* g_FindKnownProjectTarget = nullptr;

    const char* const kExtraProjects[] = { "ssd" };

    // fox::fs::impl::PathCodeResolver::FindKnownProject, EN 1.0.15.4 and 1.0.15.4a; 0 bytes in the mask are the
    // rip-relative offsets of the name table
    constexpr std::uintptr_t FIND_KNOWN_PROJECT_EN = 0x14003DAD0ull;
    const std::uint8_t kFindKnownProjectBytes[] = { 0x4C, 0x8B, 0x05, 0, 0, 0, 0, 0x4C, 0x8B, 0xD1, 0x4C, 0x8D, 0x0D, 0, 0, 0, 0, 0x4D, 0x85, 0xC0, 0x74, 0x3E };
    const std::uint8_t kFindKnownProjectMask[]  = { 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1 };

    bool FindKnownProjectMatchesSEH(const void* fn)
    {
        __try
        {
            const auto* b = static_cast<const std::uint8_t*>(fn);
            for (std::size_t i = 0; i < sizeof(kFindKnownProjectBytes); ++i)
                if (kFindKnownProjectMask[i] && b[i] != kFindKnownProjectBytes[i])
                    return false;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool __fastcall hkFindKnownProject(const char* project)
    {
        if (project)
            for (const char* extra : kExtraProjects)
                if (std::strcmp(project, extra) == 0)
                    return true;
        return g_OrigFindKnownProject(project);
    }

    // before any pack is loaded: a file registered while the root was unknown keeps its flagged id until its pack unloads
    void InstallExtraProjects()
    {
        if (gGameBuild != ::AddressSetRuntime::GameBuild::En_1_0_15_4 && gGameBuild != ::AddressSetRuntime::GameBuild::En_1_0_15_4a)
        {
            LogInfo("V_UixUtility: no address for FindKnownProject on %s, files under /Assets/ssd/ stay unfindable", GetGameBuildName(gGameBuild));
            return;
        }
        void* target = ResolveGameAddress(FIND_KNOWN_PROJECT_EN);
        if (!FindKnownProjectMatchesSEH(target))
        {
            LogError("V_UixUtility: FindKnownProject does not match this build, files under /Assets/ssd/ stay unfindable");
            return;
        }
        if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkFindKnownProject), reinterpret_cast<void**>(&g_OrigFindKnownProject)))
        {
            LogError("V_UixUtility: FindKnownProject hook failed, files under /Assets/ssd/ stay unfindable");
            return;
        }
        g_FindKnownProjectTarget = target;
    }

    // the block system is up once the game's own resident UI block is active
    bool GameResidentBlockActiveSEH()
    {
        __try
        {
            using GetInstance_t = void* (*)();
            const auto getInstance = reinterpret_cast<GetInstance_t>(ResolveGameAddress(gAddr.Ui_UiCommonDataManagerGetInstance));
            void* manager = getInstance ? getInstance() : nullptr;
            void* controller = manager ? Field<void*>(manager, UICDM_RESIDENT_CONTROLLER) : nullptr;
            void* group = controller ? Field<void*>(controller, RESIDENT_CONTROLLER_GROUP) : nullptr;
            char* blocks = group ? Field<char*>(group, GROUP_BLOCKS) : nullptr;
            return blocks && Field<std::uint32_t>(group, GROUP_BLOCK_COUNT) > 0 && Field<int>(blocks, BLOCK_STATE) == 3;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // BlockGroup::Create hands the group back unowned (refcount 0), so it goes straight into our slot
    bool CreateResidentGroupSEH(ResidentPack& pack)
    {
        alignas(16) std::uint8_t desc[0x40] = {};
        alignas(16) std::uint8_t name[0x10] = {};
        __try
        {
            ResidentFn_<BlockMemoryDescCtor_t>(RF_DESC_CTOR)(desc);
            ResidentFn_<SharedStringMake_t>(RF_STRING_MAKE)(name, "V_FrameWork_ResidentUiPack");
            ResidentFn_<SharedStringCopy_t>(RF_STRING_COPY)(desc, name);
            ResidentFn_<SharedStringDtor_t>(RF_STRING_DTOR)(name);
            *reinterpret_cast<std::uint32_t*>(desc + DESC_BLOCK_SIZE) = pack.size;
            *reinterpret_cast<std::uint32_t*>(desc + DESC_BLOCK_COUNT) = 1;
            *reinterpret_cast<std::uint32_t*>(desc + DESC_FROM_HEAP) = 0;
            *reinterpret_cast<std::uint32_t*>(desc + DESC_MEMORY_TAG) = 0x70;
            *reinterpret_cast<void**>(desc + DESC_OWNER) = nullptr;
            void* group = ResidentFn_<BlockGroupCreate_t>(RF_GROUP_CREATE)(desc);
            ResidentFn_<SharedStringDtor_t>(RF_STRING_DTOR)(desc);
            if (!group)
                return false;
            ResidentFn_<SharedPtrSet_t>(RF_SHARED_SET)(&pack.group, group);
            char* blocks = Field<char*>(group, GROUP_BLOCKS);
            pack.block = (blocks && Field<std::uint32_t>(group, GROUP_BLOCK_COUNT) > 0) ? blocks : nullptr;
            return pack.block != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    int BlockStateSEH(char* block)
    {
        __try { return Field<int>(block, BLOCK_STATE); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }

    int LoadBlockSEH(char* block, std::uint64_t pathId)
    {
        int error = -1;
        __try { ResidentFn_<BlockLoad_t>(RF_BLOCK_LOAD)(block, &error, &pathId, 1); }
        __except (EXCEPTION_EXECUTE_HANDLER) { error = -2; }
        return error;
    }

    int ActivateBlockSEH(char* block)
    {
        int error = -1;
        __try { ResidentFn_<BlockActivate_t>(RF_BLOCK_ACTIVATE)(block, &error); }
        __except (EXCEPTION_EXECUTE_HANDLER) { error = -2; }
        return error;
    }

    void StepResidentPack(ResidentPack& pack)
    {
        if (!pack.block)
        {
            if (!CreateResidentGroupSEH(pack))
            {
                LogError("LoadResidentUiPack: %s: the block group could not be made (%u bytes)", pack.path.c_str(), pack.size);
                pack.done = true;
                return;
            }
            LogInfo("LoadResidentUiPack: %s: block group made, %u bytes", pack.path.c_str(), pack.size);
        }
        const int state = BlockStateSEH(pack.block);
        const int previous = pack.lastState;
        if (state != previous)
        {
            LogInfo("LoadResidentUiPack: %s: block state %d", pack.path.c_str(), state);
            pack.lastState = state;
        }
        const ULONGLONG now = GetTickCount64();
        switch (state)
        {
        case 0:   // empty: load, or the last load failed (missing file, bad pack, too little memory)
            if (previous == 1)
                LogWarn("LoadResidentUiPack: %s: the load failed (is the pack installed, and is %u bytes enough?)", pack.path.c_str(), pack.size);
            if (pack.loads >= RESIDENT_MAX_LOADS)
            {
                LogError("LoadResidentUiPack: %s: gave up after %d loads", pack.path.c_str(), pack.loads);
                pack.done = true;
            }
            else if (now >= pack.nextLoad)
            {
                if (pack.packId == 0)
                {
                    if (!PackPathIdSEH(pack.path.c_str(), pack.packId))
                    {
                        LogError("LoadResidentUiPack: %s: the game would not make a path for the pack", pack.path.c_str());
                        pack.done = true;
                        break;
                    }
                    LogInfo("LoadResidentUiPack: %s: pack id 0x%llX (plain hash 0x%llX)", pack.path.c_str(),
                        static_cast<unsigned long long>(pack.packId), static_cast<unsigned long long>(pack.pathId));
                }
                ++pack.loads;
                pack.nextLoad = now + RESIDENT_RETRY_MS;
                if (const int error = LoadBlockSEH(pack.block, pack.packId); error < 0)
                    LogWarn("LoadResidentUiPack: %s: Block::Load refused (%d)", pack.path.c_str(), error);
            }
            break;
        case 2:   // loaded: activate, as the game's resident controller does
            if (const int error = ActivateBlockSEH(pack.block); error < 0)
                LogWarn("LoadResidentUiPack: %s: Block::Activate refused (%d)", pack.path.c_str(), error);
            break;
        case 3:
            LogInfo("LoadResidentUiPack: %s is loaded and stays loaded", pack.path.c_str());
            pack.done = true;
            break;
        default:   // 1 loading, 4 unloading: the block's own task moves it on
            break;
        }
    }

    // called once per UI frame (group 0), never while holding g_Lock or the block group lock
    void ResidentPacksTick()
    {
        if (!g_ResidentWork.load(std::memory_order_acquire) || MissionCodeGuard::ShouldBypassHooks())
            return;
        if (!ResidentFnsReady())
        {
            g_ResidentWork.store(false, std::memory_order_release);
            return;
        }
        if (!GameResidentBlockActiveSEH())
            return;
        std::lock_guard<std::mutex> lock(g_ResidentLock);
        bool pending = false;
        for (const auto& pack : g_ResidentPacks)
            if (!pack->done)
            {
                StepResidentPack(*pack);
                pending = pending || !pack->done;
            }
        g_ResidentWork.store(pending, std::memory_order_release);
    }

    std::atomic<void (*)()> g_FrameTick{ nullptr };

    void RunFrameTickSEH(void (*tick)())
    {
        __try { tick(); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void __fastcall hkUpdateLayouts(void* manager, unsigned group)
    {
        if (g_HasCommands.load(std::memory_order_acquire))
        {
            const bool bypass = MissionCodeGuard::ShouldBypassHooks();
            std::lock_guard<std::recursive_mutex> lock(g_Lock);
            DestroyWindowsOfUnloadedBlocks();
            std::vector<CreateCommand> creates;
            creates.swap(g_LayoutCreates);
            if (!bypass)
                for (const CreateCommand& c : creates)
                    RunLayoutCreate(manager, c);
            RunCommands(manager, group, bypass);
            g_HasCommands.store(!g_Commands.empty() || !g_LayoutCreates.empty() || !g_CreatedWindows.empty(), std::memory_order_release);
        }
        if (group == 0)
        {
            FrameCallbacks::NoteFrame();
            ResidentPacksTick();
            if (void (*tick)() = g_FrameTick.load(std::memory_order_acquire); tick && !MissionCodeGuard::ShouldBypassHooks())
                RunFrameTickSEH(tick);
        }
        g_OrigUpdateLayouts(manager, group);
    }

    void __fastcall hkDeleteDestroyedWindows(void* manager)
    {
        if (manager && g_HasDaemonWork.load(std::memory_order_acquire))
        {
            std::lock_guard<std::recursive_mutex> lock(g_Lock);
            RunGraphWork(manager, MissionCodeGuard::ShouldBypassHooks());
            UnlinkDyingWindows(manager);
            std::unordered_set<void*> dying;
            CollectDyingWindows(manager, dying);
            if (!dying.empty())
            {
                EraseButtonMeshesOf(dying);
                KillHandlesOf(dying);
                for (void* window : dying)
                {
                    g_CreatedWindows.erase(window);
                    g_DestroyingWindows.erase(window);
                }
            }
            AgeRetiredLines(dying);
            DeleteUnusedTextUnits(manager);
            RefreshDaemonWork();
        }
        g_OrigDeleteDestroyed(manager);
    }

    const std::uint8_t* FollowThunks(const std::uint8_t* p)
    {
        for (int hop = 0; p && hop < 3 && p[0] == 0xE9; ++hop)
            p = p + 5 + *reinterpret_cast<const std::int32_t*>(p + 1);
        return p;
    }

    // the method drops its this and forwards: SUB RSP,0x28 / MOV RCX,RDX / CALL target, or MOV RCX,RDX / JMP target
    const std::uint8_t* ForwardTarget(const std::uint8_t* p)
    {
        p = FollowThunks(p);
        if (p[0] == 0x48 && p[1] == 0x83 && p[2] == 0xEC && p[4] == 0x48 && p[5] == 0x8B && p[6] == 0xCA && p[7] == 0xE8)
            return p + 12 + *reinterpret_cast<const std::int32_t*>(p + 8);
        if (p[0] == 0x48 && p[1] == 0x8B && p[2] == 0xCA && p[3] == 0xE9)
            return p + 8 + *reinterpret_cast<const std::int32_t*>(p + 4);
        return nullptr;
    }

    // the build's UixUtility must have the dev vtable layout this library expects: its FindWindow slot forwards to FindWindow
    bool VerifySlotsSEH()
    {
        __try
        {
            const auto* slot = static_cast<const std::uint8_t*>(Method(kDevFindWindow));
            const auto* expected = static_cast<const std::uint8_t*>(ResolveGameAddress(gAddr.UiWindowFunction_FindWindow));
            const std::uint8_t* target = slot ? ForwardTarget(slot) : nullptr;
            return target && expected && FollowThunks(target) == FollowThunks(expected);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
}


namespace UixUtilityControl
{
    // -1 not checked yet: the DLL can load before the game builds its UixUtility, so the check waits for first use
    std::atomic<int> g_SlotsChecked{ -1 };

    bool SlotsReady()
    {
        const int state = g_SlotsChecked.load(std::memory_order_acquire);
        if (state >= 0)
            return state == 1;
        void** util = Utility();
        if (!util || !*reinterpret_cast<void**>(util))
            return false;
        const bool ok = VerifySlotsSEH();
        g_SlotsChecked.store(ok ? 1 : 0, std::memory_order_release);
        if (!ok)
            LogError("V_UixUtility: this build's UixUtility does not have the expected layout, UI functions do nothing");
        return ok;
    }

    bool IsSupportedOnThisBuild()
    {
        return g_Installed.load(std::memory_order_acquire) && SlotsReady();
    }

    void** Utility()
    {
        auto getter = Fn<GetUixUtility_t>(gAddr.GetUixUtilityToFeedQuarkEnvironment);
        return getter ? getter() : nullptr;
    }

    void* Method(std::size_t devOffset)
    {
        void** util = Utility();
        if (!util)
            return nullptr;
        void** vtbl = *reinterpret_cast<void***>(util);
        return vtbl ? vtbl[RetailOffset(devOffset) / sizeof(void*)] : nullptr;
    }

    void** MethodSlot(std::size_t devOffset)
    {
        void** util = Utility();
        void** vtbl = util ? *reinterpret_cast<void***>(util) : nullptr;
        return vtbl ? &vtbl[RetailOffset(devOffset) / sizeof(void*)] : nullptr;
    }

    void SetFrameTick(void (*tick)())
    {
        g_FrameTick.store(tick, std::memory_order_release);
    }

    std::uint32_t HandleFor(Kind kind, void* ptr, void* window, void* owner)
    {
        if (!ptr)
            return 0;
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        const std::uint64_t key = HandleKey(ptr, kind);
        const auto known = g_HandleOf.find(key);
        if (known != g_HandleOf.end())
        {
            Entry& e = g_Handles[known->second];
            if (!e.dead && e.object.window == window)
            {
                if (owner)
                    e.object.owner = owner;
                return known->second;
            }
        }
        const std::uint32_t handle = g_NextHandle++;
        Entry& e = g_Handles[handle];
        e.object = { kind, ptr, window, owner };
        g_HandleOf[key] = handle;
        RefreshDaemonWork();
        return handle;
    }

    bool Lookup(std::uint32_t handle, Object& out)
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        const auto it = g_Handles.find(handle);
        if (it == g_Handles.end() || it->second.dead)
            return false;
        if (it->second.object.window && StateOfSEH(Manager(), it->second.object.window) != WindowState::Alive)
            return false;
        out = it->second.object;
        return true;
    }

    void Queue(std::uint32_t handle, std::function<void(const Object&)> fn)
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        Command c;
        c.handle = handle;
        c.run = std::move(fn);
        g_Commands.push_back(std::move(c));
        g_HasCommands.store(true, std::memory_order_release);
    }

    void QueueNoHandle(std::function<void()> fn)
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        Command c;
        c.plain = std::move(fn);
        g_Commands.push_back(std::move(c));
        g_HasCommands.store(true, std::memory_order_release);
    }

    bool Guard(void (*thunk)(void*), void* context)
    {
        __try
        {
            thunk(context);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    std::uint32_t CreateTextUnit(int count)
    {
        auto create = reinterpret_cast<CreateTextUnit_t>(Method(kDevCreateTextUnit));
        void** util = Utility();
        if (!create || !util || count <= 0)
            return 0;
        if (count > kMaxTextUnits)   // the game allocates count * 32 bytes unchecked; no vanilla screen uses more than 25
        {
            LogWarnOnce("V_UixUtility.CreateTextUnit: %d units asked, %d made", count, kMaxTextUnits);
            count = kMaxTextUnits;
        }
        void* units = nullptr;
        auto call = [&]() { create(util, &units, count); };
        if (!Guarded(call) || !units)
            return 0;
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        g_TextUnits[units].count = count;
        return HandleFor(Kind::TextUnit, units, nullptr);
    }

    int TextUnitCount(void* units)
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        const auto it = g_TextUnits.find(units);
        return it != g_TextUnits.end() ? it->second.count : 0;
    }

    void DeleteTextUnit(std::uint32_t handle)
    {
        Object object;
        if (!Lookup(handle, object) || object.kind != Kind::TextUnit)
            return;
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        g_TextUnits[object.ptr].deleteWanted = true;
        g_Handles.erase(handle);
        g_HandleOf.erase(HandleKey(object.ptr, Kind::TextUnit));
        RefreshDaemonWork();
    }

    void KeepTextAlive(void* units, const Object& node, std::shared_ptr<std::string> text)
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        TextUnitInfo& info = g_TextUnits[units];
        if (text)
            info.text = std::move(text);
        for (auto& use : info.nodes)
            if (use.first == node.ptr)
            {
                use.second = node.window;
                return;
            }
        info.nodes.emplace_back(node.ptr, node.window);
    }

    std::shared_ptr<const void> PinTextUnit(void* units)
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        return g_TextUnits[units].queued;
    }

    int ConnectProblemSEH(void* child, void* childWindow, void* parent)
    {
        __try
        {
            if (child == parent)
                return 1;
            if (childWindow && RootLayout(childWindow) == child)
                return 2;   // its window draws it already: hung elsewhere too it is drawn twice
            void* above = parent;
            for (int depth = 0; above && depth < 64; ++depth)
            {
                above = Field<void*>(above, COMPONENT_PARENT);
                if (above == child)
                    return 3;   // the engine's layout walk would never end
            }
            return 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 4; }
    }

    int ConnectProblem(void* child, void* childWindow, void* parent)
    {
        return child && parent ? ConnectProblemSEH(child, childWindow, parent) : 4;
    }

    void RememberLink(void* child, void* childWindow, void* parentWindow)
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        for (Link& l : g_Links)
            if (l.child == child)
            {
                l.childWindow = childWindow;
                l.parentWindow = parentWindow;
                return;
            }
        g_Links.push_back({ child, childWindow, parentWindow });
        RefreshDaemonWork();
    }

    void ForgetLink(void* child)
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        for (auto it = g_Links.begin(); it != g_Links.end(); ++it)
            if (it->child == child)
            {
                g_Links.erase(it);
                return;
            }
    }

    int GetMarkers(MarkerEntry* out, int capacity)
    {
        if (!IsScreenPorted() || !out || capacity <= 0)
            return -1;
        const char* records = MarkerRecordsSEH();
        if (!records)
            return -1;
        const int count = CollectMarkersSEH(records, out, capacity);
        AddMarkerFlagsSEH(out, count);
        for (int i = 0; i < count; ++i)
            out[i].dying = HeadMarkShowsDying(out[i].gameObjectId);
        return count;
    }

    int GetEnemyRoutes(EnemyRoute* out, int capacity)
    {
        if (!out || capacity <= 0 || !gAddr.Ui_UiCommonDataManagerGetInstance)
            return -1;
        // the route layout above was read on 1.0.15.4; 1.0.15.3 is not checked
        if (!::AddressSetRuntime::IsEn154Family(gGameBuild) && !::AddressSetRuntime::IsJp154Family(gGameBuild))
            return -1;
        return CollectEnemyRoutesSEH(out, capacity);
    }

    bool NodeToScreen(const Object& node, const Object* component, ScreenPoint& out)
    {
        ScreenFrame frame;
        if (!node.owner || !IsScreenPorted() || !ReadScreenFrameSEH(frame, false))
            return false;
        float hud[2] = { 0.0f, 0.0f };
        if (!NodeHudSEH(node.owner, node.ptr, hud))
            return false;
        Object model;
        model.kind = Kind::Model;
        model.ptr = node.owner;
        const float scale = ComponentScaleSEH(model);
        if (scale <= 0.0f || frame.heightScale <= 0.0f)
            return false;
        float screen[2] = { hud[0] / scale, hud[1] / (scale * frame.heightScale) };
        out.onScreen = screen[0] >= -1.0f && screen[0] <= 1.0f && screen[1] >= -1.0f && screen[1] <= 1.0f;
        if (component && !ToComponentUnits(frame, *component, screen, false))
            return false;
        out.x = screen[0];
        out.y = screen[1];
        return true;
    }

    int GetMarkerPosition(std::uint16_t objectId, const Object* component, MarkerPosition& out)
    {
        ScreenFrame frame;
        if (!IsScreenPorted() || !ReadScreenFrameSEH(frame, true))
            return -1;
        const int record = FindMarkerRecordSEH(frame.records, objectId, -1);
        if (record < 0)
            return 0;
        if (!MarkerWorldSEH(frame.records + record * MARKER_STRIDE, out.world, out.enabled))
            return -1;
        float screen[2] = { 0.0f, 0.0f };
        if (!ProjectSEH(frame, out.world, screen, out.screen.onScreen))
            return -1;
        if (component && !ToComponentUnits(frame, *component, screen, true))
            return -1;
        out.screen.x = screen[0];
        out.screen.y = screen[1];
        return 1;
    }

    void CreateUiWindow(std::uint64_t name, const std::vector<LayoutRecord>& layouts, std::uint32_t group, std::uint32_t pauseMask, std::uint16_t priority)
    {
        CreateCommand c;
        c.name = name & ID48_MASK;
        c.layouts = layouts;
        c.group = group;
        c.pauseMask = pauseMask;
        c.priority = priority;
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        g_LayoutCreates.push_back(std::move(c));
        g_HasCommands.store(true, std::memory_order_release);
    }

    void CreateGraphWindow(std::uint64_t name, const std::string& graph, std::uint32_t group, std::uint32_t pauseMask, std::uint16_t priority)
    {
        CreateCommand c;
        c.name = name & ID48_MASK;
        c.graph = graph;
        c.group = group;
        c.pauseMask = pauseMask;
        c.priority = priority;
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        g_GraphCreates.push_back(std::move(c));
        g_HasDaemonWork.store(true, std::memory_order_release);
    }

    bool IsResidentUiPackLoaded(const std::string& packPathAsked)
    {
        const std::string packPath = FoxHashes::NormalizeAssetPath(packPathAsked);
        std::lock_guard<std::mutex> lock(g_ResidentLock);
        for (const auto& pack : g_ResidentPacks)
            if (pack->path == packPath)
                return pack->lastState == 3;   // active; done alone also means it gave up
        return false;
    }

    bool LoadResidentUiPack(const std::string& packPathAsked, std::uint32_t sizeBytes)
    {
        const std::string packPath = FoxHashes::NormalizeAssetPath(packPathAsked);   // one pack, however it is spelled
        if (packPath.empty() || !ResidentFns())
            return false;
        std::lock_guard<std::mutex> lock(g_ResidentLock);
        std::uint64_t total = 0;
        for (const auto& pack : g_ResidentPacks)
        {
            if (pack->path == packPath)
                return true;
            total += pack->size;
        }
        // block groups stay for the rest of the game, so there is a ceiling
        if (g_ResidentPacks.size() >= kMaxResidentPacks || total + sizeBytes > kMaxResidentBytes)
        {
            LogWarnOnce("LoadResidentUiPack: %s refused: at most %u packs and %u MiB in all", packPath.c_str(),
                static_cast<unsigned>(kMaxResidentPacks), static_cast<unsigned>(kMaxResidentBytes >> 20));
            return false;
        }
        auto pack = std::make_unique<ResidentPack>();
        pack->path = packPath;
        pack->pathId = FoxHashes::PathCode64Ext(packPath);
        pack->size = sizeBytes;
        LogInfo("LoadResidentUiPack: %s asked for, %u bytes, path id 0x%llX", packPath.c_str(), sizeBytes,
            static_cast<unsigned long long>(pack->pathId));
        g_ResidentPacks.push_back(std::move(pack));
        g_ResidentWork.store(true, std::memory_order_release);
        return true;
    }

    void DestroyUiWindow(const Object& window)
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        auto created = g_CreatedWindows.find(window.ptr);
        if (created != g_CreatedWindows.end())
        {
            void* target = window.ptr;
            g_DestroyingWindows[target] = created->second;
            g_CreatedWindows.erase(created);
            QueueNoHandle([target]() {
                std::lock_guard<std::recursive_mutex> lock(g_Lock);
                if (g_DestroyingWindows.erase(target))   // a CreateWindow of the same name may have destroyed it first
                    CallWindowSEH(Fn<WindowCall_t>(g_Addrs.destroyWindow), target);
            });
            return;
        }
        std::uint32_t id = 0;
        auto read = [&]() { id = Field<std::uint32_t>(window.ptr, WINDOW_NAME); };
        if (Guarded(read))
            for (GraphWindow& g : g_GraphWindows)
                if (g.id == id)
                {
                    g.release = true;
                    g_HasDaemonWork.store(true, std::memory_order_release);
                    return;
                }
        LogWarnOnce("V_UixUtility.DestroyWindow: that window was not made by V_UixUtility.CreateWindow, ignored");
    }

    std::vector<WindowEntry> GetWindows()
    {
        std::vector<WindowEntry> out;
        void* manager = Manager();
        if (!manager)
            return out;
        std::vector<void*> windows(kMaxArray);
        const int count = LiveWindowsSEH(manager, windows.data(), kMaxArray);
        for (int i = 0; i < count; ++i)
        {
            WindowEntry entry;
            entry.window = windows[i];
            void* root = nullptr;
            if (!WindowFieldsSEH(windows[i], entry.id, entry.group, entry.parent, root))
                continue;
            if (root)
                ReadLayouts(root, 0, 0, entry.layouts);
            out.push_back(std::move(entry));
        }
        return out;
    }

    void** UiUtility()
    {
        auto get = g_Addrs.getUiUtility ? Fn<void** (__fastcall*)()>(g_Addrs.getUiUtility) : nullptr;
        return get ? get() : nullptr;
    }

    void* UiUtilityMethod(std::size_t retailOffset)
    {
        void** util = UiUtility();
        void** vtbl = util ? *reinterpret_cast<void***>(util) : nullptr;
        return vtbl ? vtbl[retailOffset / sizeof(void*)] : nullptr;
    }

    void RememberButtonMesh(void* node, void* window, std::uint32_t registId)
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        g_ButtonMeshes[node] = { window, registId };
    }

    void ForgetButtonMesh(void* node)
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        g_ButtonMeshes.erase(node);
    }

    void ForgetButtonMeshesOf(std::uint32_t registId)
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        for (auto it = g_ButtonMeshes.begin(); it != g_ButtonMeshes.end();)
            it = it->second.registId == registId ? g_ButtonMeshes.erase(it) : std::next(it);
    }

    void NoteDrawScenePaused(bool paused)
    {
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        g_DrawScenePaused = paused;
    }

    void* Function(UiFunction f)
    {
        const int i = static_cast<int>(f);
        if (i < 0 || i >= static_cast<int>(UiFunction::Count) || !g_Addrs.functions[i])
            return nullptr;
        return ResolveGameAddress(g_Addrs.functions[i]);
    }

    void QueueLineDraw(std::uint32_t lineNode, std::vector<std::uint16_t> indices, std::vector<LineVertex> vertices)
    {
        auto buffers = std::make_shared<LineBuffers>();
        buffers->indices = std::move(indices);
        buffers->vertices.reset(new Vec4[vertices.size()]);
        for (std::size_t i = 0; i < vertices.size(); ++i)
            buffers->vertices[i] = { { vertices[i].x, vertices[i].y, vertices[i].z, vertices[i].w } };
        buffers->draw.vertexCount = static_cast<std::uint32_t>(buffers->indices.size());
        buffers->draw.indices = buffers->indices.data();
        Queue(lineNode, [buffers](const Object& node) {
            RetireLineBuffersOf(node.ptr);
            buffers->window = node.window;
            g_LineBuffers[node.ptr] = buffers;   // held before the call, so a fault halfway cannot leave the node pointing at freed memory
            RefreshDaemonWork();
            reinterpret_cast<SetLineDraw_t>(Method(kDevSetLineDraw))(Utility(), node.ptr, &buffers->draw, buffers->vertices.get());
        });
    }

    void QueueResetLineDraw(std::uint32_t lineNode)
    {
        Queue(lineNode, [](const Object& node) {
            reinterpret_cast<ResetLineDraw_t>(Method(kDevResetLineDraw))(Utility(), node.ptr);
            RetireLineBuffersOf(node.ptr);
        });
    }

    bool QueueAddChildWindow(const Object& parent, std::uint32_t childHandle, const Object& child)
    {
        if (!Function(UiFunction::AddChildWindow))
            return false;
        std::lock_guard<std::recursive_mutex> lock(g_Lock);
        bool ours = g_CreatedWindows.count(child.ptr) != 0;
        std::uint32_t id = 0;
        auto read = [&]() { id = Field<std::uint32_t>(child.ptr, WINDOW_NAME); };
        if (!ours && Guarded(read))
            for (const GraphWindow& g : g_GraphWindows)
                if (g.id == id)
                    ours = true;
        if (!ours || child.ptr == parent.ptr)
            return false;
        void* parentWindow = parent.ptr;
        Queue(childHandle, [parentWindow](const Object& c) {
            void* manager = Manager();
            if (StateOfSEH(manager, parentWindow) != WindowState::Alive || Field<void*>(c.ptr, WINDOW_PARENT))
                return;   // the parent is gone, or the child already has one (the game does not check)
            void* up = parentWindow;
            for (int depth = 0; up && depth < 32; ++depth, up = Field<void*>(up, WINDOW_PARENT))
                if (up == c.ptr)
                {
                    LogWarnOnce("V_UixUtility.AddChildWindow: the parent is inside the child's own tree, ignored");
                    return;   // a loop leaves both windows parented, so neither is ever drawn
                }
            reinterpret_cast<AddChildWindow_t>(Function(UiFunction::AddChildWindow))(parentWindow, c.ptr);
        });
        return true;
    }

    void* DisconnectComponentFn() { return ResolveGameAddress(g_Addrs.disconnectComponent); }
    void* GetTimeLengthFn()       { return ResolveGameAddress(g_Addrs.getTimeLength); }
    void* SetFontGroupFn()        { return ResolveGameAddress(g_Addrs.setFontGroup); }
    void* StartSoundFn()          { return ResolveGameAddress(g_Addrs.startSound); }
    void* SoundControlStartFn()   { return ResolveGameAddress(gAddr.Ui_SoundControlStart); }
    void* SoundControlStopFn()    { return ResolveGameAddress(g_Addrs.soundControlStop); }
    void* FocalLengthZoomRatioFn() { return g_Addrs.focalLengthZoomRatio ? ResolveGameAddress(g_Addrs.focalLengthZoomRatio) : nullptr; }

    void* SoundControl()
    {
        if (!g_Addrs.soundControl)
            return nullptr;
        return *reinterpret_cast<void**>(ResolveGameAddress(g_Addrs.soundControl));
    }

    int WindowGroup(void* window)
    {
        return window ? GroupSEH(window) : -1;
    }
}


bool Install_UixUtilityControl_Hook()
{
    g_Addrs = AddrsForBuild();
    if (!g_Addrs.updateLayouts)
    {
        LogError("V_UixUtility: not ported to %s yet, UI functions do nothing", GetGameBuildName(gGameBuild));
        return false;
    }
    const bool layoutsOk = CreateAndEnableHook(ResolveGameAddress(g_Addrs.updateLayouts),
        reinterpret_cast<void*>(&hkUpdateLayouts), reinterpret_cast<void**>(&g_OrigUpdateLayouts));
    const bool deleteOk = CreateAndEnableHook(ResolveGameAddress(g_Addrs.deleteDestroyedWindows),
        reinterpret_cast<void*>(&hkDeleteDestroyedWindows), reinterpret_cast<void**>(&g_OrigDeleteDestroyed));
    if (!layoutsOk || !deleteOk)
        LogError("V_UixUtility: UI hooks failed (layouts=%d delete=%d), UI functions do nothing", layoutsOk ? 1 : 0, deleteOk ? 1 : 0);

    InstallExtraProjects();   // its own hook: Survive's files must be findable even if the layout hooks failed

    g_Installed.store(layoutsOk && deleteOk, std::memory_order_release);
    return layoutsOk && deleteOk;
}

bool Uninstall_UixUtilityControl_Hook()
{
    uipad::Uninstall();   // gives the game's UI pad back before anything else goes
    if (g_Addrs.updateLayouts)
    {
        DisableAndRemoveHook(ResolveGameAddress(g_Addrs.updateLayouts));
        DisableAndRemoveHook(ResolveGameAddress(g_Addrs.deleteDestroyedWindows));
    }
    if (g_FindKnownProjectTarget)
    {
        DisableAndRemoveHook(g_FindKnownProjectTarget);
        g_FindKnownProjectTarget = nullptr;
    }
    g_Installed.store(false, std::memory_order_release);

    // the DLL is leaving mid-game: undo links and drop our windows, or the game keeps pointers into them
    std::lock_guard<std::recursive_mutex> lock(g_Lock);
    void* manager = Manager();
    for (const Link& l : g_Links)
    {
        const WindowState childState = StateOfSEH(manager, l.childWindow);
        if (childState == WindowState::Gone)
            continue;
        if (StateOfSEH(manager, l.parentWindow) == WindowState::Gone)
            ForgetParentSEH(l.child);
        else
            DisconnectSEH(l.child);
    }
    g_Links.clear();
    for (const auto& entry : g_ButtonMeshes)
        EraseButtonMeshSEH(entry.first);
    g_ButtonMeshes.clear();
    if (g_DrawScenePaused)
    {
        auto resume = [&]() {
            reinterpret_cast<void(__fastcall*)(void**, bool)>(UixUtilityControl::UiUtilityMethod(kUiPauseDrawScene))(UixUtilityControl::UiUtility(), false);
        };
        if (UixUtilityControl::UiUtility())
            UixUtilityControl::Guarded(resume);
        g_DrawScenePaused = false;
    }
    for (const auto& entry : g_CreatedWindows)
        CallWindowSEH(Fn<WindowCall_t>(g_Addrs.destroyWindow), entry.first);
    g_CreatedWindows.clear();
    for (const auto& entry : g_DestroyingWindows)
        CallWindowSEH(Fn<WindowCall_t>(g_Addrs.destroyWindow), entry.first);
    g_DestroyingWindows.clear();
    for (GraphWindow& g : g_GraphWindows)
        ReleaseGraphSEH(g.control.get());
    g_GraphWindows.clear();
    g_GraphCreates.clear();
    g_LayoutCreates.clear();
    g_Commands.clear();
    g_Handles.clear();
    g_HandleOf.clear();
    for (auto& entry : g_TextUnits)
        new std::shared_ptr<std::string>(std::move(entry.second.text));   // nodes may still show these strings; leak them on purpose
    g_TextUnits.clear();
    for (auto& entry : g_LineBuffers)
        new std::shared_ptr<LineBuffers>(std::move(entry.second));        // the same for line draws
    g_LineBuffers.clear();
    for (auto& retired : g_RetiredLines)
        new std::shared_ptr<LineBuffers>(std::move(retired.buffers));
    g_RetiredLines.clear();
    return true;
}
