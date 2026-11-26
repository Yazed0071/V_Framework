#include <pch.h>
#include "TppEquipConstRegistry.h"

#include <Windows.h>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "log.h"
#include "FoxHashes.h"
#include "V_FrameWorkState.h"
#include "LuaApi.h"
#include "GunBasicInject.h"
#include "EquipIdCompression.h"
#include "EquipPartParams.h"
#include "SupportStockLease.h"

namespace
{
    static const char* const kSpaceTags[kTppEquipConstSpaceCount] =
    {
        "EQP",
        "EQP_TYPE",
        "SWP_TYPE",
        "EQP_BLOCK",
        "SWP",
        "BL",
        "BLA",
        "CASING",
        "MZ",
        "LTLS",
        "WP",
        "MO",
        "UB",
        "AM",
        "ST",
        "RC",
        "BA",
        "SK",
        "RETICLE_UI",
        "SCOPE_UI",
        "BARREL_LENGTH",
        "RICOCHET_SIZE",
        "BULLET_TYPE",
        "PENETRATE_LEVEL",
        "TRIGGER",
        "WEAPON_PAINT",
    };

    static constexpr std::int32_t kSpaceEqp            = 0;
    static constexpr std::int32_t kSpaceSwp            = 4;
    static constexpr std::int32_t kSpaceBl             = 5;
    static constexpr std::int32_t kSpaceBla            = 6;
    static constexpr std::int32_t kSpaceLtls           = 9;
    static constexpr std::int32_t kSpaceWp             = 10;
    static constexpr std::int32_t kSpaceMo             = 11;
    static constexpr std::int32_t kSpaceUb             = 12;
    static constexpr std::int32_t kSpaceAm             = 13;
    static constexpr std::int32_t kSpaceSt             = 14;
    static constexpr std::int32_t kSpaceRc             = 15;
    static constexpr std::int32_t kSpaceBa             = 16;
    static constexpr std::int32_t kSpaceSk             = 17;
    static constexpr std::int32_t kDefaultConstantBase = 0x4000;
    static constexpr std::int32_t kFirstCustomSwpId    = 91;     // the engine's SupportWeaponParameter rows end at 90
    static constexpr std::int32_t kLastCustomSwpId     = 1023;   // EquipIdTable keeps the subId in 10 bits
    static constexpr std::int32_t kFirstCustomBlastId  = 49;     // vanilla BlastParameter rows are 0..48
    static constexpr std::int32_t kLastCustomBlastId   = 255;    // bullets carry the blast id in a byte
    static constexpr std::int32_t kFirstSupportWeaponEquipId = 0x15A;
    static bool IsValidConstantName(const char* name)
    {
        if (!name || !name[0])
            return false;
        size_t len = 0;
        for (const char* p = name; *p; ++p, ++len)
        {
            const char c = *p;
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            (c >= '0' && c <= '9') || c == '_';
            if (!ok)
                return false;
        }
        return len <= 128;
    }

    static std::mutex g_Mutex;
    static std::map<std::string, std::int32_t> g_Constants;
    static std::unordered_map<std::uint32_t, std::int32_t> g_EquipIdByNameHash;   // FoxStrHash32 of an EQP_ name, as fox2 locators carry it

    static bool TakesWideEquipId(const char* name)
    {
        return std::strncmp(name, "EQP_WP_", 7) == 0
            || std::strncmp(name, "EQP_AM_", 7) == 0
            || std::strncmp(name, "EQP_BL_", 7) == 0
            || std::strncmp(name, "EQP_SWP_", 8) == 0;
    }

    static bool IsSupportWeaponName(const char* name)
    {
        return std::strncmp(name, "EQP_SWP_", 8) == 0;
    }

    static bool HasCleanAmmoStockId(std::int32_t equipId)
    {
        return equipId == 0x15A || equipId == 0x15B || equipId == 0x15C
            || equipId == 0x1A8 || equipId == 0x1AC;
    }

    static void WriteConstantTo(lua_State* L, const char* table, const char* name, std::int32_t value)
    {
        const int top0 = g_lua_gettop(L);
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>(table));
        if (g_lua_type(L, -1) == LUA_TTABLE)
        {
            g_lua_pushstring(L, const_cast<char*>(name));
            g_lua_pushnumber(L, static_cast<lua_Number>(value));
            g_lua_settable(L, -3);
        }
        g_lua_settop(L, top0);
    }

    static void WriteConstant(lua_State* L, const char* name, std::int32_t value)
    {
        WriteConstantTo(L, "TppEquip", name, value);
    }

    static std::map<std::string, std::int32_t> g_DamageConstants;
}

bool TppEquipConst_Declare(
    int spaceIndex,
    const char* name,
    std::uint32_t& outValue,
    lua_State* L)
{
    outValue = 0;
    if (spaceIndex < 0 || spaceIndex >= kTppEquipConstSpaceCount)
        return false;
    if (!IsValidConstantName(name))
    {
        LogWarn("TppEquipConst: invalid constant name '%s' ([A-Za-z0-9_], max 128), rejected",
            name ? name : "(null)");
        return false;
    }

    std::int32_t value = 0;
    if (spaceIndex == kSpaceEqp)
    {
        const bool supportWeapon = IsSupportWeaponName(name);
        const std::int32_t minimumId = supportWeapon ? kFirstSupportWeaponEquipId : 0;
        if (!V_FrameWorkState::ResolveOrCreateEquipId(name, minimumId, value, TakesWideEquipId(name)))
        {
            LogError("RegisterConstantEquipId '%s': no free equipId, rejected", name);
            return false;
        }
        if (supportWeapon && !SupportStockLease::Live()
            && (EquipIdCompression::IsWideEquipId(value) || !HasCleanAmmoStockId(value)))
            LogWarn("RegisterConstantEquipId %s: equipId 0x%X shares an ammo stock id, ammo boxes and supply drops may not refill it",
                name, static_cast<unsigned>(value));
    }
    else if (spaceIndex == kSpaceSwp)
    {
        if (!V_FrameWorkState::ResolveOrCreateConstantValue(
                kSpaceTags[spaceIndex], name, kFirstCustomSwpId, value, kLastCustomSwpId))
            return false;
    }
    else if (spaceIndex == kSpaceBla)
    {
        if (!V_FrameWorkState::ResolveOrCreateConstantValue(
                kSpaceTags[spaceIndex], name, kFirstCustomBlastId, value, kLastCustomBlastId))
            return false;
    }
    else if (spaceIndex == kSpaceWp)
    {
        const int weaponId = GunBasic_AllocateWeaponIdForName(name);
        if (weaponId > 0)
        {
            value = weaponId;
        }
        else if (!V_FrameWorkState::ResolveOrCreateConstantValue(
                     kSpaceTags[spaceIndex], name, kDefaultConstantBase, value))
        {
            return false;
        }
    }
    else if (spaceIndex == kSpaceAm)
    {
        const int ammoId = EquipParam_AllocateMagazineSlotForName(name);
        if (ammoId > 0)
        {
            value = ammoId;
        }
        else if (!V_FrameWorkState::ResolveOrCreateConstantValue(
                     kSpaceTags[spaceIndex], name, kDefaultConstantBase, value))
        {
            return false;
        }
    }
    else if (spaceIndex == kSpaceSk)
    {
        const int stockId = EquipParam_AllocateStockSlotForName(name);
        if (stockId > 0)
        {
            value = stockId;
        }
        else if (!V_FrameWorkState::ResolveOrCreateConstantValue(
                     kSpaceTags[spaceIndex], name, kDefaultConstantBase, value))
        {
            return false;
        }
    }
    else if (spaceIndex == kSpaceMo)
    {
        const int muzzleId = EquipParam_AllocateMuzzleSlotForName(name);
        if (muzzleId > 0)
        {
            value = muzzleId;
        }
        else if (!V_FrameWorkState::ResolveOrCreateConstantValue(
                     kSpaceTags[spaceIndex], name, kDefaultConstantBase, value))
        {
            return false;
        }
    }
    else if (spaceIndex == kSpaceRc)
    {
        const int receiverId = EquipParam_AllocateReceiverSlotForName(name);
        if (receiverId > 0)
        {
            value = receiverId;
        }
        else if (!V_FrameWorkState::ResolveOrCreateConstantValue(
                     kSpaceTags[spaceIndex], name, kDefaultConstantBase, value))
        {
            return false;
        }
    }
    else if (spaceIndex == kSpaceSt)
    {
        const int scopeId = EquipParam_AllocateSightSlotForName(name);
        if (scopeId > 0)
        {
            value = scopeId;
        }
        else if (!V_FrameWorkState::ResolveOrCreateConstantValue(
                     kSpaceTags[spaceIndex], name, kDefaultConstantBase, value))
        {
            return false;
        }
    }
    else if (spaceIndex == kSpaceBa)
    {
        const int barrelId = EquipParam_AllocateBarrelSlotForName(name);
        if (barrelId > 0)
        {
            value = barrelId;
        }
        else if (!V_FrameWorkState::ResolveOrCreateConstantValue(
                     kSpaceTags[spaceIndex], name, kDefaultConstantBase, value))
        {
            return false;
        }
    }
    else if (spaceIndex == kSpaceUb)
    {
        const int underBarrelId = EquipParam_AllocateUnderBarrelSlotForName(name);
        if (underBarrelId > 0)
        {
            value = underBarrelId;
        }
        else if (!V_FrameWorkState::ResolveOrCreateConstantValue(
                     kSpaceTags[spaceIndex], name, kDefaultConstantBase, value))
        {
            return false;
        }
    }
    else if (spaceIndex == kSpaceLtls)
    {
        const int optionId = EquipParam_AllocateOptionSlotForName(name);
        if (optionId > 0)
        {
            value = optionId;
        }
        else if (!V_FrameWorkState::ResolveOrCreateConstantValue(
                     kSpaceTags[spaceIndex], name, kDefaultConstantBase, value))
        {
            return false;
        }
    }
    else if (spaceIndex == kSpaceBl)
    {
        const int bulletId = EquipParam_AllocateBulletSlotForName(name);
        if (bulletId > 0)
        {
            value = bulletId;
        }
        else if (!V_FrameWorkState::ResolveOrCreateConstantValue(
                     kSpaceTags[spaceIndex], name, kDefaultConstantBase, value))
        {
            return false;
        }
    }
    else if (!V_FrameWorkState::ResolveOrCreateConstantValue(
                 kSpaceTags[spaceIndex], name, kDefaultConstantBase, value))
    {
        return false;
    }

    const std::uint32_t nameHash = spaceIndex == kSpaceEqp ? FoxHashes::StrCode32(name) : 0;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_Constants[name] = value;
        if (nameHash != 0)
            g_EquipIdByNameHash[nameHash] = value;
    }

    if (L && ResolveLuaApi())
        WriteConstant(L, name, value);

    outValue = static_cast<std::uint32_t>(value);
    if (spaceIndex == kSpaceEqp)
        LogInfo("RegisterConstantEquipId %s equipId:%d", name, value);
    return true;
}

void TppEquipConst_SetValue(const char* name, std::uint32_t value, lua_State* L)
{
    if (!name || !name[0])
        return;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_Constants[name] = static_cast<std::int32_t>(value);
    }
    if (L && ResolveLuaApi())
        WriteConstant(L, name, value);
}

void TppDamageConst_SetValue(const char* name, std::uint32_t value, lua_State* L)
{
    if (!name || !name[0])
        return;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_DamageConstants[name] = static_cast<std::int32_t>(value);
    }
    if (L && ResolveLuaApi())
        WriteConstantTo(L, "TppDamage", name, value);
}

void TppDamageConst_InjectAll(lua_State* L)
{
    std::map<std::string, std::int32_t> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        snapshot = g_DamageConstants;
    }
    if (snapshot.empty() || !L || !ResolveLuaApi())
        return;

    for (const auto& kv : snapshot)
        WriteConstantTo(L, "TppDamage", kv.first.c_str(), kv.second);
}

void TppEquipConst_InjectAll(lua_State* L)
{
    std::map<std::string, std::int32_t> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        snapshot = g_Constants;
    }
    if (snapshot.empty() || !L || !ResolveLuaApi())
        return;

    for (const auto& kv : snapshot)
        WriteConstant(L, kv.first.c_str(), kv.second);
}

void TppEquipConst_ResetRuntimeState()
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    g_Constants.clear();
    g_DamageConstants.clear();
    g_EquipIdByNameHash.clear();
}

bool TppEquipConst_FindEquipIdByNameHash(std::uint32_t nameHash, std::int32_t& equipId)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    const auto it = g_EquipIdByNameHash.find(nameHash);
    if (it == g_EquipIdByNameHash.end())
        return false;
    equipId = it->second;
    return true;
}

namespace
{
    constexpr int kSpaceDamage = -1;
    constexpr int kNoSpace     = -2;

    struct DeclareFunction
    {
        const char* name;
        int         space;
    };

    constexpr DeclareFunction kDeclareFunctions[] =
    {
        { "RegisterConstantEquipId", 0 },  { "DeclareEQPTypes", 1 },      { "DeclareSWPTypes", 2 },
        { "DeclareEQPBlocks", 3 },         { "DeclareSWPs", 4 },          { "DeclareBLs", 5 },
        { "DeclareBLAs", 6 },              { "DeclareCasings", 7 },       { "DeclareMZs", 8 },
        { "DeclareLTLS", 9 },              { "DeclareWPs", 10 },          { "DeclareMOs", 11 },
        { "DeclareUBs", 12 },              { "DeclareAMs", 13 },          { "DeclareSTs", 14 },
        { "DeclareRCs", 15 },              { "DeclareBAs", 16 },          { "DeclareSKs", 17 },
        { "DeclareReticleUIs", 18 },       { "DeclareScopeUIs", 19 },     { "DeclareBarrelLengths", 20 },
        { "DeclareRicochetSizes", 21 },    { "DeclareBulletTypes", 22 },  { "DeclarePenetrateLevels", 23 },
        { "DeclareTriggers", 24 },         { "DeclareWeaponPaints", 25 }, { "DeclareDamages", kSpaceDamage },
    };

    int DeclareSpaceOf(const std::string& function)
    {
        for (const DeclareFunction& f : kDeclareFunctions)
            if (function == f.name)
                return f.space;
        return kNoSpace;
    }

    struct LuaToken
    {
        enum class Kind : std::uint8_t { Name, String, Punct } kind;
        std::string text;
    };

    std::size_t LongBracketOpen(const std::string& s, std::size_t at)
    {
        std::size_t i = at + 1;
        while (i < s.size() && s[i] == '=')
            ++i;
        return (i < s.size() && s[i] == '[') ? i - at + 1 : 0;
    }

    std::size_t SkipLongBracket(const std::string& s, std::size_t at, std::size_t open, std::string* body)
    {
        const std::string close = "]" + std::string(open - 2, '=') + "]";
        const std::size_t start = at + open;
        const std::size_t end = s.find(close, start);
        if (end == std::string::npos)
        {
            if (body)
                *body = s.substr(start);
            return s.size();
        }
        if (body)
            *body = s.substr(start, end - start);
        return end + close.size();
    }

    std::vector<LuaToken> TokenizeLua(const std::string& s)
    {
        std::vector<LuaToken> tokens;
        std::size_t i = 0;
        while (i < s.size())
        {
            const char c = s[i];
            if (c == '-' && i + 1 < s.size() && s[i + 1] == '-')
            {
                if (i + 2 < s.size() && s[i + 2] == '[')
                    if (const std::size_t open = LongBracketOpen(s, i + 2))
                    {
                        i = SkipLongBracket(s, i + 2, open, nullptr);
                        continue;
                    }
                const std::size_t eol = s.find('\n', i);
                i = eol == std::string::npos ? s.size() : eol + 1;
                continue;
            }
            if (c == '"' || c == '\'')
            {
                std::string text;
                std::size_t j = i + 1;
                while (j < s.size() && s[j] != c && s[j] != '\n')
                {
                    if (s[j] == '\\' && j + 1 < s.size())
                    {
                        text += s[j + 1];
                        j += 2;
                        continue;
                    }
                    text += s[j++];
                }
                tokens.push_back({ LuaToken::Kind::String, text });
                i = j + 1;
                continue;
            }
            if (c == '[')
                if (const std::size_t open = LongBracketOpen(s, i))
                {
                    std::string text;
                    i = SkipLongBracket(s, i, open, &text);
                    tokens.push_back({ LuaToken::Kind::String, text });
                    continue;
                }
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
            {
                std::size_t j = i + 1;
                while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_'))
                    ++j;
                tokens.push_back({ LuaToken::Kind::Name, s.substr(i, j - i) });
                i = j;
                continue;
            }
            if (!std::isspace(static_cast<unsigned char>(c)))
                tokens.push_back({ LuaToken::Kind::Punct, std::string(1, c) });
            ++i;
        }
        return tokens;
    }

    bool IsPunct(const LuaToken& t, const char* text)
    {
        return t.kind == LuaToken::Kind::Punct && t.text == text;
    }

    void CollectDeclaredNames(const std::string& source, std::unordered_map<std::string, int>& out)
    {
        const std::vector<LuaToken> t = TokenizeLua(source);
        for (std::size_t i = 0; i + 3 < t.size(); ++i)
        {
            if (t[i].kind != LuaToken::Kind::Name || t[i].text != "V_TppEquip" || !IsPunct(t[i + 1], ".")
                || t[i + 2].kind != LuaToken::Kind::Name)
                continue;
            const int space = DeclareSpaceOf(t[i + 2].text);
            if (space == kNoSpace || !(IsPunct(t[i + 3], "{") || IsPunct(t[i + 3], "(")))
                continue;
            int depth = 0;
            std::size_t j = i + 3;
            for (; j < t.size(); ++j)
            {
                if (IsPunct(t[j], "{") || IsPunct(t[j], "("))
                    ++depth;
                else if (IsPunct(t[j], "}") || IsPunct(t[j], ")"))
                {
                    if (--depth == 0)
                        break;
                }
                else if (t[j].kind == LuaToken::Kind::String && IsValidConstantName(t[j].text.c_str()))
                    out.emplace(t[j].text, space);
            }
            i = j;
        }
    }

    std::once_flag                       g_ForwardScanOnce;
    std::unordered_map<std::string, int> g_ForwardNames;

    void ScanInstalledModules()
    {
        char path[MAX_PATH]{};
        if (!GetModuleFileNameA(nullptr, path, MAX_PATH))
            return;
        char* lastSlash = std::strrchr(path, '\\');
        if (!lastSlash)
            return;
        *(lastSlash + 1) = '\0';
        const std::string folder = std::string(path) + "mod\\modules\\";
        WIN32_FIND_DATAA found{};
        const HANDLE find = FindFirstFileA((folder + "*.lua").c_str(), &found);
        if (find == INVALID_HANDLE_VALUE)
            return;
        do
        {
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            std::ifstream in(folder + found.cFileName, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            CollectDeclaredNames(text, g_ForwardNames);
        } while (FindNextFileA(find, &found));
        FindClose(find);
    }

    int ForwardDeclare(lua_State* L, bool damageTable)
    {
        const char* name = g_lua_type(L, 2) == LUA_TSTRING ? g_lua_tolstring(L, 2, nullptr) : nullptr;
        if (!name)
        {
            g_lua_pushnil(L);
            return 1;
        }
        std::call_once(g_ForwardScanOnce, ScanInstalledModules);
        const auto it = g_ForwardNames.find(name);
        if (it == g_ForwardNames.end() || (it->second == kSpaceDamage) != damageTable)
        {
            g_lua_pushnil(L);
            return 1;
        }
        std::uint32_t value = 0;
        if (damageTable)
        {
            const int id = EquipParam_AllocateDamageSlotForName(name);
            if (id <= 0)
            {
                g_lua_pushnil(L);
                return 1;
            }
            value = static_cast<std::uint32_t>(id);
            TppDamageConst_SetValue(name, value, L);
        }
        else if (!TppEquipConst_Declare(it->second, name, value, L))
        {
            g_lua_pushnil(L);
            return 1;
        }
        g_lua_pushnumber(L, static_cast<lua_Number>(value));
        return 1;
    }

    int __cdecl l_ForwardDeclareEquip(lua_State* L)
    {
        return ForwardDeclare(L, false);
    }

    int __cdecl l_ForwardDeclareDamage(lua_State* L)
    {
        return ForwardDeclare(L, true);
    }

    std::atomic<bool> g_ForwardWarned{ false };

    void InstallForwardIndex(lua_State* L, const char* tableName, lua_CFunction index)
    {
        const int top = g_lua_gettop(L);
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>(tableName));
        const int table = g_lua_gettop(L);
        if (g_lua_type(L, table) != LUA_TTABLE)
        {
            g_lua_settop(L, top);
            return;
        }
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("getmetatable"));
        g_lua_pushvalue(L, table);
        if (g_lua_pcall(L, 1, 1, 0) != 0 || g_lua_type(L, -1) != LUA_TNIL)
        {
            bool ours = false;
            if (g_lua_type(L, -1) == LUA_TTABLE)
            {
                g_lua_getfield(L, -1, const_cast<char*>("__vfwForward"));
                ours = g_lua_toboolean(L, -1) != 0;
            }
            if (!ours && !g_ForwardWarned.exchange(true))
                LogWarn("TppEquipConst: %s already has a metatable, names used before their module loads stay nil", tableName);
            g_lua_settop(L, top);
            return;
        }
        g_lua_settop(L, table);
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("setmetatable"));
        g_lua_pushvalue(L, table);
        g_lua_createtable(L, 0, 2);
        g_lua_pushstring(L, const_cast<char*>("__index"));
        g_lua_pushcclosure(L, index, 0);
        g_lua_settable(L, -3);
        g_lua_pushstring(L, const_cast<char*>("__vfwForward"));
        g_lua_pushboolean(L, 1);
        g_lua_settable(L, -3);
        if (g_lua_pcall(L, 2, 0, 0) != 0 && !g_ForwardWarned.exchange(true))
            LogWarn("TppEquipConst: could not hook %s, names used before their module loads stay nil", tableName);
        g_lua_settop(L, top);
    }
}

void TppEquipConst_InstallForwardDeclarations(lua_State* L)
{
    if (!L || !ResolveLuaApi() || !g_lua_pcall || !g_lua_pushcclosure || !g_lua_createtable || !g_lua_pushboolean
        || !g_lua_toboolean || !g_lua_pushvalue || !g_lua_pushnil || !g_lua_tolstring)
        return;
    InstallForwardIndex(L, "TppEquip", &l_ForwardDeclareEquip);
    InstallForwardIndex(L, "TppDamage", &l_ForwardDeclareDamage);
}

int TppEquipDeclareForSpace(lua_State* L, int spaceIndex)
{
    if (g_lua_type && g_lua_type(L, 1) == LUA_TTABLE && g_lua_objlen && g_lua_rawgeti && g_lua_settop)
    {
        const int n = static_cast<int>(g_lua_objlen(L, 1));
        int declared = 0;
        for (int i = 1; i <= n; ++i)
        {
            g_lua_rawgeti(L, 1, i);
            const char* name = GetLuaString(L, -1);
            if (name && name[0])
            {
                std::uint32_t value = 0;
                if (TppEquipConst_Declare(spaceIndex, name, value, L))
                    ++declared;
            }
            g_lua_settop(L, -2);
        }
        if (g_lua_pushnumber)
            g_lua_pushnumber(L, static_cast<lua_Number>(declared));
        else
            PushLuaBool(L, declared > 0);
        return 1;
    }

    const char* name = GetLuaString(L, 1);

    std::uint32_t value = 0;
    if (!name || !name[0] ||
        !TppEquipConst_Declare(spaceIndex, name, value, L))
    {
        PushLuaBool(L, false);
        return 1;
    }

    if (g_lua_pushnumber)
        g_lua_pushnumber(L, static_cast<lua_Number>(value));
    else
        PushLuaBool(L, true);
    return 1;
}
