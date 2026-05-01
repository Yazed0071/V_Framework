#pragma once

#include <cstdint>

struct lua_State;

namespace RangeAttackEffects
{
    struct Deps
    {
        bool (*ResolveLuaApi)() = nullptr;
        int (*LuaType)(lua_State* L, int idx) = nullptr;
        float (*GetLuaNumber)(lua_State* L, int idx) = nullptr;
        void (*PushLuaNumber)(lua_State* L, float n) = nullptr;
    };

    void Bind(const Deps& deps);

    // Fires a chaff request at world-space (x, y, z). Radius and duration in
    // engine units (meters / seconds). Returns true if the engine accepted
    // the request (i.e. there was a free slot in the pending-chaff queue).
    bool RequestChaffAt(float x, float y, float z, float radius, float duration);

    // V_FrameWork.RequestChaffAt(x, y, z [, radius=15] [, duration=20]) -> 1/0
    int __cdecl Lua_RequestChaffAt(lua_State* L);

    // Auto-trigger hooks: when a registered support-weapon category has
    // chaffEffect set, the throwable's UpdateAction* function fires the
    // chaff effect at the projectile's landing position on first
    // detonation tick.
    bool Install_ThrowingImpl_UpdateActionGrenade_Hook();
    bool Uninstall_ThrowingImpl_UpdateActionGrenade_Hook();

    bool Install_ThrowingImpl_UpdateActionSmoke_Hook();
    bool Uninstall_ThrowingImpl_UpdateActionSmoke_Hook();
}
