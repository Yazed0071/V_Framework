#pragma once

struct lua_State;

namespace EquipIdTableAdd
{
    struct Deps
    {
        bool (*ResolveLuaApi)() = nullptr;

        int (*GetLuaTop)(lua_State* L) = nullptr;
        int (*LuaType)(lua_State* L, int idx) = nullptr;
        bool (*LuaIsNumber)(lua_State* L, int idx) = nullptr;
        bool (*LuaIsString)(lua_State* L, int idx) = nullptr;
        size_t(*LuaObjLen)(lua_State* L, int idx) = nullptr;
        void (*LuaPop)(lua_State* L, int count) = nullptr;

        const char* (*GetLuaString)(lua_State* L, int idx) = nullptr;
        int (*GetLuaInt)(lua_State* L, int idx) = nullptr;
        void (*PushLuaNumber)(lua_State* L, float value) = nullptr;

        void (*LuaPushString)(lua_State* L, const char* value) = nullptr;
        void (*LuaCreateTable)(lua_State* L, int narr, int nrec) = nullptr;
        void (*LuaRawSet)(lua_State* L, int idx) = nullptr;
        void (*LuaSetTable)(lua_State* L, int idx) = nullptr;
        void (*LuaRawGetI)(lua_State* L, int idx, int n) = nullptr;
        void (*LuaPushValue)(lua_State* L, int idx) = nullptr;
    };

    void Bind(const Deps& deps);

    int __cdecl Lua_AddToEquipIdTable(lua_State* L);

    bool Install_EquipIdTableImpl_ReloadEquipIdTable_Hook();
    bool Uninstall_EquipIdTableImpl_ReloadEquipIdTable_Hook();

    // Installs an observer hook on tpp::gm::impl::equip::EquipIdTableImpl::AddToEquipIdTable
    // (the function vanilla boot scripts call once per equipment row).
    // The hook records each row's compressed slot via
    // EquipIdCompression::MarkCompressedSlotUsed before forwarding to
    // orig, so the framework's custom-equipId allocator can avoid
    // collisions with vanilla and stay within the 0x289-slot bound.
    //
    // MUST be installed early in DLL init — before user-mod scripts
    // run RegisterConstantEquipId — so vanilla's own boot-time calls
    // are observed.
    bool Install_StockAddToEquipIdTable_Observer();
    bool Uninstall_StockAddToEquipIdTable_Observer();
}