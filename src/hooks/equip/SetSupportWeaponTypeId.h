#pragma once

#include <cstddef>
#include <cstdint>

struct lua_State;

namespace SupportWeaponType
{
    struct Deps
    {
        bool (*ResolveLuaApi)() = nullptr;
        int (*LuaType)(lua_State* L, int idx) = nullptr;
        int (*GetLuaInt)(lua_State* L, int idx) = nullptr;

        // Optional deps used by RegisterSupportWeaponCategory. Leave null and the
        // category-registration entry-point will refuse the call (existing
        // SetSupportWeaponType / RemoveSupportWeaponType / ClearSupportWeaponTypes
        // do not need any of these).
        int (*GetLuaTop)(lua_State* L) = nullptr;
        void (*LuaGetField)(lua_State* L, int idx, const char* k) = nullptr;
        void (*LuaPop)(lua_State* L, int count) = nullptr;
        const char* (*GetLuaString)(lua_State* L, int idx) = nullptr;
        void (*PushLuaNumber)(lua_State* L, float n) = nullptr;
        void (*LuaPushString)(lua_State* L, const char* s) = nullptr;
        void (*LuaCreateTable)(lua_State* L, int narr, int nrec) = nullptr;
        void (*LuaRawSet)(lua_State* L, int idx) = nullptr;
        void (*LuaSetTable)(lua_State* L, int idx) = nullptr;

        // Required for the positional row[] form of `damage`. Leave null
        // and the row[] form is silently ignored (named fields still work).
        void (*LuaRawGetI)(lua_State* L, int idx, int n) = nullptr;
        std::size_t (*LuaObjLen)(lua_State* L, int idx) = nullptr;
    };

    void Bind(const Deps& deps);

    int __cdecl Lua_SetSupportWeaponType(lua_State* L);
    int __cdecl Lua_RemoveSupportWeaponType(lua_State* L);
    int __cdecl Lua_ClearSupportWeaponTypes(lua_State* L);

    // Brand-new SWP category support. A registered category is a fresh
    // swpType id allocated above the vanilla 0..0x16 range. It always carries
    // an `inheritsFrom` value (a vanilla 0..0x16 swpType) — every native game
    // path that switches on the result of GetSupportWeaponTypeId is hardcoded
    // to those vanilla values, so the framework spoofs the inherited value to
    // native callers while exposing the true new id to Lua via
    // V_FrameWork.GetSupportWeaponCategory.
    int __cdecl Lua_RegisterSupportWeaponCategory(lua_State* L);
    int __cdecl Lua_GetSupportWeaponCategory(lua_State* L);

    bool Install_EquipIdTableImpl_GetSupportWeaponTypeId_Hook();
    bool Uninstall_EquipIdTableImpl_GetSupportWeaponTypeId_Hook();

    bool Install_ThrowingImpl_GetBlastParamByEquipId_Hook();
    bool Uninstall_ThrowingImpl_GetBlastParamByEquipId_Hook();

    bool Install_EquipParameterTablesImpl_GetSupportWeaponParameterBlock_Hook();
    bool Uninstall_EquipParameterTablesImpl_GetSupportWeaponParameterBlock_Hook();

    bool Install_EquipParameterTablesImpl_GetAttackIdByEquipId_Hook();
    bool Uninstall_EquipParameterTablesImpl_GetAttackIdByEquipId_Hook();

    // The damage-row override is installed lazily on first inline-damage
    // category registration — the function pointer is fetched from the
    // DamageParameterTable singleton's vtable, which doesn't exist until
    // the engine has initialized the QuarkSystem.
    bool LazyInstall_DamageParameterTable_GetDamageParameter_Hook();
    bool Uninstall_DamageParameterTable_GetDamageParameter_Hook();

    // Returns true when `equipId` is bound to a registered category that has
    // a `chaffEffect = { radius, duration }` field. Outputs the configured
    // radius and duration. RangeAttackEffects consumes this from its
    // UpdateAction* hooks to auto-fire the chaff effect on detonation.
    bool TryGetChaffEffectForEquipId(int equipId, float& outRadius, float& outDuration);
}
