#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

using FoxLuaRegisterLibrary_t = void(__fastcall*)(lua_State* L, const char* libName, luaL_Reg* funcs);
using lua_tolstring_t    = const char* (__fastcall*)(lua_State* L, int idx, size_t* len);
using lua_tointeger_t    = long long(__fastcall*)(lua_State* L, int idx);
using lua_tonumber_t     = lua_Number(__fastcall*)(lua_State* L, int idx);
using lua_pushnumber_t   = void(__fastcall*)(lua_State* L, lua_Number n);
using lua_toboolean_t    = int(__fastcall*)(lua_State* L, int idx);
using lua_gettop_t       = int(__fastcall*)(lua_State* L);
using lua_settop_t       = void(__fastcall*)(lua_State* L, int idx);
using lua_getfield_t     = void(__fastcall*)(lua_State* L, int idx, char* k);
using lua_rawgeti_t      = void(__fastcall*)(lua_State* L, int idx, int n);
using lua_type_t         = int(__fastcall*)(lua_State* L, int idx);
using lua_isstring_t     = int(__fastcall*)(lua_State* L, int idx);
using lua_isnumber_t     = int(__fastcall*)(lua_State* L, int idx);
using lua_objlen_t       = size_t(__fastcall*)(lua_State* L, int idx);
using lua_pushboolean_t  = void(__fastcall*)(lua_State* L, int b);
using lua_pushstring_t   = void(__fastcall*)(lua_State* L, char* s);
using lua_createtable_t  = void(__fastcall*)(lua_State* L, int narr, int nrec);
using lua_rawset_t       = void(__fastcall*)(lua_State* L, int idx);
using lua_settable_t     = void(__fastcall*)(lua_State* L, int idx);
using lua_pushnil_t      = void(__fastcall*)(lua_State* L);
using lua_next_t         = int(__fastcall*)(lua_State* L, int idx);
using lua_gettable_t     = void(__fastcall*)(lua_State* L, int idx);
using lua_pushvalue_t    = void(__fastcall*)(lua_State* L, int idx);
using lua_pushcclosure_t = void(__fastcall*)(lua_State* L, lua_CFunction fn, int n);
using lua_pcall_t        = int(__fastcall*)(lua_State* L, int nargs, int nresults, int errfunc);

constexpr int LUA_GLOBALSINDEX_51   = -10002;
constexpr int LUA_UPVALUEINDEX_51_1 = -10003;

extern FoxLuaRegisterLibrary_t g_FoxLuaRegisterLibrary;
extern lua_tolstring_t    g_lua_tolstring;
extern lua_tointeger_t    g_lua_tointeger;
extern lua_tonumber_t     g_lua_tonumber;
extern lua_pushnumber_t   g_lua_pushnumber;
extern lua_toboolean_t    g_lua_toboolean;
extern lua_gettop_t       g_lua_gettop;
extern lua_settop_t       g_lua_settop;
extern lua_getfield_t     g_lua_getfield;
extern lua_rawgeti_t      g_lua_rawgeti;
extern lua_type_t         g_lua_type;
extern lua_isstring_t     g_lua_isstring;
extern lua_isnumber_t     g_lua_isnumber;
extern lua_objlen_t       g_lua_objlen;
extern lua_pushboolean_t  g_lua_pushboolean;
extern lua_pushstring_t   g_lua_pushstring;
extern lua_createtable_t  g_lua_createtable;
extern lua_rawset_t       g_lua_rawset;
extern lua_settable_t     g_lua_settable;
extern lua_pushnil_t      g_lua_pushnil;
extern lua_next_t         g_lua_next;
extern lua_gettable_t     g_lua_gettable;
extern lua_pushvalue_t    g_lua_pushvalue;
extern lua_pushcclosure_t g_lua_pushcclosure;
extern lua_pcall_t        g_lua_pcall;

bool ResolveLuaApi();

int           GetLuaTop(lua_State* L);
const char*   GetLuaString(lua_State* L, int idx);
int           GetLuaInt(lua_State* L, int idx);
std::uint64_t GetLuaInt64(lua_State* L, int idx);
bool          GetLuaBool(lua_State* L, int idx);
float         GetLuaNumber(lua_State* L, int idx);
int           LuaType(lua_State* L, int idx);
bool          LuaIsString(lua_State* L, int idx);
bool          LuaIsNumber(lua_State* L, int idx);
size_t        LuaObjLen(lua_State* L, int idx);
std::uint32_t GetLuaStrCode32Arg(lua_State* L, int idx);
std::uint32_t GetLuaFnvHash32Arg(lua_State* L, int idx);

void PushLuaBool(lua_State* L, bool value);
void PushLuaNumber(lua_State* L, float value);
void PushLuaString(lua_State* L, const char* s);
void PushLuaNil(lua_State* L);
void LuaPop(lua_State* L, int count);
void LuaGetField(lua_State* L, int idx, const char* fieldName);
void LuaRawGetI(lua_State* L, int idx, int n);

bool RegisterLuaLibrary(lua_State* L, const char* libName, luaL_Reg* funcs);