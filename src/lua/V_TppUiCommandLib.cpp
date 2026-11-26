#include <pch.h>

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
}

#include <cstdint>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "V_TppUiCommandLib.h"
#include "UiCommandFunctions.h"
#include "LuaApi.h"
#include "../hooks/ui/UixUtilityControl.h"
#include "../hooks/ui/LoadingSplash.h"
#include "../hooks/ui/GameOverSplash.h"
#include "../hooks/ui/LocationIconRegistry.h"
#include "../hooks/ui/MbDvcMissionListCallbackImpl_ActivatePrefabTablistParameter.h"

#include <string>

namespace
{

    using GetInstance_t = void* (*)();
    using GetQuarkCollector_t = void* (__fastcall*)(std::uint64_t creatorId);
    using LockSection_t = void(__fastcall*)(void* section);
    using GetTipInfo_t = void(__fastcall*)(void* table, std::uint32_t index, std::uint64_t* tipId, std::uint64_t* titleLangId,
                                           std::uint64_t* bodyLangId, std::int32_t* page, std::int32_t* pageCount);

    constexpr std::uint64_t LANG_ID_NONE = 0xB8A0BF169F98ull;   // the tips table's "no text"
    constexpr std::uint64_t ID48_MASK = 0xFFFFFFFFFFFFull;

    constexpr std::uintptr_t CDM_TIPS_STARTED = 0x157E;
    constexpr std::uintptr_t CDM_TIPS_ENDED   = 0x1586;
    constexpr std::uintptr_t TIPS_STATE       = 0x1B0;   // 0 splash, 1 waiting for the tips text, 2 active, 3 ending
    constexpr std::uintptr_t TIPS_TABLE       = 0x1D0;
    constexpr std::uintptr_t TIPS_CATEGORY    = 0x93C;   // 4 = loading tips
    constexpr std::uintptr_t TIPS_INDEX       = 0x940;
    constexpr std::uintptr_t TIPS_COUNT       = 0x942;
    constexpr std::uintptr_t TIPS_PAGE_TURN   = 0xA28;   // 0 none, 1 old page out, 2 waiting, 3 new text, 4 new page in, 5 waiting
    constexpr std::uintptr_t TIPS_RESUME      = 0xA31;   // the player may now leave the screen
    constexpr std::uintptr_t TIPS_HIDDEN      = 0xA32;   // another full-screen menu covers the tips
    constexpr std::uintptr_t GO_STATE         = 0x1E0;   // 0..10, see the wiki
    constexpr std::uintptr_t GO_SELECTED      = 0x1E4;
    constexpr std::uintptr_t GO_PREVIOUS      = 0x1E8;
    constexpr std::uintptr_t GO_ITEMS         = 0x1F0;   // GameOverMenuItem*[4], lang id at item +8
    constexpr std::uintptr_t GO_COUNT         = 0x210;
    constexpr std::uintptr_t GO_TYPE          = 0x214;   // 0 normal, 1 Cyprus, 2 time paradox
    constexpr std::uintptr_t GO_ITEM_LANG     = 0x08;

    using LoadMbStage_t = void* (__fastcall*)(void* self, std::int32_t* error, std::uint16_t layoutCode);
    using StageFlag_t = bool(__fastcall*)(void* self);
    using StageWord_t = std::uint16_t(__fastcall*)(void* self);
    using StageCount_t = std::uint32_t(__fastcall*)(void* self);
    using StageFloat_t = float(__fastcall*)(void* self, std::uint32_t index);
    using StageByte_t = std::uint8_t(__fastcall*)(void* self, std::uint32_t index);
    using StageBuildings_t = void(__fastcall*)(void* self, std::uint32_t index, std::uint8_t* ids, float* turns);

    constexpr std::uintptr_t CDM_MB_MAP = 0x78;   // UiCommonDataManager: MotherBaseMapCommonData
    constexpr std::uintptr_t MB_MAP_LOCATION_PARAMETER = 0x2D80;   // MotherBaseMapCommonDataImpl::GetMapLocationParameter
    // MbDvcMapLocationParameterImpl vtable slots
    constexpr std::uintptr_t SLOT_LOAD_MB_STAGE = 0x28;   // LoadMbStageParameter
    constexpr std::uintptr_t SLOT_MB_STAGE_LAYOUT = 0x68;   // GetCurrentMbStageLayoutId
    constexpr std::uintptr_t SLOT_MB_STAGE_LOADED = 0x248;
    constexpr std::uintptr_t SLOT_OBJECT_COUNT = 0x250;
    constexpr std::uintptr_t SLOT_OBJECT_POS_X = 0x258;
    constexpr std::uintptr_t SLOT_OBJECT_POS_Z = 0x260;
    constexpr std::uintptr_t SLOT_OBJECT_ROT_Y = 0x268;
    constexpr std::uintptr_t SLOT_OBJECT_TYPE = 0x270;
    constexpr std::uintptr_t SLOT_OBJECT_CLUSTER = 0x278;
    constexpr std::uintptr_t SLOT_OBJECT_PLANT = 0x280;
    constexpr std::uintptr_t SLOT_OBJECT_BUILDINGS = 0x288;   // GetMbStageObjectDataDivision
    constexpr std::uintptr_t SLOT_OBJECT_JOINED = 0x290;   // GetMbStageObjectDataConnectPartsFlag
    constexpr std::uint8_t NO_BUILDING = 0xFF;
    constexpr std::uint32_t MAX_MB_STAGE_OBJECTS = 512;

    constexpr std::uint64_t MB_DVC_MAP_CALLBACK = 0x72726F1D1E22ull;   // StringId of tpp::ui::menu::MbDvcMapCallback, the iDroid map's quark type
    constexpr std::uintptr_t COLLECTOR_TAIL = 0x08;   // fox::impl::QuarkCollector::Impl: newest node of the quark list
    constexpr std::uintptr_t COLLECTOR_LOCK = 0x20;
    constexpr std::uintptr_t NODE_PREVIOUS  = 0x00;
    constexpr std::uintptr_t NODE_QUARK     = 0x10;
    constexpr std::uintptr_t MAP_VIEW_MODE  = 0xEC;   // MbDvcMapCallbackImpl::GetViewMode: 0 while the iDroid map points north up, the map then skips its turn to the camera
    constexpr std::uintptr_t MAP_PHOTO_REAL = 0xED;   // MbDvcMapCallbackImpl: 1 while the iDroid map shows the satellite picture
    constexpr int MAX_MAP_QUARKS = 64;

    struct TipsInfo
    {
        bool session = false;
        bool seen = false;   // a tips session ran since the game started
        int  state = 0, category = 0, tipIndex = 0, tipCount = 0, pageTurn = 0;
        bool resume = false, hidden = false, hasTip = false;
        std::uint64_t tipId = LANG_ID_NONE, title = LANG_ID_NONE, body = LANG_ID_NONE;
        std::int32_t page = 0, pageCount = 0;
    };

    bool ReadTipsSEH(TipsInfo& t)
    {
        __try
        {
            if (gAddr.HudCommonDataManager_GetInstance)
            {
                const auto getInstance = reinterpret_cast<GetInstance_t>(ResolveGameAddress(gAddr.HudCommonDataManager_GetInstance));
                const auto* cdm = getInstance ? static_cast<const std::uint8_t*>(getInstance()) : nullptr;
                t.session = cdm && cdm[CDM_TIPS_STARTED] && !cdm[CDM_TIPS_ENDED];
            }
            const auto* ev = static_cast<const std::uint8_t*>(LoadingSplash_TipsEvent());
            if (!ev)
                return true;
            t.seen = true;
            t.state = ev[TIPS_STATE];
            t.category = ev[TIPS_CATEGORY];
            t.tipIndex = *reinterpret_cast<const std::uint16_t*>(ev + TIPS_INDEX);
            t.tipCount = *reinterpret_cast<const std::uint16_t*>(ev + TIPS_COUNT);
            t.pageTurn = ev[TIPS_PAGE_TURN];
            t.resume = ev[TIPS_RESUME] != 0;
            t.hidden = ev[TIPS_HIDDEN] != 0;
            void* table = *reinterpret_cast<void* const*>(ev + TIPS_TABLE);
            if (t.session && t.state == 2 && t.category == 4 && t.tipIndex < t.tipCount && table)
            {
                const auto getTipInfo = reinterpret_cast<GetTipInfo_t>((*static_cast<void***>(table))[2]);   // GetTipInfo, vtable +0x10
                getTipInfo(table, static_cast<std::uint8_t>(t.tipIndex), &t.tipId, &t.title, &t.body, &t.page, &t.pageCount);
                t.tipId &= ID48_MASK;   // ids are 48 bits; the game compares them masked too
                t.title &= ID48_MASK;
                t.body &= ID48_MASK;
                t.hasTip = true;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    struct GameOverInfo
    {
        int  open = -1;   // -1 unknown on this build
        bool seen = false;   // a game over menu ran since the game started
        int  state = 0, selected = 0, previous = 0, count = 0, type = 0;
        std::uint64_t items[4] = {};
    };

    bool ReadGameOverSEH(GameOverInfo& g)
    {
        __try
        {
            const auto* open = static_cast<const std::uint8_t*>(ResolveGameAddress(gAddr.GameOver_OpenFlag));
            if (open)
                g.open = *open ? 1 : 0;   // set by GameOver::StartGameOver, cleared when the menu ends
            void* vtable = nullptr;
            const auto* ev = static_cast<const std::uint8_t*>(GameOverSplash_GameOverEvent(&vtable));
            g.seen = ev != nullptr;
            if (!ev || g.open == 0)
            {
                g.open = 0;
                return true;
            }
            const void* current = *reinterpret_cast<void* const*>(ev);
            if (current != vtable || (open && current != ResolveGameAddress(gAddr.GameOverEvent_Vtable)))
            {
                g.open = open ? g.open : 0;
                return true;
            }
            g.state = *reinterpret_cast<const std::int32_t*>(ev + GO_STATE);
            g.selected = *reinterpret_cast<const std::int32_t*>(ev + GO_SELECTED);
            g.previous = *reinterpret_cast<const std::int32_t*>(ev + GO_PREVIOUS);
            g.type = *reinterpret_cast<const std::int32_t*>(ev + GO_TYPE);
            const std::uint32_t count = *reinterpret_cast<const std::uint32_t*>(ev + GO_COUNT);
            g.count = count > 4 ? 4 : static_cast<int>(count);
            for (int i = 0; i < g.count; ++i)
            {
                const auto* item = *reinterpret_cast<const std::uint8_t* const*>(ev + GO_ITEMS + i * sizeof(void*));
                g.items[i] = item ? *reinterpret_cast<const std::uint64_t*>(item + GO_ITEM_LANG) & ID48_MASK : 0;
            }
            if (g.open == -1)   // no open flag known on this build: the menu runs in states 1..9, 10 is its end
                g.open = (g.state >= 1 && g.state <= 9) ? 1 : 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    int ReadMapQuarkByteSEH(const std::uint8_t* collector, const void* mapVtable, std::uintptr_t offset)
    {
        __try
        {
            const auto* node = *reinterpret_cast<const std::uint8_t* const*>(collector + COLLECTOR_TAIL);
            for (int i = 0; node && i < MAX_MAP_QUARKS; ++i)
            {
                const auto* quark = *reinterpret_cast<const std::uint8_t* const*>(node + NODE_QUARK);
                if (quark && *reinterpret_cast<const void* const*>(quark) == mapVtable)
                    return quark[offset];
                node = *reinterpret_cast<const std::uint8_t* const*>(node + NODE_PREVIOUS);
            }
            return -1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }

    // a byte of the newest iDroid map, -1 when there is no iDroid map or the build is unknown
    int ReadMapQuarkByte(std::uintptr_t offset)
    {
        if (!gAddr.QuarkCollector_GetInstanceForCreatorId || !gAddr.LwLockSection_Enter || !gAddr.LwLockSection_Leave
            || !gAddr.MbDvcMapCallbackImpl_QuarkVtable)
            return -1;
        const auto getCollector = reinterpret_cast<GetQuarkCollector_t>(ResolveGameAddress(gAddr.QuarkCollector_GetInstanceForCreatorId));
        const auto enter = reinterpret_cast<LockSection_t>(ResolveGameAddress(gAddr.LwLockSection_Enter));
        const auto leave = reinterpret_cast<LockSection_t>(ResolveGameAddress(gAddr.LwLockSection_Leave));
        const void* mapVtable = ResolveGameAddress(gAddr.MbDvcMapCallbackImpl_QuarkVtable);
        void* holder = getCollector(MB_DVC_MAP_CALLBACK);
        auto* collector = holder ? *static_cast<std::uint8_t**>(holder) : nullptr;
        if (!collector)
            return -1;
        enter(collector + COLLECTOR_LOCK);
        const int value = ReadMapQuarkByteSEH(collector, mapVtable, offset);
        leave(collector + COLLECTOR_LOCK);
        return value;
    }

    struct MbStageObject
    {
        float x = 0, z = 0, turn = 0;
        std::uint8_t type = 0, cluster = 0, plant = 0, joined = 0;
        std::uint8_t buildings[4] = {NO_BUILDING, NO_BUILDING, NO_BUILDING, NO_BUILDING};
        float buildingTurns[4] = {};
    };

    template <typename T>
    T Slot(void* object, std::uintptr_t slot)
    {
        return reinterpret_cast<T>((*static_cast<void***>(object))[slot / sizeof(void*)]);
    }

    // The iDroid's location parameter, which holds the Mother Base stage the iDroid map draws from
    void* MbDvcMapLocationParameterSEH()
    {
        __try
        {
            if (!gAddr.Ui_UiCommonDataManagerGetInstance || !gAddr.MbDvcMapLocationParameterImpl_Vtable)
                return nullptr;
            const auto getInstance = reinterpret_cast<GetInstance_t>(ResolveGameAddress(gAddr.Ui_UiCommonDataManagerGetInstance));
            const auto* manager = getInstance ? static_cast<const std::uint8_t*>(getInstance()) : nullptr;
            const auto* mbMap = manager ? *reinterpret_cast<const std::uint8_t* const*>(manager + CDM_MB_MAP) : nullptr;
            void* parameter = mbMap ? *reinterpret_cast<void* const*>(mbMap + MB_MAP_LOCATION_PARAMETER) : nullptr;
            if (!parameter || *static_cast<void**>(parameter) != ResolveGameAddress(gAddr.MbDvcMapLocationParameterImpl_Vtable))
                return nullptr;
            return parameter;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    // false while the game is still loading the layout; asks it to load the one wanted, as the iDroid map does
    bool ReadMbStageSEH(void* parameter, std::uint16_t layoutCode, std::vector<MbStageObject>& objects)
    {
        __try
        {
            if (Slot<StageWord_t>(parameter, SLOT_MB_STAGE_LAYOUT)(parameter) != layoutCode)
            {
                std::int32_t error = 0;
                Slot<LoadMbStage_t>(parameter, SLOT_LOAD_MB_STAGE)(parameter, &error, layoutCode);
            }
            if (!Slot<StageFlag_t>(parameter, SLOT_MB_STAGE_LOADED)(parameter))
                return false;
            const std::uint32_t count = Slot<StageCount_t>(parameter, SLOT_OBJECT_COUNT)(parameter);
            for (std::uint32_t i = 0; i < count && i < MAX_MB_STAGE_OBJECTS; ++i)
            {
                MbStageObject o;
                o.x = Slot<StageFloat_t>(parameter, SLOT_OBJECT_POS_X)(parameter, i);
                o.z = Slot<StageFloat_t>(parameter, SLOT_OBJECT_POS_Z)(parameter, i);
                o.turn = Slot<StageFloat_t>(parameter, SLOT_OBJECT_ROT_Y)(parameter, i);
                o.type = Slot<StageByte_t>(parameter, SLOT_OBJECT_TYPE)(parameter, i);
                o.cluster = Slot<StageByte_t>(parameter, SLOT_OBJECT_CLUSTER)(parameter, i);
                o.plant = Slot<StageByte_t>(parameter, SLOT_OBJECT_PLANT)(parameter, i);
                o.joined = Slot<StageByte_t>(parameter, SLOT_OBJECT_JOINED)(parameter, i);
                Slot<StageBuildings_t>(parameter, SLOT_OBJECT_BUILDINGS)(parameter, i, o.buildings, o.buildingTurns);
                objects.push_back(o);
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void SetNumberField(lua_State* L, const char* key, double value)
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_pushnumber(L, value);
        g_lua_settable(L, -3);
    }

    void SetBoolField(lua_State* L, const char* key, bool value)
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_pushboolean(L, value ? 1 : 0);
        g_lua_settable(L, -3);
    }

    int __cdecl l_GetLoadingTipsInfo(lua_State* L)
    {
        TipsInfo t;
        if (!ReadTipsSEH(t))
            return 0;
        g_lua_createtable(L, 0, 14);
        SetBoolField(L, "session", t.session);
        SetBoolField(L, "seen", t.seen);
        SetNumberField(L, "state", t.state);
        SetNumberField(L, "tipIndex", t.tipIndex);
        SetNumberField(L, "tipCount", t.tipCount);
        SetNumberField(L, "pageTurn", t.pageTurn);
        SetBoolField(L, "resumePermitted", t.resume);
        SetBoolField(L, "hidden", t.hidden);
        if (t.hasTip)
        {
            SetNumberField(L, "tipId", static_cast<double>(t.tipId));
            if (t.title != LANG_ID_NONE)
                SetNumberField(L, "titleLangId", static_cast<double>(t.title));
            if (t.body != LANG_ID_NONE)
                SetNumberField(L, "bodyLangId", static_cast<double>(t.body));
            SetNumberField(L, "page", t.page);
            SetNumberField(L, "pageCount", t.pageCount);
        }
        return 1;
    }

    int __cdecl l_GetGameOverInfo(lua_State* L)
    {
        GameOverInfo g;
        if (!ReadGameOverSEH(g))
            return 0;
        g_lua_createtable(L, 0, 8);
        if (g.open >= 0)
            SetBoolField(L, "open", g.open == 1);
        SetBoolField(L, "seen", g.seen);
        SetNumberField(L, "state", g.state);
        SetNumberField(L, "selected", g.selected);
        SetNumberField(L, "previous", g.previous);
        SetNumberField(L, "count", g.count);
        SetNumberField(L, "type", g.type);
        g_lua_pushstring(L, const_cast<char*>("items"));
        g_lua_createtable(L, g.count, 0);
        for (int i = 0; i < g.count; ++i)
        {
            g_lua_pushnumber(L, static_cast<double>(i + 1));
            g_lua_pushnumber(L, static_cast<double>(g.items[i]));
            g_lua_settable(L, -3);
        }
        g_lua_settable(L, -3);
        return 1;
    }

    int __cdecl l_LoadResidentUiPack(lua_State* L)
    {
        const char* path = GetLuaString(L, 1);
        if (!path || !*path)
        {
            LogWarn("V_TppUiCommand.LoadResidentUiPack: no pack path given");
            return 0;
        }
        float size = (LuaType(L, 2) == LUA_TNUMBER) ? GetLuaNumber(L, 2) : 1024.0f * 1024.0f;
        if (size < 64.0f * 1024.0f)
            size = 64.0f * 1024.0f;
        if (size > 32.0f * 1024.0f * 1024.0f)
            size = 32.0f * 1024.0f * 1024.0f;
        g_lua_pushboolean(L, UixUtilityControl::LoadResidentUiPack(path, static_cast<std::uint32_t>(size)) ? 1 : 0);
        return 1;
    }

    int __cdecl l_IsResidentUiPackLoaded(lua_State* L)
    {
        const char* path = GetLuaString(L, 1);
        g_lua_pushboolean(L, path && *path && UixUtilityControl::IsResidentUiPackLoaded(path) ? 1 : 0);
        return 1;
    }

    int __cdecl l_IsMbMapPhotoRealMode(lua_State* L)
    {
        const int photoReal = ReadMapQuarkByte(MAP_PHOTO_REAL);
        if (photoReal < 0)
            return 0;
        g_lua_pushboolean(L, photoReal != 0);
        return 1;
    }

    int __cdecl l_IsMbMapNorthUpMode(lua_State* L)
    {
        const int viewMode = ReadMapQuarkByte(MAP_VIEW_MODE);
        if (viewMode < 0)
            return 0;
        g_lua_pushboolean(L, viewMode == 0);
        return 1;
    }

    // {x, z, turn, type, cluster, plant, joined = {[edge] = true}, buildings = {[slot] = {id, turn}}}, turns in radians
    int __cdecl l_GetMbStageObjects(lua_State* L)
    {
        if (MissionCodeGuard::ShouldBypassHooks() || LuaType(L, 1) != LUA_TNUMBER)
            return 0;
        void* parameter = MbDvcMapLocationParameterSEH();
        std::vector<MbStageObject> objects;
        if (!parameter || !ReadMbStageSEH(parameter, static_cast<std::uint16_t>(GetLuaInt(L, 1)), objects) || objects.empty())
            return 0;
        g_lua_createtable(L, static_cast<int>(objects.size()), 0);
        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            const MbStageObject& o = objects[i];
            g_lua_pushnumber(L, static_cast<double>(i + 1));
            g_lua_createtable(L, 0, 8);
            SetNumberField(L, "x", o.x);
            SetNumberField(L, "z", o.z);
            SetNumberField(L, "turn", o.turn);
            SetNumberField(L, "type", o.type);
            SetNumberField(L, "cluster", o.cluster);
            SetNumberField(L, "plant", o.plant);
            g_lua_pushstring(L, const_cast<char*>("joined"));
            g_lua_createtable(L, 0, 8);
            for (int edge = 0; edge < 8; ++edge)
            {
                if (o.joined & (1 << edge))
                {
                    g_lua_pushnumber(L, edge);
                    g_lua_pushboolean(L, 1);
                    g_lua_settable(L, -3);
                }
            }
            g_lua_settable(L, -3);
            g_lua_pushstring(L, const_cast<char*>("buildings"));
            g_lua_createtable(L, 4, 0);
            for (int slot = 0; slot < 4; ++slot)
            {
                if (o.buildings[slot] != NO_BUILDING)
                {
                    g_lua_pushnumber(L, slot + 1);
                    g_lua_createtable(L, 0, 2);
                    SetNumberField(L, "id", o.buildings[slot]);
                    SetNumberField(L, "turn", o.buildingTurns[slot]);
                    g_lua_settable(L, -3);
                }
            }
            g_lua_settable(L, -3);
            g_lua_settable(L, -3);
        }
        return 1;
    }

    // every UI marker the iDroid map can draw: {slot, gameObjectId, markerType, markerFlag, x, y, z, view*, important, questImportant, radiusLevel, radius}
    int __cdecl l_GetMapMarkerSlots(lua_State* L)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return 0;
        UixUtilityControl::UiMarkerSlot slots[128];
        const int count = UixUtilityControl::GetUiMarkerSlots(slots, 128);
        if (count < 0)
            return 0;
        g_lua_createtable(L, count, 0);
        for (int i = 0; i < count; ++i)
        {
            const UixUtilityControl::UiMarkerSlot& s = slots[i];
            g_lua_pushnumber(L, static_cast<double>(i + 1));
            g_lua_createtable(L, 0, 16);
            SetNumberField(L, "slot", s.slot);
            SetNumberField(L, "gameObjectId", s.gameObjectId);
            SetNumberField(L, "markerType", s.markerFlag & 0xFF);
            SetNumberField(L, "markerFlag", s.markerFlag);
            SetNumberField(L, "x", s.x);
            SetNumberField(L, "y", s.y);
            SetNumberField(L, "z", s.z);
            SetBoolField(L, "viewMapIcon", s.viewMapIcon);
            SetBoolField(L, "viewWorldIcon", s.viewWorldIcon);
            SetBoolField(L, "viewMapGoal", s.viewMapGoal);
            SetBoolField(L, "viewWorldGoal", s.viewWorldGoal);
            SetBoolField(L, "important", s.important);
            SetBoolField(L, "questImportant", s.questImportant);
            SetNumberField(L, "radiusLevel", s.radiusLevel);
            SetNumberField(L, "radius", s.radius);
            g_lua_settable(L, -3);
        }
        return 1;
    }

    int TableInt(lua_State* L, const char* key, int fallback)
    {
        LuaGetField(L, 1, key);
        const int value = LuaIsNumber(L, -1) ? GetLuaInt(L, -1) : fallback;
        LuaPop(L, 1);
        return value;
    }

    float TableNumber(lua_State* L, const char* key, float fallback)
    {
        LuaGetField(L, 1, key);
        const float value = LuaIsNumber(L, -1) ? GetLuaNumber(L, -1) : fallback;
        LuaPop(L, 1);
        return value;
    }

    std::string TableString(lua_State* L, const char* key)
    {
        LuaGetField(L, 1, key);
        const char* value = LuaIsString(L, -1) ? GetLuaString(L, -1) : nullptr;
        std::string text = value ? value : "";
        LuaPop(L, 1);
        return text;
    }

    int __cdecl l_SetLocationIcon(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("V_TppUiCommand.SetLocationIcon: needs a table {locationId=..., texturePath=... or iconLocationId=...}, icon not set");
            PushLuaBool(L, false);
            return 1;
        }
        const int locationId = TableInt(L, "locationId", 0);
        const std::string texturePath = TableString(L, "texturePath");
        bool ok = false;
        if (!texturePath.empty())
            ok = Set_LocationIconTexture(locationId, texturePath.c_str(), TableNumber(L, "u", 0.0f), TableNumber(L, "v", 0.0f));
        else
            ok = Set_LocationIconVanilla(locationId, TableInt(L, "iconLocationId", 0));
        PushLuaBool(L, ok);
        return 1;
    }

    int __cdecl l_AddMissionListLocationTab(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TTABLE)
        {
            LogWarn("V_TppUiCommand.AddMissionListLocationTab: needs a table {locationId=..., nameLangId=...}, tab not added");
            PushLuaBool(L, false);
            return 1;
        }
        const int locationId = TableInt(L, "locationId", 0);
        const std::string nameLangId = TableString(L, "nameLangId");
        PushLuaBool(L, Add_MissionListLocationTab(locationId, nameLangId.c_str()));
        return 1;
    }

    int __cdecl l_SetMissionListLocationTabVisible(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_TNUMBER || LuaType(L, 2) != LUA_TBOOLEAN)
        {
            LogWarn("V_TppUiCommand.SetMissionListLocationTabVisible: needs (locationId, true/false), tab unchanged");
            return 0;
        }
        Set_MissionListLocationTabVisible(GetLuaInt(L, 1), GetLuaBool(L, 2));
        return 0;
    }

    static luaL_Reg g_VTppUiCommandLib[] =
    {
        { "SetDefaultEquipBgTexturePath",             l_SetDefaultEquipBgTexturePath },
        { "ClearDefaultEquipBgTexture",               l_ClearDefaultEquipBgTexture },
        { "SetEquipBgTexturePath",                    l_SetEquipBgTexturePath },
        { "ClearEquipBgTexture",                      l_ClearEquipBgTexture },
        { "SetEnemyWeaponBgTexturePath",              l_SetEnemyWeaponBgTexturePath },
        { "ClearEnemyWeaponBgTexture",                l_ClearEnemyWeaponBgTexture },
        { "SetEnemyEquipBgTexturePath",               l_SetEnemyEquipBgTexturePath },
        { "ClearEnemyEquipBgTexture",                 l_ClearEnemyEquipBgTexture },
        { "ClearAllEquipBgTextures",                  l_ClearAllEquipBgTextures },
        { "SetLoadingSplashMainTexturePath",          l_SetLoadingSplashMainTexturePath },
        { "SetLoadingSplashBlurTexturePath",          l_SetLoadingSplashBlurTexturePath },
        { "ClearLoadingSplashTextures",               l_ClearLoadingSplashTextures },
        { "SetGameOverSplashMainTexturePath",         l_SetGameOverSplashMainTexturePath },
        { "SetGameOverSplashBlurTexturePath",         l_SetGameOverSplashBlurTexturePath },
        { "ClearGameOverSplashTextures",              l_ClearGameOverSplashTextures },
        { "SetBonusScreenPopupTexturePath",           l_SetBonusScreenPopupTexturePath },
        { "SetReportScreenPopupTexturePath",          l_SetReportScreenPopupTexturePath },
        { "ClearBonusPopupTextures",                  l_ClearBonusPopupTextures },
        { "SetMissionTelopSplashTexturePath",         l_SetMissionTelopSplashTexturePath },
        { "UnsetMissionTelopSplashTexturePath",       l_UnsetMissionTelopSplashTexturePath },

        { "SetEquipIconFtexPath",                     l_SetEquipIconFtexPath },
        { "ClearIconFtexPath",                        l_ClearIconFtexPath },
        { "ClearAllIconFtexPaths",                    l_ClearAllIconFtexPaths },
        { "SetMissionEmergency",                      l_SetMissionEmergency },
        { "IsMissionEmergency",                       l_IsMissionEmergency },
        { "SetEmergencyMissionPopup",                 l_SetEmergencyMissionPopup },
        { "SetEmergencyMissionPopupLangId",           l_SetEmergencyMissionPopupLangId },
        { "ClearEmergencyMissionPopupOverride",       l_ClearEmergencyMissionPopupOverride },
        { "ShowMissionIcon",                          l_ShowMissionIcon },
        { "ShowTimeCigaretteUi",                      l_ShowTimeCigaretteUi },
        { "HideTimeCigaretteUi",                      l_HideTimeCigaretteUi },
        { "ShowMbDvcAnnouncePopupReport",             l_ShowMbDvcAnnouncePopupReport },
        { "ShowMbDvcAnnouncePopupReportLangId",       l_ShowMbDvcAnnouncePopupReportLangId },
        { "ShowMbDvcAnnouncePopupReward",             l_ShowMbDvcAnnouncePopupReward },
        { "ShowMbDvcAnnouncePopupRewardLangId",       l_ShowMbDvcAnnouncePopupRewardLangId },

        { "SetEnemyInformationLangId",                l_SetEnemyInformationLangId },
        { "SetEnemyUnitName",                         l_SetEnemyUnitName },

        { "SetEnemyInformationLangIdForSoldier",      l_SetEnemyInformationLangIdForSoldier },
        { "SetEnemyUnitNameForSoldier",               l_SetEnemyUnitNameForSoldier },

        { "SetAnnounceLogSE",                         l_SetAnnounceLogSE },
        { "RegisterAnnounceLogSfx",                   l_RegisterAnnounceLogSfx },
        { "UnsetAnnounceLogSE",                       l_UnsetAnnounceLogSE },
        { "UnregisterAnnounceLogSfx",                 l_UnregisterAnnounceLogSfx },
        { "SetAnnounceLogTypingSpeed",                l_SetAnnounceLogTypingSpeed },
        { "SetAnnounceLogHoldTime",                   l_SetAnnounceLogHoldTime },
        { "SetAnnounceLogPriority",                   l_SetAnnounceLogPriority },
        { "RequestMbSoundControllerDialogue",         l_RequestMbSoundControllerDialogue },

        { "SetMissionAcceptWarning",                  l_SetMissionAcceptWarning },

        { "SetMissionMenuHelp",                       l_SetMissionMenuHelp },
        { "ClearMissionMenuHelp",                     l_ClearMissionMenuHelp },
        { "SetEquipLangInfo",                         l_SetEquipLangInfo },

        { "LoadResidentUiPack",                       l_LoadResidentUiPack },
        { "IsResidentUiPackLoaded",                   l_IsResidentUiPackLoaded },
        { "GetLoadingTipsInfo",                       l_GetLoadingTipsInfo },
        { "GetGameOverInfo",                          l_GetGameOverInfo },
        { "IsMbMapPhotoRealMode",                     l_IsMbMapPhotoRealMode },
        { "IsMbMapNorthUpMode",                       l_IsMbMapNorthUpMode },
        { "GetMapMarkerSlots",                        l_GetMapMarkerSlots },
        { "GetMbStageObjects",                        l_GetMbStageObjects },

        { "SetLocationIcon",                          l_SetLocationIcon },
        { "AddMissionListLocationTab",                l_AddMissionListLocationTab },
        { "SetMissionListLocationTabVisible",         l_SetMissionListLocationTabVisible },

        { nullptr,          nullptr }
    };
}

bool Register_V_TppUiCommandLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_TppUiCommand", g_VTppUiCommandLib);
}
