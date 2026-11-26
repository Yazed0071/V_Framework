#include <pch.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaApi.h"
#include "log.h"
#include "FoxHashes.h"
#include "MissionCodeGuard.h"
#include "V_UixUtilitySubViewport.h"

#include "../hooks/ui/UixUtilityControl.h"

namespace
{
    using namespace UixUtilityControl;
    using AS = AddressSetRuntime::AddressSet;

    constexpr int LUA_T_BOOLEAN = 1;
    constexpr int LUA_T_NUMBER  = 3;
    constexpr int LUA_T_STRING  = 4;

    constexpr std::size_t   VIEWPORT_SCENE         = 0x270;
    constexpr std::size_t   VIEWPORT_COLOR_TEXTURE = 0x560;
    constexpr std::size_t   VIEWPORT_CAMERA        = 0x570;
    constexpr std::size_t   VIEWPORT_EXPOSURE      = 0x594;
    constexpr std::size_t   VIEWPORT_PASSES        = 0x5E8;
    constexpr std::uint32_t VIEWPORT_PASS_BITS     = 0x7F;
    constexpr std::uint32_t MIRROR_PASSES          = 0x62;   // bits 1, 5 and 6: what the hospital mirror draws the world with
    constexpr std::size_t   VIEWPORT_FLAGS         = 0x6E0;
    constexpr std::uint32_t VIEWPORT_DRAWS         = 0x10000;

    constexpr std::size_t   CAMERA_WORLD           = 0x30;
    constexpr std::size_t   CAMERA_VIEW            = 0x70;
    constexpr std::size_t   CAMERA_FLAGS           = 0x100;
    constexpr std::uint32_t CAMERA_ORTHOGONAL      = 0x4;
    constexpr std::size_t   CAMERA_HALF_WIDTH      = 0x120;
    constexpr std::size_t   CAMERA_NEAR            = 0x168;
    constexpr std::size_t   CAMERA_FAR             = 0x16C;

    constexpr std::size_t   MESH_DIRTY             = 0x70;
    constexpr std::uint16_t MESH_DIRTY_MATERIAL    = 0x100;
    constexpr std::size_t   MESH_STATE             = 0x80;
    constexpr std::size_t   MESH_MATERIAL          = 0xA8;
    constexpr std::size_t   MESH_SLOT_COUNT        = 0xD8;
    constexpr std::size_t   MESH_SLOTS             = 0xE0;
    constexpr std::size_t   SLOT_SIZE              = 0x20;
    constexpr std::size_t   SLOT_FLAGS             = 0x04;
    constexpr std::uint32_t SLOT_LOADING           = 0x200000;
    constexpr std::size_t   SLOT_TEXTURE           = 0x08;
    constexpr std::size_t   SLOT_RECT              = 0x10;
    constexpr std::uint32_t SLOT_RECT_UNSET        = 0xBF169F98;   // what ModelNodeMesh::SetTextureName writes after a bind

    constexpr int MIN_SIZE = 64;
    constexpr int MAX_SIZE = 2048;

    struct SubViewportDesc   // what TppSubViewport's constructor reads
    {
        const void*   name = nullptr;   // fox::SharedString*
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::int32_t  order = -10;      // the mirror's value
        std::uint8_t  ownTarget = 1;
        std::uint8_t  fixedScale = 0;   // 0: width and height become a share of the main view's size
        std::uint8_t  unused0[2] = {};
        float         scaleX = 1.0f;
        float         scaleY = 1.0f;
        std::uint8_t  scaleDirty = 1;
        std::uint8_t  unused1[7] = {};
        std::uint64_t unused2 = 0;
    };
    static_assert(sizeof(SubViewportDesc) == 0x30, "TppSubViewport desc layout");

    using Create_t           = void** (__fastcall*)(void** out, SubViewportDesc* desc);
    using GetDefault_t       = void*  (__fastcall*)();
    using RenderViewport_t   = void   (__fastcall*)(void* render, void* viewport);
    using SetScene_t         = void   (__fastcall*)(void* viewport, void* scene);
    using SetExposure_t      = void   (__fastcall*)(void* viewport, float exposure);
    using UpdateFromCamera_t = void   (__fastcall*)(void* viewport, bool restartHistory);
    using BindTexture_t      = int    (__fastcall*)(void* material, int slot, void* texture);
    using EntitySet_t        = void   (__fastcall*)(void** slot, void* entity);
    using SharedSet_t        = void   (__fastcall*)(void** slot, void* object);
    using StringMake_t       = void*  (__fastcall*)(void* out, const char* text);
    using StringDtor_t       = void   (__fastcall*)(void* text);
    using IsFobMode_t        = bool   (__fastcall*)();

    struct EngineFn
    {
        std::uintptr_t AS::* address;
        std::uint8_t   bytes[16];
        std::uint8_t   length;
        std::uint8_t   skipFrom;   // bytes skipFrom..skipFrom+3 are a rel32 and differ per build
    };

    const EngineFn kEngineFns[] =
    {
        { &AS::TppSubViewport_Create,          { 0x48, 0x89, 0x4C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30 }, 10, 0xFF },
        { &AS::FoxGrRender_GetDefault,         { 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x40, 0x28, 0xC3 }, 12, 3 },
        { &AS::FoxGrRender_AddViewport,        { 0x48, 0x85, 0xD2, 0x0F, 0x84, 0xF3, 0x00, 0x00, 0x00, 0x55, 0x56, 0x57 }, 12, 0xFF },
        { &AS::FoxGrRender_DeleteViewport,     { 0x40, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x40 }, 8, 0xFF },
        { &AS::FoxGrViewport_SetScene,         { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x89, 0x91, 0x70, 0x02, 0x00, 0x00 }, 16, 0xFF },
        { &AS::FoxGrViewport_SetExposure,      { 0x8B, 0x81, 0x94, 0x05, 0x00, 0x00, 0x0F, 0x28, 0x81, 0xE0, 0x04, 0x00, 0x00 }, 13, 0xFF },
        { &AS::FoxGrViewport_UpdateFromCamera, { 0x40, 0x57, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF9, 0x84, 0xD2 }, 14, 0xFF },
        { &AS::FoxGrMaterial_BindTexture,      { 0x48, 0x83, 0xEC, 0x28, 0x4D, 0x85, 0xC0, 0x75, 0x08, 0x83, 0xC8, 0xFF }, 12, 0xFF },
    };

    struct View
    {
        void* entity = nullptr;   // our fox EntityPtr reference to the TppSubViewport
    };

    std::mutex g_Lock;
    std::unordered_map<std::string, std::unique_ptr<View>> g_Views;
    int g_EngineChecked = 0;   // 0 not yet, 1 good, -1 a function does not match this build

    template <typename T>
    T Fn(std::uintptr_t address)
    {
        return reinterpret_cast<T>(ResolveGameAddress(address));
    }

    bool BytesMatchSEH(const std::uint8_t* code, const EngineFn& fn)
    {
        __try
        {
            for (std::uint8_t i = 0; i < fn.length; ++i)
            {
                const bool rel32 = fn.skipFrom != 0xFF && i >= fn.skipFrom && i < fn.skipFrom + 4;
                if (!rel32 && code[i] != fn.bytes[i])
                    return false;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool EngineReady()
    {
        if (g_EngineChecked != 0)
            return g_EngineChecked > 0;
        g_EngineChecked = 1;
        for (const EngineFn& fn : kEngineFns)
        {
            const std::uintptr_t address = gAddr.*fn.address;
            const auto* code = static_cast<const std::uint8_t*>(address ? ResolveGameAddress(address) : nullptr);
            if (code && BytesMatchSEH(code, fn))
                continue;
            LogError("V_UixUtility: sub viewport function at 0x%llX does not match this build, sub viewports stay off",
                static_cast<unsigned long long>(address));
            g_EngineChecked = -1;
            break;
        }
        const bool helpers = gAddr.Uix_EntityPtrBase_Set && gAddr.FoxSharedPtrBase_Set && gAddr.FoxSharedString_FromCString
            && gAddr.FoxSharedString_Dtor && gAddr.MainViewport_Global && gAddr.Barrier_IsFobMode;
        if (g_EngineChecked > 0 && !helpers)
        {
            LogError("V_UixUtility: sub viewport helpers have no address on this build, sub viewports stay off");
            g_EngineChecked = -1;
        }
        return g_EngineChecked > 0;
    }

    bool InFobSEH()
    {
        __try { return Fn<IsFobMode_t>(gAddr.Barrier_IsFobMode)(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    }

    bool Allowed()
    {
        return IsSupportedOnThisBuild() && !MissionCodeGuard::ShouldBypassHooks() && EngineReady() && !InFobSEH();
    }

    std::uint8_t* MainViewportSEH()
    {
        __try
        {
            auto** global = static_cast<std::uint8_t**>(ResolveGameAddress(gAddr.MainViewport_Global));
            return global ? *global : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    std::string NameArg(lua_State* L, int idx)
    {
        const char* s = LuaType(L, idx) == LUA_T_STRING ? GetLuaString(L, idx) : nullptr;
        return s ? std::string(s) : std::string();
    }

    float Num(lua_State* L, int idx, float fallback)
    {
        return LuaType(L, idx) == LUA_T_NUMBER ? GetLuaNumber(L, idx) : fallback;
    }

    int Size(lua_State* L, int idx)
    {
        const int asked = LuaType(L, idx) == LUA_T_NUMBER ? GetLuaInt(L, idx) : 512;
        return std::clamp(asked, MIN_SIZE, MAX_SIZE) & ~1;   // the engine rounds viewport sizes to even pixels
    }

    std::uint8_t* ViewportOf(const std::string& name)   // caller holds g_Lock
    {
        const auto it = g_Views.find(name);
        return it != g_Views.end() ? static_cast<std::uint8_t*>(it->second->entity) : nullptr;
    }

    bool CreateSEH(void** entity, const char* name, std::uint32_t width, std::uint32_t height)
    {
        alignas(16) std::uint8_t text[0x10] = {};
        alignas(16) SubViewportDesc desc;
        __try
        {
            Fn<StringMake_t>(gAddr.FoxSharedString_FromCString)(text, name);
            desc.name = text;
            desc.width = width;
            desc.height = height;
            Fn<Create_t>(gAddr.TppSubViewport_Create)(entity, &desc);
            Fn<StringDtor_t>(gAddr.FoxSharedString_Dtor)(text);
            auto* viewport = static_cast<std::uint8_t*>(*entity);
            void* render = Fn<GetDefault_t>(gAddr.FoxGrRender_GetDefault)();
            if (!viewport || !render)
            {
                Fn<EntitySet_t>(gAddr.Uix_EntityPtrBase_Set)(entity, nullptr);
                return false;
            }
            auto& passes = *reinterpret_cast<std::uint32_t*>(viewport + VIEWPORT_PASSES);
            passes = (passes & ~VIEWPORT_PASS_BITS) | MIRROR_PASSES;
            *reinterpret_cast<std::uint32_t*>(viewport + VIEWPORT_FLAGS) &= ~VIEWPORT_DRAWS;   // made drawing; nothing to draw until SetSubViewportDraw
            Fn<RenderViewport_t>(gAddr.FoxGrRender_AddViewport)(render, viewport);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void DestroySEH(void** entity)
    {
        auto* viewport = static_cast<std::uint8_t*>(*entity);
        if (!viewport)
            return;
        __try
        {
            *reinterpret_cast<std::uint32_t*>(viewport + VIEWPORT_FLAGS) &= ~VIEWPORT_DRAWS;
            Fn<EntitySet_t>(gAddr.Uix_EntityPtrBase_Set)(reinterpret_cast<void**>(viewport + VIEWPORT_CAMERA), nullptr);
            Fn<SetScene_t>(gAddr.FoxGrViewport_SetScene)(viewport, nullptr);
            if (void* render = Fn<GetDefault_t>(gAddr.FoxGrRender_GetDefault)())
                Fn<RenderViewport_t>(gAddr.FoxGrRender_DeleteViewport)(render, viewport);
            Fn<EntitySet_t>(gAddr.Uix_EntityPtrBase_Set)(entity, nullptr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            *entity = nullptr;   // a fault halfway: leak it rather than release it twice
        }
    }

    struct Aim
    {
        float eye[3];
        float target[3];
        float halfWidth;
        float nearClip;
        float farClip;
    };

    bool Normalize(float (&v)[3])
    {
        const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (length < 1e-6f)
            return false;
        v[0] /= length;
        v[1] /= length;
        v[2] /= length;
        return true;
    }

    void Cross(const float (&a)[3], const float (&b)[3], float (&out)[3])
    {
        out[0] = a[1] * b[2] - a[2] * b[1];
        out[1] = a[2] * b[0] - a[0] * b[2];
        out[2] = a[0] * b[1] - a[1] * b[0];
    }

    bool CameraMatrices(const Aim& aim, float (&world)[16], float (&view)[16])   // Fox cameras look along their world matrix's third row; the first row points to screen left
    {
        float forward[3] = { aim.target[0] - aim.eye[0], aim.target[1] - aim.eye[1], aim.target[2] - aim.eye[2] };
        if (!Normalize(forward))
            return false;
        float up[3] = { 0.0f, 1.0f, 0.0f };
        if (std::fabs(forward[1]) > 0.99f)
        {
            up[1] = 0.0f;
            up[2] = -1.0f;   // looking straight down or up: north is the top of the picture
        }
        float left[3];
        Cross(up, forward, left);
        if (!Normalize(left))
            return false;
        Cross(forward, left, up);
        const float* rows[3] = { left, up, forward };
        for (int r = 0; r < 3; ++r)
        {
            world[r * 4 + 0] = rows[r][0];
            world[r * 4 + 1] = rows[r][1];
            world[r * 4 + 2] = rows[r][2];
            world[r * 4 + 3] = 0.0f;
            view[0 * 4 + r] = rows[r][0];
            view[1 * 4 + r] = rows[r][1];
            view[2 * 4 + r] = rows[r][2];
            view[12 + r] = -(rows[r][0] * aim.eye[0] + rows[r][1] * aim.eye[1] + rows[r][2] * aim.eye[2]);
        }
        world[12] = aim.eye[0];
        world[13] = aim.eye[1];
        world[14] = aim.eye[2];
        world[15] = 1.0f;
        view[3] = view[7] = view[11] = 0.0f;
        view[15] = 1.0f;
        return true;
    }

    void AimSEH(std::uint8_t* viewport, const float* world, const float* view, const Aim& aim, void* mainScene)
    {
        __try
        {
            auto* camera = *reinterpret_cast<std::uint8_t**>(viewport + VIEWPORT_CAMERA);
            if (!camera)
                return;
            std::memcpy(camera + CAMERA_WORLD, world, 16 * sizeof(float));
            std::memcpy(camera + CAMERA_VIEW, view, 16 * sizeof(float));
            *reinterpret_cast<std::uint32_t*>(camera + CAMERA_FLAGS) |= CAMERA_ORTHOGONAL;
            *reinterpret_cast<float*>(camera + CAMERA_HALF_WIDTH) = aim.halfWidth;
            *reinterpret_cast<float*>(camera + CAMERA_NEAR) = aim.nearClip;
            *reinterpret_cast<float*>(camera + CAMERA_FAR) = aim.farClip;
            if (mainScene && *reinterpret_cast<void**>(viewport + VIEWPORT_SCENE) != mainScene)
                Fn<SetScene_t>(gAddr.FoxGrViewport_SetScene)(viewport, mainScene);
            Fn<UpdateFromCamera_t>(gAddr.FoxGrViewport_UpdateFromCamera)(viewport, true);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void* SceneOfSEH(std::uint8_t* viewport)
    {
        __try { return viewport ? *reinterpret_cast<void**>(viewport + VIEWPORT_SCENE) : nullptr; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    void SetDrawSEH(std::uint8_t* viewport, bool on)
    {
        __try
        {
            auto& flags = *reinterpret_cast<std::uint32_t*>(viewport + VIEWPORT_FLAGS);
            flags = on ? (flags | VIEWPORT_DRAWS) : (flags & ~VIEWPORT_DRAWS);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void SetExposureSEH(std::uint8_t* viewport, float exposure)
    {
        __try { Fn<SetExposure_t>(gAddr.FoxGrViewport_SetExposure)(viewport, exposure); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    bool HasTextureSEH(std::uint8_t* viewport)
    {
        __try { return viewport && *reinterpret_cast<void**>(viewport + VIEWPORT_COLOR_TEXTURE) != nullptr; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    float ExposureOfSEH(std::uint8_t* viewport)
    {
        __try { return viewport ? *reinterpret_cast<float*>(viewport + VIEWPORT_EXPOSURE) : 0.0f; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0.0f; }
    }

    void BindSEH(std::uint8_t* mesh, int slot, std::uint8_t* viewport)   // ModelNodeMesh::SetTextureName's steps for a ready texture
    {
        __try
        {
            void* texture = *reinterpret_cast<void**>(viewport + VIEWPORT_COLOR_TEXTURE);
            void* material = *reinterpret_cast<void**>(mesh + MESH_MATERIAL);
            if (!texture || !material)
                return;
            Fn<BindTexture_t>(gAddr.FoxGrMaterial_BindTexture)(material, slot, texture);
            const int count = *reinterpret_cast<int*>(mesh + MESH_SLOT_COUNT);
            auto* slots = *reinterpret_cast<std::uint8_t**>(mesh + MESH_SLOTS);
            for (int i = 0; slots && i < count; ++i)
            {
                std::uint8_t* entry = slots + i * SLOT_SIZE;
                if (*reinterpret_cast<int*>(entry) != slot)
                    continue;
                *reinterpret_cast<std::uint32_t*>(entry + SLOT_FLAGS) &= ~SLOT_LOADING;
                Fn<SharedSet_t>(gAddr.FoxSharedPtrBase_Set)(reinterpret_cast<void**>(entry + SLOT_TEXTURE), texture);   // keeps the picture alive while the mesh shows it
                for (int k = 0; k < 4; ++k)
                    *reinterpret_cast<std::uint32_t*>(entry + SLOT_RECT + k * 4) = SLOT_RECT_UNSET;
                break;
            }
            auto& state = *reinterpret_cast<std::uint32_t*>(mesh + MESH_STATE);
            state = (state & ~0x20u) | 0x8u;
            *reinterpret_cast<std::uint16_t*>(mesh + MESH_DIRTY) |= MESH_DIRTY_MATERIAL;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void CreateNow(const std::string& name, int width, int height)
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        const auto it = g_Views.find(name);
        if (it == g_Views.end() || it->second->entity)
            return;
        const std::string engineName = "V_SubViewport_" + name;
        if (!CreateSEH(&it->second->entity, engineName.c_str(), static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)))
        {
            LogError("V_UixUtility.CreateSubViewport: the game did not make sub viewport \"%s\"", name.c_str());
            g_Views.erase(it);
        }
    }
}

namespace UixUtilitySubViewport
{
    int __cdecl l_CreateSubViewport(lua_State* L)
    {
        const std::string name = NameArg(L, 1);
        if (name.empty() || !Allowed())
        {
            g_lua_pushboolean(L, false);
            return 1;
        }
        const int width = Size(L, 2);
        const int height = Size(L, 3);
        {
            std::lock_guard<std::mutex> lock(g_Lock);
            if (g_Views.find(name) != g_Views.end())
            {
                g_lua_pushboolean(L, true);
                return 1;
            }
            g_Views.emplace(name, std::make_unique<View>());
        }
        QueueNoHandle([name, width, height]() { CreateNow(name, width, height); });
        g_lua_pushboolean(L, true);
        return 1;
    }

    int __cdecl l_DeleteSubViewport(lua_State* L)
    {
        const std::string name = NameArg(L, 1);
        std::lock_guard<std::mutex> lock(g_Lock);
        const auto it = g_Views.find(name);
        if (it == g_Views.end())
            return 0;
        DestroySEH(&it->second->entity);
        g_Views.erase(it);
        return 0;
    }

    int __cdecl l_IsSubViewportReady(lua_State* L)
    {
        const std::string name = NameArg(L, 1);
        bool ready = false;
        {
            std::lock_guard<std::mutex> lock(g_Lock);
            ready = HasTextureSEH(ViewportOf(name));
        }
        g_lua_pushboolean(L, ready);
        return 1;
    }

    int __cdecl l_SetSubViewportCamera(lua_State* L)
    {
        const std::string name = NameArg(L, 1);
        if (name.empty() || !Allowed())
            return 0;
        Aim aim{};
        for (int i = 0; i < 3; ++i)
        {
            aim.eye[i] = Num(L, 2 + i, 0.0f);
            aim.target[i] = Num(L, 5 + i, 0.0f);
        }
        aim.halfWidth = (std::max)(Num(L, 8, 100.0f), 1.0f);
        aim.nearClip = (std::max)(Num(L, 9, 1.0f), 0.01f);
        aim.farClip = (std::max)(Num(L, 10, 2000.0f), aim.nearClip + 1.0f);
        float world[16], view[16];
        if (!CameraMatrices(aim, world, view))
        {
            LogWarnOnce("V_UixUtility.SetSubViewportCamera: the eye and the target are the same point");
            return 0;
        }
        std::vector<float> matrices(world, world + 16);
        matrices.insert(matrices.end(), view, view + 16);
        QueueNoHandle([name, aim, matrices]() {
            std::lock_guard<std::mutex> lock(g_Lock);
            std::uint8_t* viewport = ViewportOf(name);
            if (viewport)
                AimSEH(viewport, matrices.data(), matrices.data() + 16, aim, SceneOfSEH(MainViewportSEH()));
        });
        return 0;
    }

    int __cdecl l_SetSubViewportDraw(lua_State* L)
    {
        const std::string name = NameArg(L, 1);
        if (name.empty() || !Allowed())
            return 0;
        const bool on = LuaType(L, 2) == LUA_T_BOOLEAN ? GetLuaBool(L, 2) : true;
        QueueNoHandle([name, on]() {
            std::lock_guard<std::mutex> lock(g_Lock);
            if (std::uint8_t* viewport = ViewportOf(name))
                SetDrawSEH(viewport, on);
        });
        return 0;
    }

    int __cdecl l_SetSubViewportExposure(lua_State* L)
    {
        const std::string name = NameArg(L, 1);
        if (name.empty() || !Allowed() || LuaType(L, 2) != LUA_T_NUMBER)
            return 0;
        const float exposure = GetLuaNumber(L, 2);
        QueueNoHandle([name, exposure]() {
            std::lock_guard<std::mutex> lock(g_Lock);
            if (std::uint8_t* viewport = ViewportOf(name))
                SetExposureSEH(viewport, exposure);
        });
        return 0;
    }

    int __cdecl l_GetMainViewportExposure(lua_State* L)
    {
        if (!IsSupportedOnThisBuild() || !EngineReady())
            return 0;
        std::uint8_t* main = MainViewportSEH();
        if (!main)
            return 0;
        g_lua_pushnumber(L, static_cast<double>(ExposureOfSEH(main)));
        return 1;
    }

    int __cdecl l_SetSubViewportTexture(lua_State* L)
    {
        std::uint32_t handle = 0;
        Object mesh;
        if (!Allowed() || LuaType(L, 1) != LUA_T_NUMBER)
            return 0;
        handle = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
        if (!Lookup(handle, mesh) || mesh.kind != Kind::NodeMesh)
        {
            LogWarnOnce("V_UixUtility.SetSubViewportTexture: argument 1 must be a mesh node handle");
            return 0;
        }
        const std::string name = NameArg(L, 2);
        const char* slotName = LuaType(L, 3) == LUA_T_STRING ? GetLuaString(L, 3) : "Base_Texture";
        const int slot = static_cast<int>(static_cast<std::uint32_t>(FoxHashes::StrCode64(slotName)));
        if (name.empty())
            return 0;
        Queue(handle, [name, slot](const Object& node) {
            std::lock_guard<std::mutex> lock(g_Lock);
            if (std::uint8_t* viewport = ViewportOf(name))
                BindSEH(static_cast<std::uint8_t*>(node.ptr), slot, viewport);
        });
        return 0;
    }

    void ClearForNewMission()   // not queued: queued calls are dropped while hooks are bypassed
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        if (g_Views.empty())
            return;
        for (auto& view : g_Views)
            DestroySEH(&view.second->entity);
        g_Views.clear();
    }
}
