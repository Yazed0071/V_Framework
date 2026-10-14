#include "pch.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "LuaApi.h"
#include "log.h"
#include "FoxHashes.h"
#include "MissionCodeGuard.h"
#include "V_UixUtilityLib.h"

#include "../hooks/ui/UixUtilityControl.h"
#include "../hooks/ui/UiPadLock.h"
#include "../hooks/ui/UixUtilityDev.h"

// V_UixUtility: the dev team's fox::uix::UixUtility for Lua. Every function keeps the dev name and argument order;
// pointers become handles (numbers) from FindWindow / GetLayout / GetModel / GetModelNode* / GetAnimation.
namespace
{
    using namespace UixUtilityControl;

    constexpr int LUA_T_NIL     = 0;
    constexpr int LUA_T_NUMBER  = 3;
    constexpr int LUA_T_STRING  = 4;
    constexpr int LUA_T_TABLE   = 5;

    constexpr std::uint64_t ID48_MASK = 0xFFFFFFFFFFFFull;

    struct alignas(16) V4 { float v[4]; };

    const std::initializer_list<Kind> NODES      = { Kind::NodeCommon, Kind::NodeText, Kind::NodeMesh, Kind::NodeLine };
    const std::initializer_list<Kind> COMPONENTS = { Kind::Layout, Kind::Model };
    const std::initializer_list<Kind> SCENE      = { Kind::NodeCommon, Kind::NodeText, Kind::NodeMesh, Kind::NodeLine, Kind::Layout, Kind::Model };

    bool IsNode(Kind k) { return k == Kind::NodeCommon || k == Kind::NodeText || k == Kind::NodeMesh || k == Kind::NodeLine; }

    // ---- calling the game's UixUtility ----

    template <typename R, typename... A>
    R Call(std::size_t devOffset, A... args)
    {
        using F = R(__fastcall*)(void**, A...);
        return reinterpret_cast<F>(Method(devOffset))(Utility(), args...);
    }

    // node and component overloads sit next to each other in the dev vtable, node first
    std::size_t Overload(std::size_t nodeOffset, const Object& o)
    {
        return IsNode(o.kind) ? nodeOffset : nodeOffset + sizeof(void*);
    }

    // ---- Lua arguments ----

    bool Active()
    {
        return IsSupportedOnThisBuild() && !MissionCodeGuard::ShouldBypassHooks();
    }

    bool Present(lua_State* L, int idx)
    {
        const int type = LuaType(L, idx);
        return type != LUA_T_NIL && type != -1;
    }

    float Num(lua_State* L, int idx, float fallback = 0.0f)
    {
        return LuaType(L, idx) == LUA_T_NUMBER ? GetLuaNumber(L, idx) : fallback;
    }

    std::uint64_t Name64(lua_State* L, int idx)
    {
        const int type = LuaType(L, idx);
        if (type == LUA_T_NUMBER)
            return GetLuaInt64(L, idx);
        if (type != LUA_T_STRING)
            return 0;
        const char* s = GetLuaString(L, idx);
        if (s && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
            return std::strtoull(s + 2, nullptr, 16);
        return s && *s ? FoxHashes::StrCode64(s) : 0;
    }

    std::uint64_t Name(lua_State* L, int idx)
    {
        return static_cast<std::uint32_t>(Name64(L, idx));
    }

    std::shared_ptr<std::string> Text(lua_State* L, int idx)
    {
        const char* s = LuaType(L, idx) == LUA_T_STRING ? GetLuaString(L, idx) : nullptr;
        return std::make_shared<std::string>(s ? s : "");
    }

    bool KindIn(Kind k, std::initializer_list<Kind> kinds)
    {
        for (Kind x : kinds)
            if (x == k)
                return true;
        return false;
    }

    bool Arg(lua_State* L, int idx, const char* fn, std::initializer_list<Kind> kinds, std::uint32_t& handle, Object& object)
    {
        if (LuaType(L, idx) != LUA_T_NUMBER)
        {
            LogWarnOnce("V_UixUtility.%s: argument %d must be a handle", fn, idx);
            return false;
        }
        handle = static_cast<std::uint32_t>(GetLuaInt64(L, idx));
        if (!Lookup(handle, object))
            return false;   // its window is gone; a dev pointer would dangle here, the handle just says no
        if (!KindIn(object.kind, kinds))
        {
            LogWarnOnce("V_UixUtility.%s: argument %d is the wrong kind of handle", fn, idx);
            return false;
        }
        return true;
    }

    bool Arg(lua_State* L, int idx, const char* fn, std::initializer_list<Kind> kinds, Object& object)
    {
        std::uint32_t handle = 0;
        return Arg(L, idx, fn, kinds, handle, object);
    }

    int PushHandle(lua_State* L, std::uint32_t handle)
    {
        if (handle)
            g_lua_pushnumber(L, static_cast<double>(handle));
        else
            g_lua_pushnil(L);
        return 1;
    }

    int PushV(lua_State* L, const V4& v, int n)
    {
        for (int i = 0; i < n; ++i)
            g_lua_pushnumber(L, v.v[i]);
        return n;
    }

    template <typename F>
    bool Now(F f)
    {
        return Guarded(f);
    }

    // ---- windows ----

    int __cdecl l_FindWindow(lua_State* L)
    {
        const std::uint64_t name = Name64(L, 1);
        if (!Active() || !name)
            return 0;
        void* window = nullptr;
        Now([&]() { window = Call<void*>(dev::FindWindow, name & ID48_MASK); });
        return PushHandle(L, HandleFor(Kind::Window, window, window));
    }

    int __cdecl l_GetChildWindowInterface(lua_State* L)
    {
        Object w;
        if (!Active() || !Arg(L, 1, "GetChildWindowInterface", { Kind::Window }, w))
            return 0;
        const std::uint64_t name = Name64(L, 2) & ID48_MASK;
        void* child = nullptr;
        Now([&]() { child = Call<void*>(dev::GetChildWindowInterface, w.ptr, name); });
        return PushHandle(L, HandleFor(Kind::Window, child, child));
    }

    // The window messages are queued like the setters: they run on the UI thread in the window's next update, where the
    // window is known to be alive (a call made right away could reach a window the game is deleting).
    int WindowMessage(lua_State* L, const char* fn, std::size_t offset)
    {
        std::uint32_t handle = 0;
        Object w;
        if (Active() && Arg(L, 1, fn, { Kind::Window }, handle, w))
            Queue(handle, [offset](const Object& t) { Call<void>(offset, t.ptr); });
        return 0;
    }

    int __cdecl l_PostShowAndStartMessage(lua_State* L) { return WindowMessage(L, "PostShowAndStartMessage", dev::PostShowAndStartMessage); }
    int __cdecl l_PostHideAndStopMessage(lua_State* L)  { return WindowMessage(L, "PostHideAndStopMessage", dev::PostHideAndStopMessage); }
    int __cdecl l_PostWakeLayoutMessage(lua_State* L)   { return WindowMessage(L, "PostWakeLayoutMessage", dev::PostWakeLayoutMessage); }
    int __cdecl l_PostSleepLayoutMessage(lua_State* L)  { return WindowMessage(L, "PostSleepLayoutMessage", dev::PostSleepLayoutMessage); }

    int __cdecl l_SendVisibleWindowMessage(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object w;
        if (!Active() || !Arg(L, 1, "SendVisibleWindowMessage", { Kind::Window }, handle, w))
            return 0;
        const bool visible = GetLuaBool(L, 2);
        Queue(handle, [visible](const Object& t) { Call<void>(dev::SendVisibleWindowMessage, t.ptr, visible); });
        return 0;
    }

    int Trigger(lua_State* L, const char* fn, std::size_t offset)
    {
        std::uint32_t handle = 0;
        Object w;
        if (!Active() || !Arg(L, 1, fn, { Kind::Window }, handle, w))
            return 0;
        const std::uint64_t trigger = Name64(L, 2) & ID48_MASK;
        const std::uint64_t param = Present(L, 3) ? Name64(L, 3) : 0;
        Queue(handle, [offset, trigger, param](const Object& t) { Call<void>(offset, t.ptr, trigger, param); });
        return 0;
    }

    int __cdecl l_SendTrigger(lua_State* L)         { return Trigger(L, "SendTrigger", dev::SendTrigger); }
    int __cdecl l_SendTriggerToParent(lua_State* L) { return Trigger(L, "SendTriggerToParent", dev::SendTriggerToParent); }

    int __cdecl l_SendConnectWindowMessage(lua_State* L)
    {
        std::uint32_t handle = 0, modelHandle = 0, nodeHandle = 0;
        Object w, model, node;
        if (!Active() || !Arg(L, 1, "SendConnectWindowMessage", { Kind::Window }, handle, w)
            || !Arg(L, 2, "SendConnectWindowMessage", { Kind::Model }, modelHandle, model)
            || !Arg(L, 3, "SendConnectWindowMessage", NODES, nodeHandle, node))
            return 0;
        Queue(handle, [modelHandle, nodeHandle](const Object& t) {
            Object m, n;   // looked up again: their windows may have gone since
            if (Lookup(modelHandle, m) && Lookup(nodeHandle, n))
                Call<void>(dev::SendConnectWindowMessage, t.ptr, m.ptr, n.ptr);
        });
        return 0;
    }

    int __cdecl l_GetTriggerPool(lua_State* L)
    {
        Object w;
        if (!Active() || !Arg(L, 1, "GetTriggerPool", { Kind::Window }, w))
            return 0;
        void* pool = nullptr;
        Now([&]() { pool = Call<void*>(dev::GetTriggerPool, w.ptr); });
        return PushHandle(L, HandleFor(Kind::TriggerPool, pool, w.window));
    }

    int __cdecl l_IsTriggerReceived(lua_State* L)
    {
        Object pool;
        if (!Active() || !Arg(L, 1, "IsTriggerReceived", { Kind::TriggerPool }, pool))
            return 0;
        const std::uint64_t trigger = Name64(L, 2) & ID48_MASK;
        std::uint64_t param = 0;
        bool received = false;
        Now([&]() { received = Call<bool>(dev::IsTriggerReceived, pool.ptr, trigger, &param); });
        g_lua_pushboolean(L, received);
        g_lua_pushnumber(L, static_cast<double>(param));
        return 2;
    }

    int Inheritance(lua_State* L, const char* fn, std::size_t offset)
    {
        std::uint32_t handle = 0;
        Object w;
        if (Active() && Arg(L, 1, fn, { Kind::Window }, handle, w))
            Queue(handle, [offset](const Object& o) { Call<void>(offset, o.ptr); });
        return 0;
    }

    constexpr char kEnableAll[] = "EnableAllInheritance", kEnableScale[] = "EnableScaleInheritance", kEnableRotation[] = "EnableRotationInheritance";
    constexpr char kEnableTranslation[] = "EnableTranslationInheritance", kEnableColorRGB[] = "EnableColorRGBInheritance", kEnableColorAlpha[] = "EnableColorAlphaInheritance";
    constexpr char kDisableAll[] = "DisableAllInheritance", kDisableScale[] = "DisableScaleInheritance", kDisableRotation[] = "DisableRotationInheritance";
    constexpr char kDisableTranslation[] = "DisableTranslationInheritance", kDisableColorRGB[] = "DisableColorRGBInheritance", kDisableColorAlpha[] = "DisableColorAlphaInheritance";

    // the dev vtable lists All, Scale, Rotation, Translation, ColorRGB, ColorAlpha for Enable and again for Disable
    template <std::size_t Offset, const char* Fn>
    int __cdecl l_Inheritance(lua_State* L) { return Inheritance(L, Fn, Offset); }

    int __cdecl l_CreateWindow(lua_State* L)
    {
        if (!Active())
            return 0;
        if (LuaType(L, 1) != LUA_T_TABLE)
        {
            LogWarnOnce("V_UixUtility.CreateWindow: argument must be a table {name=..., layouts={...}} or {name=..., graph=...}");
            return 0;
        }
        LuaGetField(L, 1, "name");
        const std::uint64_t name = Name64(L, -1);
        LuaPop(L, 1);
        if (!name)
        {
            LogWarnOnce("V_UixUtility.CreateWindow: name is missing");
            return 0;
        }

        LuaGetField(L, 1, "group");
        const float group = Num(L, -1, 0.0f);
        LuaPop(L, 1);
        if (group != 0.0f && group != 1.0f)
        {
            LogWarnOnce("V_UixUtility.CreateWindow: group must be 0 or 1, window not created");
            return 0;
        }
        LuaGetField(L, 1, "pauseMask");
        const std::uint32_t pauseMask = LuaType(L, -1) == LUA_T_NUMBER ? static_cast<std::uint32_t>(GetLuaInt64(L, -1)) : 2u;
        LuaPop(L, 1);
        LuaGetField(L, 1, "priority");
        const float priorityValue = Num(L, -1, 0.0f);
        LuaPop(L, 1);
        const std::uint16_t priority = static_cast<std::uint16_t>(priorityValue < 0.0f ? 0.0f : (priorityValue > 65535.0f ? 65535.0f : priorityValue));

        LuaGetField(L, 1, "graph");
        const int graphType = LuaType(L, -1);
        const std::string graph = graphType == LUA_T_STRING ? FoxHashes::NormalizeAssetPath(GetLuaString(L, -1)) : std::string();
        LuaPop(L, 1);
        if (graphType != LUA_T_NIL && graphType != -1 && graphType != LUA_T_STRING)
        {
            LogWarnOnce("V_UixUtility.CreateWindow: graph must be a .uigb path");
            return 0;
        }
        if (!graph.empty())
        {
            CreateGraphWindow(name, graph, static_cast<std::uint32_t>(group), pauseMask, priority);
            return 0;
        }

        std::vector<LayoutRecord> layouts;
        LuaGetField(L, 1, "layouts");
        if (LuaType(L, -1) == LUA_T_TABLE)
        {
            const int count = static_cast<int>(LuaObjLen(L, -1));
            for (int i = 1; i <= count && layouts.size() < 255; ++i)
            {
                LuaRawGetI(L, -1, i);
                const int rec = GetLuaTop(L);
                if (LuaType(L, rec) == LUA_T_TABLE)
                {
                    LayoutRecord r;
                    LuaGetField(L, rec, "path");
                    if (LuaType(L, -1) == LUA_T_STRING)
                        r.path = FoxHashes::NormalizeAssetPath(GetLuaString(L, -1));
                    LuaPop(L, 1);
                    LuaGetField(L, rec, "id");
                    r.id = Name64(L, -1);
                    LuaPop(L, 1);
                    LuaGetField(L, rec, "drawPriority");
                    r.drawPriority = Present(L, -1) ? Name64(L, -1) : FoxHashes::StrCode64("__HUD");
                    LuaPop(L, 1);
                    LuaGetField(L, rec, "fontType");
                    r.fontType = LuaType(L, -1) == LUA_T_NUMBER ? GetLuaInt(L, -1) : 0;
                    LuaPop(L, 1);
                    if (r.path.empty() || !r.id)
                        LogWarnOnce("V_UixUtility.CreateWindow: layouts[%d] needs path and id, skipped", i);
                    else
                        layouts.push_back(r);
                }
                LuaPop(L, 1);
            }
        }
        LuaPop(L, 1);
        if (layouts.empty())
        {
            LogWarnOnce("V_UixUtility.CreateWindow: no layouts or graph given");
            return 0;
        }
        CreateUiWindow(name, layouts, static_cast<std::uint32_t>(group), pauseMask, priority);
        return 0;
    }

    int __cdecl l_DestroyWindow(lua_State* L)
    {
        Object w;
        if (Active() && Arg(L, 1, "DestroyWindow", { Kind::Window }, w))
            DestroyUiWindow(w);
        return 0;
    }

    // ---- the tree ----

    int __cdecl l_GetLayout(lua_State* L)
    {
        Object parent;
        if (!Active() || !Arg(L, 1, "GetLayout", { Kind::Window, Kind::Layout }, parent))
            return 0;
        const std::uint64_t name = Name(L, 2);
        void* layout = nullptr;
        const std::size_t offset = parent.kind == Kind::Window ? dev::GetLayoutOfWindow : dev::GetLayoutOfLayout;
        Now([&]() { layout = Call<void*>(offset, parent.ptr, name); });
        return PushHandle(L, HandleFor(Kind::Layout, layout, parent.window));
    }

    int __cdecl l_IsHaveLayout(lua_State* L)
    {
        Object layout;
        if (!Active() || !Arg(L, 1, "IsHaveLayout", { Kind::Layout }, layout))
            return 0;
        const std::uint64_t name = Name(L, 2);
        bool have = false;
        Now([&]() { have = Call<bool>(dev::IsHaveLayout, layout.ptr, name); });
        g_lua_pushboolean(L, have);
        return 1;
    }

    int __cdecl l_GetModel(lua_State* L)
    {
        Object layout;
        if (!Active() || !Arg(L, 1, "GetModel", { Kind::Layout }, layout))
            return 0;
        const std::uint64_t name = Name(L, 2);
        void* model = nullptr;
        Now([&]() { model = Call<void*>(dev::GetModel, layout.ptr, name); });
        return PushHandle(L, HandleFor(Kind::Model, model, layout.window));
    }

    int __cdecl l_GetAnimation(lua_State* L)
    {
        Object parent;
        if (!Active() || !Arg(L, 1, "GetAnimation", COMPONENTS, parent))
            return 0;
        const std::uint64_t name = Name(L, 2);
        void* animation = nullptr;
        const std::size_t offset = parent.kind == Kind::Model ? dev::GetAnimationOfModel : dev::GetAnimationOfLayout;
        Now([&]() { animation = Call<void*>(offset, parent.ptr, name); });
        return PushHandle(L, HandleFor(Kind::Animation, animation, parent.window));
    }

    int __cdecl l_IsHaveModelNodeCommon(lua_State* L)
    {
        Object model;
        if (!Active() || !Arg(L, 1, "IsHaveModelNodeCommon", { Kind::Model }, model))
            return 0;
        const std::uint64_t name = Name(L, 2);
        bool have = false;
        Now([&]() { have = Call<bool>(dev::IsHaveModelNodeCommon, model.ptr, name); });
        g_lua_pushboolean(L, have);
        return 1;
    }

    int GetNode(lua_State* L, const char* fn, std::size_t offset, Kind kind)
    {
        Object model;
        if (!Active() || !Arg(L, 1, fn, { Kind::Model }, model))
            return 0;
        const std::uint64_t name = Name(L, 2);
        void* node = nullptr;
        Now([&]() { node = Call<void*>(offset, model.ptr, name); });
        return PushHandle(L, HandleFor(kind, node, model.window, model.ptr));
    }

    int __cdecl l_GetModelNodeCommon(lua_State* L) { return GetNode(L, "GetModelNodeCommon", dev::GetModelNodeCommon, Kind::NodeCommon); }
    int __cdecl l_GetModelNodeText(lua_State* L)   { return GetNode(L, "GetModelNodeText", dev::GetModelNodeText, Kind::NodeText); }
    int __cdecl l_GetModelNodeMesh(lua_State* L)   { return GetNode(L, "GetModelNodeMesh", dev::GetModelNodeMesh, Kind::NodeMesh); }
    int __cdecl l_GetModelNodeLine(lua_State* L)   { return GetNode(L, "GetModelNodeLine", dev::GetModelNodeLine, Kind::NodeLine); }

    int __cdecl l_GetOrthogonalProjectionScaleFromModelComponentCamera(lua_State* L)
    {
        Object model;
        if (!Active() || !Arg(L, 1, "GetOrthogonalProjectionScaleFromModelComponentCamera", { Kind::Model }, model))
            return 0;
        float scale = 0.0f;
        Now([&]() { scale = Call<float>(dev::GetOrthogonalProjectionScale, model.ptr); });
        g_lua_pushnumber(L, scale);
        return 1;
    }

    // ---- nodes and components ----

    int __cdecl l_SetVisible(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "SetVisible", SCENE, handle, o))
            return 0;
        const bool visible = GetLuaBool(L, 2);
        Queue(handle, [visible](const Object& x) { Call<void>(Overload(dev::SetVisible, x), x.ptr, visible); });
        return 0;
    }

    int __cdecl l_IsVisible(lua_State* L)
    {
        Object o;
        if (!Active() || !Arg(L, 1, "IsVisible", SCENE, o))
            return 0;
        bool visible = false;
        Now([&]() { visible = Call<bool>(Overload(dev::IsVisible, o), o.ptr); });
        g_lua_pushboolean(L, visible);
        return 1;
    }

    int __cdecl l_SetAlpha(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "SetAlpha", SCENE, handle, o))
            return 0;
        const float alpha = Num(L, 2, 1.0f);
        Queue(handle, [alpha](const Object& x) { Call<void>(Overload(dev::SetAlpha, x), x.ptr, alpha); });
        return 0;
    }

    int __cdecl l_GetAlpha(lua_State* L)
    {
        Object o;
        if (!Active() || !Arg(L, 1, "GetAlpha", SCENE, o))
            return 0;
        float alpha = 0.0f;
        Now([&]() { alpha = Call<float>(Overload(dev::GetAlpha, o), o.ptr); });
        g_lua_pushnumber(L, alpha);
        return 1;
    }

    int __cdecl l_SetColorRGB(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "SetColorRGB", SCENE, handle, o))
            return 0;
        const float r = Num(L, 2, 1.0f), g = Num(L, 3, 1.0f), b = Num(L, 4, 1.0f);
        Queue(handle, [r, g, b](const Object& x) { Call<void>(Overload(dev::SetColorRGB, x), x.ptr, r, g, b); });
        return 0;
    }

    int __cdecl l_SetColorGroup(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "SetColorGroup", SCENE, handle, o))
            return 0;
        const std::uint32_t group = static_cast<std::uint32_t>(Name(L, 2));
        Queue(handle, [group](const Object& x) { Call<void>(Overload(dev::SetColorGroup, x), x.ptr, group); });
        return 0;
    }

    int __cdecl l_GetColorGroup(lua_State* L)
    {
        if (!Active())
            return 0;
        const std::uint32_t group = static_cast<std::uint32_t>(Name(L, 1));
        V4 color{};
        Now([&]() { Call<void*>(dev::GetColorGroup, &color, group); });
        return PushV(L, color, 4);
    }

    int GetVector(lua_State* L, const char* fn, std::size_t nodeOffset)
    {
        Object o;
        if (!Active() || !Arg(L, 1, fn, SCENE, o))
            return 0;
        V4 v{};
        Now([&]() { Call<void*>(Overload(nodeOffset, o), &v, o.ptr); });
        return PushV(L, v, 3);
    }

    int __cdecl l_GetTranslation(lua_State* L)    { return GetVector(L, "GetTranslation", dev::GetTranslation); }
    int __cdecl l_GetRotation(lua_State* L)       { return GetVector(L, "GetRotation", dev::GetRotation); }
    int __cdecl l_GetRotationDegree(lua_State* L) { return GetVector(L, "GetRotationDegree", dev::GetRotationDegree); }
    int __cdecl l_GetScale(lua_State* L)          { return GetVector(L, "GetScale", dev::GetScale); }

    int SetVector(lua_State* L, const char* fn, std::size_t nodeOffset)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, fn, SCENE, handle, o))
            return 0;
        const float x = Num(L, 2), y = Num(L, 3), z = Num(L, 4);
        Queue(handle, [nodeOffset, x, y, z](const Object& t) {
            V4 v{ { x, y, z, 0.0f } };
            Call<void>(Overload(nodeOffset, t), t.ptr, &v);
        });
        return 0;
    }

    int __cdecl l_SetTranslation(lua_State* L)    { return SetVector(L, "SetTranslation", dev::SetTranslation); }
    int __cdecl l_SetRotation(lua_State* L)       { return SetVector(L, "SetRotation", dev::SetRotation); }
    int __cdecl l_SetRotationDegree(lua_State* L) { return SetVector(L, "SetRotationDegree", dev::SetRotationDegree); }

    int SetComponent(lua_State* L, const char* fn, std::size_t nodeOffset)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, fn, SCENE, handle, o))
            return 0;
        const float value = Num(L, 2);
        Queue(handle, [nodeOffset, value](const Object& t) { Call<void>(Overload(nodeOffset, t), t.ptr, value); });
        return 0;
    }

    int __cdecl l_SetTranslationX(lua_State* L)    { return SetComponent(L, "SetTranslationX", dev::SetTranslationX); }
    int __cdecl l_SetTranslationY(lua_State* L)    { return SetComponent(L, "SetTranslationY", dev::SetTranslationY); }
    int __cdecl l_SetTranslationZ(lua_State* L)    { return SetComponent(L, "SetTranslationZ", dev::SetTranslationZ); }
    int __cdecl l_SetRotationDegreeX(lua_State* L) { return SetComponent(L, "SetRotationDegreeX", dev::SetRotationDegreeX); }
    int __cdecl l_SetRotationDegreeY(lua_State* L) { return SetComponent(L, "SetRotationDegreeY", dev::SetRotationDegreeY); }
    int __cdecl l_SetRotationDegreeZ(lua_State* L) { return SetComponent(L, "SetRotationDegreeZ", dev::SetRotationDegreeZ); }

    int __cdecl l_SetQuaternion(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "SetQuaternion", SCENE, handle, o))
            return 0;
        const float x = Num(L, 2), y = Num(L, 3), z = Num(L, 4), w = Num(L, 5, 1.0f);
        Queue(handle, [x, y, z, w](const Object& t) {
            V4 q{ { x, y, z, w } };
            Call<void>(Overload(dev::SetQuaternion, t), t.ptr, &q);
        });
        return 0;
    }

    // SetScale(handle, s) scales evenly, SetScale(handle, x, y, z) per axis
    int __cdecl l_SetScale(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "SetScale", SCENE, handle, o))
            return 0;
        const float x = Num(L, 2, 1.0f);
        if (!Present(L, 3))
        {
            Queue(handle, [x](const Object& t) { Call<void>(Overload(dev::SetScaleFloat, t), t.ptr, x); });
            return 0;
        }
        const float y = Num(L, 3, x), z = Num(L, 4, 1.0f);
        Queue(handle, [x, y, z](const Object& t) {
            V4 v{ { x, y, z, 0.0f } };
            Call<void>(Overload(dev::SetScaleVector, t), t.ptr, &v);
        });
        return 0;
    }

    int Status(lua_State* L, const char* fn, std::size_t lineOffset, std::size_t textOffset)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, fn, { Kind::NodeText, Kind::NodeLine }, handle, o))
            return 0;
        const std::uint32_t bits = static_cast<std::uint32_t>(GetLuaInt64(L, 2));
        const std::size_t offset = o.kind == Kind::NodeText ? textOffset : lineOffset;
        Queue(handle, [offset, bits](const Object& t) { Call<void>(offset, t.ptr, bits); });
        return 0;
    }

    int __cdecl l_SetStatus(lua_State* L)   { return Status(L, "SetStatus", dev::SetStatusLine, dev::SetStatusText); }
    int __cdecl l_ResetStatus(lua_State* L) { return Status(L, "ResetStatus", dev::ResetStatusLine, dev::ResetStatusText); }

    // a connect the engine cannot survive: to itself, a window's root layout, or into a loop (its layout walk recurses
    // until the stack runs out). Each reason is logged once.
    bool ConnectRefused(const Object& child, void* parent)
    {
        static const char* const kWhy[] = { "", "to itself", "a window's root layout (its window draws it already)",
                                            "into a loop (the parent hangs under the child)", "a component that cannot be read" };
        const int why = ConnectProblem(child.ptr, child.window, parent);
        if (why == 0)
            return false;
        LogWarnOnce("V_UixUtility.ConnectComponent: refused, connecting %s", kWhy[why]);
        return true;
    }

    // ConnectComponent(child, parent, node): node is a node handle or a node name
    int __cdecl l_ConnectComponent(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object child, parent;
        if (!Active() || !Arg(L, 1, "ConnectComponent", COMPONENTS, handle, child) || !Arg(L, 2, "ConnectComponent", COMPONENTS, parent))
            return 0;
        if (WindowGroup(child.window) != WindowGroup(parent.window))
        {
            LogWarnOnce("V_UixUtility.ConnectComponent: the two windows are in different update groups");
            return 0;
        }
        void* parentPtr = parent.ptr;
        void* parentWindow = parent.window;
        Object node;
        if (LuaType(L, 3) == LUA_T_NUMBER && Lookup(static_cast<std::uint32_t>(GetLuaInt64(L, 3)), node) && IsNode(node.kind))
        {
            void* nodePtr = node.ptr;
            Queue(handle, [parentPtr, parentWindow, nodePtr](const Object& c) {
                if (ConnectRefused(c, parentPtr))
                    return;
                Call<void>(dev::ConnectComponentToNode, c.ptr, parentPtr, nodePtr);
                RememberLink(c.ptr, c.window, parentWindow);
            });
            return 0;
        }
        const std::uint64_t name = Name(L, 3);
        Queue(handle, [parentPtr, parentWindow, name](const Object& c) {
            if (ConnectRefused(c, parentPtr))
                return;
            Call<void>(dev::ConnectComponentByName, c.ptr, parentPtr, name);
            RememberLink(c.ptr, c.window, parentWindow);
        });
        return 0;
    }

    // the optional layout or model whose HUD units a screen position should come in
    bool UnitsOf(lua_State* L, int idx, const char* fn, Object& component)
    {
        return Present(L, idx) && Arg(L, idx, fn, COMPONENTS, component);
    }

    void SetField(lua_State* L, const char* key, double value)
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_pushnumber(L, value);
        g_lua_settable(L, -3);
    }

    void SetField(lua_State* L, const char* key, bool value)
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_pushboolean(L, value);
        g_lua_settable(L, -3);
    }

    // GetMarkers() -> { {gameObjectId, x, y, z, markerType, markerFlag, radiusLevel, radius, mapX, mapY, mapZ, dying}, ... } for every
    // marker that is on (marked, or enabled by a mission script). From the HUD's own marker slot (nil when it has none this frame):
    // markerType/markerFlag = UiMarkerCommonData::GetMarkerFlag (low byte = type, higher bits = state; test with bit.band),
    // important/questImportant = a mission target / side-op target (the head marker's yellow / blue flashing circle),
    // viewWorldIcon/viewMapIcon = shown in the world as a head marker / on the iDroid map (a mission can enable map-only markers),
    // viewWorldGoal/viewMapGoal = the same as a mission goal (TppMarker.Enable view "all" / "map"),
    // radiusLevel/radius = the area TppMarker.Enable gave it (level 0-9, radius in metres), mapX/Y/Z = where the HUD and iDroid draw it
    // (an area's centre, offset from the target by randomLevel). dying = the head marker shows the dying colour
    int __cdecl l_GetMarkers(lua_State* L)
    {
        if (!Active())
            return 0;
        static MarkerEntry entries[0x3B6];
        const int count = UixUtilityControl::GetMarkers(entries, 0x3B6);
        if (count < 0)
            return 0;
        g_lua_createtable(L, count, 0);
        for (int i = 0; i < count; ++i)
        {
            g_lua_pushnumber(L, static_cast<double>(i + 1));
            g_lua_createtable(L, 0, 18);
            SetField(L, "gameObjectId", static_cast<double>(entries[i].gameObjectId));
            SetField(L, "x", static_cast<double>(entries[i].x));
            SetField(L, "y", static_cast<double>(entries[i].y));
            SetField(L, "z", static_cast<double>(entries[i].z));
            if (entries[i].hasMarkerFlag)
            {
                SetField(L, "markerType", static_cast<double>(entries[i].markerFlag & 0xFF));
                SetField(L, "markerFlag", static_cast<double>(entries[i].markerFlag));
                SetField(L, "important", entries[i].important);
                SetField(L, "questImportant", entries[i].questImportant);
                SetField(L, "viewWorldIcon", entries[i].viewWorldIcon);
                SetField(L, "viewMapIcon", entries[i].viewMapIcon);
                SetField(L, "viewWorldGoal", entries[i].viewWorldGoal);
                SetField(L, "viewMapGoal", entries[i].viewMapGoal);
                SetField(L, "radiusLevel", static_cast<double>(entries[i].radiusLevel));
                SetField(L, "radius", static_cast<double>(entries[i].radius));
                SetField(L, "mapX", static_cast<double>(entries[i].mapX));
                SetField(L, "mapY", static_cast<double>(entries[i].mapY));
                SetField(L, "mapZ", static_cast<double>(entries[i].mapZ));
            }
            SetField(L, "dying", entries[i].dying);
            g_lua_settable(L, -3);
        }
        return 1;
    }

    // GetEnemyRoutePoints() -> { {groupIndex, width, points = { {x, z}, ... }}, ... }: every predicted route the game shows now
    // (registered by TppUiCommand.ShowEnemyRoutePoints, removed by InitEnemyRoutePoints / InitAllEnemyRoutePoints)
    int __cdecl l_GetEnemyRoutePoints(lua_State* L)
    {
        if (!Active())
            return 0;
        static UixUtilityControl::EnemyRoute routes[5];
        const int count = UixUtilityControl::GetEnemyRoutes(routes, 5);
        if (count < 0)
            return 0;
        g_lua_createtable(L, count, 0);
        for (int i = 0; i < count; ++i)
        {
            g_lua_pushnumber(L, static_cast<double>(i + 1));
            g_lua_createtable(L, 0, 3);
            SetField(L, "groupIndex", static_cast<double>(routes[i].group));
            SetField(L, "width", static_cast<double>(routes[i].width));
            g_lua_pushstring(L, const_cast<char*>("points"));
            g_lua_createtable(L, routes[i].count, 0);
            for (int p = 0; p < routes[i].count; ++p)
            {
                g_lua_pushnumber(L, static_cast<double>(p + 1));
                g_lua_createtable(L, 0, 2);
                SetField(L, "x", static_cast<double>(routes[i].x[p]));
                SetField(L, "z", static_cast<double>(routes[i].z[p]));
                g_lua_settable(L, -3);
            }
            g_lua_settable(L, -3);
            g_lua_settable(L, -3);
        }
        return 1;
    }

    void SetHandleField(lua_State* L, const char* key, std::uint32_t handle)
    {
        if (!handle)
            return;
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_pushnumber(L, static_cast<double>(handle));
        g_lua_settable(L, -3);
    }

    // GetWindows() -> { {window, id, group, parent, layouts = { {layout, id, depth, parentId, models = { {model, id}, ... }}, ... }}, ... }
    // every live window, the game's own included; layouts are one flat list (depth 0 = the window's root layout), all as handles.
    // Kept flat because a C function only gets about 20 Lua stack slots.
    int __cdecl l_GetWindows(lua_State* L)
    {
        if (!Active())
            return 0;
        const std::vector<WindowEntry> windows = UixUtilityControl::GetWindows();
        g_lua_createtable(L, static_cast<int>(windows.size()), 0);
        for (std::size_t w = 0; w < windows.size(); ++w)
        {
            const WindowEntry& window = windows[w];
            g_lua_pushnumber(L, static_cast<double>(w + 1));
            g_lua_createtable(L, 0, 5);
            SetHandleField(L, "window", HandleFor(Kind::Window, window.window, window.window));
            SetField(L, "id", static_cast<double>(window.id));
            SetField(L, "group", static_cast<double>(window.group));
            if (window.parent)
                SetHandleField(L, "parent", HandleFor(Kind::Window, window.parent, window.parent));

            g_lua_pushstring(L, const_cast<char*>("layouts"));
            g_lua_createtable(L, static_cast<int>(window.layouts.size()), 0);
            for (std::size_t l = 0; l < window.layouts.size(); ++l)
            {
                const LayoutEntry& layout = window.layouts[l];
                g_lua_pushnumber(L, static_cast<double>(l + 1));
                g_lua_createtable(L, 0, 5);
                SetHandleField(L, "layout", HandleFor(Kind::Layout, layout.layout, window.window));
                SetField(L, "id", static_cast<double>(layout.id));
                SetField(L, "depth", static_cast<double>(layout.depth));
                SetField(L, "parentId", static_cast<double>(layout.parentId));

                g_lua_pushstring(L, const_cast<char*>("models"));
                g_lua_createtable(L, static_cast<int>(layout.models.size()), 0);
                for (std::size_t m = 0; m < layout.models.size(); ++m)
                {
                    g_lua_pushnumber(L, static_cast<double>(m + 1));
                    g_lua_createtable(L, 0, 2);
                    SetHandleField(L, "model", HandleFor(Kind::Model, layout.models[m].model, window.window));
                    SetField(L, "id", static_cast<double>(layout.models[m].id));
                    g_lua_settable(L, -3);
                }
                g_lua_settable(L, -3);

                g_lua_settable(L, -3);
            }
            g_lua_settable(L, -3);

            g_lua_settable(L, -3);
        }
        return 1;
    }

    // GetNodeScreenPosition(node[, layoutOrModel]) -> screenX, screenY, onScreen
    int __cdecl l_GetNodeScreenPosition(lua_State* L)
    {
        Object node;
        if (!Active() || !Arg(L, 1, "GetNodeScreenPosition", NODES, node))
            return 0;
        Object component;
        const bool inUnits = UnitsOf(L, 2, "GetNodeScreenPosition", component);
        ScreenPoint point;
        if (!UixUtilityControl::NodeToScreen(node, inUnits ? &component : nullptr, point))
            return 0;
        g_lua_pushnumber(L, point.x);
        g_lua_pushnumber(L, point.y);
        g_lua_pushboolean(L, point.onScreen);
        return 3;
    }

    // GetMarkerPosition(gameObjectId[, layoutOrModel]) -> {enabled, x, y, z, screenX, screenY, onScreen}, nil without a marker
    int __cdecl l_GetMarkerPosition(lua_State* L)
    {
        if (!Active() || LuaType(L, 1) != LUA_T_NUMBER)
            return 0;
        const std::uint64_t id = GetLuaInt64(L, 1);
        Object component;
        const bool inUnits = UnitsOf(L, 2, "GetMarkerPosition", component);
        MarkerPosition marker;
        if (id >= 0xFFFF || UixUtilityControl::GetMarkerPosition(static_cast<std::uint16_t>(id), inUnits ? &component : nullptr, marker) != 1)
            return 0;
        g_lua_createtable(L, 0, 7);
        SetField(L, "enabled", marker.enabled);
        SetField(L, "x", static_cast<double>(marker.world[0]));
        SetField(L, "y", static_cast<double>(marker.world[1]));
        SetField(L, "z", static_cast<double>(marker.world[2]));
        SetField(L, "screenX", static_cast<double>(marker.screen.x));
        SetField(L, "screenY", static_cast<double>(marker.screen.y));
        SetField(L, "onScreen", marker.screen.onScreen);
        return 1;
    }

    int __cdecl l_DisconnectComponent(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object child;
        if (!Active() || !Arg(L, 1, "DisconnectComponent", COMPONENTS, handle, child))
            return 0;
        Queue(handle, [](const Object& c) {
            reinterpret_cast<void(__fastcall*)(void*)>(DisconnectComponentFn())(c.ptr);
            ForgetLink(c.ptr);
        });
        return 0;
    }

    int Traversal(lua_State* L, const char* fn, std::size_t offset)
    {
        Object component, node;
        if (!Active() || !Arg(L, 1, fn, COMPONENTS, component) || !Arg(L, 2, fn, NODES, node))
            return 0;
        V4 v{};
        Now([&]() { Call<void*>(offset, &v, component.ptr, node.ptr); });
        return PushV(L, v, 3);
    }

    int __cdecl l_GetTraversalTranslation(lua_State* L) { return Traversal(L, "GetTraversalTranslation", dev::GetTraversalTranslation); }
    int __cdecl l_GetTraversalScale(lua_State* L)       { return Traversal(L, "GetTraversalScale", dev::GetTraversalScale); }

    // ---- text ----

    int TextByte(lua_State* L, const char* fn, std::size_t offset)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, fn, { Kind::NodeText }, handle, o))
            return 0;
        const std::uint8_t value = static_cast<std::uint8_t>(GetLuaInt(L, 2));
        Queue(handle, [offset, value](const Object& t) { Call<void>(offset, t.ptr, value); });
        return 0;
    }

    int __cdecl l_SetVerticalAlign(lua_State* L) { return TextByte(L, "SetVerticalAlign", dev::SetVerticalAlign); }
    int __cdecl l_SetTextAlign(lua_State* L)     { return TextByte(L, "SetTextAlign", dev::SetTextAlign); }

    int __cdecl l_SetFontSize(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "SetFontSize", { Kind::NodeText }, handle, o))
            return 0;
        const float width = Num(L, 2), height = Num(L, 3, width);
        Queue(handle, [width, height](const Object& t) { Call<void>(dev::SetFontSize, t.ptr, width, height); });
        return 0;
    }

    int LineGet(lua_State* L, const char* fn, std::size_t offset)
    {
        Object o;
        if (!Active() || !Arg(L, 1, fn, { Kind::NodeText }, o))
            return 0;
        const std::uint32_t line = static_cast<std::uint32_t>(GetLuaInt(L, 2));
        float value = 0.0f;
        Now([&]() { value = Call<float>(offset, o.ptr, line); });
        g_lua_pushnumber(L, value);
        return 1;
    }

    int LineSet(lua_State* L, const char* fn, std::size_t offset)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, fn, { Kind::NodeText }, handle, o))
            return 0;
        const std::uint32_t line = static_cast<std::uint32_t>(GetLuaInt(L, 2));
        const float value = Num(L, 3);
        Queue(handle, [offset, line, value](const Object& t) { Call<void>(offset, t.ptr, line, value); });
        return 0;
    }

    int __cdecl l_GetLineWidth(lua_State* L)  { return LineGet(L, "GetLineWidth", dev::GetLineWidth); }
    int __cdecl l_GetLineHeight(lua_State* L) { return LineGet(L, "GetLineHeight", dev::GetLineHeight); }
    int __cdecl l_SetLineWidth(lua_State* L)  { return LineSet(L, "SetLineWidth", dev::SetLineWidth); }
    int __cdecl l_SetLineHeight(lua_State* L) { return LineSet(L, "SetLineHeight", dev::SetLineHeight); }

    int __cdecl l_CreateTextUnit(lua_State* L)
    {
        if (!Active())
            return 0;
        const int count = Present(L, 1) ? GetLuaInt(L, 1) : 1;
        return PushHandle(L, CreateTextUnit(count));
    }

    int __cdecl l_DeleteTextUnit(lua_State* L)
    {
        if (Active() && LuaType(L, 1) == LUA_T_NUMBER)
            DeleteTextUnit(static_cast<std::uint32_t>(GetLuaInt64(L, 1)));
        return 0;
    }

    // SetTextForModelNodeText(textNode, textUnit, text, useLanguageFont)
    int TextWithUnits(lua_State* L, const char* fn, std::size_t offset)
    {
        std::uint32_t handle = 0;
        Object node, units;
        if (!Active() || !Arg(L, 1, fn, { Kind::NodeText }, handle, node) || !Arg(L, 2, fn, { Kind::TextUnit }, units))
            return 0;
        auto text = Text(L, 3);
        const bool languageFont = GetLuaBool(L, 4);
        void* unitsPtr = units.ptr;
        Queue(handle, [offset, unitsPtr, text, languageFont, pin = PinTextUnit(unitsPtr)](const Object& t) {
            Call<void>(offset, t.ptr, unitsPtr, text->c_str(), languageFont);
            KeepTextAlive(unitsPtr, t, text);
        });
        return 0;
    }

    int __cdecl l_SetTextForModelNodeText(lua_State* L)       { return TextWithUnits(L, "SetTextForModelNodeText", dev::SetTextForModelNodeText); }
    int __cdecl l_SetScrollTextForModelNodeText(lua_State* L) { return TextWithUnits(L, "SetScrollTextForModelNodeText", dev::SetScrollTextForModelNodeText); }

    std::uint32_t UnitCount(lua_State* L, int idx, void* units)
    {
        const int allocated = TextUnitCount(units);
        int count = Present(L, idx) ? GetLuaInt(L, idx) : allocated;
        if (allocated > 0 && (count <= 0 || count > allocated))
            count = allocated;
        return count > 0 ? static_cast<std::uint32_t>(count) : 1u;   // never 0, the game's parser reads that as no limit
    }

    bool TaggedTextIsSafe(const char* fn, const std::string& text)
    {
        const char* problem = nullptr;
        std::size_t run = 0;
        for (std::size_t i = 0; i < text.size() && !problem; )
        {
            if (text[i] == '<' && i + 2 < text.size() && (text[i + 1] == 'I' || text[i + 1] == 'i') && text[i + 2] == '=')
            {
                const char kind = i + 3 < text.size() ? text[i + 3] : '\0';
                const std::size_t end = text.find('>', i);
                if ((kind != 'G' && kind != 'C' && kind != 'R' && kind != 'r') || i + 4 >= text.size() || text[i + 4] != '='
                    || end == std::string::npos)
                {
                    problem = "a tag is not <I=G=name>, <I=C=name> or <I=R=text>";
                    break;
                }
                if (kind == 'G')
                {
                    const std::size_t amp = text.find('&', i + 5);
                    const std::size_t nameEnd = amp < end ? amp : end;
                    if (nameEnd == i + 5 || nameEnd - (i + 5) >= 0x40)   // the game's parser kills the process on an icon name this long
                        problem = "an icon name is empty or 64 bytes or longer";
                }
                run = 0;
                i = end + 1;
                continue;
            }
            if (++run > 0xFFF)   // and on a plain run past this
                problem = "a run without tags is longer than 4095 bytes";
            ++i;
        }
        if (problem)
            LogWarnOnce("V_UixUtility.%s: text refused, %s", fn, problem);
        return problem == nullptr;
    }

    // SetupTextUnits(textNode, textUnit, count, text, flagA, flagB) fills the units without showing them. Queued like the
    // other text setters: the game frees and rewrites the units while the UI thread may be drawing from them.
    int __cdecl l_SetupTextUnits(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object node, units;
        if (!Active() || !Arg(L, 1, "SetupTextUnits", { Kind::NodeText }, handle, node) || !Arg(L, 2, "SetupTextUnits", { Kind::TextUnit }, units))
            return 0;
        const std::uint32_t count = UnitCount(L, 3, units.ptr);
        auto text = Text(L, 4);
        if (!TaggedTextIsSafe("SetupTextUnits", *text))
            return 0;
        const bool a = GetLuaBool(L, 5), b = GetLuaBool(L, 6);
        void* unitsPtr = units.ptr;
        Queue(handle, [unitsPtr, count, text, a, b, pin = PinTextUnit(unitsPtr)](const Object& t) {
            Call<int>(dev::SetupTextUnits, t.ptr, unitsPtr, count, text->c_str(), a, b);
            KeepTextAlive(unitsPtr, t, text);
        });
        return 0;
    }

    int __cdecl l_SetTextUnitsForModelNodeText(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object node, units;
        if (!Active() || !Arg(L, 1, "SetTextUnitsForModelNodeText", { Kind::NodeText }, handle, node)
            || !Arg(L, 2, "SetTextUnitsForModelNodeText", { Kind::TextUnit }, units))
            return 0;
        const std::uint32_t count = UnitCount(L, 3, units.ptr);
        void* unitsPtr = units.ptr;
        Queue(handle, [unitsPtr, count, pin = PinTextUnit(unitsPtr)](const Object& t) {
            Call<void>(dev::SetTextUnitsForModelNodeText, t.ptr, unitsPtr, count);
            KeepTextAlive(unitsPtr, t, nullptr);
        });
        return 0;
    }

    int __cdecl l_CreateBoxText(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object node, units;
        if (!Active() || !Arg(L, 1, "CreateBoxText", { Kind::NodeText }, handle, node) || !Arg(L, 2, "CreateBoxText", { Kind::TextUnit }, units))
            return 0;
        const std::uint32_t count = UnitCount(L, 3, units.ptr);
        auto text = Text(L, 4);
        if (!TaggedTextIsSafe("CreateBoxText", *text))
            return 0;
        const bool a = GetLuaBool(L, 5), b = GetLuaBool(L, 6);
        void* unitsPtr = units.ptr;
        Queue(handle, [unitsPtr, count, text, a, b, pin = PinTextUnit(unitsPtr)](const Object& t) {
            Call<int>(dev::CreateBoxText, t.ptr, unitsPtr, count, text->c_str(), a, b);
            KeepTextAlive(unitsPtr, t, text);
        });
        return 0;
    }

    int __cdecl l_CreateScrollBoxText(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object node, units;
        if (!Active() || !Arg(L, 1, "CreateScrollBoxText", { Kind::NodeText }, handle, node) || !Arg(L, 2, "CreateScrollBoxText", { Kind::TextUnit }, units))
            return 0;
        const std::uint32_t count = UnitCount(L, 3, units.ptr);
        auto text = Text(L, 4);
        if (!TaggedTextIsSafe("CreateScrollBoxText", *text))
            return 0;
        void* unitsPtr = units.ptr;
        Queue(handle, [unitsPtr, count, text, pin = PinTextUnit(unitsPtr)](const Object& t) {
            Call<int>(dev::CreateScrollBoxText, t.ptr, unitsPtr, count, text->c_str());
            KeepTextAlive(unitsPtr, t, text);
        });
        return 0;
    }

    int __cdecl l_GetLangText(lua_State* L)
    {
        if (!Active())
            return 0;
        const std::uint64_t langId = Name64(L, 1);
        const char* text = nullptr;
        Now([&]() { text = Call<const char*>(dev::GetLangText, langId); });
        if (!text)
            return 0;
        g_lua_pushstring(L, const_cast<char*>(text));
        return 1;
    }

    int TextCall(lua_State* L, const char* fn, std::size_t offset)
    {
        std::uint32_t handle = 0;
        Object o;
        if (Active() && Arg(L, 1, fn, { Kind::NodeText }, handle, o))
            Queue(handle, [offset](const Object& t) { Call<void>(offset, t.ptr); });
        return 0;
    }

    int __cdecl l_StartTextScroll(lua_State* L) { return TextCall(L, "StartTextScroll", dev::StartTextScroll); }
    int __cdecl l_StopTextScroll(lua_State* L)  { return TextCall(L, "StopTextScroll", dev::StopTextScroll); }

    int TextGet(lua_State* L, const char* fn, std::size_t offset)
    {
        Object o;
        if (!Active() || !Arg(L, 1, fn, { Kind::NodeText }, o))
            return 0;
        float value = 0.0f;
        Now([&]() { value = Call<float>(offset, o.ptr); });
        g_lua_pushnumber(L, value);
        return 1;
    }

    int __cdecl l_GetTextTotalHeightInTextNode(lua_State* L) { return TextGet(L, "GetTextTotalHeightInTextNode", dev::GetTextTotalHeightInTextNode); }
    int __cdecl l_GetTextMaxWidthInTextNode(lua_State* L)    { return TextGet(L, "GetTextMaxWidthInTextNode", dev::GetTextMaxWidthInTextNode); }
    int __cdecl l_GetTextNodeWidth(lua_State* L)             { return TextGet(L, "GetTextNodeWidth", dev::GetTextNodeWidth); }
    int __cdecl l_GetTextNodeHeight(lua_State* L)            { return TextGet(L, "GetTextNodeHeight", dev::GetTextNodeHeight); }

    int TextSet(lua_State* L, const char* fn, std::size_t offset)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, fn, { Kind::NodeText }, handle, o))
            return 0;
        const float value = Num(L, 2);
        Queue(handle, [offset, value](const Object& t) { Call<void>(offset, t.ptr, value); });
        return 0;
    }

    int __cdecl l_SetTextNodeWidth(lua_State* L)  { return TextSet(L, "SetTextNodeWidth", dev::SetTextNodeWidth); }
    int __cdecl l_SetTextNodeHeight(lua_State* L) { return TextSet(L, "SetTextNodeHeight", dev::SetTextNodeHeight); }

    // fox::ui::ModelNodeText::SetFontGroup: a font of the UI font palette, e.g. "hud-def-M"
    int __cdecl l_SetFontGroup(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "SetFontGroup", { Kind::NodeText }, handle, o))
            return 0;
        const std::uint32_t font = static_cast<std::uint32_t>(Name(L, 2));
        Queue(handle, [font](const Object& t) { reinterpret_cast<void(__fastcall*)(void*, std::uint32_t)>(SetFontGroupFn())(t.ptr, font); });
        return 0;
    }

    // ---- meshes and textures ----

    int TextureName(lua_State* L, const char* fn, std::size_t plain, std::size_t withLevel)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, fn, { Kind::NodeMesh }, handle, o))
            return 0;
        auto path = std::make_shared<std::string>(LuaType(L, 2) == LUA_T_STRING ? FoxHashes::NormalizeAssetPath(GetLuaString(L, 2)) : std::string());
        const std::uint64_t slot = Name(L, 3);
        if (Present(L, 4))
        {
            const int level = GetLuaInt(L, 4);
            Queue(handle, [withLevel, path, slot, level](const Object& t) { Call<void>(withLevel, t.ptr, path->c_str(), slot, level); });
        }
        else
        {
            Queue(handle, [plain, path, slot](const Object& t) { Call<void>(plain, t.ptr, path->c_str(), slot); });
        }
        return 0;
    }

    int __cdecl l_SetTextureName(lua_State* L)         { return TextureName(L, "SetTextureName", dev::SetTextureName, dev::SetTextureNameLevel); }
    int __cdecl l_CheckAndSetTextureName(lua_State* L) { return TextureName(L, "CheckAndSetTextureName", dev::CheckAndSetTextureName, dev::CheckAndSetTextureNameLevel); }

    int __cdecl l_SetTextureLevel(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "SetTextureLevel", { Kind::NodeMesh }, handle, o))
            return 0;
        const std::uint64_t slot = Name(L, 2);
        const int level = GetLuaInt(L, 3);
        Queue(handle, [slot, level](const Object& t) { Call<void>(dev::SetTextureLevel, t.ptr, slot, level); });
        return 0;
    }

    int __cdecl l_IsTextureLoaded(lua_State* L)
    {
        Object o;
        if (!Active() || !Arg(L, 1, "IsTextureLoaded", { Kind::NodeMesh }, o))
            return 0;
        const std::uint64_t slot = Name(L, 2);
        const int level = GetLuaInt(L, 3);
        bool loaded = false;
        Now([&]() { loaded = Call<bool>(dev::IsTextureLoaded, o.ptr, slot, level); });
        g_lua_pushboolean(L, loaded);
        return 1;
    }

    std::uint64_t PathId(lua_State* L, int idx)
    {
        if (LuaType(L, idx) == LUA_T_NUMBER)
            return GetLuaInt64(L, idx);
        const char* s = LuaType(L, idx) == LUA_T_STRING ? GetLuaString(L, idx) : nullptr;
        if (s && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
            return std::strtoull(s + 2, nullptr, 16);   // a path id from GetPathIdFromChar
        return s && *s ? FoxHashes::PathCode64Ext(FoxHashes::NormalizeAssetPath(s)) : 0;
    }

    int __cdecl l_SetLoadingTexture(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "SetLoadingTexture", { Kind::NodeMesh }, handle, o))
            return 0;
        const std::uint64_t path = PathId(L, 2);
        const int level = GetLuaInt(L, 3);
        Queue(handle, [path, level](const Object& t) { Call<void>(dev::SetLoadingTexture, t.ptr, path, level); });
        return 0;
    }

    int __cdecl l_IsDispLoadingTexture(lua_State* L)
    {
        Object o;
        if (!Active() || !Arg(L, 1, "IsDispLoadingTexture", { Kind::NodeMesh }, o))
            return 0;
        const std::uint32_t slot = static_cast<std::uint32_t>(Name(L, 2));
        bool showing = false;
        Now([&]() { showing = Call<bool>(dev::IsDispLoadingTexture, o.ptr, slot); });
        g_lua_pushboolean(L, showing);
        return 1;
    }

    int __cdecl l_CreatePrefetchTexture(lua_State* L)
    {
        const std::uint64_t path = PathId(L, 1);
        const int level = GetLuaInt(L, 2);
        if (Active() && path)
            QueueNoHandle([path, level]() { Call<void>(dev::CreatePrefetchTexture, path, level); });
        return 0;
    }

    int PathOnly(lua_State* L, std::size_t offset)
    {
        const std::uint64_t path = PathId(L, 1);
        if (Active() && path)
            QueueNoHandle([offset, path]() { Call<void>(offset, path); });
        return 0;
    }

    int __cdecl l_RemovePrefetchTexture(lua_State* L)        { return PathOnly(L, dev::RemovePrefetchTexture); }
    int __cdecl l_RegisterLoadingMeshTexture(lua_State* L)   { return PathOnly(L, dev::RegisterLoadingMeshTexture); }
    int __cdecl l_UnregisterLoadingMeshTexture(lua_State* L) { return PathOnly(L, dev::UnregisterLoadingMeshTexture); }

    int __cdecl l_IsReadyPrefetchTexture(lua_State* L)
    {
        if (!Active())
            return 0;
        const std::uint64_t path = PathId(L, 1);
        const int level = GetLuaInt(L, 2);
        bool ready = false;
        Now([&]() { ready = Call<bool>(dev::IsReadyPrefetchTexture, path, level); });
        g_lua_pushboolean(L, ready);
        return 1;
    }

    int __cdecl l_SetShaderParameter(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "SetShaderParameter", { Kind::NodeMesh }, handle, o))
            return 0;
        const std::uint64_t name = Name(L, 2);
        const float x = Num(L, 3), y = Num(L, 4), z = Num(L, 5), w = Num(L, 6);
        Queue(handle, [name, x, y, z, w](const Object& t) {
            V4 v{ { x, y, z, w } };
            Call<void>(dev::SetShaderParameter, t.ptr, name, &v);
        });
        return 0;
    }

    int __cdecl l_SetShaderTechnique(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "SetShaderTechnique", { Kind::NodeMesh }, handle, o))
            return 0;
        const std::uint64_t name = Name(L, 2);
        Queue(handle, [name](const Object& t) { Call<void>(dev::SetShaderTechnique, t.ptr, name); });
        return 0;
    }

    int Uv(lua_State* L, const char* fn, std::size_t offset, float fallback)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, fn, { Kind::NodeMesh }, handle, o))
            return 0;
        const float u = Num(L, 2, fallback), v = Num(L, 3, fallback);
        Queue(handle, [offset, u, v](const Object& t) { Call<void>(offset, t.ptr, u, v); });
        return 0;
    }

    int __cdecl l_SetShaderBaseTexUvShift(lua_State* L)    { return Uv(L, "SetShaderBaseTexUvShift", dev::SetShaderBaseTexUvShift, 0.0f); }
    int __cdecl l_SetShaderMaskTexUvShift(lua_State* L)    { return Uv(L, "SetShaderMaskTexUvShift", dev::SetShaderMaskTexUvShift, 0.0f); }
    int __cdecl l_SetShaderScreenTexUvShift(lua_State* L)  { return Uv(L, "SetShaderScreenTexUvShift", dev::SetShaderScreenTexUvShift, 0.0f); }
    int __cdecl l_SetShaderBaseTexUvRepeat(lua_State* L)   { return Uv(L, "SetShaderBaseTexUvRepeat", dev::SetShaderBaseTexUvRepeat, 1.0f); }
    int __cdecl l_SetShaderMaskTexUvRepeat(lua_State* L)   { return Uv(L, "SetShaderMaskTexUvRepeat", dev::SetShaderMaskTexUvRepeat, 1.0f); }
    int __cdecl l_SetShaderScreenTexUvRepeat(lua_State* L) { return Uv(L, "SetShaderScreenTexUvRepeat", dev::SetShaderScreenTexUvRepeat, 1.0f); }

    // SetVertexTranslate(mesh, control, x, y, z, keepX, keepY, keepZ): keep 1 leaves that axis as the file has it
    int __cdecl l_SetVertexTranslate(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "SetVertexTranslate", { Kind::NodeMesh }, handle, o))
            return 0;
        const std::uint64_t control = Name(L, 2);
        const float x = Num(L, 3), y = Num(L, 4), z = Num(L, 5);
        const float kx = Num(L, 6), ky = Num(L, 7), kz = Num(L, 8);
        Queue(handle, [control, x, y, z, kx, ky, kz](const Object& t) {
            V4 position{ { x, y, z, 0.0f } };
            V4 keep{ { kx, ky, kz, 0.0f } };
            Call<void>(dev::SetVertexTranslate, t.ptr, control, &position, &keep);
        });
        return 0;
    }

    int __cdecl l_DisableShaderReset(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "DisableShaderReset", { Kind::NodeMesh, Kind::Model, Kind::Layout }, handle, o))
            return 0;
        const std::size_t offset = o.kind == Kind::NodeMesh ? dev::DisableShaderResetMesh
            : (o.kind == Kind::Model ? dev::DisableShaderResetModel : dev::DisableShaderResetLayout);
        Queue(handle, [offset](const Object& t) { Call<void>(offset, t.ptr); });
        return 0;
    }

    // ---- animations ----

    int AnimationCall(lua_State* L, const char* fn, std::size_t offset)
    {
        std::uint32_t handle = 0;
        Object o;
        if (Active() && Arg(L, 1, fn, { Kind::Animation }, handle, o))
            Queue(handle, [offset](const Object& t) { Call<void>(offset, t.ptr); });
        return 0;
    }

    int AnimationFloat(lua_State* L, const char* fn, std::size_t offset)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, fn, { Kind::Animation }, handle, o))
            return 0;
        const float value = Num(L, 2);
        Queue(handle, [offset, value](const Object& t) { Call<void>(offset, t.ptr, value); });
        return 0;
    }

    int __cdecl l_StartAnimation(lua_State* L)               { return AnimationCall(L, "StartAnimation", dev::StartAnimation); }
    int __cdecl l_StartLoopAnimation(lua_State* L)           { return AnimationCall(L, "StartLoopAnimation", dev::StartLoopAnimation); }
    int __cdecl l_RecoverForAnimationWithRatio(lua_State* L) { return AnimationCall(L, "RecoverForAnimationWithRatio", dev::RecoverForAnimationWithRatio); }
    // fox::ui::Animation::Pause / ForceLoop / UnsetForceLoop, called by tpp code directly
    template <typename... A>
    int AnimationMember(lua_State* L, const char* fn, UiFunction member, A... args)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, fn, { Kind::Animation }, handle, o))
            return 0;
        if (!Function(member))
        {
            LogWarnOnce("V_UixUtility.%s: not ported to this game version yet", fn);
            return 0;
        }
        Queue(handle, [member, args...](const Object& t) {
            reinterpret_cast<void(__fastcall*)(void*, A...)>(Function(member))(t.ptr, args...);
        });
        return 0;
    }

    // PauseAnimation(animation[, paused]): false resumes from where it stopped
    int __cdecl l_PauseAnimation(lua_State* L)
    {
        if (Present(L, 2) && !GetLuaBool(L, 2))
            return AnimationMember(L, "PauseAnimation", UiFunction::AnimationPause, false);
        return AnimationCall(L, "PauseAnimation", dev::PauseAnimation);
    }

    int __cdecl l_ForceLoopAnimation(lua_State* L)      { return AnimationMember(L, "ForceLoopAnimation", UiFunction::AnimationForceLoop, GetLuaBool(L, 2)); }
    int __cdecl l_UnsetForceLoopAnimation(lua_State* L) { return AnimationMember(L, "UnsetForceLoopAnimation", UiFunction::AnimationUnsetForceLoop); }
    int __cdecl l_PauseInitAnimation(lua_State* L)           { return AnimationCall(L, "PauseInitAnimation", dev::PauseInitAnimation); }
    int __cdecl l_PauseEndAnimation(lua_State* L)            { return AnimationCall(L, "PauseEndAnimation", dev::PauseEndAnimation); }
    int __cdecl l_StartAnimationWithRatio(lua_State* L)      { return AnimationFloat(L, "StartAnimationWithRatio", dev::StartAnimationWithRatio); }
    int __cdecl l_StartAnimationWithStartRatio(lua_State* L) { return AnimationFloat(L, "StartAnimationWithStartRatio", dev::StartAnimationWithStartRatio); }
    int __cdecl l_SetAnimationStopTime(lua_State* L)         { return AnimationFloat(L, "SetAnimationStopTime", dev::SetAnimationStopTime); }
    int __cdecl l_SetAnimationSpeed(lua_State* L)            { return AnimationFloat(L, "SetAnimationSpeed", dev::SetAnimationSpeed); }

    int __cdecl l_StartInstantAnimation(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "StartInstantAnimation", { Kind::Animation }, handle, o))
            return 0;
        const bool toEnd = GetLuaBool(L, 2);
        Queue(handle, [toEnd](const Object& t) { Call<void>(dev::StartInstantAnimation, t.ptr, toEnd); });
        return 0;
    }

    int __cdecl l_IsAnimationRunning(lua_State* L)
    {
        Object o;
        if (!Active() || !Arg(L, 1, "IsAnimationRunning", { Kind::Animation }, o))
            return 0;
        bool running = false;
        Now([&]() { running = Call<bool>(dev::IsAnimationRunning, o.ptr); });
        g_lua_pushboolean(L, running);
        return 1;
    }

    int __cdecl l_IsAnimationPastTime(lua_State* L)
    {
        Object o;
        if (!Active() || !Arg(L, 1, "IsAnimationPastTime", { Kind::Animation }, o))
            return 0;
        const float seconds = Num(L, 2);
        bool past = false;
        Now([&]() { past = Call<bool>(dev::IsAnimationPastTime, o.ptr, seconds); });
        g_lua_pushboolean(L, past);
        return 1;
    }

    // fox::ui::Animation::GetTimeLength
    int __cdecl l_GetAnimationTimeLength(lua_State* L)
    {
        Object o;
        if (!Active() || !Arg(L, 1, "GetAnimationTimeLength", { Kind::Animation }, o))
            return 0;
        float seconds = 0.0f;
        Now([&]() { seconds = reinterpret_cast<float(__fastcall*)(void*)>(GetTimeLengthFn())(o.ptr); });
        g_lua_pushnumber(L, seconds);
        return 1;
    }

    // ---- draw order ----

    int __cdecl l_SetModelNodePriority(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "SetModelNodePriority", NODES, handle, o))
            return 0;
        const std::uint8_t priority = static_cast<std::uint8_t>(GetLuaInt(L, 2));
        Queue(handle, [priority](const Object& t) { Call<void>(dev::SetModelNodePriority, t.ptr, priority); });
        return 0;
    }

    int __cdecl l_UpdateModelDrawOrder(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (Active() && Arg(L, 1, "UpdateModelDrawOrder", { Kind::Model }, handle, o))
            Queue(handle, [](const Object& t) { Call<void>(dev::UpdateModelDrawOrder, t.ptr); });
        return 0;
    }

    int __cdecl l_SetModelStencil(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "SetModelStencil", { Kind::Model }, handle, o))
            return 0;
        const bool on = GetLuaBool(L, 2);
        Queue(handle, [on](const Object& t) { Call<void>(dev::SetModelStencil, t.ptr, on); });
        return 0;
    }

    int __cdecl l_SetModelNodeBillboard(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "SetModelNodeBillboard", NODES, handle, o))
            return 0;
        const bool on = GetLuaBool(L, 2);
        Queue(handle, [on](const Object& t) { Call<void>(dev::SetModelNodeBillboard, t.ptr, on); });
        return 0;
    }

    int __cdecl l_GetDrawPriority(lua_State* L)
    {
        Object o;
        if (!Active() || !Arg(L, 1, "GetDrawPriority", { Kind::Layout }, o))
            return 0;
        std::uint8_t priority = 0;
        Now([&]() { priority = Call<std::uint8_t>(dev::GetDrawPriority, o.ptr); });
        g_lua_pushnumber(L, priority);
        return 1;
    }

    int __cdecl l_SetDrawPriority(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "SetDrawPriority", { Kind::Layout }, handle, o))
            return 0;
        const std::uint8_t priority = static_cast<std::uint8_t>(GetLuaInt(L, 2));
        Queue(handle, [priority](const Object& t) { Call<void>(dev::SetDrawPriority, t.ptr, priority); });
        return 0;
    }

    int __cdecl l_GetDrawPriorityFromTable(lua_State* L)
    {
        if (!Active())
            return 0;
        const std::uint32_t name = static_cast<std::uint32_t>(Name(L, 1));
        std::uint8_t priority = 0;
        Now([&]() { priority = Call<std::uint8_t>(dev::GetDrawPriorityFromTable, name); });
        g_lua_pushnumber(L, priority);
        return 1;
    }

    // ---- time, maths, palette ----

    int __cdecl l_GetGameFrameRawDeltaTime(lua_State* L)
    {
        if (!Active())
            return 0;
        float seconds = 0.0f;
        Now([&]() { seconds = Call<float>(dev::GetGameFrameRawDeltaTime); });
        g_lua_pushnumber(L, seconds);
        return 1;
    }

    int __cdecl l_GetGameFrameElapsedTimeSinceStartup(lua_State* L)
    {
        if (!Active())
            return 0;
        double seconds = 0.0;
        Now([&]() { seconds = Call<double>(dev::GetGameFrameElapsedTimeSinceStartup); });
        g_lua_pushnumber(L, seconds);
        return 1;
    }

    int Convert(lua_State* L, std::size_t offset, int in, int out)
    {
        if (!Active())
            return 0;
        V4 source{ { Num(L, 1), Num(L, 2), Num(L, 3), in == 4 ? Num(L, 4, 1.0f) : 0.0f } };
        V4 result{};
        Now([&]() { Call<void*>(offset, &result, &source); });
        return PushV(L, result, out);
    }

    int __cdecl l_RotQuatToEuler(lua_State* L)         { return Convert(L, dev::RotQuatToEuler, 4, 3); }
    int __cdecl l_RotEulerToQuat(lua_State* L)         { return Convert(L, dev::RotEulerToQuat, 3, 4); }
    int __cdecl l_RotEularRadianToDegree(lua_State* L) { return Convert(L, dev::RotEularRadianToDegree, 3, 3); }
    int __cdecl l_RotEularDegreeToRadian(lua_State* L) { return Convert(L, dev::RotEularDegreeToRadian, 3, 3); }

    int __cdecl l_GetPathIdFromChar(lua_State* L)
    {
        if (!Active() || LuaType(L, 1) != LUA_T_STRING)
            return 0;
        const std::string path = FoxHashes::NormalizeAssetPath(GetLuaString(L, 1));
        std::uint64_t id = 0;
        Now([&]() { Call<void*>(dev::GetPathIdFromChar, &id, path.c_str()); });
        char text[24];
        std::snprintf(text, sizeof(text), "0x%016llX", static_cast<unsigned long long>(id));   // 61-bit ids do not fit a Lua number
        g_lua_pushstring(L, text);
        return 1;
    }

    int __cdecl l_GetPaletteColor(lua_State* L)
    {
        if (!Active())
            return 0;
        const std::uint64_t name = Name(L, 1);
        V4 color{};
        Now([&]() { Call<void*>(dev::GetPaletteColor, &color, name); });
        return PushV(L, color, 4);
    }

    // ---- UI sounds (tpp::ui::utility and the UI SoundControl) ----

    int __cdecl l_StartSound(lua_State* L)
    {
        void* control = SoundControl();
        const int type = GetLuaInt(L, 1);
        if (!Active() || !control || type < 0 || type >= 0x98)   // the game's UI sound table has 0x98 entries
            return 0;
        Now([&]() { reinterpret_cast<void(__fastcall*)(void*, std::uint8_t)>(StartSoundFn())(control, static_cast<std::uint8_t>(type)); });
        return 0;
    }

    int __cdecl l_StartSoundEvent(lua_State* L)
    {
        void* control = SoundControl();
        const std::uint32_t event = static_cast<std::uint32_t>(Name(L, 1));
        if (!Active() || !control || !event)
            return 0;
        Now([&]() { reinterpret_cast<bool(__fastcall*)(void*, std::uint32_t, bool)>(SoundControlStartFn())(control, event, false); });
        return 0;
    }

    int __cdecl l_StopSoundEvent(lua_State* L)
    {
        void* control = SoundControl();
        const std::uint32_t event = static_cast<std::uint32_t>(Name(L, 1));
        const float fade = Num(L, 2);
        if (!Active() || !control || !event)
            return 0;
        Now([&]() { reinterpret_cast<bool(__fastcall*)(void*, std::uint32_t, float, int)>(SoundControlStopFn())(control, event, fade, 4); });   // 4 = linear fade
        return 0;
    }

    // ---- input: the UI pad, keyboard and mouse the game's menus read ----

    std::uint32_t ButtonName(lua_State* L, int idx)
    {
        return static_cast<std::uint32_t>(Name(L, idx));
    }

    int PushBool(lua_State* L, bool value)
    {
        g_lua_pushboolean(L, value);
        return 1;
    }

    bool ReadThroughLock(std::size_t offset, std::uint32_t name, bool& on)
    {
        if (!uipad::HoldsOwnDisableRequest())
            return false;
        switch (offset)
        {
        case dev::IsButtonPress: case dev::IsButtonPressByName:       return uipad::Press(name, on);
        case dev::IsButtonPressing: case dev::IsButtonPressingByName: return uipad::Pressing(name, on);
        case dev::IsButtonRelease: case dev::IsButtonReleaseByName:   return uipad::Release(name, on);
        default:                                                      return false;
        }
    }

    int ByNumber(lua_State* L, std::size_t offset)
    {
        if (!Active() || LuaType(L, 1) != LUA_T_NUMBER)
            return 0;
        const std::uint32_t button = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
        bool on = false;
        if (!ReadThroughLock(offset, button, on))
        {
            uipad::OwnQuery own;
            Now([&]() { on = Call<bool>(offset, button); });
        }
        return PushBool(L, on);
    }

    int ByName(lua_State* L, std::size_t offset)
    {
        if (!Active())
            return 0;
        const std::uint32_t name = ButtonName(L, 1);
        // the game takes a 48-bit StringId here: its decide checks compare the whole id (and then also count the mouse)
        const std::uint64_t id = Name64(L, 1) & ID48_MASK;
        bool on = false;
        if (!ReadThroughLock(offset, name, on))
        {
            uipad::OwnQuery own;
            Now([&]() { on = Call<bool>(offset, id); });
        }
        return PushBool(L, on);
    }

    int __cdecl l_IsButtonPress(lua_State* L)                 { return ByNumber(L, dev::IsButtonPress); }
    int __cdecl l_IsButtonPressByName(lua_State* L)           { return ByName(L, dev::IsButtonPressByName); }
    int __cdecl l_IsButtonPressing(lua_State* L)              { return ByNumber(L, dev::IsButtonPressing); }
    int __cdecl l_IsButtonPressingByName(lua_State* L)        { return ByName(L, dev::IsButtonPressingByName); }
    int __cdecl l_IsButtonRelease(lua_State* L)               { return ByNumber(L, dev::IsButtonRelease); }
    int __cdecl l_IsButtonReleaseByName(lua_State* L)         { return ByName(L, dev::IsButtonReleaseByName); }
    int __cdecl l_IsKeyboardPress(lua_State* L)               { return ByNumber(L, dev::IsKeyboardPress); }
    int __cdecl l_IsKeyboardPressByName(lua_State* L)         { return ByName(L, dev::IsKeyboardPressByName); }
    int __cdecl l_IsKeyboardPressing(lua_State* L)            { return ByNumber(L, dev::IsKeyboardPressing); }
    int __cdecl l_IsKeyboardPressingByName(lua_State* L)      { return ByName(L, dev::IsKeyboardPressingByName); }
    int __cdecl l_IsKeyboardRelease(lua_State* L)             { return ByNumber(L, dev::IsKeyboardRelease); }
    int __cdecl l_IsKeyboardReleaseByName(lua_State* L)       { return ByName(L, dev::IsKeyboardReleaseByName); }
    int __cdecl l_IsMouseButtonDown(lua_State* L)             { return ByNumber(L, dev::IsMouseButtonDown); }
    int __cdecl l_IsMouseButtonDownByName(lua_State* L)       { return ByName(L, dev::IsMouseButtonDownByName); }
    int __cdecl l_IsMouseButtonPressing(lua_State* L)         { return ByNumber(L, dev::IsMouseButtonPressing); }
    int __cdecl l_IsMouseButtonPressingByName(lua_State* L)   { return ByName(L, dev::IsMouseButtonPressingByName); }
    int __cdecl l_IsMouseButtonUp(lua_State* L)               { return ByNumber(L, dev::IsMouseButtonUp); }
    int __cdecl l_IsMouseButtonUpByName(lua_State* L)         { return ByName(L, dev::IsMouseButtonUpByName); }

    int DisablePadId(lua_State* L, std::uint32_t& id)
    {
        id = static_cast<std::uint32_t>(Name64(L, 1));
        return id != 0;
    }

    // SetDisablePad(id): the game's pad and keyboard reads stop until ResetDisablePad(id). Unlike the exe's own, a request
    // made here also stops the game's mouse reads (buttons and wheel); V_UixUtility's reads keep working
    int __cdecl l_SetDisablePad(lua_State* L)
    {
        std::uint32_t id = 0;
        if (!Active() || !DisablePadId(L, id))
            return 0;
        bool held = false;
        Now([&]() {
            Call<void>(dev::SetDisablePad, id);
            held = Call<bool>(dev::IsDisablePad, id);
        });
        uipad::NoteOwnDisableRequest(id, held);
        return 0;
    }

    int __cdecl l_ResetDisablePad(lua_State* L)
    {
        std::uint32_t id = 0;
        if (!DisablePadId(L, id))
            return 0;
        if (Active())
            Now([&]() { Call<void>(dev::ResetDisablePad, id); });
        uipad::NoteOwnDisableRequest(id, false);
        return 0;
    }

    int __cdecl l_IsDisablePad(lua_State* L)
    {
        std::uint32_t id = 0;
        if (!Active() || !DisablePadId(L, id))
            return 0;
        bool held = false;
        Now([&]() { held = Call<bool>(dev::IsDisablePad, id); });
        return PushBool(L, held);
    }

    // GetButtonNumber(name, convert): convert maps the name to the pad button it is bound to
    int __cdecl l_GetButtonNumber(lua_State* L)
    {
        if (!Active())
            return 0;
        const std::uint32_t name = ButtonName(L, 1);
        const bool convert = GetLuaBool(L, 2);
        std::uint32_t number = 0;
        Now([&]() { number = Call<std::uint32_t>(dev::GetButtonNumber, name, convert); });
        g_lua_pushnumber(L, number);
        return 1;
    }

    int __cdecl l_IsButtonRepeat(lua_State* L)
    {
        if (!Active() || LuaType(L, 1) != LUA_T_NUMBER)
            return 0;
        const std::uint32_t button = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
        const float a = Num(L, 2), b = Num(L, 3);
        bool on = false;
        if (!uipad::HoldsOwnDisableRequest() || !uipad::Repeat(button, a, b, on))
            Now([&]() { on = Call<bool>(dev::IsButtonRepeat, button, a, b); });
        return PushBool(L, on);
    }

    int __cdecl l_IsButtonRepeatByName(lua_State* L)
    {
        if (!Active())
            return 0;
        const std::uint32_t name = ButtonName(L, 1);
        const float a = Num(L, 2), b = Num(L, 3);
        bool on = false;
        if (!uipad::HoldsOwnDisableRequest() || !uipad::Repeat(name, a, b, on))
            Now([&]() { on = Call<bool>(dev::IsButtonRepeatByName, name, a, b); });
        return PushBool(L, on);
    }

    int __cdecl l_IsButtonLongPress(lua_State* L)
    {
        if (!Active() || LuaType(L, 1) != LUA_T_NUMBER)
            return 0;
        const std::uint32_t button = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
        const float seconds = Num(L, 2);
        bool on = false;
        int result = 0;
        if (uipad::HoldsOwnDisableRequest() && uipad::LongPressResult(button, seconds, result))
            on = result == 1;   // 1 means it was held long enough
        else
            Now([&]() { on = Call<bool>(dev::IsButtonLongPress, button, seconds); });
        return PushBool(L, on);
    }

    int __cdecl l_IsButtonLongPressByName(lua_State* L)
    {
        if (!Active())
            return 0;
        const std::uint32_t name = ButtonName(L, 1);
        const float seconds = Num(L, 2);
        bool on = false;
        int result = 0;
        if (uipad::HoldsOwnDisableRequest() && uipad::LongPressResult(name, seconds, result))
            on = result == 1;   // 1 means it was held long enough
        else
            Now([&]() { on = Call<bool>(dev::IsButtonLongPressByName, name, seconds); });
        return PushBool(L, on);
    }

    int __cdecl l_GetButtonLongPressResultByName(lua_State* L)
    {
        if (!Active())
            return 0;
        const std::uint32_t name = ButtonName(L, 1);
        const float seconds = Num(L, 2);
        int result = 0;
        if (!uipad::HoldsOwnDisableRequest() || !uipad::LongPressResult(name, seconds, result))
            Now([&]() { result = Call<int>(dev::GetButtonLongPressResultByName, name, seconds); });
        g_lua_pushnumber(L, result);   // 0 idle, 1 held long enough, 2 still holding, 3 let go too early
        return 1;
    }

    // GetAnalogStickAxis(stick): 0 left stick, 1 right stick
    int __cdecl l_GetAnalogStickAxis(lua_State* L)
    {
        if (!Active())
            return 0;
        const std::uint32_t stick = static_cast<std::uint32_t>(GetLuaInt(L, 1));
        float x = 0.0f, y = 0.0f;
        Now([&]() { Call<void>(dev::GetAnalogStickAxis, stick, &x, &y); });
        g_lua_pushnumber(L, x);
        g_lua_pushnumber(L, y);
        return 2;
    }

    int __cdecl l_IsUseKeyboard(lua_State* L)
    {
        if (!Active())
            return 0;
        bool on = false;
        Now([&]() { on = Call<bool>(dev::IsUseKeyboard); });
        return PushBool(L, on);
    }

    // ButtonNameToKeyCode(name) -> status, keyCode; status 0x14 = no key bound
    int __cdecl l_ButtonNameToKeyCode(lua_State* L)
    {
        if (!Active())
            return 0;
        const std::uint32_t name = ButtonName(L, 1);
        std::uint32_t key = 0, status = 0;
        Now([&]() { status = Call<std::uint32_t>(dev::ButtonNameToKeyCode, name, &key); });
        g_lua_pushnumber(L, status);
        g_lua_pushnumber(L, key);
        return 2;
    }

    int __cdecl l_GetMouseMoveXY(lua_State* L)
    {
        if (!Active())
            return 0;
        float x = 0.0f, y = 0.0f;
        Now([&]() { Call<void>(dev::GetMouseMoveXY, &x, &y); });
        g_lua_pushnumber(L, x);
        g_lua_pushnumber(L, y);
        return 2;
    }

    int __cdecl l_GetMouseWheel(lua_State* L)
    {
        if (!Active())
            return 0;
        float wheel = 0.0f;
        uipad::OwnQuery own;
        Now([&]() { wheel = Call<float>(dev::GetMouseWheel); });
        g_lua_pushnumber(L, wheel);
        return 1;
    }

    // ---- button glyphs and atlas cells ----

    int __cdecl l_SetButtonIconTextureByName(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "SetButtonIconTextureByName", { Kind::NodeMesh }, handle, o) || !Function(UiFunction::SetButtonIconTextureByName))
            return 0;
        const std::uint64_t name = ButtonName(L, 2);
        Queue(handle, [name](const Object& t) {
            reinterpret_cast<void(__fastcall*)(void*, std::uint64_t)>(Function(UiFunction::SetButtonIconTextureByName))(t.ptr, name);
        });
        return 0;
    }

    // binds this platform's button atlas to the mesh; SetButtonIconTextureByName picks the cell
    int __cdecl l_SetPlatformButtonTexture(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "SetPlatformButtonTexture", { Kind::NodeMesh }, handle, o) || !Function(UiFunction::SetPlatformButtonTexture))
            return 0;
        Queue(handle, [](const Object& t) {
            reinterpret_cast<bool(__fastcall*)(void*)>(Function(UiFunction::SetPlatformButtonTexture))(t.ptr);
        });
        return 0;
    }

    // SetUvShift(mesh, cell, gridSize): shows one cell of a gridSize x gridSize atlas, counted left to right, top to bottom
    int UvCell(lua_State* L, const char* fn, std::size_t offset)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, fn, { Kind::NodeMesh }, handle, o))
            return 0;
        const std::uint32_t cell = static_cast<std::uint32_t>(GetLuaInt(L, 2));
        const std::uint32_t grid = static_cast<std::uint32_t>(GetLuaInt(L, 3));
        if (grid == 0)
            return 0;
        const float u = static_cast<float>(cell % grid) / grid;
        const float v = static_cast<float>(cell / grid) / grid;
        Queue(handle, [offset, u, v](const Object& t) { Call<void>(offset, t.ptr, u, v); });
        return 0;
    }

    int __cdecl l_SetUvShift(lua_State* L)     { return UvCell(L, "SetUvShift", dev::SetShaderMaskTexUvShift); }
    int __cdecl l_SetUvShiftBase(lua_State* L) { return UvCell(L, "SetUvShiftBase", dev::SetShaderBaseTexUvShift); }

    // ---- line drawing ----

    // SetLineDraw(lineNode, indices, vertices): indices are 0-based vertex numbers, one line per neighbouring pair;
    // vertices are {x, y[, z[, w]]}
    int __cdecl l_SetLineDraw(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!Active() || !Arg(L, 1, "SetLineDraw", { Kind::NodeLine }, handle, o))
            return 0;
        if (LuaType(L, 2) != LUA_T_TABLE || LuaType(L, 3) != LUA_T_TABLE)
        {
            LogWarnOnce("V_UixUtility.SetLineDraw: needs a table of indices and a table of vertices");
            return 0;
        }
        constexpr int kMaxVertices = 4096;
        std::vector<LineVertex> vertices;
        const int vertexCount = static_cast<int>(LuaObjLen(L, 3));
        for (int i = 1; i <= vertexCount && i <= kMaxVertices; ++i)
        {
            LuaRawGetI(L, 3, i);
            const int v = GetLuaTop(L);
            LineVertex vertex{ 0.0f, 0.0f, 0.0f, 1.0f };
            if (LuaType(L, v) == LUA_T_TABLE)
            {
                float* fields[4] = { &vertex.x, &vertex.y, &vertex.z, &vertex.w };
                for (int k = 0; k < 4; ++k)
                {
                    LuaRawGetI(L, v, k + 1);
                    if (LuaType(L, -1) == LUA_T_NUMBER)
                        *fields[k] = GetLuaNumber(L, -1);
                    LuaPop(L, 1);
                }
            }
            vertices.push_back(vertex);
            LuaPop(L, 1);
        }
        std::vector<std::uint16_t> indices;
        const int indexCount = static_cast<int>(LuaObjLen(L, 2));
        for (int i = 1; i <= indexCount && i <= 0xFFFF; ++i)
        {
            LuaRawGetI(L, 2, i);
            const int index = LuaType(L, -1) == LUA_T_NUMBER ? GetLuaInt(L, -1) : -1;
            LuaPop(L, 1);
            if (index < 0 || index >= static_cast<int>(vertices.size()))
            {
                LogWarnOnce("V_UixUtility.SetLineDraw: indices[%d] is not a vertex number from 0 to %d", i, static_cast<int>(vertices.size()) - 1);
                return 0;
            }
            indices.push_back(static_cast<std::uint16_t>(index));
        }
        if (indices.size() < 2)
        {
            LogWarnOnce("V_UixUtility.SetLineDraw: a line needs at least two indices");
            return 0;
        }
        QueueLineDraw(handle, std::move(indices), std::move(vertices));
        return 0;
    }

    int __cdecl l_ResetLineDraw(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (Active() && Arg(L, 1, "ResetLineDraw", { Kind::NodeLine }, handle, o))
            QueueResetLineDraw(handle);
        return 0;
    }

    // ---- window tree ----

    // AddChildWindow(parent, child): the child follows the parent and gets its messages; only windows from CreateWindow
    int __cdecl l_AddChildWindow(lua_State* L)
    {
        std::uint32_t childHandle = 0;
        Object parent, child;
        if (!Active() || !Arg(L, 1, "AddChildWindow", { Kind::Window }, parent) || !Arg(L, 2, "AddChildWindow", { Kind::Window }, childHandle, child))
            return 0;
        if (!QueueAddChildWindow(parent, childHandle, child))
            LogWarnOnce("V_UixUtility.AddChildWindow: the child must be a window made by V_UixUtility.CreateWindow, other than the parent");
        return 0;
    }

    // PostTriggerMessage(window, trigger): SendTrigger for the window and every window below it
    int __cdecl l_PostTriggerMessage(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object w;
        if (!Active() || !Arg(L, 1, "PostTriggerMessage", { Kind::Window }, handle, w) || !Function(UiFunction::PostTriggerMessage))
            return 0;
        const std::uint64_t trigger = Name64(L, 2) & ID48_MASK;
        Queue(handle, [trigger](const Object& t) {
            reinterpret_cast<void(__fastcall*)(void*, std::uint64_t)>(Function(UiFunction::PostTriggerMessage))(t.ptr, trigger);
        });
        return 0;
    }

    // ---- TPP's own UiUtility: button icons that follow pad / keyboard, pausing the 3D scene ----

    namespace ui   // its retail EN/JP 1.0.15.4 vtable slots
    {
        constexpr std::size_t PauseDrawScene = 0x228, GetPadButtonUvShift = 0x4D0;
        constexpr std::size_t SetPadKeyboardIconTexture4 = 0x8C8, SetPadKeyboardIconTexture2 = 0x8D0;
        constexpr std::size_t RegistButtonMeshParam = 0x8E8, EraseButtonMeshParam = 0x8F0, EraseButtonMeshParamFromRegistId = 0x8F8;
        constexpr std::size_t IsChangePadKeyboard = 0x920;
    }

    template <typename R, typename... A>
    R UiCall(std::size_t offset, A... args)
    {
        using F = R(__fastcall*)(void**, A...);
        return reinterpret_cast<F>(UiUtilityMethod(offset))(UiUtility(), args...);
    }

    bool UiActive()
    {
        return Active() && UiUtility() != nullptr;
    }

    // GetPadButtonUvShift(type, number) -> u, v: where a pad glyph sits in the button atlas (type 0 = pad button number 0-15)
    int __cdecl l_GetPadButtonUvShift(lua_State* L)
    {
        if (!UiActive())
            return 0;
        const std::uint8_t type = static_cast<std::uint8_t>(GetLuaInt(L, 1));
        const std::uint32_t number = static_cast<std::uint32_t>(GetLuaInt(L, 2));
        float u = 0.0f, v = 0.0f;
        bool found = false;
        Now([&]() { found = UiCall<bool>(ui::GetPadButtonUvShift, &u, &v, type, number); });
        if (!found)
            return 0;   // no picture for that type and number
        g_lua_pushnumber(L, u);
        g_lua_pushnumber(L, v);
        return 2;
    }

    // SetPadKeyboardIconTexture(mesh, buttonName[, u, v]): the key glyph when the player is on keyboard;
    // otherwise, with u and v, the pad glyph at that atlas position
    int __cdecl l_SetPadKeyboardIconTexture(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!UiActive() || !Arg(L, 1, "SetPadKeyboardIconTexture", { Kind::NodeMesh }, handle, o))
            return 0;
        const std::uint32_t button = ButtonName(L, 2);
        if (Present(L, 3))
        {
            const float u = Num(L, 3), v = Num(L, 4);
            Queue(handle, [button, u, v](const Object& t) { UiCall<bool>(ui::SetPadKeyboardIconTexture4, t.ptr, button, u, v); });
        }
        else
        {
            Queue(handle, [button](const Object& t) { UiCall<bool>(ui::SetPadKeyboardIconTexture2, t.ptr, button); });
        }
        return 0;
    }

    // RegistButtonMeshParam(mesh, buttonName, u, v, registId): shows the glyph now and again whenever the player
    // switches between pad and keyboard; the game holds up to 50
    int __cdecl l_RegistButtonMeshParam(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!UiActive() || !Arg(L, 1, "RegistButtonMeshParam", { Kind::NodeMesh }, handle, o))
            return 0;
        const std::uint32_t button = ButtonName(L, 2);
        const float u = Num(L, 3), v = Num(L, 4);
        const std::uint32_t registId = static_cast<std::uint32_t>(Name(L, 5));
        Queue(handle, [button, u, v, registId](const Object& t) {
            if (UiCall<bool>(ui::RegistButtonMeshParam, t.ptr, button, u, v, registId))
                RememberButtonMesh(t.ptr, t.window, registId);
            else
                LogWarnOnce("V_UixUtility.RegistButtonMeshParam: not registered (that mesh already is, or the game's 50 slots are full)");
        });
        return 0;
    }

    int __cdecl l_EraseButtonMeshParam(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object o;
        if (!UiActive() || !Arg(L, 1, "EraseButtonMeshParam", { Kind::NodeMesh }, handle, o))
            return 0;
        Queue(handle, [](const Object& t) {
            UiCall<bool>(ui::EraseButtonMeshParam, t.ptr);
            ForgetButtonMesh(t.ptr);
        });
        return 0;
    }

    int __cdecl l_EraseButtonMeshParamFromRegistId(lua_State* L)
    {
        if (!UiActive())
            return 0;
        const std::uint32_t registId = static_cast<std::uint32_t>(Name(L, 1));
        QueueNoHandle([registId]() {
            UiCall<bool>(ui::EraseButtonMeshParamFromRegistId, registId);
            ForgetButtonMeshesOf(registId);
        });
        return 0;
    }

    // true on the frame the player switches between pad and keyboard / mouse
    int __cdecl l_IsChangePadKeyboard(lua_State* L)
    {
        if (!UiActive())
            return 0;
        bool changed = false;
        Now([&]() { changed = UiCall<bool>(ui::IsChangePadKeyboard); });
        return PushBool(L, changed);
    }

    // PauseDrawScene(true) stops drawing the 3D world under a full-screen menu; PauseDrawScene(false) brings it back
    int __cdecl l_PauseDrawScene(lua_State* L)
    {
        if (!UiActive())
            return 0;
        const bool pause = GetLuaBool(L, 1);
        QueueNoHandle([pause]() {
            UiCall<void>(ui::PauseDrawScene, pause);
            NoteDrawScenePaused(pause);
        });
        return 0;
    }

    // ---- game state, names, icons, sounds ----

    int StateQuery(lua_State* L, UiFunction f)
    {
        if (!Active() || !Function(f))
            return 0;
        bool on = false;
        Now([&]() { on = reinterpret_cast<bool(__fastcall*)()>(Function(f))(); });
        return PushBool(L, on);
    }

    int __cdecl l_IsGamePauseMenu(lua_State* L) { return StateQuery(L, UiFunction::IsGamePauseMenu); }
    int __cdecl l_IsInDemo(lua_State* L)        { return StateQuery(L, UiFunction::IsInDemo); }

    // GetFocalLengthZoomRatio() -> the game camera's focal length / 21 mm: 1 = no zoom, higher while binoculars, a scope or aiming close in
    int __cdecl l_GetFocalLengthZoomRatio(lua_State* L)
    {
        void* fn = FocalLengthZoomRatioFn();
        if (!Active() || !fn)
            return 0;
        float ratio = 1.0f;
        Now([&]() { ratio = reinterpret_cast<float(__fastcall*)()>(fn)(); });
        g_lua_pushnumber(L, static_cast<double>(ratio));
        return 1;
    }

    // GetIconFtexPath(equipId[, variant]) -> the icon's path id as "0x..." (61-bit ids do not fit a Lua number)
    int __cdecl l_GetIconFtexPath(lua_State* L)
    {
        if (!Active() || !Function(UiFunction::GetIconFtexPath))
            return 0;
        const int equipId = GetLuaInt(L, 1);
        const int variant = Present(L, 2) ? GetLuaInt(L, 2) : 0;
        std::uint64_t path = 0;
        Now([&]() { reinterpret_cast<void*(__fastcall*)(std::uint64_t*, int, int)>(Function(UiFunction::GetIconFtexPath))(&path, equipId, variant); });
        char text[24];
        std::snprintf(text, sizeof(text), "0x%016llX", static_cast<unsigned long long>(path));
        g_lua_pushstring(L, text);
        return 1;
    }

    // GetWeaponItemNameLangId(equipId[, flag]) -> a lang id for GetLangText
    int __cdecl l_GetWeaponItemNameLangId(lua_State* L)
    {
        if (!Active() || !Function(UiFunction::GetWeaponItemNameLangId))
            return 0;
        const int equipId = GetLuaInt(L, 1);
        const bool flag = GetLuaBool(L, 2);
        std::uint64_t langId = 0;
        Now([&]() { reinterpret_cast<void*(__fastcall*)(std::uint64_t*, int, bool)>(Function(UiFunction::GetWeaponItemNameLangId))(&langId, equipId, flag); });
        g_lua_pushnumber(L, static_cast<double>(langId));
        return 1;
    }

    int __cdecl l_StopSound(lua_State* L)
    {
        void* control = SoundControl();
        const int type = GetLuaInt(L, 1);
        if (!Active() || !control || !Function(UiFunction::StopSound) || type < 0 || type >= 0x98)
            return 0;
        Now([&]() { reinterpret_cast<void(__fastcall*)(void*, std::uint8_t)>(Function(UiFunction::StopSound))(control, static_cast<std::uint8_t>(type)); });
        return 0;
    }

    // GetSoundTypeId(eventName) -> the UI sound type StartSound/StopSound take, nil when the UI sound table has no such event
    int __cdecl l_GetSoundTypeId(lua_State* L)
    {
        if (!Active() || !Function(UiFunction::GetSoundTypeId) || LuaType(L, 1) != LUA_T_STRING)
            return 0;
        const std::string name = GetLuaString(L, 1);
        std::uint8_t type = 0x98;
        Now([&]() { type = reinterpret_cast<std::uint8_t(__fastcall*)(const char*)>(Function(UiFunction::GetSoundTypeId))(name.c_str()); });
        if (type >= 0x98)
            return 0;
        g_lua_pushnumber(L, type);
        return 1;
    }

    luaL_Reg g_VUixUtilityLib[] =
    {
        { "CreateWindow",                    l_CreateWindow },
        { "DestroyWindow",                   l_DestroyWindow },
        { "FindWindow",                      l_FindWindow },
        { "GetWindows",                      l_GetWindows },
        { "GetChildWindowInterface",         l_GetChildWindowInterface },
        { "PostShowAndStartMessage",         l_PostShowAndStartMessage },
        { "PostHideAndStopMessage",          l_PostHideAndStopMessage },
        { "PostWakeLayoutMessage",           l_PostWakeLayoutMessage },
        { "PostSleepLayoutMessage",          l_PostSleepLayoutMessage },
        { "SendVisibleWindowMessage",        l_SendVisibleWindowMessage },
        { "SendConnectWindowMessage",        l_SendConnectWindowMessage },
        { "SendTrigger",                     l_SendTrigger },
        { "SendTriggerToParent",             l_SendTriggerToParent },
        { "GetTriggerPool",                  l_GetTriggerPool },
        { "IsTriggerReceived",               l_IsTriggerReceived },
        { kEnableAll,                        l_Inheritance<dev::EnableAllInheritance, kEnableAll> },
        { kEnableScale,                      l_Inheritance<dev::EnableAllInheritance + 0x08, kEnableScale> },
        { kEnableRotation,                   l_Inheritance<dev::EnableAllInheritance + 0x10, kEnableRotation> },
        { kEnableTranslation,                l_Inheritance<dev::EnableAllInheritance + 0x18, kEnableTranslation> },
        { kEnableColorRGB,                   l_Inheritance<dev::EnableAllInheritance + 0x20, kEnableColorRGB> },
        { kEnableColorAlpha,                 l_Inheritance<dev::EnableAllInheritance + 0x28, kEnableColorAlpha> },
        { kDisableAll,                       l_Inheritance<dev::DisableAllInheritance, kDisableAll> },
        { kDisableScale,                     l_Inheritance<dev::DisableAllInheritance + 0x08, kDisableScale> },
        { kDisableRotation,                  l_Inheritance<dev::DisableAllInheritance + 0x10, kDisableRotation> },
        { kDisableTranslation,               l_Inheritance<dev::DisableAllInheritance + 0x18, kDisableTranslation> },
        { kDisableColorRGB,                  l_Inheritance<dev::DisableAllInheritance + 0x20, kDisableColorRGB> },
        { kDisableColorAlpha,                l_Inheritance<dev::DisableAllInheritance + 0x28, kDisableColorAlpha> },

        { "GetLayout",                       l_GetLayout },
        { "IsHaveLayout",                    l_IsHaveLayout },
        { "GetModel",                        l_GetModel },
        { "GetAnimation",                    l_GetAnimation },
        { "IsHaveModelNodeCommon",           l_IsHaveModelNodeCommon },
        { "GetModelNodeCommon",              l_GetModelNodeCommon },
        { "GetModelNodeText",                l_GetModelNodeText },
        { "GetModelNodeMesh",                l_GetModelNodeMesh },
        { "GetModelNodeLine",                l_GetModelNodeLine },
        { "GetOrthogonalProjectionScaleFromModelComponentCamera", l_GetOrthogonalProjectionScaleFromModelComponentCamera },

        { "SetVisible",                      l_SetVisible },
        { "IsVisible",                       l_IsVisible },
        { "SetAlpha",                        l_SetAlpha },
        { "GetAlpha",                        l_GetAlpha },
        { "SetColorRGB",                     l_SetColorRGB },
        { "SetColorGroup",                   l_SetColorGroup },
        { "GetColorGroup",                   l_GetColorGroup },
        { "GetTranslation",                  l_GetTranslation },
        { "SetTranslation",                  l_SetTranslation },
        { "SetTranslationX",                 l_SetTranslationX },
        { "SetTranslationY",                 l_SetTranslationY },
        { "SetTranslationZ",                 l_SetTranslationZ },
        { "GetRotation",                     l_GetRotation },
        { "GetRotationDegree",               l_GetRotationDegree },
        { "SetRotation",                     l_SetRotation },
        { "SetRotationDegree",               l_SetRotationDegree },
        { "SetRotationDegreeX",              l_SetRotationDegreeX },
        { "SetRotationDegreeY",              l_SetRotationDegreeY },
        { "SetRotationDegreeZ",              l_SetRotationDegreeZ },
        { "SetQuaternion",                   l_SetQuaternion },
        { "SetScale",                        l_SetScale },
        { "GetScale",                        l_GetScale },
        { "SetStatus",                       l_SetStatus },
        { "ResetStatus",                     l_ResetStatus },
        { "ConnectComponent",                l_ConnectComponent },
        { "DisconnectComponent",             l_DisconnectComponent },
        { "GetMarkerPosition",               l_GetMarkerPosition },
        { "GetNodeScreenPosition",           l_GetNodeScreenPosition },
        { "GetMarkers",                      l_GetMarkers },
        { "GetEnemyRoutePoints",             l_GetEnemyRoutePoints },
        { "GetTraversalTranslation",         l_GetTraversalTranslation },
        { "GetTraversalScale",               l_GetTraversalScale },

        { "SetVerticalAlign",                l_SetVerticalAlign },
        { "SetTextAlign",                    l_SetTextAlign },
        { "SetFontSize",                     l_SetFontSize },
        { "SetFontGroup",                    l_SetFontGroup },
        { "GetLineWidth",                    l_GetLineWidth },
        { "GetLineHeight",                   l_GetLineHeight },
        { "SetLineWidth",                    l_SetLineWidth },
        { "SetLineHeight",                   l_SetLineHeight },
        { "CreateTextUnit",                  l_CreateTextUnit },
        { "DeleteTextUnit",                  l_DeleteTextUnit },
        { "SetTextForModelNodeText",         l_SetTextForModelNodeText },
        { "SetScrollTextForModelNodeText",   l_SetScrollTextForModelNodeText },
        { "SetupTextUnits",                  l_SetupTextUnits },
        { "SetTextUnitsForModelNodeText",    l_SetTextUnitsForModelNodeText },
        { "CreateBoxText",                   l_CreateBoxText },
        { "CreateScrollBoxText",             l_CreateScrollBoxText },
        { "GetLangText",                     l_GetLangText },
        { "StartTextScroll",                 l_StartTextScroll },
        { "StopTextScroll",                  l_StopTextScroll },
        { "GetTextTotalHeightInTextNode",    l_GetTextTotalHeightInTextNode },
        { "GetTextMaxWidthInTextNode",       l_GetTextMaxWidthInTextNode },
        { "GetTextNodeWidth",                l_GetTextNodeWidth },
        { "GetTextNodeHeight",               l_GetTextNodeHeight },
        { "SetTextNodeWidth",                l_SetTextNodeWidth },
        { "SetTextNodeHeight",               l_SetTextNodeHeight },

        { "SetTextureName",                  l_SetTextureName },
        { "CheckAndSetTextureName",          l_CheckAndSetTextureName },
        { "SetTextureLevel",                 l_SetTextureLevel },
        { "IsTextureLoaded",                 l_IsTextureLoaded },
        { "SetLoadingTexture",               l_SetLoadingTexture },
        { "IsDispLoadingTexture",            l_IsDispLoadingTexture },
        { "CreatePrefetchTexture",           l_CreatePrefetchTexture },
        { "RemovePrefetchTexture",           l_RemovePrefetchTexture },
        { "IsReadyPrefetchTexture",          l_IsReadyPrefetchTexture },
        { "RegisterLoadingMeshTexture",      l_RegisterLoadingMeshTexture },
        { "UnregisterLoadingMeshTexture",    l_UnregisterLoadingMeshTexture },
        { "SetShaderParameter",              l_SetShaderParameter },
        { "SetShaderTechnique",              l_SetShaderTechnique },
        { "SetShaderBaseTexUvShift",         l_SetShaderBaseTexUvShift },
        { "SetShaderMaskTexUvShift",         l_SetShaderMaskTexUvShift },
        { "SetShaderScreenTexUvShift",       l_SetShaderScreenTexUvShift },
        { "SetShaderBaseTexUvRepeat",        l_SetShaderBaseTexUvRepeat },
        { "SetShaderMaskTexUvRepeat",        l_SetShaderMaskTexUvRepeat },
        { "SetShaderScreenTexUvRepeat",      l_SetShaderScreenTexUvRepeat },
        { "SetVertexTranslate",              l_SetVertexTranslate },
        { "DisableShaderReset",              l_DisableShaderReset },

        { "StartAnimation",                  l_StartAnimation },
        { "StartInstantAnimation",           l_StartInstantAnimation },
        { "StartLoopAnimation",              l_StartLoopAnimation },
        { "StartAnimationWithRatio",         l_StartAnimationWithRatio },
        { "StartAnimationWithStartRatio",    l_StartAnimationWithStartRatio },
        { "RecoverForAnimationWithRatio",    l_RecoverForAnimationWithRatio },
        { "PauseAnimation",                  l_PauseAnimation },
        { "PauseInitAnimation",              l_PauseInitAnimation },
        { "PauseEndAnimation",               l_PauseEndAnimation },
        { "IsAnimationRunning",              l_IsAnimationRunning },
        { "SetAnimationStopTime",            l_SetAnimationStopTime },
        { "SetAnimationSpeed",               l_SetAnimationSpeed },
        { "IsAnimationPastTime",             l_IsAnimationPastTime },
        { "GetAnimationTimeLength",          l_GetAnimationTimeLength },

        { "SetModelNodePriority",            l_SetModelNodePriority },
        { "UpdateModelDrawOrder",            l_UpdateModelDrawOrder },
        { "SetModelStencil",                 l_SetModelStencil },
        { "SetModelNodeBillboard",           l_SetModelNodeBillboard },
        { "GetDrawPriority",                 l_GetDrawPriority },
        { "SetDrawPriority",                 l_SetDrawPriority },
        { "GetDrawPriorityFromTable",        l_GetDrawPriorityFromTable },

        { "GetGameFrameRawDeltaTime",        l_GetGameFrameRawDeltaTime },
        { "GetGameFrameElapsedTimeSinceStartup", l_GetGameFrameElapsedTimeSinceStartup },
        { "RotQuatToEuler",                  l_RotQuatToEuler },
        { "RotEulerToQuat",                  l_RotEulerToQuat },
        { "RotEularRadianToDegree",          l_RotEularRadianToDegree },
        { "RotEularDegreeToRadian",          l_RotEularDegreeToRadian },
        { "GetPathIdFromChar",               l_GetPathIdFromChar },
        { "GetPaletteColor",                 l_GetPaletteColor },

        { "ForceLoopAnimation",              l_ForceLoopAnimation },
        { "UnsetForceLoopAnimation",         l_UnsetForceLoopAnimation },

        { "GetButtonNumber",                 l_GetButtonNumber },
        { "IsButtonPress",                   l_IsButtonPress },
        { "IsButtonPressByName",             l_IsButtonPressByName },
        { "IsButtonPressing",                l_IsButtonPressing },
        { "IsButtonPressingByName",          l_IsButtonPressingByName },
        { "IsButtonRelease",                 l_IsButtonRelease },
        { "IsButtonReleaseByName",           l_IsButtonReleaseByName },
        { "IsButtonRepeat",                  l_IsButtonRepeat },
        { "IsButtonRepeatByName",            l_IsButtonRepeatByName },
        { "IsButtonLongPress",               l_IsButtonLongPress },
        { "IsButtonLongPressByName",         l_IsButtonLongPressByName },
        { "GetButtonLongPressResultByName",  l_GetButtonLongPressResultByName },
        { "GetAnalogStickAxis",              l_GetAnalogStickAxis },
        { "IsUseKeyboard",                   l_IsUseKeyboard },
        { "ButtonNameToKeyCode",             l_ButtonNameToKeyCode },
        { "IsKeyboardPress",                 l_IsKeyboardPress },
        { "IsKeyboardPressByName",           l_IsKeyboardPressByName },
        { "IsKeyboardPressing",              l_IsKeyboardPressing },
        { "IsKeyboardPressingByName",        l_IsKeyboardPressingByName },
        { "IsKeyboardRelease",               l_IsKeyboardRelease },
        { "IsKeyboardReleaseByName",         l_IsKeyboardReleaseByName },
        { "GetMouseMoveXY",                  l_GetMouseMoveXY },
        { "GetMouseWheel",                   l_GetMouseWheel },
        { "IsMouseButtonDown",               l_IsMouseButtonDown },
        { "IsMouseButtonDownByName",         l_IsMouseButtonDownByName },
        { "IsMouseButtonPressing",           l_IsMouseButtonPressing },
        { "IsMouseButtonPressingByName",     l_IsMouseButtonPressingByName },
        { "IsMouseButtonUp",                 l_IsMouseButtonUp },
        { "IsMouseButtonUpByName",           l_IsMouseButtonUpByName },
        { "SetDisablePad",                   l_SetDisablePad },
        { "ResetDisablePad",                 l_ResetDisablePad },
        { "IsDisablePad",                    l_IsDisablePad },

        { "GetPadButtonUvShift",             l_GetPadButtonUvShift },
        { "SetPadKeyboardIconTexture",       l_SetPadKeyboardIconTexture },
        { "RegistButtonMeshParam",           l_RegistButtonMeshParam },
        { "EraseButtonMeshParam",            l_EraseButtonMeshParam },
        { "EraseButtonMeshParamFromRegistId", l_EraseButtonMeshParamFromRegistId },
        { "IsChangePadKeyboard",             l_IsChangePadKeyboard },
        { "PauseDrawScene",                  l_PauseDrawScene },
        { "SetButtonIconTextureByName",      l_SetButtonIconTextureByName },
        { "SetPlatformButtonTexture",        l_SetPlatformButtonTexture },
        { "SetUvShift",                      l_SetUvShift },
        { "SetUvShiftBase",                  l_SetUvShiftBase },
        { "SetLineDraw",                     l_SetLineDraw },
        { "ResetLineDraw",                   l_ResetLineDraw },
        { "AddChildWindow",                  l_AddChildWindow },
        { "PostTriggerMessage",              l_PostTriggerMessage },
        { "IsGamePauseMenu",                 l_IsGamePauseMenu },
        { "IsInDemo",                        l_IsInDemo },
        { "GetFocalLengthZoomRatio",         l_GetFocalLengthZoomRatio },
        { "GetIconFtexPath",                 l_GetIconFtexPath },
        { "GetWeaponItemNameLangId",         l_GetWeaponItemNameLangId },

        { "StartSound",                      l_StartSound },
        { "StopSound",                       l_StopSound },
        { "GetSoundTypeId",                  l_GetSoundTypeId },
        { "StartSoundEvent",                 l_StartSoundEvent },
        { "StopSoundEvent",                  l_StopSoundEvent },
        { nullptr, nullptr }
    };
}

bool Register_V_UixUtilityLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_UixUtility", g_VUixUtilityLib);
}
