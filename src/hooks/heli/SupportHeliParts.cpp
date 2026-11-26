#include <pch.h>
#include "SupportHeliParts.h"

#include <cstring>
#include <mutex>
#include <vector>

#include "FoxHashes.h"
#include "V_FrameWorkState.h"
#include "log.h"
#include "../../lua/LuaApi.h"
#include "../equip/EquipDevelop_SetEquipUndeveloped.h"
#include "TppHeli2_Allocate.h"

namespace
{
    using SupportHeliParts::Kind;
    using SupportHeliParts::Part;

    struct Paint
    {
        std::uint64_t vanilla;   // the paint file the game would apply
        std::uint64_t own;       // the outfit's file for that paint
    };

    struct Registered
    {
        std::string        key;
        std::string        packFile;
        Part               part;
        std::vector<Paint> paints;
    };

    constexpr const char*  kIdSpaces[SupportHeliParts::kKindCount]  = { "SupportHeliOutfit", "SupportHeliGatling", "SupportHeliSideGun" };
    constexpr const char*  kPickSpace                               = "SupportHeliParts";
    constexpr const char*  kPickNames[SupportHeliParts::kKindCount] = { "outfitPick", "gatlingPick", "sideGunPick" };
    constexpr const char*  kDefaultLook = "Default";   // fovaFiles: the look the heli wears under its paint
    constexpr std::uint64_t kNameMask    = 0xFFFFFFFFFFFF;
    constexpr std::int32_t kMostAttackId = 0xFFFF;
    constexpr std::int32_t kMostBulletId = 0xFF;

    std::mutex              g_Mutex;
    std::vector<Registered> g_Parts;
    std::uint8_t            g_Picked[SupportHeliParts::kKindCount] = {};
    bool                    g_PickedLoaded = false;
    std::uint8_t            g_LoadedOutfit = 0;
    std::uint8_t            g_Built[SupportHeliParts::kKindCount] = {};   // per kind: the picked part whose files the heli was built with
    bool                    g_BuiltKnown = false;
    std::uint8_t            g_Kept[SupportHeliParts::kKindCount] = {};
    bool                    g_KeptKnown = false;

    void ResolveDevelopRow(Registered& entry)
    {
        if (entry.part.developId != 0)
            return;
        const std::int32_t developId = V_FrameWorkState::GetDevelopIdByKey(entry.key.c_str());
        if (developId > 0)
            entry.part.developId = static_cast<std::uint32_t>(developId);
    }

    void LoadPicked()
    {
        if (g_PickedLoaded)
            return;
        g_PickedLoaded = true;
        for (std::size_t kind = 0; kind < SupportHeliParts::kKindCount; ++kind)
        {
            const std::int32_t saved = V_FrameWorkState::GetPersistedConstant(kPickSpace, kPickNames[kind]);
            g_Picked[kind] = (saved > 0 && saved <= static_cast<std::int32_t>(SupportHeliParts::kMostParts)) ? static_cast<std::uint8_t>(saved) : 0;
        }
    }

    std::string TableString(lua_State* L, const char* field)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, 2, field);
        const char* text = LuaType(L, -1) == LUA_TSTRING ? GetLuaString(L, -1) : nullptr;
        std::string value = text ? text : "";
        g_lua_settop(L, top);
        return value;
    }

    bool TableNumber(lua_State* L, const char* field, std::int32_t& value)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, 2, field);
        const bool present = LuaIsNumber(L, -1);
        if (present)
            value = static_cast<std::int32_t>(GetLuaNumber(L, -1));
        g_lua_settop(L, top);
        return present;
    }

    bool EndsWith(const std::string& text, const char* ending)
    {
        const std::size_t length = std::strlen(ending);
        return text.size() >= length && _stricmp(text.c_str() + text.size() - length, ending) == 0;
    }

    void AddModelFile(const char* function, const char* key, Part& part, const char* name, const char* path)
    {
        const std::string file = path ? path : "";
        SupportHeliParts::ModelFile model{};
        if (!path)
            model.kind = SupportHeliParts::ModelFileKind::EveryList;
        else if (EndsWith(file, ".parts"))
            model.kind = SupportHeliParts::ModelFileKind::Parts;
        else if (EndsWith(file, ".fmdl"))
            model.kind = SupportHeliParts::ModelFileKind::Fmdl;
        else if (EndsWith(file, ".fcnp"))
            model.kind = SupportHeliParts::ModelFileKind::Fcnp;
        else if (EndsWith(file, ".gskl"))
            model.kind = SupportHeliParts::ModelFileKind::Gskl;
        else if (EndsWith(file, ".vfx"))
            model.kind = SupportHeliParts::ModelFileKind::Vfx;
        else if (EndsWith(file, ".fv2"))
            model.kind = SupportHeliParts::ModelFileKind::Fova;
        else if (EndsWith(file, ".mtar"))
            model.kind = SupportHeliParts::ModelFileKind::Mtar;
        else
        {
            LogWarn("%s: '%s' models.%s must be a .parts, .fmdl, .fcnp, .gskl, .vfx, .fv2 or .mtar file, ignored", function, key, name);
            return;
        }
        if (part.modelCount >= SupportHeliParts::kMostModelFiles)
        {
            LogWarn("%s: '%s' holds %u model files at most, models.%s ignored", function, key,
                    static_cast<unsigned>(SupportHeliParts::kMostModelFiles), name);
            return;
        }
        model.name = FoxHashes::StrCode64(name) & kNameMask;
        model.path = path ? FoxHashes::PathCode64Ext(file) : 0;
        part.models[part.modelCount++] = model;
    }

    // models = { GunL = "/Assets/....fmdl", Main = { "/Assets/....parts", "/Assets/....fcnp" }, Logo = false }
    void ReadModels(lua_State* L, const char* function, const char* key, Part& part)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, 2, "models");
        if (LuaType(L, -1) != LUA_TTABLE)
        {
            g_lua_settop(L, top);
            return;
        }
        const int models = GetLuaTop(L);
        g_lua_pushnil(L);
        while (g_lua_next(L, models) != 0)
        {
            const int value = GetLuaTop(L);
            if (LuaType(L, value - 1) == LUA_TSTRING)   // only a string key is read: converting a number key would break the walk
            {
                const std::string name = GetLuaString(L, value - 1);
                if (LuaType(L, value) == LUA_TSTRING)
                    AddModelFile(function, key, part, name.c_str(), GetLuaString(L, value));
                else if (LuaType(L, value) == LUA_TBOOLEAN && !GetLuaBool(L, value))
                    AddModelFile(function, key, part, name.c_str(), nullptr);
                else if (LuaType(L, value) == LUA_TTABLE)
                {
                    for (int i = 1;; ++i)
                    {
                        LuaRawGetI(L, value, i);
                        if (LuaType(L, -1) != LUA_TSTRING)
                            break;
                        AddModelFile(function, key, part, name.c_str(), GetLuaString(L, -1));
                        LuaPop(L, 1);
                    }
                }
            }
            g_lua_settop(L, value - 1);   // keep the key for the next step
        }
        g_lua_settop(L, top);
    }

    // paints = { ["/Assets/tpp/fova/mecha/all/mfv_scol_c00.fv2"] = "/Assets/....fv2" }
    void ReadPaints(lua_State* L, std::vector<Paint>& paints)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, 2, "paints");
        if (LuaType(L, -1) != LUA_TTABLE)
        {
            g_lua_settop(L, top);
            return;
        }
        const int table = GetLuaTop(L);
        g_lua_pushnil(L);
        while (g_lua_next(L, table) != 0)
        {
            const int value = GetLuaTop(L);
            if (LuaType(L, value - 1) == LUA_TSTRING && LuaType(L, value) == LUA_TSTRING)
            {
                const std::string vanilla = GetLuaString(L, value - 1);
                const std::string own     = GetLuaString(L, value);
                paints.push_back({ FoxHashes::PathCode64Ext(vanilla), FoxHashes::PathCode64Ext(own) });
            }
            g_lua_settop(L, value - 1);
        }
        g_lua_settop(L, top);
    }

    bool TableFlag(lua_State* L, const char* field)
    {
        const int top = GetLuaTop(L);
        LuaGetField(L, 2, field);
        const bool set = LuaType(L, -1) == LUA_TBOOLEAN && GetLuaBool(L, -1);
        g_lua_settop(L, top);
        return set;
    }

    const char* KeyArgument(lua_State* L)
    {
        const char* key = (GetLuaTop(L) >= 1 && LuaIsString(L, 1)) ? GetLuaString(L, 1) : nullptr;
        return (key && *key && LuaType(L, 2) == LUA_TTABLE) ? key : nullptr;
    }

    bool Store(const char* function, const char* key, Part part, const std::string& packFile, std::vector<Paint> paints = {})
    {
        const std::size_t kind = static_cast<std::size_t>(part.kind);
        const std::int32_t most = static_cast<std::int32_t>(SupportHeliParts::kMostParts);
        std::int32_t id = 0;
        if (!V_FrameWorkState::ResolveOrCreateConstantValue(kIdSpaces[kind], key, 1, id, most))
        {
            LogWarn("%s: '%s' no free id left (%d max), ignored", function, key, most);
            return false;
        }
        part.id = static_cast<std::uint8_t>(id);

        std::lock_guard<std::mutex> lock(g_Mutex);
        Registered* entry = nullptr;
        for (Registered& known : g_Parts)
        {
            if (known.part.kind == part.kind && known.part.id == part.id)
                entry = &known;
        }
        if (!entry)
        {
            g_Parts.emplace_back();
            entry = &g_Parts.back();
        }
        entry->key      = key;
        entry->packFile = packFile;
        entry->part     = part;
        entry->paints   = std::move(paints);
        ResolveDevelopRow(*entry);
        return true;
    }

    int RegisterGun(lua_State* L, Kind kind, const char* function)
    {
        if (!ResolveLuaApi())
            return 0;
        const char* key = KeyArgument(L);
        if (!key)
        {
            LogWarn("%s: expects (key, { attackId = TppDamage.ATK_*, bulletId = TppEquip.BL_* }), ignored", function);
            PushLuaBool(L, false);
            return 1;
        }

        Part part{};
        part.kind = kind;
        std::int32_t attackId = 0;
        if (TableNumber(L, "attackId", attackId))
        {
            if (attackId <= 0 || attackId > kMostAttackId)
                LogWarn("%s: '%s' attackId %d is not an attack id, vanilla damage kept", function, key, attackId);
            else
                part.attackId = attackId;
        }
        std::int32_t bulletId = 0;
        if (TableNumber(L, "bulletId", bulletId))
        {
            if (bulletId < 0 || bulletId > kMostBulletId)
                LogWarn("%s: '%s' bulletId %d is not a bullet row (TppEquip.BL_*, 0-255), vanilla round kept", function, key, bulletId);
            else
                part.bulletId = bulletId;
        }
        ReadModels(L, function, key, part);
        const std::string pack = TableString(L, "pack");
        part.packPath = pack.empty() ? 0 : FoxHashes::PathCode64Ext(pack);
        if (part.attackId < 0 && part.bulletId < 0 && part.modelCount == 0)
            LogWarn("%s: '%s' has no attackId, bulletId or models, it is the vanilla gun under another name", function, key);

        const bool stored = Store(function, key, part, pack);
        if (stored)
            LogInfo("%s: '%s' registered", function, key);
        PushLuaBool(L, stored);
        return 1;
    }
}

namespace SupportHeliParts
{
    int __cdecl Lua_RegisterSupportHeliOutfit(lua_State* L)
    {
        if (!ResolveLuaApi())
            return 0;
        const char* key = KeyArgument(L);
        if (!key)
        {
            LogWarn("RegisterSupportHeliOutfit: expects (key, { fova = \"/Assets/....fv2\", models = { ... }, pack = \"/Assets/....fpk\" }), ignored");
            PushLuaBool(L, false);
            return 1;
        }
        const std::string fova = TableString(L, "fova");
        const std::string pack = TableString(L, "pack");
        Part part{};
        part.kind = Kind::Outfit;
        ReadModels(L, "RegisterSupportHeliOutfit", key, part);
        if (!fova.empty())
            AddModelFile("RegisterSupportHeliOutfit", key, part, kDefaultLook, fova.c_str());
        std::vector<Paint> paints;
        ReadPaints(L, paints);
        if (part.modelCount == 0 && paints.empty())
        {
            LogWarn("RegisterSupportHeliOutfit: '%s' needs fova = the .fv2 file of the look, models = { ... } or paints = { ... }, ignored", key);
            PushLuaBool(L, false);
            return 1;
        }
        part.wholeHeli = TableFlag(L, "wholeHeli");
        part.packPath  = pack.empty() ? 0 : FoxHashes::PathCode64Ext(pack);
        const bool stored = Store("RegisterSupportHeliOutfit", key, part, pack, std::move(paints));
        if (stored)
            LogInfo("RegisterSupportHeliOutfit: '%s' registered", key);
        PushLuaBool(L, stored);
        return 1;
    }

    int __cdecl Lua_RegisterSupportHeliGatling(lua_State* L)
    {
        return RegisterGun(L, Kind::Gatling, "RegisterSupportHeliGatling");
    }

    int __cdecl Lua_RegisterSupportHeliSideGun(lua_State* L)
    {
        return RegisterGun(L, Kind::SideGun, "RegisterSupportHeliSideGun");
    }

    bool RebuildNeeded()
    {
        bool differs = false;
        for (std::size_t kind = 0; kind < kKindCount && g_BuiltKnown; ++kind)
        {
            Part part{};
            const std::uint8_t picked = PickedPart(static_cast<Kind>(kind), part) && part.modelCount != 0 ? part.id : 0;
            differs = differs || picked != g_Built[kind];
        }
        return differs;
    }

    int __cdecl Lua_IsSupportHeliRebuildNeeded(lua_State* L)
    {
        if (!ResolveLuaApi())
            return 0;
        PushLuaBool(L, RebuildNeeded());
        return 1;
    }

    int __cdecl Lua_KeepSupportHeliPicks(lua_State* L)
    {
        for (std::size_t kind = 0; kind < kKindCount; ++kind)
            g_Kept[kind] = PickedId(static_cast<Kind>(kind));
        g_KeptKnown = true;
        return 0;
    }

    int __cdecl Lua_RestoreSupportHeliPicks(lua_State* L)
    {
        if (!g_KeptKnown)
            return 0;
        for (std::size_t kind = 0; kind < kKindCount; ++kind)
            SetPicked(static_cast<Kind>(kind), g_Kept[kind]);
        g_KeptKnown = false;
        if (RebuildNeeded())
            TppHeli2_RebuildPequodModels();
        return 0;
    }

    bool HasAny(Kind kind)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const Registered& entry : g_Parts)
        {
            if (entry.part.kind == kind)
                return true;
        }
        return false;
    }

    std::size_t List(Kind kind, Part* out, std::size_t capacity)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        std::size_t count = 0;
        for (Registered& entry : g_Parts)
        {
            if (entry.part.kind != kind)
                continue;
            ResolveDevelopRow(entry);
            if (entry.part.developId != 0 && count < capacity)
                out[count++] = entry.part;
        }
        return count;
    }

    bool FindByDevelopId(std::uint32_t developId, Part& out)
    {
        if (developId == 0)
            return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (Registered& entry : g_Parts)
        {
            ResolveDevelopRow(entry);
            if (entry.part.developId == developId)
            {
                out = entry.part;
                return true;
            }
        }
        return false;
    }

    std::size_t PackFiles(bool everyPart, std::string* out, std::size_t capacity)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadPicked();
        std::size_t count = 0;
        for (const Registered& entry : g_Parts)
        {
            if (entry.packFile.empty() || (!everyPart && g_Picked[static_cast<std::size_t>(entry.part.kind)] != entry.part.id))
                continue;
            bool listed = false;
            for (std::size_t i = 0; i < count; ++i)
                listed = listed || out[i] == entry.packFile;
            if (!listed && count < capacity)
                out[count++] = entry.packFile;
        }
        return count;
    }

    std::uint8_t PickedId(Kind kind)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadPicked();
        return g_Picked[static_cast<std::size_t>(kind)];
    }

    void SetPicked(Kind kind, std::uint8_t id)
    {
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            LoadPicked();
            if (g_Picked[static_cast<std::size_t>(kind)] == id)
                return;
            g_Picked[static_cast<std::size_t>(kind)] = id;
        }
        V_FrameWorkState::SetPersistedConstant(kPickSpace, kPickNames[static_cast<std::size_t>(kind)], id);
    }

    bool PickedPart(Kind kind, Part& out)
    {
        const std::uint8_t id = PickedId(kind);
        if (id == 0)
            return false;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            bool found = false;
            for (Registered& entry : g_Parts)
            {
                if (entry.part.kind != kind || entry.part.id != id)
                    continue;
                ResolveDevelopRow(entry);
                out = entry.part;
                found = true;
            }
            if (!found)
                return false;
        }
        return out.developId != 0 && EquipDevelop_IsDevelopedByDevelopId(out.developId);
    }

    void NoteHeliBuilt()
    {
        for (std::size_t kind = 0; kind < kKindCount; ++kind)
        {
            Part part{};
            g_Built[kind] = PickedPart(static_cast<Kind>(kind), part) && part.modelCount != 0 ? part.id : 0;
        }
        g_BuiltKnown = true;
    }

    void SetLoadedOutfit(std::uint8_t id)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_LoadedOutfit = id;
    }

    bool HasLoadedOutfit()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_LoadedOutfit != 0;
    }

    bool LoadedOutfitPaint(std::uint64_t vanillaFova, std::uint64_t& pack, std::uint64_t& fova)
    {
        if (vanillaFova == 0)
            return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (g_LoadedOutfit == 0)
            return false;
        for (const Registered& entry : g_Parts)
        {
            if (entry.part.kind != Kind::Outfit || entry.part.id != g_LoadedOutfit)
                continue;
            for (const Paint& paint : entry.paints)
            {
                if (paint.vanilla != vanillaFova)
                    continue;
                if (entry.part.packPath != 0)
                    pack = entry.part.packPath;
                fova = paint.own;
                return true;
            }
        }
        return false;
    }
}
